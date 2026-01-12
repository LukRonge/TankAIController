// Copyright ArenaBreakers. All Rights Reserved.

#include "EnemyDetectionComponent.h"
#include "WR_Tank_Pawn.h"
#include "WR_Turret.h"
#include "WR_ControlsInterface.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Kismet/KismetSystemLibrary.h"
#include "EngineUtils.h"

// ========== NETWORK SERIALIZATION ==========

bool FDetectedEnemyInfo::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	// Serialize enemy reference
	Ar << Enemy;

	// Pack float values into bytes for bandwidth optimization
	uint8 PackedVisibility = 0;
	uint8 PackedAwareness = 0;
	uint8 PackedState = 0;

	if (Ar.IsSaving())
	{
		PackedVisibility = FMath::RoundToInt(VisibilityPercent * 255.0f);
		PackedAwareness = FMath::RoundToInt(AwarenessLevel * 255.0f);
		PackedState = static_cast<uint8>(AwarenessState);
	}

	Ar << PackedVisibility;
	Ar << PackedAwareness;
	Ar << PackedState;

	if (Ar.IsLoading())
	{
		VisibilityPercent = PackedVisibility / 255.0f;
		AwarenessLevel = PackedAwareness / 255.0f;
		AwarenessState = static_cast<EAwarenessState>(FMath::Clamp(PackedState, 0, 3));
	}

	// Location with quantization (1cm precision is enough)
	Ar << LastKnownLocation;
	Ar << LastKnownVelocity;

	// Remaining fields
	Ar << Distance;
	Ar << AngleToEnemy;
	Ar << TimeSinceLastSeen;
	Ar << bInFiringCone;
	Ar << VisibleSocketsMask;

	bOutSuccess = true;
	return true;
}

// ========== CONSTRUCTOR ==========

UEnemyDetectionComponent::UEnemyDetectionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics; // After movement
	SetIsReplicatedByDefault(true);
}

// ========== LIFECYCLE ==========

void UEnemyDetectionComponent::BeginPlay()
{
	Super::BeginPlay();

	// Pre-allocate arrays to avoid runtime allocations
	DetectedEnemies.Reserve(MaxTrackedEnemies);
	CachedPotentialTargets.Reserve(32);

	// NOTE: Detection is DISABLED by default (bDetectionEnabled = false)
	// It will be enabled when EnableInferenceMode() is called (NumPad 7)
	// This keeps the AI tank inactive until explicitly started.

	UE_LOG(LogTemp, Log, TEXT("EnemyDetectionComponent: BeginPlay - Detection DISABLED (waiting for inference mode)"));
}

void UEnemyDetectionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DetectedEnemies.Empty();
	CachedPotentialTargets.Empty();
	Super::EndPlay(EndPlayReason);
}

void UEnemyDetectionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Check if detection is enabled (disabled by default until inference mode starts)
	if (!bDetectionEnabled)
	{
		return;
	}

	// Server-authoritative detection only
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	++FrameCounter;

	// Update detection at configured interval
	DetectionUpdateTimer += DeltaTime;
	if (DetectionUpdateTimer >= DetectionUpdateInterval)
	{
		UpdateDetection(DetectionUpdateTimer);
		DetectionUpdateTimer = 0.0f;
	}

	// Debug visualization
	if (bDrawDebug)
	{
		DrawDebugVisualization();
	}
}

void UEnemyDetectionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Replicate detected enemies only to owning client (bandwidth optimization)
	DOREPLIFETIME_CONDITION(UEnemyDetectionComponent, DetectedEnemies, COND_OwnerOnly);
	DOREPLIFETIME(UEnemyDetectionComponent, TeamID);
}

void UEnemyDetectionComponent::OnRep_DetectedEnemies()
{
	// Client-side callback - can trigger UI updates
}

// ========== MAIN DETECTION LOOP ==========

