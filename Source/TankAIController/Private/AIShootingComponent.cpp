// Copyright ArenaBreakers. All Rights Reserved.

#include "AIShootingComponent.h"
#include "WR_Tank_Pawn.h"
#include "WR_Turret.h"
#include "EnemyDetectionComponent.h"
#include "DrawDebugHelpers.h"
#include "Kismet/KismetMathLibrary.h"

UAIShootingComponent::UAIShootingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// Apply default difficulty preset
	ApplyDifficultyPreset(Difficulty);
}

void UAIShootingComponent::BeginPlay()
{
	Super::BeginPlay();

	// Apply difficulty preset (in case it was changed in editor)
	if (Difficulty != EAIDifficulty::Custom)
	{
		ApplyDifficultyPreset(Difficulty);
	}
}

void UAIShootingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bShootingEnabled)
	{
		UpdateShooting(DeltaTime);
	}

	if (bDrawDebug)
	{
		DrawDebugVisualization();
	}
}

// ============================================================================
// PUBLIC API - TARGET MANAGEMENT
// ============================================================================

void UAIShootingComponent::SetTarget(AActor* NewTarget, const FDetectedEnemyInfo& EnemyInfo)
{
	if (!NewTarget)
	{
		ClearTarget(EEngagementEndReason::TargetLost);
		return;
	}

	// Check if target changed
	const bool bTargetChanged = (CurrentTarget.Get() != NewTarget);

	CurrentTarget = NewTarget;
	LastKnownTargetPosition = NewTarget->GetActorLocation();
	CachedTargetVisibility = EnemyInfo.VisibilityPercent;

	if (bTargetChanged)
	{
		// Reset state for new target
		bJustSwitchedTarget = true;
		ShootingState.bTargetAcquired = false;
		ShootingState.TimeOnTarget = 0.0f;
		ShootingState.CurrentSpread = Config.BaseSpread;
		ShootingState.CurrentBurstShots = 0;
		ShootingState.bInBurst = false;

		// Generate new reaction time
		ShootingState.ReactionTimeRemaining = GenerateReactionTime();
		ShootingState.State = EShootingState::Acquiring;

		// Select weapon for new engagement
		UpdateContext();
		UpdateWeaponSelection();

		// Broadcast engagement started
		OnEngagementStarted.Broadcast(NewTarget, ShootingState.SelectedWeapon);

		LogShootingEvent(FString::Printf(TEXT("New target acquired: %s (Reaction: %.2fs)"),
			*NewTarget->GetName(), ShootingState.ReactionTimeRemaining));
	}
}

void UAIShootingComponent::ClearTarget(EEngagementEndReason Reason)
{
	AActor* PreviousTarget = CurrentTarget.Get();

	if (PreviousTarget)
	{
		// Broadcast engagement ended
		OnEngagementEnded.Broadcast(PreviousTarget, Reason);
		LogShootingEvent(FString::Printf(TEXT("Target lost: %s (Reason: %d)"),
			*PreviousTarget->GetName(), static_cast<int32>(Reason)));
	}

	CurrentTarget.Reset();
	ShootingState.Reset();
	ShootingState.State = EShootingState::Idle;
	Context = FShootingContext();
}

// ============================================================================
// PUBLIC API - CONFIGURATION
// ============================================================================

void UAIShootingComponent::ApplyDifficultyPreset(EAIDifficulty NewDifficulty)
{
	Difficulty = NewDifficulty;

	if (NewDifficulty != EAIDifficulty::Custom)
	{
		FAIDifficultyPreset Preset = FAIDifficultyPreset::GetPreset(NewDifficulty);
		Config = Preset.Config;
	}

	// Reset current spread to new base
	ShootingState.CurrentSpread = Config.BaseSpread;
}

void UAIShootingComponent::SetShootingEnabled(bool bEnabled)
{
	bShootingEnabled = bEnabled;

	if (!bEnabled)
	{
		// Stop any active firing
		ShootingState.bIsFiringPrimary = false;
		ShootingState.bIsFiringSecondary = false;
	}
}

void UAIShootingComponent::SetOwnerTank(AWR_Tank_Pawn* Tank)
{
	OwnerTank = Tank;

	if (Tank)
	{
		// Cache turret reference
		AActor* TurretActor = Tank->GetTurret_Implementation();
		OwnerTurret = Cast<AWR_Turret>(TurretActor);
	}
}

void UAIShootingComponent::SetEnemyDetectionComponent(UEnemyDetectionComponent* DetectionComp)
{
	EnemyDetection = DetectionComp;
}

void UAIShootingComponent::SetSelectedWeapon(EWeaponSlot NewWeapon)
{
	if (ShootingState.SelectedWeapon != NewWeapon)
	{
		EWeaponSlot OldWeapon = ShootingState.SelectedWeapon;
		ShootingState.SelectedWeapon = NewWeapon;

		// Reset burst when switching weapons
		ShootingState.CurrentBurstShots = 0;
		ShootingState.bInBurst = false;

		OnWeaponSwitched.Broadcast(OldWeapon, NewWeapon);
	}
}

