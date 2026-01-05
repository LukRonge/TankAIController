// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILearningAgentsController.h"
#include "TankWaypointComponent.h"
#include "TurretMathHelper.h"
#include "WR_Tank_Pawn.h"
#include "WR_Turret.h"
#include "Math/UnrealMathUtility.h"
#include "Kismet/KismetMathLibrary.h"

AAILearningAgentsController::AAILearningAgentsController()
{
	// Create enemy detection component
	EnemyDetectionComponent = CreateDefaultSubobject<UEnemyDetectionComponent>(TEXT("EnemyDetectionComponent"));
}

void AAILearningAgentsController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Enable AI turret control - prevents camera-based targeting from overwriting AI's target
	// This is set HERE (not in BaseTankAIController) because HumanPlayerController also
	// inherits from BaseTankAIController and needs camera-based turret targeting.
	// Must be in OnPossess (not BeginPlay) because ControlledTank is set in Super::OnPossess.
	if (ControlledTank)
	{
		ControlledTank->bUseAITurretControl = true;
		UE_LOG(LogTemp, Log, TEXT("AAILearningAgentsController::OnPossess: bUseAITurretControl = TRUE (AI handles turret)"));
	}
}

void AAILearningAgentsController::BeginPlay()
{
	Super::BeginPlay();

	// WaypointComponent is created in BaseTankAIController::BeginPlay()

	// Log configuration
	UE_LOG(LogTemp, Log, TEXT("AAILearningAgentsController: StuckDetection=%s, ReverseDistance=%.0fcm, MaxAttempts=%d"),
		bEnableStuckDetection ? TEXT("ON") : TEXT("OFF"), RecoveryReverseDistance, MaxRecoveryAttempts);

	// Setup enemy detection component and bind events
	if (EnemyDetectionComponent)
	{
		// Bind detection events
		EnemyDetectionComponent->OnEnemyDetected.AddDynamic(this, &AAILearningAgentsController::OnEnemyDetectedHandler);
		EnemyDetectionComponent->OnAwarenessStateChanged.AddDynamic(this, &AAILearningAgentsController::OnAwarenessStateChangedHandler);
		EnemyDetectionComponent->OnEnemyLost.AddDynamic(this, &AAILearningAgentsController::OnEnemyLostHandler);

		// Enable debug visualization for detection
		EnemyDetectionComponent->bDrawDebug = bEnableDetectionDebug;
		EnemyDetectionComponent->DebugDrawDuration = 0.0f; // Single frame updates

		UE_LOG(LogTemp, Log, TEXT("========================================"));
		UE_LOG(LogTemp, Log, TEXT("AAILearningAgentsController: EnemyDetection READY"));
		UE_LOG(LogTemp, Log, TEXT("  -> Detection Range: %.0f m"), EnemyDetectionComponent->DetectionConfig.MaxDetectionRange / 100.0f);
		UE_LOG(LogTemp, Log, TEXT("  -> FOV: %.0f deg (half-angle: %.0f)"), EnemyDetectionComponent->DetectionConfig.DetectionFOVHalfAngle * 2.0f, EnemyDetectionComponent->DetectionConfig.DetectionFOVHalfAngle);
		UE_LOG(LogTemp, Log, TEXT("  -> Peripheral Vision: +%.0f deg"), EnemyDetectionComponent->DetectionConfig.PeripheralVisionAngle);
		UE_LOG(LogTemp, Log, TEXT("  -> Max Tracked Enemies: %d"), EnemyDetectionComponent->MaxTrackedEnemies);
		UE_LOG(LogTemp, Log, TEXT("  -> Debug Visualization: %s"), bEnableDetectionDebug ? TEXT("ENABLED") : TEXT("DISABLED"));
		UE_LOG(LogTemp, Log, TEXT("  -> Enemy Targeting: %s (Min Awareness: %d)"), bEnableEnemyTargeting ? TEXT("ENABLED") : TEXT("DISABLED"), static_cast<int32>(MinAwarenessForTargeting));
		UE_LOG(LogTemp, Log, TEXT("========================================"));
	}
}