void UEnemyDetectionComponent::UpdateDetection(float DeltaTime)
{
	// Refresh potential targets periodically
	PotentialTargetRefreshTimer += DeltaTime;
	if (PotentialTargetRefreshTimer >= PotentialTargetRefreshInterval)
	{
		RefreshPotentialTargets();
		PotentialTargetRefreshTimer = 0.0f;
	}

	const FVector EyeLocation = GetEyeLocation();
	const FVector LookDir = GetLookDirection();

	// Debug: Log if eye location or look direction is invalid
	if (EyeLocation.IsZero())
	{
		UE_LOG(LogTemp, Warning, TEXT("EnemyDetection: EyeLocation is ZERO - pawn or turret not found?"));
	}
	if (LookDir.IsNearlyZero())
	{
		UE_LOG(LogTemp, Warning, TEXT("EnemyDetection: LookDir is ZERO - turret not found?"));
	}

	// Reset raycast budget for this frame
	RemainingRaycastBudget = MaxRaycastsPerFrame;

	// Process potential targets
	const int32 NumTargets = CachedPotentialTargets.Num();
	int32 TargetsChecked = 0;

	for (int32 i = 0; i < NumTargets && RemainingRaycastBudget > 0; ++i)
	{
		// Round-robin starting point for fairness when budget-limited
		const int32 TargetIndex = (EnemyCheckIndex + i) % NumTargets;
		AActor* Target = CachedPotentialTargets[TargetIndex].Get();

		if (!Target || !IsValid(Target))
		{
			continue;
		}

		// Skip non-enemies
		if (!IsEnemy(Target))
		{
			continue;
		}

		// Get distance for LOD
		const float Distance = FVector::Dist(Target->GetActorLocation(), EyeLocation);

		// Apply LOD system - skip some enemies based on distance
		if (bUseLODSystem)
		{
			const EDetectionPriority Priority = GetDetectionPriority(Distance);
			if (!ShouldCheckEnemyThisFrame(Priority))
			{
				continue;
			}
		}

		// Calculate visibility (this does the expensive raycasts)
		FVector LastSeenLocation;
		uint8 VisibleMask;
		const float Visibility = CalculateVisibilityToTarget(Target, LastSeenLocation, VisibleMask);

		// Find or create enemy info entry
		FDetectedEnemyInfo* Info = FindOrCreateEnemyInfo(Target);
		if (!Info)
		{
			continue; // Max enemies reached, lowest priority was not removable
		}

		// Store previous state for events
		const EAwarenessState PreviousState = Info->AwarenessState;
		const bool bWasInFiringCone = Info->bInFiringCone;

		// Update awareness level
		UpdateEnemyAwareness(*Info, Visibility, DeltaTime);

		// Update location/velocity
		if (Visibility > 0.0f)
		{
			Info->LastKnownLocation = LastSeenLocation;
			Info->LastKnownVelocity = Target->GetVelocity();
			Info->TimeSinceLastSeen = 0.0f;
			Info->VisibleSocketsMask = VisibleMask;
		}

		// Update distance and angle
		const FVector ToTarget = Target->GetActorLocation() - EyeLocation;
		Info->Distance = ToTarget.Size();

		const FVector ToTargetNorm = ToTarget.GetSafeNormal();
		const float DotProduct = FVector::DotProduct(LookDir, ToTargetNorm);
		Info->AngleToEnemy = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(DotProduct, -1.0f, 1.0f)));

		// Determine left/right sign for angle
		const float CrossZ = FVector::CrossProduct(LookDir, ToTargetNorm).Z;
		if (CrossZ < 0.0f)
		{
			Info->AngleToEnemy = -Info->AngleToEnemy;
		}

		// Check firing cone
		Info->bInFiringCone = FMath::Abs(Info->AngleToEnemy) <= DetectionConfig.FiringConeHalfAngle;

		// Fire events
		if (PreviousState == EAwarenessState::Unaware && Info->AwarenessState != EAwarenessState::Unaware)
		{
			OnEnemyDetected.Broadcast(Target, *Info);
		}

		if (PreviousState != Info->AwarenessState)
		{
			OnAwarenessStateChanged.Broadcast(Target, PreviousState, Info->AwarenessState);
		}

		if (bWasInFiringCone != Info->bInFiringCone)
		{
			OnEnemyInFiringCone.Broadcast(Target, Info->bInFiringCone);
		}

		++TargetsChecked;
	}

	// Update round-robin index
	EnemyCheckIndex = (EnemyCheckIndex + TargetsChecked) % FMath::Max(1, NumTargets);

	// Cleanup stale entries
	CleanupStaleEntries(DeltaTime);
}

void UEnemyDetectionComponent::RefreshPotentialTargets()
{
	CachedPotentialTargets.Reset();

	const FVector EyeLocation = GetEyeLocation();
	const float MaxRangeSq = FMath::Square(DetectionConfig.MaxDetectionRange);

	// Find all tank pawns in range
	// NOTE: For large-scale games, consider spatial hash/octree
	for (TActorIterator<AWR_Tank_Pawn> It(GetWorld()); It; ++It)
	{
		AWR_Tank_Pawn* Tank = *It;

		// Skip self
		if (Tank == GetOwner())
		{
			continue;
		}

		// Also skip if owner is a controller controlling this tank
		if (AController* Controller = Cast<AController>(GetOwner()))
		{
			if (Controller->GetPawn() == Tank)
			{
				continue;
			}
		}

		// Skip dead tanks
		// TODO: Add health check when death system is implemented
		// if (Tank->IsDead()) continue;

		// Distance check (cheap)
		const float DistSq = FVector::DistSquared(Tank->GetActorLocation(), EyeLocation);
		if (DistSq > MaxRangeSq)
		{
			continue;
		}

		CachedPotentialTargets.Add(Tank);
	}
}

// ========== VISIBILITY CALCULATION ==========