// ============================================================================
// INTERNAL METHODS - MAIN UPDATE
// ============================================================================

void UAIShootingComponent::UpdateShooting(float DeltaTime)
{
	// No target - nothing to do
	if (!CurrentTarget.IsValid())
	{
		ShootingState.bIsFiringPrimary = false;
		ShootingState.bIsFiringSecondary = false;
		return;
	}

	// Update context information
	UpdateContext();

	// Update cooldowns regardless of state
	if (ShootingState.SecondaryCooldownTimer > 0.0f)
	{
		ShootingState.SecondaryCooldownTimer -= DeltaTime;
	}

	if (ShootingState.WeaponSwitchTimer > 0.0f)
	{
		ShootingState.WeaponSwitchTimer -= DeltaTime;
	}

	// State machine
	switch (ShootingState.State)
	{
	case EShootingState::Idle:
		// Should not happen with valid target
		ShootingState.State = EShootingState::Acquiring;
		break;

	case EShootingState::Acquiring:
		UpdateReactionTime(DeltaTime);
		if (ShootingState.bTargetAcquired)
		{
			ShootingState.State = EShootingState::Tracking;
		}
		break;

	case EShootingState::Tracking:
		UpdateAccuracy(DeltaTime);
		UpdateWeaponSelection();
		if (CanFire(ShootingState.SelectedWeapon))
		{
			ShootingState.State = EShootingState::Firing;
			// Start new burst
			ShootingState.TargetBurstSize = GenerateBurstSize();
			ShootingState.CurrentBurstShots = 0;
			ShootingState.bInBurst = true;
			ShootingState.BurstShotTimer = 0.0f;
		}
		break;

	case EShootingState::Firing:
		UpdateAccuracy(DeltaTime);
		UpdateFireControl(DeltaTime);
		ExecuteFiring();
		if (!ShootingState.bInBurst)
		{
			ShootingState.State = EShootingState::Cooldown;
			ShootingState.BurstCooldownTimer = Config.BurstCooldown;
		}
		break;

	case EShootingState::Cooldown:
		UpdateAccuracy(DeltaTime);
		ShootingState.BurstCooldownTimer -= DeltaTime;
		if (ShootingState.BurstCooldownTimer <= 0.0f)
		{
			ShootingState.State = EShootingState::Tracking;
		}
		// Stop firing during cooldown
		ShootingState.bIsFiringPrimary = false;
		ShootingState.bIsFiringSecondary = false;
		break;

	default:
		break;
	}

	// Update aim location every frame
	ShootingState.AdjustedAimLocation = CalculatePerfectAimPosition();

	// Apply spread
	if (ShootingState.bTargetAcquired)
	{
		ShootingState.AdjustedAimLocation = ApplySpreadToAim(ShootingState.AdjustedAimLocation);
	}

	// Check for intentional miss
	if (ShootingState.State == EShootingState::Firing && ShootingState.BurstShotTimer <= 0.0f)
	{
		float MissChance = CalculateMissChance();
		if (FMath::FRand() < MissChance)
		{
			ShootingState.LastMissType = DetermineMissType();
			ShootingState.AdjustedAimLocation = ApplyIntentionalMiss(
				ShootingState.AdjustedAimLocation, ShootingState.LastMissType);
		}
		else
		{
			ShootingState.LastMissType = EAIMissType::None;
		}
	}

	// Reset target switch flag
	bJustSwitchedTarget = false;
}

void UAIShootingComponent::UpdateContext()
{
	if (!CurrentTarget.IsValid())
	{
		return;
	}

	AActor* Target = CurrentTarget.Get();
	Context.Target = Target;
	Context.TargetLocation = Target->GetActorLocation();
	Context.TargetVelocity = Target->GetVelocity();

	// Get turret/muzzle location for accurate calculations
	// NOTE: GetOwner() returns the AIController which has no meaningful world position!
	// We must use the actual tank/turret location instead.
	FVector ShooterLocation = GetMuzzleLocation();

	// Distance
	Context.DistanceToTarget = FVector::Dist(ShooterLocation, Context.TargetLocation);
	ShootingState.DistanceToTarget = Context.DistanceToTarget;

	// Target stationary check
	Context.bTargetIsStationary = Context.TargetVelocity.Size() < Config.LeadVelocityThreshold;

	// Angle to target - from turret direction to target
	FVector ToTarget = (Context.TargetLocation - ShooterLocation).GetSafeNormal();
	FVector TurretDir = GetTurretDirection();
	Context.AngleToTarget = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(TurretDir, ToTarget)));

	// Use visibility from EnemyDetectionComponent (already computed with multi-raycast)
	// No need for redundant LOS check - CachedTargetVisibility is updated in SetTarget()
	Context.bHasLineOfSight = (CachedTargetVisibility > 0.0f);

	// Owner state
	AWR_Tank_Pawn* Tank = GetOwnerTank();
	if (Tank)
	{
		Context.OwnerSpeed = Tank->GetVelocity().Size();
		// TODO: Get actual health from tank
		Context.OwnerHealthPercent = 1.0f;
	}

	// Enemy count
	if (EnemyDetection.IsValid())
	{
		Context.EnemyCount = EnemyDetection->GetDetectedEnemyCount();
	}

	// Ammo counts
	Context.PrimaryAmmo = GetAmmoCount(EWeaponSlot::Primary);
	Context.SecondaryAmmo = GetAmmoCount(EWeaponSlot::Secondary);

	// Just acquired flag
	Context.bJustAcquired = bJustSwitchedTarget;
}