void AAILearningAgentsController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!ControlledTank)
	{
		return;
	}

	// 1. Update stuck detection
	if (bEnableStuckDetection)
	{
		UpdateStuckDetection(DeltaTime);
	}

	// 2. Update turret aim toward waypoint (always update, even during recovery)
	UpdateTurretAimToWaypoint(DeltaTime);

	// 3. If recovering, handle recovery movement and skip normal AI control
	if (bIsRecovering)
	{
		UpdateRecovery();
		return;
	}

	// 4. Normal operation - apply AI inputs with throttle limit
	const float FinalThrottle = FMath::Clamp(CurrentThrottle, -MaxThrottleLimit, MaxThrottleLimit);
	ApplyMovementToTank(FinalThrottle, CurrentSteering);
}

// ========== STUCK DETECTION ==========

void AAILearningAgentsController::UpdateStuckDetection(float DeltaTime)
{
	if (bIsRecovering)
	{
		return;
	}

	const float CurrentSpeed = FMath::Abs(GetForwardSpeed());
	const float AbsThrottle = FMath::Abs(CurrentThrottle);

	// Check stuck condition: throttle applied but not moving
	if (AbsThrottle > StuckThrottleThreshold && CurrentSpeed < StuckVelocityThreshold)
	{
		StuckTimer += DeltaTime;

		if (StuckTimer >= StuckTimeThreshold && !bIsStuck)
		{
			bIsStuck = true;
			StartRecovery();
		}
	}
	else
	{
		// Moving normally - reset timer
		StuckTimer = 0.0f;
		bIsStuck = false;
	}
}

void AAILearningAgentsController::StartRecovery()
{
	bIsRecovering = true;
	RecoveryStartPosition = ControlledTank->GetActorLocation();
	RecoveryAttemptCount++;

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("STUCK DETECTED! Starting recovery attempt %d/%d"),
		RecoveryAttemptCount, MaxRecoveryAttempts);
	UE_LOG(LogTemp, Warning, TEXT("  -> Position: %s"), *RecoveryStartPosition.ToString());
	UE_LOG(LogTemp, Warning, TEXT("  -> Reversing %.1f cm with throttle %.2f"),
		RecoveryReverseDistance, RecoveryThrottle);
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

void AAILearningAgentsController::UpdateRecovery()
{
	if (!bIsRecovering || !ControlledTank)
	{
		return;
	}

	// Calculate distance moved from recovery start
	const FVector CurrentPos = ControlledTank->GetActorLocation();
	const float DistanceMoved = FVector::Dist2D(RecoveryStartPosition, CurrentPos);

	// Check if moved enough (100cm)
	if (DistanceMoved >= RecoveryReverseDistance)
	{
		EndRecovery(true);
		return;
	}

	// Apply recovery inputs - just reverse, no steering
	// This gives AI clean reverse without turning
	ApplyMovementToTank(RecoveryThrottle, 0.0f);
}

void AAILearningAgentsController::EndRecovery(bool bSuccess)
{
	bIsRecovering = false;
	StuckTimer = 0.0f;
	bIsStuck = false;

	if (bSuccess)
	{
		const FVector CurrentPos = ControlledTank->GetActorLocation();
		const float ActualDistance = FVector::Dist2D(RecoveryStartPosition, CurrentPos);

		UE_LOG(LogTemp, Warning, TEXT("Recovery SUCCESS! Moved %.1f cm"), ActualDistance);
		RecoveryAttemptCount = 0;  // Reset on success
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Recovery INCOMPLETE - attempt %d/%d"),
			RecoveryAttemptCount, MaxRecoveryAttempts);

		if (RecoveryAttemptCount >= MaxRecoveryAttempts)
		{
			OnRecoveryFailed();
		}
		else
		{
			// Try again
			bIsStuck = true;
			StartRecovery();
		}
	}
}

void AAILearningAgentsController::OnRecoveryFailed()
{
	UE_LOG(LogTemp, Error, TEXT("========================================"));
	UE_LOG(LogTemp, Error, TEXT("RECOVERY FAILED after %d attempts!"), MaxRecoveryAttempts);
	UE_LOG(LogTemp, Error, TEXT("Regenerating waypoints from current position..."));
	UE_LOG(LogTemp, Error, TEXT("========================================"));

	RecoveryAttemptCount = 0;

	// Regenerate waypoints from current position
	if (WaypointComponent)
	{
		WaypointComponent->RegenerateWaypointsFromCurrentPosition();
	}
}

// ========== AI ACTION API ==========

void AAILearningAgentsController::SetThrottleFromAI(float Value)
{
	CurrentThrottle = FMath::Clamp(Value, -1.0f, 1.0f);
}