float UEnemyDetectionComponent::CalculateVisibilityToTarget(AActor* Target, FVector& OutBestVisibleLocation, uint8& OutVisibleMask) const
{
	if (!Target)
	{
		return 0.0f;
	}

	const FVector EyeLocation = GetEyeLocation();
	const FVector TargetCenter = Target->GetActorLocation();

	// Layer 1: FOV check (cheapest)
	float Angle;
	float FOVEffectiveness;
	if (!IsInDetectionFOV(TargetCenter, Angle, FOVEffectiveness))
	{
		OutVisibleMask = 0;
		return 0.0f;
	}

	// Layer 2: Distance check
	const float Distance = FVector::Dist(EyeLocation, TargetCenter);
	if (Distance > DetectionConfig.MaxDetectionRange)
	{
		OutVisibleMask = 0;
		return 0.0f;
	}

	// Instant detection at very close range
	if (Distance <= DetectionConfig.InstantDetectionRange)
	{
		OutBestVisibleLocation = TargetCenter;
		OutVisibleMask = 0xFF;
		return 1.0f;
	}

	// Distance falloff (smooth curve)
	const float NormalizedDist = Distance / DetectionConfig.MaxDetectionRange;
	const float DistanceFactor = 1.0f - FMath::Square(NormalizedDist); // Quadratic falloff

	// Layer 3: Multi-raycast visibility check (most expensive)
	float TotalWeight = 0.0f;
	float VisibleWeight = 0.0f;
	OutBestVisibleLocation = TargetCenter;
	OutVisibleMask = 0;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());

	// Also ignore controlled pawn if owner is a controller
	if (AController* Controller = Cast<AController>(GetOwner()))
	{
		if (Controller->GetPawn())
		{
			QueryParams.AddIgnoredActor(Controller->GetPawn());
		}
	}
	QueryParams.bTraceComplex = false; // Simple collision for performance

	const int32 NumSockets = FMath::Min(DetectionConfig.TargetSockets.Num(), 8); // Max 8 for bitmask

	for (int32 i = 0; i < NumSockets; ++i)
	{
		const FDetectionSocket& Socket = DetectionConfig.TargetSockets[i];
		const FVector SocketLocation = GetTargetSocketLocation(Target, Socket);

		TotalWeight += Socket.Weight;

		// Decrement raycast budget
		const_cast<UEnemyDetectionComponent*>(this)->RemainingRaycastBudget--;

		FHitResult HitResult;
		const bool bHit = GetWorld()->LineTraceSingleByChannel(
			HitResult,
			EyeLocation,
			SocketLocation,
			ECC_Visibility,
			QueryParams
		);

		// Socket is visible if no hit, or if we hit the target itself
		if (!bHit || HitResult.GetActor() == Target)
		{
			VisibleWeight += Socket.Weight;
			OutBestVisibleLocation = SocketLocation;
			OutVisibleMask |= (1 << i);
		}

		// Early exit if budget exhausted
		if (RemainingRaycastBudget <= 0)
		{
			// Scale up based on partial check
			const float CheckedWeight = TotalWeight;
			const float ScaleFactor = DetectionConfig.TargetSockets.Num() > 0 ?
				(float)(i + 1) / DetectionConfig.TargetSockets.Num() : 1.0f;
			TotalWeight /= ScaleFactor;
			break;
		}
	}

	if (TotalWeight <= 0.0f)
	{
		return 0.0f;
	}

	// Calculate final visibility
	const float BaseVisibility = VisibleWeight / TotalWeight;
	const float FinalVisibility = BaseVisibility * FOVEffectiveness * DistanceFactor;

	return FMath::Clamp(FinalVisibility, 0.0f, 1.0f);
}

FVector UEnemyDetectionComponent::GetTargetSocketLocation(AActor* Target, const FDetectionSocket& Socket) const
{
	if (!Target)
	{
		return FVector::ZeroVector;
	}

	// Try named socket first
	if (Socket.SocketName != NAME_None)
	{
		if (USkeletalMeshComponent* Mesh = Target->FindComponentByClass<USkeletalMeshComponent>())
		{
			if (Mesh->DoesSocketExist(Socket.SocketName))
			{
				return Mesh->GetSocketLocation(Socket.SocketName) + Socket.Offset;
			}
		}
	}

	// Fallback: actor location + offset transformed to world space
	return Target->GetActorLocation() + Target->GetActorRotation().RotateVector(Socket.Offset);
}

bool UEnemyDetectionComponent::IsInDetectionFOV(const FVector& TargetLocation, float& OutAngle, float& OutEffectiveness) const
{
	const FVector EyeLocation = GetEyeLocation();
	const FVector LookDir = GetLookDirection();

	// Early out if look direction is invalid
	if (LookDir.IsNearlyZero())
	{
		OutAngle = 180.0f;
		OutEffectiveness = 0.0f;
		return false;
	}

	const FVector ToTarget = (TargetLocation - EyeLocation).GetSafeNormal();

	const float DotProduct = FVector::DotProduct(LookDir, ToTarget);
	OutAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(DotProduct, -1.0f, 1.0f)));

	// Main FOV
	if (OutAngle <= DetectionConfig.DetectionFOVHalfAngle)
	{
		OutEffectiveness = 1.0f;
		return true;
	}

	// Peripheral vision
	const float TotalFOV = DetectionConfig.DetectionFOVHalfAngle + DetectionConfig.PeripheralVisionAngle;
	if (OutAngle <= TotalFOV)
	{
		// Linear falloff in peripheral zone
		const float PeripheralProgress = (OutAngle - DetectionConfig.DetectionFOVHalfAngle) / DetectionConfig.PeripheralVisionAngle;
		OutEffectiveness = FMath::Lerp(1.0f, DetectionConfig.PeripheralEffectiveness, PeripheralProgress);
		return true;
	}

	OutEffectiveness = 0.0f;
	return false;
}

// ========== AWARENESS SYSTEM ==========