void UAIShootingComponent::UpdateReactionTime(float DeltaTime)
{
	if (ShootingState.bTargetAcquired)
	{
		return;
	}

	ShootingState.ReactionTimeRemaining -= DeltaTime;

	if (ShootingState.ReactionTimeRemaining <= 0.0f)
	{
		ShootingState.bTargetAcquired = true;
		ShootingState.ReactionTimeRemaining = 0.0f;
	}
}

void UAIShootingComponent::UpdateAccuracy(float DeltaTime)
{
	// Increase time on target
	ShootingState.TimeOnTarget += DeltaTime;

	// Calculate new spread
	ShootingState.CurrentSpread = CalculateCurrentSpread();
}

void UAIShootingComponent::UpdateWeaponSelection()
{
	EWeaponSlot BestWeapon = SelectBestWeapon();

	if (BestWeapon != ShootingState.SelectedWeapon)
	{
		// Hysteresis: Don't switch weapons too rapidly
		// Only switch if we've been on current weapon for at least 0.5 seconds
		// OR if switching TO secondary (prioritize using secondary when conditions are met)
		const float MinWeaponSwitchTime = 0.5f;
		const bool bCanSwitch = (ShootingState.WeaponSwitchTimer <= 0.0f) ||
								(BestWeapon == EWeaponSlot::Secondary);

		if (bCanSwitch)
		{
			SetSelectedWeapon(BestWeapon);
			ShootingState.WeaponSwitchTimer = MinWeaponSwitchTime;
		}
	}
}

void UAIShootingComponent::UpdateFireControl(float DeltaTime)
{
	if (!ShootingState.bInBurst)
	{
		return;
	}

	ShootingState.BurstShotTimer -= DeltaTime;
}

void UAIShootingComponent::ExecuteFiring()
{
	if (!ShootingState.bInBurst || ShootingState.BurstShotTimer > 0.0f)
	{
		ShootingState.bIsFiringPrimary = false;
		ShootingState.bIsFiringSecondary = false;
		return;
	}

	// Fire the shot
	if (ShootingState.SelectedWeapon == EWeaponSlot::Primary)
	{
		ShootingState.bIsFiringPrimary = true;
		ShootingState.bIsFiringSecondary = false;
	}
	else
	{
		ShootingState.bIsFiringPrimary = false;
		ShootingState.bIsFiringSecondary = true;

		// Apply secondary cooldown
		ShootingState.SecondaryCooldownTimer = Config.SecondaryCooldown;
	}

	// Broadcast shot fired
	OnShotFired.Broadcast(ShootingState.SelectedWeapon, ShootingState.AdjustedAimLocation,
		ShootingState.LastMissType != EAIMissType::None);

	// Advance burst
	ShootingState.CurrentBurstShots++;
	ShootingState.BurstShotTimer = Config.TimeBetweenBurstShots;

	// Check if burst complete
	if (ShootingState.CurrentBurstShots >= ShootingState.TargetBurstSize)
	{
		ShootingState.bInBurst = false;
		ShootingState.bIsFiringPrimary = false;
		ShootingState.bIsFiringSecondary = false;
	}

	// Secondary always single shot - end burst but keep firing flag for this frame
	if (ShootingState.SelectedWeapon == EWeaponSlot::Secondary)
	{
		ShootingState.bInBurst = false;
		// Note: bIsFiringSecondary stays true for this frame so the fire command is sent
		// It will be reset to false at the start of next ExecuteFiring call
	}
}

// ============================================================================
// INTERNAL METHODS - CALCULATIONS
// ============================================================================