void AAILearningAgentsController::SetSteeringFromAI(float Value)
{
	CurrentSteering = FMath::Clamp(Value, -1.0f, 1.0f);
}

void AAILearningAgentsController::SetBrakeFromAI(float Value)
{
	CurrentBrake = FMath::Clamp(Value, 0.0f, 1.0f);
}

void AAILearningAgentsController::SetTurretRotationFromAI(float Yaw, float Pitch)
{
	// Store current turret rotation for observation
	CurrentTurretRotation = FRotator(Pitch * 45.0f, Yaw * 180.0f, 0.0f);

	// Apply turret rotation via tank
	if (ControlledTank)
	{
		ControlledTank->SetAITurretInput(Yaw, Pitch);
	}
}

// ========== NAVIGATION HELPERS ==========

float AAILearningAgentsController::GetRecoveryProgress() const
{
	if (!bIsRecovering || !ControlledTank || RecoveryReverseDistance <= 0.0f)
	{
		return 0.0f;
	}

	const FVector CurrentPos = ControlledTank->GetActorLocation();
	const float DistanceMoved = FVector::Dist2D(RecoveryStartPosition, CurrentPos);
	return FMath::Clamp(DistanceMoved / RecoveryReverseDistance, 0.0f, 1.0f);
}

float AAILearningAgentsController::GetHeadingErrorToWaypoint() const
{
	if (!WaypointComponent || !ControlledTank || !WaypointComponent->HasActiveTarget())
	{
		return 0.0f;
	}

	// Get direction to waypoint in local space
	const FVector LocalDir = WaypointComponent->GetLocalDirectionToCurrentWaypoint();

	if (LocalDir.IsNearlyZero())
	{
		return 0.0f;
	}

	// atan2(Y, X) gives angle from forward axis: positive = right, negative = left
	// Normalize by PI to get -1 to +1 range
	return FMath::Atan2(LocalDir.Y, LocalDir.X) / PI;
}

// ========== TURRET CONTROL ==========

FVector AAILearningAgentsController::GetTurretAimTargetLocation() const
{
	// This method returns the location and updates targeting state
	// Priority: Enemy (if valid AND in front) > Last Known Position (with delay) > Waypoint

	AAILearningAgentsController* MutableThis = const_cast<AAILearningAgentsController*>(this);

	// Check for enemy target first (if enabled)
	if (bEnableEnemyTargeting && EnemyDetectionComponent && ControlledTank)
	{
		FDetectedEnemyInfo PriorityTarget;
		if (EnemyDetectionComponent->GetPriorityTarget(PriorityTarget))
		{
			// Check if awareness is high enough for targeting
			if (PriorityTarget.AwarenessState >= MinAwarenessForTargeting)
			{
				// Use AngleToEnemy from detection component - this is already calculated
				// relative to TURRET direction (not tank body), which is correct because
				// detection FOV is based on turret direction.
				// FDetectedEnemyInfo::AngleToEnemy is signed: negative = left, positive = right
				const float AngleToEnemy = FMath::Abs(PriorityTarget.AngleToEnemy);

				// Check if we should engage this enemy:
				// - If we're ALREADY tracking this enemy, continue tracking (360 degree turret rotation)
				// - If we're NOT tracking yet, only engage if within initial engage angle limit
				const bool bAlreadyTrackingThisEnemy = bIsTargetingEnemy && CurrentTurretTarget.Get() == PriorityTarget.Enemy.Get();

				// When already tracking, allow full 360 degree tracking (no angle limit)
				// When starting to track, require enemy to be within EnemyEngageAngleLimit from TURRET direction
				if (bAlreadyTrackingThisEnemy || AngleToEnemy <= EnemyEngageAngleLimit)
				{
					// Valid enemy target - engage
					MutableThis->bIsTargetingEnemy = true;
					MutableThis->CurrentTurretTarget = PriorityTarget.Enemy;
					MutableThis->LastEnemyTargetLocation = PriorityTarget.LastKnownLocation;
					MutableThis->ReturnToWaypointTimer = 0.0f; // Reset timer when actively targeting

					return PriorityTarget.LastKnownLocation;
				}
			}
		}
	}

	// No valid enemy target - check if we should hold last position (delay period)
	// Check: Have a last known location AND within delay period
	// NOTE: We don't use bIsTargetingEnemy here because we need to set it to FALSE
	// during delay to allow timer increment, but still return LastEnemyTargetLocation
	if (!LastEnemyTargetLocation.IsZero() && ReturnToWaypointDelay > 0.0f && ReturnToWaypointTimer < ReturnToWaypointDelay)
	{
		// In delay period - hold last known enemy position
		// We're not actively targeting, just watching the last position
		MutableThis->bIsTargetingEnemy = false;
		MutableThis->CurrentTurretTarget = nullptr;

		UE_LOG(LogTemp, Verbose, TEXT("Turret: Holding last enemy position (%.2f/%.2f sec)"),
			ReturnToWaypointTimer, ReturnToWaypointDelay);

		return LastEnemyTargetLocation;
	}

	// Delay expired or no last position - log transition if needed
	if (ReturnToWaypointTimer >= ReturnToWaypointDelay && !LastEnemyTargetLocation.IsZero())
	{
		UE_LOG(LogTemp, Log, TEXT("Turret: ReturnToWaypointDelay expired (%.2f sec), switching to waypoint mode"),
			ReturnToWaypointDelay);
	}

	// Clear targeting state - returning to waypoint mode
	MutableThis->bIsTargetingEnemy = false;
	MutableThis->CurrentTurretTarget = nullptr;
	MutableThis->LastEnemyTargetLocation = FVector::ZeroVector;  // Clear to prevent re-entering delay

	// Fall back to waypoint
	if (!WaypointComponent || !ControlledTank || !WaypointComponent->HasActiveTarget())
	{
		// No target - aim forward at WaypointAimHeight above current ground level
		if (ControlledTank)
		{
			FVector ForwardTarget = ControlledTank->GetActorLocation() + ControlledTank->GetActorForwardVector() * 10000.0f;
			ForwardTarget.Z = ControlledTank->GetActorLocation().Z + WaypointAimHeight;  // Aim at height above ground
			return ForwardTarget;
		}
		return FVector::ZeroVector;
	}

	// Return waypoint location with adjusted Z for proper pitch angle
	// WaypointAimHeight = height above ground to aim at (e.g., 50cm = aim at 50cm above ground)
	FVector WaypointTarget = WaypointComponent->GetCurrentWaypointLocation();
	WaypointTarget.Z += WaypointAimHeight;  // Aim at specified height above waypoint (ground level)
	return WaypointTarget;
}