void UEnemyDetectionComponent::UpdateEnemyAwareness(FDetectedEnemyInfo& Info, float Visibility, float DeltaTime)
{
	if (Visibility > 0.0f)
	{
		// Increase awareness based on visibility
		const float GainAmount = Visibility * DetectionConfig.AwarenessGainRate * DeltaTime;
		Info.AwarenessLevel = FMath::Clamp(Info.AwarenessLevel + GainAmount, 0.0f, 1.0f);
		Info.VisibilityPercent = Visibility;
	}
	else
	{
		// Decay awareness when not seeing enemy
		const float DecayAmount = DetectionConfig.AwarenessDecayRate * DeltaTime;
		Info.AwarenessLevel = FMath::Max(0.0f, Info.AwarenessLevel - DecayAmount);
		Info.VisibilityPercent = 0.0f;
		Info.TimeSinceLastSeen += DeltaTime;
	}

	// Update state
	Info.AwarenessState = GetAwarenessStateFromLevel(Info.AwarenessLevel);
}

EAwarenessState UEnemyDetectionComponent::GetAwarenessStateFromLevel(float AwarenessLevel) const
{
	if (AwarenessLevel >= AwarenessThresholds::Combat)
	{
		return EAwarenessState::Combat;
	}
	if (AwarenessLevel >= AwarenessThresholds::Alerted)
	{
		return EAwarenessState::Alerted;
	}
	if (AwarenessLevel >= AwarenessThresholds::Suspicious)
	{
		return EAwarenessState::Suspicious;
	}
	return EAwarenessState::Unaware;
}

// ========== LOD SYSTEM ==========

EDetectionPriority UEnemyDetectionComponent::GetDetectionPriority(float Distance) const
{
	const float MaxRange = DetectionConfig.MaxDetectionRange;

	if (Distance < MaxRange * 0.25f)
	{
		return EDetectionPriority::Critical; // Very close - check every frame
	}
	if (Distance < MaxRange * 0.5f)
	{
		return EDetectionPriority::High; // Check every 2 frames
	}
	if (Distance < MaxRange * 0.75f)
	{
		return EDetectionPriority::Normal; // Check every 4 frames
	}
	return EDetectionPriority::Low; // Far away - check every 8 frames
}

bool UEnemyDetectionComponent::ShouldCheckEnemyThisFrame(EDetectionPriority Priority) const
{
	switch (Priority)
	{
	case EDetectionPriority::Critical:
		return true;
	case EDetectionPriority::High:
		return (FrameCounter % 2) == 0;
	case EDetectionPriority::Normal:
		return (FrameCounter % 4) == 0;
	case EDetectionPriority::Low:
		return (FrameCounter % 8) == 0;
	default:
		return true;
	}
}

// ========== ENEMY MANAGEMENT ==========

bool UEnemyDetectionComponent::IsEnemy(AActor* Actor) const
{
	if (!Actor || Actor == GetOwner())
	{
		return false;
	}

	// Also check if owner is a controller controlling this actor
	if (AController* Controller = Cast<AController>(GetOwner()))
	{
		if (Controller->GetPawn() == Actor)
		{
			return false;
		}
	}

	// No team = everyone is enemy
	if (TeamID < 0)
	{
		return true;
	}

	// Check other actor's team via their detection component
	// First try on the actor itself
	UEnemyDetectionComponent* OtherDetection = Actor->FindComponentByClass<UEnemyDetectionComponent>();

	// If not found on actor (e.g., tank pawn), try to find via controller
	if (!OtherDetection)
	{
		if (APawn* OtherPawn = Cast<APawn>(Actor))
		{
			if (AController* OtherController = OtherPawn->GetController())
			{
				OtherDetection = OtherController->FindComponentByClass<UEnemyDetectionComponent>();
			}
		}
	}

	if (OtherDetection)
	{
		// Same team = not enemy
		if (OtherDetection->TeamID == TeamID)
		{
			return false;
		}
		// Different team = enemy
		return OtherDetection->TeamID >= 0;
	}

	// No detection component = assume enemy (e.g., human player without AI controller)
	return true;
}

FDetectedEnemyInfo* UEnemyDetectionComponent::FindOrCreateEnemyInfo(AActor* Enemy)
{
	// Find existing entry
	for (FDetectedEnemyInfo& Info : DetectedEnemies)
	{
		if (Info.Enemy.Get() == Enemy)
		{
			return &Info;
		}
	}

	// Check capacity
	if (DetectedEnemies.Num() >= MaxTrackedEnemies)
	{
		// Find lowest priority enemy to replace
		int32 LowestIndex = -1;
		float LowestScore = FLT_MAX;

		for (int32 i = 0; i < DetectedEnemies.Num(); ++i)
		{
			// Don't remove combat targets
			if (DetectedEnemies[i].AwarenessState == EAwarenessState::Combat)
			{
				continue;
			}

			// Score = awareness - time factor
			const float Score = DetectedEnemies[i].AwarenessLevel -
				(DetectedEnemies[i].TimeSinceLastSeen / DetectionConfig.MemoryDuration);

			if (Score < LowestScore)
			{
				LowestScore = Score;
				LowestIndex = i;
			}
		}

		if (LowestIndex >= 0)
		{
			AActor* RemovedEnemy = DetectedEnemies[LowestIndex].Enemy.Get();
			DetectedEnemies.RemoveAt(LowestIndex);
			if (RemovedEnemy)
			{
				OnEnemyLost.Broadcast(RemovedEnemy);
			}
		}
		else
		{
			return nullptr; // All slots occupied by combat targets
		}
	}

	// Create new entry
	FDetectedEnemyInfo NewInfo;
	NewInfo.Enemy = Enemy;
	NewInfo.LastKnownLocation = Enemy->GetActorLocation();

	return &DetectedEnemies.Add_GetRef(NewInfo);
}