FVector UAIShootingComponent::CalculatePerfectAimPosition() const
{
	if (!CurrentTarget.IsValid())
	{
		return LastKnownTargetPosition;
	}

	FVector TargetPos = CurrentTarget->GetActorLocation();
	FVector TargetVel = CurrentTarget->GetVelocity();

	// Calculate lead for moving targets
	if (bUseTargetPrediction && TargetVel.Size() > Config.LeadVelocityThreshold)
	{
		float ProjectileSpeed = (ShootingState.SelectedWeapon == EWeaponSlot::Primary)
			? Config.PrimaryProjectileSpeed
			: Config.SecondaryProjectileSpeed;

		FVector LeadPos = CalculateLeadPosition(TargetPos, TargetVel, ProjectileSpeed);

		// Apply lead accuracy (AI doesn't perfectly calculate lead)
		TargetPos = FMath::Lerp(TargetPos, LeadPos, Config.LeadAccuracy);

		// Store lead position for debug visualization (const_cast required in const method)
		const_cast<UAIShootingComponent*>(this)->ShootingState.LeadPosition = LeadPos;
	}

	// Apply grenade arc compensation for secondary weapon
	if (ShootingState.SelectedWeapon == EWeaponSlot::Secondary && bUseGrenadeArcCompensation)
	{
		float ArcOffset = CalculateGrenadeArcOffset(Context.DistanceToTarget);
		TargetPos.Z += ArcOffset;
	}

	return TargetPos;
}

FVector UAIShootingComponent::CalculateLeadPosition(const FVector& TargetPos, const FVector& TargetVel, float ProjectileSpeed) const
{
	FVector ShooterPos = GetMuzzleLocation();
	float Distance = FVector::Dist(ShooterPos, TargetPos);

	// Initial time estimate
	float TimeToTarget = Distance / ProjectileSpeed;

	// Iterative refinement (2 iterations)
	for (int32 i = 0; i < 2; i++)
	{
		FVector PredictedPos = TargetPos + (TargetVel * TimeToTarget);
		Distance = FVector::Dist(ShooterPos, PredictedPos);
		TimeToTarget = Distance / ProjectileSpeed;
	}

	return TargetPos + (TargetVel * TimeToTarget);
}

float UAIShootingComponent::CalculateGrenadeArcOffset(float Distance) const
{
	if (Distance < Config.GrenadeArcStartDistance)
	{
		return 0.0f;
	}

	// Normalize distance (0-1 over arc range)
	float MaxArcDistance = Config.SecondaryMaxRange;
	float NormalizedDist = (Distance - Config.GrenadeArcStartDistance) /
		(MaxArcDistance - Config.GrenadeArcStartDistance);
	NormalizedDist = FMath::Clamp(NormalizedDist, 0.0f, 1.0f);

	// Apply curve exponent
	float ArcFactor = FMath::Pow(NormalizedDist, Config.GrenadeArcExponent);

	return ArcFactor * Config.GrenadeArcMaxOffset;
}

FVector UAIShootingComponent::ApplySpreadToAim(const FVector& PerfectAim) const
{
	if (ShootingState.CurrentSpread <= 0.0f)
	{
		return PerfectAim;
	}

	FVector ShooterPos = GetMuzzleLocation();
	FVector AimDir = (PerfectAim - ShooterPos).GetSafeNormal();

	// Random point in cone
	float RandomAngle = FMath::RandRange(0.0f, 360.0f);
	float RandomSpread = FMath::RandRange(0.0f, ShootingState.CurrentSpread);

	// Convert to rotation offset
	FRotator AimRotation = AimDir.Rotation();
	AimRotation.Pitch += FMath::Sin(FMath::DegreesToRadians(RandomAngle)) * RandomSpread;
	AimRotation.Yaw += FMath::Cos(FMath::DegreesToRadians(RandomAngle)) * RandomSpread;

	// Convert back to position
	FVector SpreadDir = AimRotation.Vector();
	float Distance = FVector::Dist(ShooterPos, PerfectAim);

	return ShooterPos + (SpreadDir * Distance);
}

float UAIShootingComponent::CalculateCurrentSpread() const
{
	float Spread = Config.BaseSpread;

	// Zeroing: Spread decreases over time on target
	Spread -= (ShootingState.TimeOnTarget * Config.ZeroingRate);

	// Movement penalty
	if (Context.OwnerSpeed > Config.MovementSpreadThreshold)
	{
		Spread += Config.MovementSpreadPenalty;
	}

	// Target just switched - reset to base
	if (bJustSwitchedTarget)
	{
		Spread = Config.BaseSpread;
	}

	return FMath::Clamp(Spread, Config.MinSpread, Config.MaxSpread);
}

float UAIShootingComponent::CalculateMissChance() const
{
	float MissChance = Config.BaseMissChance;

	// Moving target bonus
	if (!Context.bTargetIsStationary)
	{
		MissChance += Config.MovingTargetMissBonus;
	}

	// Distance bonus
	float DistanceFactor = Context.DistanceToTarget / Config.PrimaryEffectiveRange;
	MissChance += DistanceFactor * Config.DistanceMissBonus;

	// Panic bonus (low HP)
	if (Context.OwnerHealthPercent < 0.3f)
	{
		MissChance += Config.PanicMissBonus;
	}

	// Multiple enemies
	if (Context.EnemyCount > 2)
	{
		MissChance += 0.05f;
	}

	return FMath::Clamp(MissChance, 0.0f, Config.MaxMissChance);
}