void AAILearningAgentsController::UpdateTurretAimToWaypoint(float DeltaTime)
{
	if (!bEnableTurretAiming || !ControlledTank || !WaypointComponent)
	{
		return;
	}

	// Get turret actor
	AWR_Turret* Turret = Cast<AWR_Turret>(ControlledTank->GetTurret_Implementation());
	if (!Turret)
	{
		return;
	}

	// Store previous targeting state for change detection
	const bool bWasTargetingEnemy = bIsTargetingEnemy;
	AActor* PreviousTarget = CurrentTurretTarget.Get();
	const bool bHadLastEnemyLocation = !LastEnemyTargetLocation.IsZero();  // Track if we were in delay period

	// Get target location for aiming (this also updates bIsTargetingEnemy, CurrentTurretTarget, and LastEnemyTargetLocation)
	const FVector TargetLocation = GetTurretAimTargetLocation();

	// Update return-to-waypoint timer AFTER getting target location
	// Timer counts when:
	// 1. NOT actively targeting enemy (bIsTargetingEnemy = false)
	// 2. Have a last known enemy position (indicates we're in delay period)
	// Timer is reset when actively targeting enemy
	// Note: LastEnemyTargetLocation is cleared when delay expires, so timer naturally stops
	if (!bIsTargetingEnemy && !LastEnemyTargetLocation.IsZero())
	{
		// In delay period - increment timer
		ReturnToWaypointTimer += DeltaTime;
	}
	else if (bIsTargetingEnemy)
	{
		// Actively targeting - reset timer
		ReturnToWaypointTimer = 0.0f;
	}
	// When LastEnemyTargetLocation is cleared (delay expired), timer stays at its value
	// but that's OK because we won't re-enter delay without a new LastEnemyTargetLocation

	// Determine if we're in "holding last position" mode (delay period)
	// Check: NOT targeting, have last position, timer within delay
	const bool bInDelayPeriod = !bIsTargetingEnemy && !LastEnemyTargetLocation.IsZero() && ReturnToWaypointTimer < ReturnToWaypointDelay;

	// Show debug message when targeting state changes
	if (bNotifyDetectedEnemyHUD && GEngine)
	{
		if (bIsTargetingEnemy && CurrentTurretTarget.IsValid() && !bWasTargetingEnemy)
		{
			// Started targeting enemy
			FString Message = FString::Printf(TEXT("TURRET LOCKED ON [%s]"),
				*CurrentTurretTarget->GetName());
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red, Message, true, FVector2D(1.5f, 1.5f));
		}
		else if (!bIsTargetingEnemy && bWasTargetingEnemy && bInDelayPeriod)
		{
			// Just lost target - entering delay period (holding last position)
			FString Message = FString::Printf(TEXT("TURRET: Holding last position (%.1fs)"), ReturnToWaypointDelay);
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow, Message, true, FVector2D(1.3f, 1.3f));
		}
		else if (!bIsTargetingEnemy && !bInDelayPeriod && bHadLastEnemyLocation && LastEnemyTargetLocation.IsZero())
		{
			// Just transitioned from delay period to waypoint mode (delay expired)
			// Detected by: had last enemy location before, now cleared
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::White, TEXT("TURRET: Waypoint mode"), true, FVector2D(1.3f, 1.3f));
		}
		else if (bIsTargetingEnemy && CurrentTurretTarget.IsValid() && CurrentTurretTarget.Get() != PreviousTarget && PreviousTarget != nullptr)
		{
			// Switched to different enemy
			FString Message = FString::Printf(TEXT("TURRET SWITCHED TO [%s]"),
				*CurrentTurretTarget->GetName());
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Orange, Message, true, FVector2D(1.5f, 1.5f));
		}
	}

	// ========================================================================
	// SMOOTH TURRET ROTATION - AI controller handles interpolation
	// ========================================================================
	// Calculate desired yaw/pitch angles, then interpolate using TurretYawSpeed
	// and TurretPitchSpeed. This gives precise control over AI turret rotation speed.
	// ========================================================================

	// Get turret location using helper
	FVector TurretLocation, TurretDirection;
	FTurretMathHelper::GetTurretLocationAndDirection(Turret, TurretLocation, TurretDirection);
	const float TankYaw = ControlledTank->GetActorRotation().Yaw;

	// Calculate desired angles to target using helper
	float DesiredRelativeYaw, DesiredPitch;
	FTurretMathHelper::CalculateAimAngles(TurretLocation, TargetLocation, TankYaw, DesiredRelativeYaw, DesiredPitch);

	// Clamp pitch to turret limits
	const float ClampedPitch = FTurretMathHelper::ClampPitch(DesiredPitch);

	// Store target angles
	TargetTurretYaw = DesiredRelativeYaw;
	TargetTurretPitch = ClampedPitch;

	// Use RInterpTo for proper quaternion-based interpolation (handles -180/+180 wraparound)
	const FRotator CurrentRot(CurrentTurretPitch, CurrentTurretYaw, 0.0f);
	const FRotator TargetRot(TargetTurretPitch, TargetTurretYaw, 0.0f);
	const FRotator InterpolatedRot = FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, TurretRotationInterpSpeed);

	CurrentTurretYaw = InterpolatedRot.Yaw;
	CurrentTurretPitch = InterpolatedRot.Pitch;

	// Project target point at interpolated direction using helper
	const float InterpolatedWorldYaw = TankYaw + CurrentTurretYaw;
	const FVector InterpolatedTargetLocation = FTurretMathHelper::ProjectTargetLocation(
		TurretLocation, InterpolatedWorldYaw, CurrentTurretPitch);

	// Pass interpolated location to turret
	ControlledTank->SetAITurretTargetLocation(InterpolatedTargetLocation);

	// DEBUG: Log smooth rotation progress
	static int32 TurretDebugCounter = 0;
	if (++TurretDebugCounter % 60 == 0) // Every 1 sec at 60fps
	{
		// Get actual turret world rotation for comparison
		float ActualTurretWorldYaw = 0.0f;
		if (Turret->YawComponent)
		{
			ActualTurretWorldYaw = Turret->YawComponent->GetComponentRotation().Yaw;
		}

		const float YawError = FRotator::NormalizeAxis(TargetTurretYaw - CurrentTurretYaw);
		const float PitchError = TargetTurretPitch - CurrentTurretPitch;

		// Get enemy info if targeting
		FString TargetInfo = bIsTargetingEnemy && CurrentTurretTarget.IsValid() ?
			FString::Printf(TEXT("Enemy: %s"), *CurrentTurretTarget->GetName()) :
			TEXT("Waypoint");

		UE_LOG(LogTemp, Log, TEXT("AI Turret [%s]: Yaw %.1f->%.1f (err:%.1f) | Pitch %.1f->%.1f (err:%.1f)"),
			*TargetInfo,
			CurrentTurretYaw, TargetTurretYaw, YawError,
			CurrentTurretPitch, TargetTurretPitch, PitchError);
	}
}