void UEnemyDetectionComponent::RemoveEnemy(AActor* Enemy)
{
	for (int32 i = DetectedEnemies.Num() - 1; i >= 0; --i)
	{
		if (DetectedEnemies[i].Enemy.Get() == Enemy)
		{
			OnEnemyLost.Broadcast(Enemy);
			DetectedEnemies.RemoveAt(i);
			return;
		}
	}
}

void UEnemyDetectionComponent::CleanupStaleEntries(float DeltaTime)
{
	for (int32 i = DetectedEnemies.Num() - 1; i >= 0; --i)
	{
		FDetectedEnemyInfo& Info = DetectedEnemies[i];

		// Remove invalid actors
		if (!Info.Enemy.IsValid())
		{
			DetectedEnemies.RemoveAt(i);
			continue;
		}

		// Remove if awareness is 0 and memory has expired
		if (Info.AwarenessLevel <= 0.0f && Info.TimeSinceLastSeen > DetectionConfig.MemoryDuration)
		{
			AActor* Enemy = Info.Enemy.Get();
			DetectedEnemies.RemoveAt(i);
			if (Enemy)
			{
				OnEnemyLost.Broadcast(Enemy);
			}
			continue;
		}
	}
}

// ========== PUBLIC API IMPLEMENTATION ==========

bool UEnemyDetectionComponent::GetPriorityTarget(FDetectedEnemyInfo& OutInfo) const
{
	if (DetectedEnemies.Num() == 0)
	{
		return false;
	}

	const FDetectedEnemyInfo* BestTarget = nullptr;
	float BestScore = -FLT_MAX;

	for (const FDetectedEnemyInfo& Info : DetectedEnemies)
	{
		if (!Info.Enemy.IsValid())
		{
			continue;
		}

		// Scoring: Combat state + firing cone + awareness + proximity
		float Score = Info.AwarenessLevel * 2.0f;

		// Combat state bonus
		if (Info.AwarenessState == EAwarenessState::Combat)
		{
			Score += 10.0f;
		}
		else if (Info.AwarenessState == EAwarenessState::Alerted)
		{
			Score += 5.0f;
		}

		// Firing cone bonus
		if (Info.bInFiringCone)
		{
			Score += 8.0f;
		}

		// Proximity bonus (closer = higher score)
		const float NormalizedDist = Info.Distance / DetectionConfig.MaxDetectionRange;
		Score += (1.0f - NormalizedDist) * 3.0f;

		if (Score > BestScore)
		{
			BestScore = Score;
			BestTarget = &Info;
		}
	}

	if (BestTarget)
	{
		OutInfo = *BestTarget;
		return true;
	}

	return false;
}

TArray<FDetectedEnemyInfo> UEnemyDetectionComponent::GetCombatTargets() const
{
	TArray<FDetectedEnemyInfo> Result;
	Result.Reserve(DetectedEnemies.Num());

	for (const FDetectedEnemyInfo& Info : DetectedEnemies)
	{
		if (Info.AwarenessState == EAwarenessState::Combat && Info.Enemy.IsValid())
		{
			Result.Add(Info);
		}
	}

	return Result;
}

TArray<FDetectedEnemyInfo> UEnemyDetectionComponent::GetEnemiesInFiringCone() const
{
	TArray<FDetectedEnemyInfo> Result;
	Result.Reserve(DetectedEnemies.Num());

	for (const FDetectedEnemyInfo& Info : DetectedEnemies)
	{
		if (Info.bInFiringCone && Info.Enemy.IsValid())
		{
			Result.Add(Info);
		}
	}

	return Result;
}

bool UEnemyDetectionComponent::IsActorDetected(AActor* Actor, FDetectedEnemyInfo& OutInfo) const
{
	for (const FDetectedEnemyInfo& Info : DetectedEnemies)
	{
		if (Info.Enemy.Get() == Actor)
		{
			OutInfo = Info;
			return true;
		}
	}
	return false;
}

int32 UEnemyDetectionComponent::GetEnemyCountByState(EAwarenessState State) const
{
	int32 Count = 0;
	for (const FDetectedEnemyInfo& Info : DetectedEnemies)
	{
		if (Info.AwarenessState == State)
		{
			++Count;
		}
	}
	return Count;
}

bool UEnemyDetectionComponent::HasCombatTarget() const
{
	for (const FDetectedEnemyInfo& Info : DetectedEnemies)
	{
		if (Info.AwarenessState == EAwarenessState::Combat)
		{
			return true;
		}
	}
	return false;
}

bool UEnemyDetectionComponent::HasTargetInFiringCone() const
{
	for (const FDetectedEnemyInfo& Info : DetectedEnemies)
	{
		if (Info.bInFiringCone)
		{
			return true;
		}
	}
	return false;
}

void UEnemyDetectionComponent::ForceDetectionUpdate()
{
	if (GetOwner()->HasAuthority())
	{
		RefreshPotentialTargets();
		UpdateDetection(DetectionUpdateInterval);
	}
}