EAIMissType UAIShootingComponent::DetermineMissType() const
{
	// Weighted random selection based on expanded context
	const float Roll = FMath::FRand();

	// Calculate context factors
	const float DistanceNormalized = FMath::Clamp(Context.DistanceToTarget / Config.PrimaryEffectiveRange, 0.0f, 1.0f);
	const bool bOwnerMoving = Context.OwnerSpeed > Config.MovementSpreadThreshold;
	const float TargetSpeed = Context.TargetVelocity.Size();
	const bool bPanic = Context.OwnerHealthPercent < 0.3f;

	// Calculate target heading relative to shooter (crossing vs approaching/retreating)
	// CrossingFactor: 0 = approaching/retreating, 1 = perpendicular crossing
	float CrossingFactor = 0.0f;
	if (TargetSpeed > Config.LeadVelocityThreshold)
	{
		const FVector ToTarget = (Context.TargetLocation - GetMuzzleLocation()).GetSafeNormal();
		const FVector TargetMoveDir = Context.TargetVelocity.GetSafeNormal();
		CrossingFactor = 1.0f - FMath::Abs(FVector::DotProduct(ToTarget, TargetMoveDir));
	}

	if (!Context.bTargetIsStationary)
	{
		// === MOVING TARGET ===

		// High speed + crossing = tracking loss more likely
		if (TargetSpeed > 500.0f && CrossingFactor > 0.6f && Roll < 0.35f)
		{
			return EAIMissType::TrackingLoss;
		}

		// Long distance = lead misjudgment (overshoot/undershoot more likely)
		if (DistanceNormalized > 0.5f)
		{
			// Approaching targets -> tend to undershoot, retreating -> overshoot
			const FVector ToTarget = (Context.TargetLocation - GetMuzzleLocation()).GetSafeNormal();
			const float ApproachFactor = FVector::DotProduct(ToTarget, Context.TargetVelocity.GetSafeNormal());

			if (ApproachFactor > 0.3f) // Target retreating
			{
				if (Roll < 0.50f) return EAIMissType::Overshoot;
				if (Roll < 0.80f) return EAIMissType::Undershoot;
				return EAIMissType::TrackingLoss;
			}
			else if (ApproachFactor < -0.3f) // Target approaching
			{
				if (Roll < 0.30f) return EAIMissType::Overshoot;
				if (Roll < 0.75f) return EAIMissType::Undershoot;
				return EAIMissType::TrackingLoss;
			}
			else // Crossing
			{
				if (Roll < 0.40f) return EAIMissType::TrackingLoss;
				if (Roll < 0.70f) return EAIMissType::Overshoot;
				return EAIMissType::Undershoot;
			}
		}

		// Own movement = flinch more likely
		if (bOwnerMoving && Roll < 0.4f)
		{
			return EAIMissType::Flinch;
		}

		// Default moving target distribution
		if (Roll < 0.35f) return EAIMissType::Overshoot;
		if (Roll < 0.65f) return EAIMissType::Undershoot;
		if (Roll < 0.85f) return EAIMissType::TrackingLoss;
		return EAIMissType::Flinch;
	}
	else
	{
		// === STATIONARY TARGET ===

		// Panic = erratic shots
		if (bPanic && Roll < 0.5f)
		{
			return EAIMissType::PanicShot;
		}

		// Own movement = flinch dominant
		if (bOwnerMoving)
		{
			if (Roll < 0.6f) return EAIMissType::Flinch;
			if (Roll < 0.85f) return EAIMissType::Overshoot;
			return EAIMissType::PanicShot;
		}

		// Long distance = slight misjudgment even for stationary targets
		if (DistanceNormalized > 0.6f && Roll < 0.35f)
		{
			return EAIMissType::Overshoot;
		}

		// Default stationary distribution
		if (Roll < 0.7f) return EAIMissType::Flinch;
		return EAIMissType::Overshoot;
	}
}