// ========== DETECTION EVENT HANDLERS ==========

void AAILearningAgentsController::OnEnemyDetectedHandler(AActor* Enemy, const FDetectedEnemyInfo& Info)
{
	if (!bNotifyDetectedEnemyHUD || !Enemy)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("AI Controller: Enemy DETECTED - %s (Visibility: %.0f%%, State: %d)"),
		*Enemy->GetName(), Info.VisibilityPercent * 100.0f, static_cast<int32>(Info.AwarenessState));

	// Show on-screen debug message (similar to Recording/Training notifications)
	if (GEngine)
	{
		FString AwarenessStr;
		FColor Color;

		switch (Info.AwarenessState)
		{
		case EAwarenessState::Suspicious:
			AwarenessStr = TEXT("SUSPICIOUS");
			Color = FColor::Yellow;
			break;
		case EAwarenessState::Alerted:
			AwarenessStr = TEXT("ALERTED");
			Color = FColor::Orange;
			break;
		case EAwarenessState::Combat:
			AwarenessStr = TEXT("COMBAT");
			Color = FColor::Red;
			break;
		default:
			AwarenessStr = TEXT("DETECTED");
			Color = FColor::White;
			break;
		}

		FString Message = FString::Printf(TEXT("ENEMY %s! [%s] (%.0fm, %.0f%% visible)"),
			*AwarenessStr, *Enemy->GetName(), Info.Distance / 100.0f, Info.VisibilityPercent * 100.0f);

		// Key -1 = one-time message, 3.0s duration
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, Color, Message, true, FVector2D(1.5f, 1.5f));
	}
}