void UEnemyDetectionComponent::ReportEnemyContact(AActor* Enemy, float InitialAwareness)
{
	if (!GetOwner()->HasAuthority() || !Enemy || !IsEnemy(Enemy))
	{
		return;
	}

	FDetectedEnemyInfo* Info = FindOrCreateEnemyInfo(Enemy);
	if (Info)
	{
		const EAwarenessState PreviousState = Info->AwarenessState;

		Info->AwarenessLevel = FMath::Max(Info->AwarenessLevel, FMath::Clamp(InitialAwareness, 0.0f, 1.0f));
		Info->AwarenessState = GetAwarenessStateFromLevel(Info->AwarenessLevel);
		Info->LastKnownLocation = Enemy->GetActorLocation();
		Info->TimeSinceLastSeen = 0.0f;

		// Fire events
		if (PreviousState == EAwarenessState::Unaware && Info->AwarenessState != EAwarenessState::Unaware)
		{
			OnEnemyDetected.Broadcast(Enemy, *Info);
		}
		if (PreviousState != Info->AwarenessState)
		{
			OnAwarenessStateChanged.Broadcast(Enemy, PreviousState, Info->AwarenessState);
		}
	}
}

void UEnemyDetectionComponent::ClearAllDetections()
{
	for (const FDetectedEnemyInfo& Info : DetectedEnemies)
	{
		if (Info.Enemy.IsValid())
		{
			OnEnemyLost.Broadcast(Info.Enemy.Get());
		}
	}
	DetectedEnemies.Empty();
}

void UEnemyDetectionComponent::SetDetectionEnabled(bool bEnabled)
{
	bDetectionEnabled = bEnabled;
	if (!bEnabled)
	{
		ClearAllDetections();
	}
}

// ========== UTILITY ==========

AWR_Turret* UEnemyDetectionComponent::EnsureTurretCached() const
{
	// Return cached turret if valid
	if (CachedTurret.IsValid())
	{
		return Cast<AWR_Turret>(CachedTurret.Get());
	}

	// Get the pawn (could be owner directly, or controlled pawn if owner is a controller)
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	AActor* PawnActor = nullptr;
	if (AController* Controller = Cast<AController>(Owner))
	{
		PawnActor = Controller->GetPawn();
	}
	else
	{
		PawnActor = Owner;
	}

	if (!PawnActor)
	{
		return nullptr;
	}

	// Try to get turret from tank pawn
	AWR_Tank_Pawn* TankPawn = Cast<AWR_Tank_Pawn>(PawnActor);
	if (!TankPawn)
	{
		// Log warning once
		if (!bTurretSetupLogged)
		{
			bTurretSetupLogged = true;
			UE_LOG(LogTemp, Error, TEXT("EnemyDetection: Pawn is not AWR_Tank_Pawn! Detection will use pawn forward."));
		}
		return nullptr;
	}

	AActor* TurretActor = TankPawn->GetTurret_Implementation();
	AWR_Turret* Turret = Cast<AWR_Turret>(TurretActor);

	if (Turret)
	{
		// Cache for future calls
		CachedTurret = Turret;

		// Log once when turret is found
		if (!bTurretSetupLogged)
		{
			bTurretSetupLogged = true;
			UE_LOG(LogTemp, Log, TEXT("========================================"));
			UE_LOG(LogTemp, Log, TEXT("EnemyDetection: TURRET DETECTED"));
			UE_LOG(LogTemp, Log, TEXT("  -> Turret: %s"), *Turret->GetName());
			UE_LOG(LogTemp, Log, TEXT("  -> PitchComponent: %s"), Turret->PitchComponent ? TEXT("YES") : TEXT("NO"));
			UE_LOG(LogTemp, Log, TEXT("  -> YawComponent: %s"), Turret->YawComponent ? TEXT("YES") : TEXT("NO"));
			UE_LOG(LogTemp, Log, TEXT("  -> Detection will follow TURRET direction"));
			UE_LOG(LogTemp, Log, TEXT("========================================"));
		}
	}
	else if (!bTurretSetupLogged)
	{
		bTurretSetupLogged = true;
		UE_LOG(LogTemp, Error, TEXT("EnemyDetection: TURRET NOT FOUND on tank %s!"), *TankPawn->GetName());
	}

	return Turret;
}

void UEnemyDetectionComponent::GetEyeLocationAndDirection(FVector& OutLocation, FVector& OutDirection) const
{
	// Try to get turret (caches it for subsequent calls)
	AWR_Turret* Turret = EnsureTurretCached();

	if (Turret)
	{
		// Use PitchComponent for both location and direction (barrel origin)
		if (Turret->PitchComponent)
		{
			OutLocation = Turret->PitchComponent->GetComponentLocation();
			OutDirection = Turret->PitchComponent->GetForwardVector();
			return;
		}
		// Fallback to YawComponent for direction
		if (Turret->YawComponent)
		{
			OutLocation = Turret->GetActorLocation();
			OutDirection = Turret->YawComponent->GetForwardVector();
			return;
		}
		// Fallback to turret actor
		OutLocation = Turret->GetActorLocation();
		OutDirection = Turret->GetActorForwardVector();
		return;
	}

	// Fallback to pawn
	AActor* Owner = GetOwner();
	AActor* PawnActor = nullptr;

	if (AController* Controller = Cast<AController>(Owner))
	{
		PawnActor = Controller->GetPawn();
	}
	else
	{
		PawnActor = Owner;
	}

	if (PawnActor)
	{
		// Try socket first
		if (EyeSocketName != NAME_None)
		{
			if (USkeletalMeshComponent* Mesh = PawnActor->FindComponentByClass<USkeletalMeshComponent>())
			{
				if (Mesh->DoesSocketExist(EyeSocketName))
				{
					OutLocation = Mesh->GetSocketLocation(EyeSocketName);
					OutDirection = PawnActor->GetActorForwardVector();
					return;
				}
			}
		}

		OutLocation = PawnActor->GetActorLocation() + PawnActor->GetActorRotation().RotateVector(EyeOffset);
		OutDirection = PawnActor->GetActorForwardVector();
		return;
	}

	OutLocation = FVector::ZeroVector;
	OutDirection = FVector::ForwardVector;
}