FVector UAIShootingComponent::ApplyIntentionalMiss(const FVector& AimPos, EAIMissType MissType) const
{
	if (MissType == EAIMissType::None)
	{
		return AimPos;
	}

	FVector ShooterPos = GetMuzzleLocation();
	FVector AimDir = (AimPos - ShooterPos).GetSafeNormal();
	float Distance = FVector::Dist(ShooterPos, AimPos);

	FVector MissOffset = FVector::ZeroVector;
	float OffsetMagnitude = FMath::Tan(FMath::DegreesToRadians(Config.MissAngleOffset)) * Distance;

	switch (MissType)
	{
	case EAIMissType::Overshoot:
		// Miss ahead of target movement
		if (Context.TargetVelocity.Size() > 10.0f)
		{
			MissOffset = Context.TargetVelocity.GetSafeNormal() * OffsetMagnitude;
		}
		else
		{
			MissOffset = AimDir * OffsetMagnitude * 0.5f;
		}
		break;

	case EAIMissType::Undershoot:
		// Miss behind target movement
		if (Context.TargetVelocity.Size() > 10.0f)
		{
			MissOffset = -Context.TargetVelocity.GetSafeNormal() * OffsetMagnitude;
		}
		else
		{
			MissOffset = -AimDir * OffsetMagnitude * 0.3f;
		}
		break;

	case EAIMissType::Flinch:
		// Random small offset
		MissOffset = FVector(
			FMath::RandRange(-1.0f, 1.0f),
			FMath::RandRange(-1.0f, 1.0f),
			FMath::RandRange(-0.5f, 0.5f)
		).GetSafeNormal() * OffsetMagnitude * 0.7f;
		break;

	case EAIMissType::TrackingLoss:
		// Larger random offset
		MissOffset = FVector(
			FMath::RandRange(-1.0f, 1.0f),
			FMath::RandRange(-1.0f, 1.0f),
			FMath::RandRange(-0.3f, 0.3f)
		).GetSafeNormal() * OffsetMagnitude * 1.5f;
		break;

	case EAIMissType::PanicShot:
		// Large erratic offset
		MissOffset = FVector(
			FMath::RandRange(-1.0f, 1.0f),
			FMath::RandRange(-1.0f, 1.0f),
			FMath::RandRange(-1.0f, 1.0f)
		).GetSafeNormal() * OffsetMagnitude * 2.0f;
		break;

	default:
		break;
	}

	return AimPos + MissOffset;
}

float UAIShootingComponent::GenerateReactionTime() const
{
	float BaseTime = FMath::RandRange(Config.ReactionTimeMin, Config.ReactionTimeMax);

	// Apply multipliers based on context
	if (!Context.bTargetIsStationary)
	{
		BaseTime *= Config.ReactionTimeMovingTargetMultiplier;
	}

	if (Context.bJustAcquired)
	{
		BaseTime *= Config.ReactionTimeSurpriseMultiplier;
	}

	return BaseTime;
}

int32 UAIShootingComponent::GenerateBurstSize() const
{
	return FMath::RandRange(Config.BurstSizeMin, Config.BurstSizeMax);
}

// ============================================================================
// INTERNAL METHODS - WEAPON SELECTION
// ============================================================================

EWeaponSlot UAIShootingComponent::SelectBestWeapon() const
{
	// Check if secondary weapon is enabled and available
	if (!bUseSecondaryWeapon)
	{
		return EWeaponSlot::Primary;
	}

	// Check if secondary can be used (ammo, cooldown, distance)
	if (!CanUseSecondaryWeapon())
	{
		return EWeaponSlot::Primary;
	}

	// Only prefer secondary at clearly beneficial distances
	if (ShouldPreferSecondary())
	{
		return EWeaponSlot::Secondary;
	}

	// Default to primary - it's more versatile
	return EWeaponSlot::Primary;
}

bool UAIShootingComponent::CanUseSecondaryWeapon() const
{
	// Check ammo
	if (Context.SecondaryAmmo < Config.SecondaryMinAmmoThreshold)
	{
		return false;
	}

	// Check cooldown
	if (ShootingState.SecondaryCooldownTimer > 0.0f)
	{
		return false;
	}

	// Check minimum safe distance (self-damage prevention)
	if (Context.DistanceToTarget < Config.SecondaryMinSafeDistance)
	{
		return false;
	}

	// Check maximum range
	if (Context.DistanceToTarget > Config.SecondaryMaxRange)
	{
		return false;
	}

	return true;
}

bool UAIShootingComponent::ShouldPreferSecondary() const
{
	// Always prefer at longer distances (beyond preferred min)
	if (Context.DistanceToTarget > Config.SecondaryPreferredMinDistance)
	{
		return true;
	}

	// In medium range (safe distance to preferred), use secondary with probability based on distance
	// This ensures secondary gets used even at medium ranges, not just long range
	if (Context.DistanceToTarget >= Config.SecondaryMinSafeDistance)
	{
		// Calculate probability: closer to preferred distance = higher chance
		const float DistanceRatio = (Context.DistanceToTarget - Config.SecondaryMinSafeDistance) /
			(Config.SecondaryPreferredMinDistance - Config.SecondaryMinSafeDistance);

		// Base 30% chance + up to 40% based on distance (so 30-70% in medium range)
		const float SecondaryProbability = 0.3f + (DistanceRatio * 0.4f);

		// Use frame-stable random (based on time on target) to avoid flickering
		const float StableRandom = FMath::Fmod(ShootingState.TimeOnTarget * 7.13f, 1.0f);

		if (StableRandom < SecondaryProbability)
		{
			return true;
		}
	}

	// Prefer for stationary targets (easier to hit with grenades)
	if (Context.bTargetIsStationary && Context.DistanceToTarget > Config.SecondaryMinSafeDistance * 1.2f)
	{
		return true;
	}

	// Prefer for targets in cover (splash damage)
	if (Context.bTargetInCover && Context.SecondaryAmmo > 2)
	{
		return true;
	}

	return false;
}