void AAILearningAgentsController::OnAwarenessStateChangedHandler(AActor* Enemy, EAwarenessState OldState, EAwarenessState NewState)
{
	if (!bNotifyDetectedEnemyHUD || !Enemy)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("AI Controller: Enemy awareness changed - %s (State: %d -> %d)"),
		*Enemy->GetName(), static_cast<int32>(OldState), static_cast<int32>(NewState));

	// Show on-screen debug message for state transitions
	if (GEngine)
	{
		FString OldStateStr, NewStateStr;
		FColor Color;

		auto GetAwarenessStateName = [](EAwarenessState State) -> FString {
			switch (State)
			{
			case EAwarenessState::Unaware: return TEXT("UNAWARE");
			case EAwarenessState::Suspicious: return TEXT("SUSPICIOUS");
			case EAwarenessState::Alerted: return TEXT("ALERTED");
			case EAwarenessState::Combat: return TEXT("COMBAT");
			default: return TEXT("UNKNOWN");
			}
		};

		OldStateStr = GetAwarenessStateName(OldState);
		NewStateStr = GetAwarenessStateName(NewState);

		// Color based on new state
		switch (NewState)
		{
		case EAwarenessState::Suspicious: Color = FColor::Yellow; break;
		case EAwarenessState::Alerted: Color = FColor::Orange; break;
		case EAwarenessState::Combat: Color = FColor::Red; break;
		default: Color = FColor::White; break;
		}

		FString Message = FString::Printf(TEXT("[%s] %s -> %s"),
			*Enemy->GetName(), *OldStateStr, *NewStateStr);

		GEngine->AddOnScreenDebugMessage(-1, 2.0f, Color, Message, true, FVector2D(1.3f, 1.3f));
	}
}

void AAILearningAgentsController::OnEnemyLostHandler(AActor* Enemy)
{
	if (!bNotifyDetectedEnemyHUD || !Enemy)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("AI Controller: Enemy LOST - %s"), *Enemy->GetName());

	// Show on-screen debug message
	if (GEngine)
	{
		FString Message = FString::Printf(TEXT("ENEMY LOST [%s]"), *Enemy->GetName());
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor(100, 100, 100), Message, true, FVector2D(1.3f, 1.3f));
	}
}