FVector UEnemyDetectionComponent::GetEyeLocation() const
{
	FVector Location, Direction;
	GetEyeLocationAndDirection(Location, Direction);
	return Location;
}

FVector UEnemyDetectionComponent::GetLookDirection() const
{
	FVector Location, Direction;
	GetEyeLocationAndDirection(Location, Direction);
	return Direction;
}

float UEnemyDetectionComponent::CalculateVisibilityTo(AActor* Target) const
{
	FVector DummyLocation;
	uint8 DummyMask;
	return CalculateVisibilityToTarget(Target, DummyLocation, DummyMask);
}

AWR_Tank_Pawn* UEnemyDetectionComponent::GetOwnerTank() const
{
	if (!CachedOwnerTank.IsValid())
	{
		AActor* Owner = GetOwner();
		if (!Owner)
		{
			return nullptr;
		}

		// Check if owner is a controller - if so, get the controlled pawn
		if (AController* Controller = Cast<AController>(Owner))
		{
			CachedOwnerTank = Cast<AWR_Tank_Pawn>(Controller->GetPawn());
		}
		else
		{
			CachedOwnerTank = Cast<AWR_Tank_Pawn>(Owner);
		}
	}
	return CachedOwnerTank.Get();
}

// ========== AI OBSERVATION HELPERS ==========

float UEnemyDetectionComponent::GetNearestEnemyDistanceNormalized() const
{
	float MinDist = DetectionConfig.MaxDetectionRange;

	for (const FDetectedEnemyInfo& Info : DetectedEnemies)
	{
		if (Info.AwarenessState != EAwarenessState::Unaware && Info.Distance < MinDist)
		{
			MinDist = Info.Distance;
		}
	}

	return MinDist / DetectionConfig.MaxDetectionRange;
}

float UEnemyDetectionComponent::GetNearestEnemyAngleNormalized() const
{
	float NearestAngle = 0.0f;
	float MinDist = FLT_MAX;

	for (const FDetectedEnemyInfo& Info : DetectedEnemies)
	{
		if (Info.AwarenessState != EAwarenessState::Unaware && Info.Distance < MinDist)
		{
			MinDist = Info.Distance;
			NearestAngle = Info.AngleToEnemy;
		}
	}

	// Normalize: -180..180 -> -1..1
	return NearestAngle / 180.0f;
}

TArray<float> UEnemyDetectionComponent::GetSectorThreatLevels() const
{
	// 8 sectors: N, NE, E, SE, S, SW, W, NW (each 45 degrees)
	TArray<float> Sectors;
	Sectors.SetNumZeroed(8);

	for (const FDetectedEnemyInfo& Info : DetectedEnemies)
	{
		if (Info.AwarenessState == EAwarenessState::Unaware)
		{
			continue;
		}

		// Convert angle to sector index
		// AngleToEnemy: -180 to 180, 0 = front
		float NormalizedAngle = Info.AngleToEnemy + 180.0f; // 0 to 360
		if (NormalizedAngle >= 360.0f) NormalizedAngle -= 360.0f;

		const int32 SectorIndex = FMath::Clamp(FMath::FloorToInt(NormalizedAngle / 45.0f), 0, 7);

		// Threat = awareness * inverse distance
		const float DistFactor = 1.0f - (Info.Distance / DetectionConfig.MaxDetectionRange);
		const float Threat = Info.AwarenessLevel * DistFactor;

		Sectors[SectorIndex] = FMath::Max(Sectors[SectorIndex], Threat);
	}

	return Sectors;
}

float UEnemyDetectionComponent::GetMaxAwarenessLevel() const
{
	float MaxLevel = 0.0f;

	for (const FDetectedEnemyInfo& Info : DetectedEnemies)
	{
		if (Info.AwarenessLevel > MaxLevel)
		{
			MaxLevel = Info.AwarenessLevel;
		}
	}

	return MaxLevel;
}

// ========== DEBUG ==========