// ============================================================================
// INTERNAL METHODS - FIRE CONDITIONS
// ============================================================================

bool UAIShootingComponent::CanFire(EWeaponSlot Weapon) const
{
	// DEBUG: Log why we can't fire (every 60 frames)
	static int32 CanFireDebugCounter = 0;
	const bool bShouldLog = (++CanFireDebugCounter % 60 == 0) && CurrentTarget.IsValid();

	// Must have acquired target
	if (!ShootingState.bTargetAcquired)
	{
		if (bShouldLog) UE_LOG(LogTemp, Warning, TEXT("[Shooting] CanFire=NO: Target not acquired (Reaction: %.2fs)"), ShootingState.ReactionTimeRemaining);
		return false;
	}

	// Must have line of sight
	if (!Context.bHasLineOfSight)
	{
		if (bShouldLog) UE_LOG(LogTemp, Warning, TEXT("[Shooting] CanFire=NO: No line of sight"));
		return false;
	}

	// Turret must be on target
	if (!IsTurretOnTarget())
	{
		if (bShouldLog) UE_LOG(LogTemp, Warning, TEXT("[Shooting] CanFire=NO: Turret not on target (Angle: %.1f > Max: %.1f)"), Context.AngleToTarget, Config.MaxFireAngle);
		return false;
	}

	// Ammo check
	int32 Ammo = GetAmmoCount(Weapon);
	if (Ammo <= 0)
	{
		if (bShouldLog) UE_LOG(LogTemp, Warning, TEXT("[Shooting] CanFire=NO: No ammo"));
		return false;
	}

	// Weapon-specific checks
	if (Weapon == EWeaponSlot::Secondary)
	{
		if (!CanUseSecondaryWeapon())
		{
			if (bShouldLog) UE_LOG(LogTemp, Warning, TEXT("[Shooting] CanFire=NO: Can't use secondary"));
			return false;
		}
	}

	// Burst cooldown check
	if (ShootingState.BurstCooldownTimer > 0.0f)
	{
		if (bShouldLog) UE_LOG(LogTemp, Warning, TEXT("[Shooting] CanFire=NO: Burst cooldown (%.2fs)"), ShootingState.BurstCooldownTimer);
		return false;
	}

	if (bShouldLog) UE_LOG(LogTemp, Log, TEXT("[Shooting] CanFire=YES! Firing at %s"), *CurrentTarget->GetName());
	return true;
}

bool UAIShootingComponent::IsTurretOnTarget() const
{
	return Context.AngleToTarget <= Config.MaxFireAngle;
}

bool UAIShootingComponent::CheckLineOfSight(const FVector& FromLocation, const FVector& ToLocation) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// Get owner tank to ignore in trace
	AWR_Tank_Pawn* Tank = GetOwnerTank();
	if (!Tank)
	{
		return false;
	}

	// Setup trace parameters - ignore owner tank and its components
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(AIShootingLOS), true);
	TraceParams.AddIgnoredActor(Tank);

	// Also ignore turret
	AWR_Turret* Turret = GetOwnerTurret();
	if (Turret)
	{
		TraceParams.AddIgnoredActor(Turret);
	}

	// Perform line trace
	FHitResult HitResult;
	bool bHit = World->LineTraceSingleByChannel(
		HitResult,
		FromLocation,
		ToLocation,
		ECC_Visibility,
		TraceParams
	);

	// If we didn't hit anything, we have clear LOS
	if (!bHit)
	{
		return true;
	}

	// Check if we hit the target (or something attached to target)
	AActor* HitActor = HitResult.GetActor();
	if (HitActor && CurrentTarget.IsValid())
	{
		// Direct hit on target
		if (HitActor == CurrentTarget.Get())
		{
			return true;
		}

		// Check if hit actor is owned by target (e.g., turret component)
		if (HitActor->GetOwner() == CurrentTarget.Get())
		{
			return true;
		}

		// Check if target is owner of hit actor
		if (CurrentTarget->GetOwner() == HitActor)
		{
			return true;
		}
	}

	// Hit something else - no clear line of sight
	return false;
}

int32 UAIShootingComponent::GetAmmoCount(EWeaponSlot Weapon) const
{
	AWR_Turret* Turret = GetOwnerTurret();
	if (!Turret)
	{
		return 0;
	}

	if (Weapon == EWeaponSlot::Primary)
	{
		return Turret->PrimaryAmmoCount_Actual;
	}
	else
	{
		return Turret->SecondaryAmmoCount_Actual;
	}
}

// ============================================================================
// INTERNAL METHODS - UTILITY
// ============================================================================

AWR_Tank_Pawn* UAIShootingComponent::GetOwnerTank() const
{
	if (OwnerTank.IsValid())
	{
		return OwnerTank.Get();
	}

	// Try to find from owner
	AActor* Owner = GetOwner();
	if (AController* Controller = Cast<AController>(Owner))
	{
		return Cast<AWR_Tank_Pawn>(Controller->GetPawn());
	}

	return nullptr;
}