void UEnemyDetectionComponent::DrawDebugVisualization() const
{
#if ENABLE_DRAW_DEBUG
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector EyeLocation = GetEyeLocation();
	const FVector LookDir = GetLookDirection().GetSafeNormal();
	const float DrawTime = DebugDrawDuration > 0.0f ? DebugDrawDuration : -1.0f;

	// Ensure we have valid direction
	if (LookDir.IsNearlyZero())
	{
		return;
	}

	// Calculate cone display length (proportional to detection range, but not too long)
	const float ConeLength = FMath::Min(DetectionConfig.MaxDetectionRange * 0.5f, 2000.0f);

	// Draw main direction line (turret forward) - GREEN for clarity
	DrawDebugLine(
		World,
		EyeLocation,
		EyeLocation + LookDir * ConeLength,
		FColor::Green,
		false,
		DrawTime,
		0,
		3.0f
	);

	// Draw FOV cone edges (left and right boundaries)
	const float FOVRad = FMath::DegreesToRadians(DetectionConfig.DetectionFOVHalfAngle);
	const FVector RightDir = FRotationMatrix(FRotator(0, FMath::RadiansToDegrees(FOVRad), 0)).TransformVector(LookDir);
	const FVector LeftDir = FRotationMatrix(FRotator(0, -FMath::RadiansToDegrees(FOVRad), 0)).TransformVector(LookDir);

	// Main FOV edges - YELLOW
	DrawDebugLine(
		World,
		EyeLocation,
		EyeLocation + RightDir * ConeLength,
		FColor::Yellow,
		false,
		DrawTime,
		0,
		2.0f
	);
	DrawDebugLine(
		World,
		EyeLocation,
		EyeLocation + LeftDir * ConeLength,
		FColor::Yellow,
		false,
		DrawTime,
		0,
		2.0f
	);

	// Draw FOV arc at the end
	const int32 ArcSegments = 16;
	const float AngleStep = (2.0f * FOVRad) / ArcSegments;
	for (int32 i = 0; i < ArcSegments; ++i)
	{
		const float Angle1 = -FOVRad + i * AngleStep;
		const float Angle2 = -FOVRad + (i + 1) * AngleStep;

		const FVector Dir1 = FRotationMatrix(FRotator(0, FMath::RadiansToDegrees(Angle1), 0)).TransformVector(LookDir);
		const FVector Dir2 = FRotationMatrix(FRotator(0, FMath::RadiansToDegrees(Angle2), 0)).TransformVector(LookDir);

		DrawDebugLine(
			World,
			EyeLocation + Dir1 * ConeLength,
			EyeLocation + Dir2 * ConeLength,
			FColor::Yellow,
			false,
			DrawTime,
			0,
			1.5f
		);
	}

	// Draw peripheral vision edges (dimmer orange)
	const float TotalAngle = DetectionConfig.DetectionFOVHalfAngle + DetectionConfig.PeripheralVisionAngle;
	const float TotalRad = FMath::DegreesToRadians(TotalAngle);
	const FVector RightPeripheral = FRotationMatrix(FRotator(0, TotalAngle, 0)).TransformVector(LookDir);
	const FVector LeftPeripheral = FRotationMatrix(FRotator(0, -TotalAngle, 0)).TransformVector(LookDir);

	DrawDebugLine(
		World,
		EyeLocation,
		EyeLocation + RightPeripheral * ConeLength,
		FColor(128, 64, 0),
		false,
		DrawTime,
		0,
		1.0f
	);
	DrawDebugLine(
		World,
		EyeLocation,
		EyeLocation + LeftPeripheral * ConeLength,
		FColor(128, 64, 0),
		false,
		DrawTime,
		0,
		1.0f
	);

	// Draw detected enemies
	for (const FDetectedEnemyInfo& Info : DetectedEnemies)
	{
		if (!Info.Enemy.IsValid())
		{
			continue;
		}

		// Color based on awareness state
		FColor Color;
		switch (Info.AwarenessState)
		{
		case EAwarenessState::Suspicious:
			Color = FColor::Yellow;
			break;
		case EAwarenessState::Alerted:
			Color = FColor::Orange;
			break;
		case EAwarenessState::Combat:
			Color = FColor::Red;
			break;
		default:
			Color = FColor(128, 128, 128);
			break;
		}

		// Line to enemy
		DrawDebugLine(
			World,
			EyeLocation,
			Info.LastKnownLocation,
			Color,
			false,
			DrawTime,
			0,
			Info.bInFiringCone ? 3.0f : 1.5f
		);

		// Awareness sphere
		const float SphereRadius = 20.0f + 30.0f * Info.AwarenessLevel;
		DrawDebugSphere(
			World,
			Info.LastKnownLocation + FVector(0, 0, 100),
			SphereRadius,
			8,
			Color,
			false,
			DrawTime
		);

		// Info text
		const FString InfoText = FString::Printf(
			TEXT("V:%.0f%% A:%.0f%% D:%.0fm"),
			Info.VisibilityPercent * 100.0f,
			Info.AwarenessLevel * 100.0f,
			Info.Distance / 100.0f
		);

		DrawDebugString(
			World,
			Info.LastKnownLocation + FVector(0, 0, 150),
			InfoText,
			nullptr,
			Color,
			DrawTime,
			false,
			1.0f
		);

		// Firing cone indicator
		if (Info.bInFiringCone)
		{
			DrawDebugSphere(
				World,
				Info.LastKnownLocation + FVector(0, 0, 50),
				15.0f,
				6,
				FColor::Green,
				false,
				DrawTime
			);
		}
	}

	// Draw range circle
	DrawDebugCircle(
		World,
		EyeLocation,
		DetectionConfig.MaxDetectionRange,
		48,
		FColor(100, 100, 100),
		false,
		DrawTime,
		0,
		1.0f,
		FVector(1, 0, 0),
		FVector(0, 1, 0),
		false
	);
#endif
}