AWR_Turret* UAIShootingComponent::GetOwnerTurret() const
{
	if (OwnerTurret.IsValid())
	{
		return OwnerTurret.Get();
	}

	AWR_Tank_Pawn* Tank = GetOwnerTank();
	if (Tank)
	{
		AActor* TurretActor = Tank->GetTurret_Implementation();
		return Cast<AWR_Turret>(TurretActor);
	}

	return nullptr;
}

FVector UAIShootingComponent::GetMuzzleLocation() const
{
	AWR_Turret* Turret = GetOwnerTurret();
	if (Turret && Turret->PitchComponent)
	{
		return Turret->PitchComponent->GetComponentLocation();
	}

	AWR_Tank_Pawn* Tank = GetOwnerTank();
	if (Tank)
	{
		return Tank->GetActorLocation() + FVector(0, 0, 100);
	}

	// Fallback to owner location if no tank/turret available
	AActor* Owner = GetOwner();
	if (Owner)
	{
		return Owner->GetActorLocation();
	}

	return FVector::ZeroVector;
}

FVector UAIShootingComponent::GetTurretDirection() const
{
	AWR_Turret* Turret = GetOwnerTurret();
	if (Turret && Turret->PitchComponent)
	{
		return Turret->PitchComponent->GetForwardVector();
	}

	return GetOwner()->GetActorForwardVector();
}

void UAIShootingComponent::DrawDebugVisualization() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector ShooterPos = GetMuzzleLocation();
	float Duration = DebugDrawDuration;

	// Only draw when we have a target
	if (CurrentTarget.IsValid())
	{
		FVector TargetPos = CurrentTarget->GetActorLocation();

		// Draw line from muzzle to target - color indicates state
		FColor LineColor;
		float LineThickness = 2.0f;

		switch (ShootingState.State)
		{
		case EShootingState::Acquiring:
			LineColor = FColor::Yellow;  // Acquiring - reaction time
			break;
		case EShootingState::Tracking:
			LineColor = FColor::Orange;  // Tracking - zeroing
			break;
		case EShootingState::Firing:
			LineColor = FColor::Red;     // Firing
			LineThickness = 4.0f;
			break;
		case EShootingState::Cooldown:
			LineColor = FColor::Blue;    // Cooldown between bursts
			break;
		default:
			LineColor = FColor::White;
			break;
		}

		// Main aim line: muzzle -> adjusted aim location
		DrawDebugLine(World, ShooterPos, ShootingState.AdjustedAimLocation, LineColor, false, Duration, 0, LineThickness);

		// Target marker - small sphere at actual target position
		DrawDebugSphere(World, TargetPos, 40.0f, 6, FColor::Magenta, false, Duration);

		// Aim point marker - where we're actually aiming (affected by spread/miss)
		if (ShootingState.State == EShootingState::Firing || ShootingState.State == EShootingState::Tracking)
		{
			FColor AimColor = (ShootingState.LastMissType == EAIMissType::None) ? FColor::Green : FColor::Red;
			DrawDebugSphere(World, ShootingState.AdjustedAimLocation, 25.0f, 4, AimColor, false, Duration);
		}

		// State info text above muzzle
		FString StateText = FString::Printf(TEXT("%s | Spr:%.1f | Dist:%.0fm | Ang:%.1f"),
			ShootingState.State == EShootingState::Acquiring ? TEXT("ACQUIRING") :
			ShootingState.State == EShootingState::Tracking ? TEXT("TRACKING") :
			ShootingState.State == EShootingState::Firing ? TEXT("FIRING") :
			ShootingState.State == EShootingState::Cooldown ? TEXT("COOLDOWN") : TEXT("IDLE"),
			ShootingState.CurrentSpread,
			ShootingState.DistanceToTarget / 100.0f,
			Context.AngleToTarget);

		DrawDebugString(World, ShooterPos + FVector(0, 0, 120), StateText, nullptr, LineColor, Duration);

		// If not firing, show reason
		if (ShootingState.State == EShootingState::Acquiring)
		{
			FString ReactionText = FString::Printf(TEXT("Reaction: %.2fs"), ShootingState.ReactionTimeRemaining);
			DrawDebugString(World, ShooterPos + FVector(0, 0, 100), ReactionText, nullptr, FColor::Yellow, Duration);
		}
		else if (ShootingState.State == EShootingState::Tracking && !IsTurretOnTarget())
		{
			FString AngleText = FString::Printf(TEXT("Turret align: %.1f > %.1f"), Context.AngleToTarget, Config.MaxFireAngle);
			DrawDebugString(World, ShooterPos + FVector(0, 0, 100), AngleText, nullptr, FColor::Orange, Duration);
		}
	}
}

void UAIShootingComponent::LogShootingEvent(const FString& Event) const
{
	UE_LOG(LogTemp, Log, TEXT("AIShootingComponent [%s]: %s"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"), *Event);
}
