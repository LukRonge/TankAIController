// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILearningAgentsController.h"
#include "TankWaypointComponent.h"
#include "TankLearningAgentsInteractor.h"
#include "TurretMathHelper.h"
#include "CombatManeuverTypes.h"
#include "AIShootingTypes.h"
#include "WR_Tank_Pawn.h"
#include "WR_Turret.h"
#include "Math/UnrealMathUtility.h"
#include "Kismet/KismetMathLibrary.h"

// Learning Agents includes for standalone inference
#include "LearningAgentsManager.h"
#include "LearningAgentsPolicy.h"
#include "LearningAgentsInteractor.h"
#include "LearningAgentsNeuralNetwork.h"
#include "Misc/Paths.h"

// Static member definitions - shared across all AI controllers
ULearningAgentsManager* AAILearningAgentsController::SharedManager = nullptr;
UTankLearningAgentsInteractor* AAILearningAgentsController::SharedInteractor = nullptr;
ULearningAgentsPolicy* AAILearningAgentsController::SharedPolicy = nullptr;
bool AAILearningAgentsController::bSharedLearningAgentsInitialized = false;

AAILearningAgentsController::AAILearningAgentsController()
{
	// Create enemy detection component
	EnemyDetectionComponent = CreateDefaultSubobject<UEnemyDetectionComponent>(TEXT("EnemyDetectionComponent"));

	// Create combat maneuver component
	CombatManeuverComponent = CreateDefaultSubobject<UCombatManeuverComponent>(TEXT("CombatManeuverComponent"));

	// Create AI shooting component
	ShootingComponent = CreateDefaultSubobject<UAIShootingComponent>(TEXT("ShootingComponent"));
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

	// PRE-INITIALIZE Learning Agents to avoid lag when Num7 is pressed
	// This loads the neural network during level load rather than on first movement enable
	if (bUseLearningAgentsInference && ControlledTank)
	{
		InitializeLearningAgentsForInference();
	}

	// Setup enemy detection component and bind events
	if (EnemyDetectionComponent)
	{
		// TeamID = -1 means NO TEAM - attacks everyone including other AI tanks
		// All tanks fight against all tanks (free-for-all / deathmatch mode)
		EnemyDetectionComponent->TeamID = -1;

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
		UE_LOG(LogTemp, Log, TEXT("  -> TeamID: %d (free-for-all mode)"), EnemyDetectionComponent->TeamID);
		UE_LOG(LogTemp, Log, TEXT("========================================"));
	}

	// Setup combat maneuver component
	if (CombatManeuverComponent && bEnableCombatManeuvers)
	{
		// Set references to enemy detection and waypoint components
		CombatManeuverComponent->SetReferences(EnemyDetectionComponent, WaypointComponent);

		// Bind combat events
		CombatManeuverComponent->OnCombatStateChanged.AddDynamic(this, &AAILearningAgentsController::OnCombatStateChangedHandler);
		CombatManeuverComponent->OnManeuverStarted.AddDynamic(this, &AAILearningAgentsController::OnManeuverStartedHandler);
		CombatManeuverComponent->OnManeuverCompleted.AddDynamic(this, &AAILearningAgentsController::OnManeuverCompletedHandler);
		CombatManeuverComponent->OnWaypointAdvanced.AddDynamic(this, &AAILearningAgentsController::OnCombatWaypointAdvancedHandler);

		// Enable debug visualization
		CombatManeuverComponent->bDrawDebug = bEnableCombatDebug;
		CombatManeuverComponent->bLogManeuverSelection = bEnableCombatDebug;

		UE_LOG(LogTemp, Log, TEXT("========================================"));
		UE_LOG(LogTemp, Log, TEXT("AAILearningAgentsController: CombatManeuver READY"));
		UE_LOG(LogTemp, Log, TEXT("  -> Combat Maneuvers: ENABLED"));
		UE_LOG(LogTemp, Log, TEXT("  -> Debug Visualization: %s"), bEnableCombatDebug ? TEXT("ENABLED") : TEXT("DISABLED"));
		UE_LOG(LogTemp, Log, TEXT("========================================"));
	}
	else if (CombatManeuverComponent)
	{
		// Disable combat component if combat maneuvers are disabled
		CombatManeuverComponent->bEnabled = false;
		UE_LOG(LogTemp, Log, TEXT("AAILearningAgentsController: CombatManeuver DISABLED"));
	}

	// Setup AI shooting component
	if (ShootingComponent && bEnableShooting)
	{
		// Set references
		ShootingComponent->SetOwnerTank(ControlledTank);
		ShootingComponent->SetEnemyDetectionComponent(EnemyDetectionComponent);

		// Enable debug visualization
		ShootingComponent->bDrawDebug = bEnableShootingDebug;
	}
	else if (ShootingComponent)
	{
		ShootingComponent->SetShootingEnabled(false);
	}
}

void AAILearningAgentsController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unregister this controller's tank from shared manager
	if (bRegisteredWithSharedManager && SharedManager && LocalAgentId != INDEX_NONE)
	{
		SharedManager->RemoveAgent(LocalAgentId);
		bRegisteredWithSharedManager = false;
		LocalAgentId = INDEX_NONE;
	}

	// CLEANUP STATIC OBJECTS when game ends
	// Only the last controller to EndPlay should clean up shared objects
	// Check if we're the last by verifying no other agents are registered
	if (bSharedLearningAgentsInitialized && SharedManager)
	{
		// Check if manager has no more agents
		if (SharedManager->GetAgentNum() == 0)
		{
			UE_LOG(LogTemp, Log, TEXT("AILearningAgentsController: Cleaning up shared Learning Agents objects..."));

			// Remove from root to allow garbage collection
			if (SharedPolicy)
			{
				SharedPolicy->RemoveFromRoot();
				SharedPolicy = nullptr;
			}
			if (SharedInteractor)
			{
				SharedInteractor->RemoveFromRoot();
				SharedInteractor = nullptr;
			}
			if (SharedManager)
			{
				SharedManager->RemoveFromRoot();
				SharedManager = nullptr;
			}

			bSharedLearningAgentsInitialized = false;
			UE_LOG(LogTemp, Log, TEXT("AILearningAgentsController: Shared Learning Agents cleanup complete"));
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AAILearningAgentsController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!ControlledTank)
	{
		return;
	}

	// Skip all AI behavior if movement is disabled
	if (!bAIMovementEnabled)
	{
		// Still update turret aiming for visual feedback
		UpdateTurretAimToWaypoint(DeltaTime);
		return;
	}

	// 1. Update stuck detection
	if (bEnableStuckDetection)
	{
		UpdateStuckDetection(DeltaTime);
	}

	// 2. Update turret aim toward waypoint (always update, even during recovery)
	UpdateTurretAimToWaypoint(DeltaTime);

	// 3. Update AI shooting system
	UpdateShooting(DeltaTime);

	// 4. If recovering, handle recovery movement and skip normal AI control
	if (bIsRecovering)
	{
		UpdateRecovery(DeltaTime);
		return;
	}

	// 5. MOVEMENT - Use neural network inference OR simple waypoint following
	if (bUseLearningAgentsInference && bRegisteredWithSharedManager)
	{
		// Neural network inference - uses trained policy (shared across all AI tanks)
		RunLearningAgentsInference();

		// Apply the actions set by inference (SetThrottleFromAI/SetSteeringFromAI)
		const float FinalThrottle = FMath::Clamp(CurrentThrottle, -MaxThrottleLimit, MaxThrottleLimit);
		ApplyMovementToTank(FinalThrottle, CurrentSteering);

		// CRITICAL: Check if target reached and generate new one
		// Only in patrol mode - combat mode uses CombatManeuverComponent for waypoints
		if (WaypointComponent && !bInCombatMode)
		{
			if (WaypointComponent->IsTargetReached() || WaypointComponent->AreAllWaypointsCompleted())
			{
				WaypointComponent->GenerateRandomTarget();
			}
		}
	}
	else
	{
		// Fallback: Simple waypoint following (no neural network)
		UpdateAutonomousMovement(DeltaTime);
	}
}

void AAILearningAgentsController::SetAIMovementEnabled(bool bEnabled)
{
	bAIMovementEnabled = bEnabled;

	// Reset movement state when disabling
	if (!bEnabled)
	{
		CurrentThrottle = 0.0f;
		CurrentSteering = 0.0f;
		CurrentBrake = 0.0f;
		ApplyMovementToTank(0.0f, 0.0f);

		// Disable enemy detection when AI is disabled
		if (EnemyDetectionComponent)
		{
			EnemyDetectionComponent->SetDetectionEnabled(false);
		}
	}
	else
	{
		// Fallback: Initialize Learning Agents if not already done (should have been done in BeginPlay)
		// This handles edge cases where ControlledTank wasn't valid during BeginPlay
		if (bUseLearningAgentsInference && !bRegisteredWithSharedManager && ControlledTank)
		{
			UE_LOG(LogTemp, Warning, TEXT("AILearningAgentsController: Late initialization of Learning Agents (fallback path)"));
			InitializeLearningAgentsForInference();
		}

		// Generate initial waypoints if needed
		if (WaypointComponent && !WaypointComponent->HasActiveTarget())
		{
			WaypointComponent->GenerateRandomTarget();
		}

		// Enable enemy detection when AI is enabled
		if (EnemyDetectionComponent)
		{
			EnemyDetectionComponent->SetDetectionEnabled(true);
			UE_LOG(LogTemp, Warning, TEXT("AILearningAgentsController: Enemy detection ENABLED"));
		}

		// Enable shooting component
		if (ShootingComponent && bEnableShooting)
		{
			ShootingComponent->SetShootingEnabled(true);
		}
	}
}

void AAILearningAgentsController::UpdateAutonomousMovement(float DeltaTime)
{
	if (!WaypointComponent)
	{
		return;
	}

	// Generate waypoints if none exist
	// Note: GenerateRandomTarget() already calls GenerateWaypointsToTarget() internally
	if (!WaypointComponent->HasActiveTarget())
	{
		WaypointComponent->GenerateRandomTarget();
		return;
	}

	// Check if all waypoints completed - generate new target
	// Note: TankWaypointComponent::Tick handles AdvanceToNextWaypoint() automatically
	if (WaypointComponent->AreAllWaypointsCompleted())
	{
		WaypointComponent->GenerateRandomTarget();
		return;
	}

	// Get direction to current waypoint in local space
	// X = forward, Y = right
	const FVector LocalDir = WaypointComponent->GetLocalDirectionToCurrentWaypoint();

	if (LocalDir.IsNearlyZero())
	{
		ApplyMovementToTank(0.0f, 0.0f);
		return;
	}

	// Calculate steering: atan2(Y, X) gives angle from forward
	// Positive Y = waypoint is to the right = steer right (positive)
	// Negative Y = waypoint is to the left = steer left (negative)
	const float HeadingError = FMath::Atan2(LocalDir.Y, LocalDir.X);
	float DesiredSteering = FMath::Clamp(HeadingError * AutonomousSteeringSensitivity, -1.0f, 1.0f);

	// Calculate throttle based on heading error
	// Reduce throttle when turning sharply
	const float AbsHeadingError = FMath::Abs(HeadingError);
	float DesiredThrottle = AutonomousThrottle;

	if (AbsHeadingError > PI * 0.5f)
	{
		// Waypoint is behind us - need to turn around
		// Use minimal throttle while turning
		DesiredThrottle = AutonomousThrottle * 0.3f;
	}
	else if (AbsHeadingError > PI * 0.25f)
	{
		// Sharp turn - reduce throttle
		DesiredThrottle = AutonomousThrottle * 0.6f;
	}

	// Apply movement
	CurrentThrottle = DesiredThrottle;
	CurrentSteering = DesiredSteering;

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
	const float AbsSteering = FMath::Abs(CurrentSteering);

	// Check stuck condition: throttle applied but not moving
	// EXCEPTION: If actively steering (turning), low forward speed is expected
	const bool bIsActivelyTurning = AbsSteering > StuckSteeringThreshold;
	const bool bHasThrottleApplied = AbsThrottle > StuckThrottleThreshold;
	const bool bIsNotMoving = CurrentSpeed < StuckVelocityThreshold;

	// DEBUG: Log stuck detection state periodically
	static int32 StuckDebugCounter = 0;
	if (++StuckDebugCounter % 60 == 0)  // Every second
	{
		UE_LOG(LogTemp, Log, TEXT("[StuckDetection] Speed=%.1f(<%.1f?) Throttle=%.3f(>%.3f?) Steering=%.3f(>%.3f?) | HasThrottle=%d NotMoving=%d Turning=%d | Timer=%.2f/%.2f"),
			CurrentSpeed, StuckVelocityThreshold,
			AbsThrottle, StuckThrottleThreshold,
			AbsSteering, StuckSteeringThreshold,
			bHasThrottleApplied, bIsNotMoving, bIsActivelyTurning,
			StuckTimer, StuckTimeThreshold);
	}

	if (bHasThrottleApplied && bIsNotMoving && !bIsActivelyTurning)
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
		// Moving normally OR actively turning - reset timer
		StuckTimer = 0.0f;
		bIsStuck = false;
	}
}

float AAILearningAgentsController::GetRearClearance() const
{
	// Index 12 = rear direction in 24-trace ellipse pattern
	const TArray<float>& Traces = GetLineTraceDistances();
	if (Traces.Num() >= 24)
	{
		return Traces[12];
	}
	return 1000.0f;  // Default - assume clear if no traces
}

void AAILearningAgentsController::StartRecovery()
{
	// Check rear clearance before attempting reverse
	const float RearClearance = GetRearClearance();

	if (RearClearance < MinRearClearanceForRecovery)
	{
		UE_LOG(LogTemp, Warning, TEXT("========================================"));
		UE_LOG(LogTemp, Warning, TEXT("STUCK DETECTED but CANNOT REVERSE!"));
		UE_LOG(LogTemp, Warning, TEXT("  -> Rear clearance: %.1f cm < required %.1f cm"),
			RearClearance, MinRearClearanceForRecovery);
		UE_LOG(LogTemp, Warning, TEXT("  -> Skipping reverse, regenerating waypoints..."));
		UE_LOG(LogTemp, Warning, TEXT("========================================"));

		// Skip reverse recovery, go directly to waypoint regeneration
		OnRecoveryFailed();
		return;
	}

	bIsRecovering = true;
	RecoveryStartPosition = ControlledTank->GetActorLocation();
	RecoveryAttemptCount++;
	RecoveryTimer = 0.0f;  // Reset recovery timer

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("STUCK DETECTED! Starting recovery attempt %d/%d"),
		RecoveryAttemptCount, MaxRecoveryAttempts);
	UE_LOG(LogTemp, Warning, TEXT("  -> Position: %s"), *RecoveryStartPosition.ToString());
	UE_LOG(LogTemp, Warning, TEXT("  -> Rear clearance: %.1f cm (min: %.1f cm)"),
		RearClearance, MinRearClearanceForRecovery);
	UE_LOG(LogTemp, Warning, TEXT("  -> Reversing %.1f cm with throttle %.2f (timeout: %.1fs)"),
		RecoveryReverseDistance, RecoveryThrottle, RecoveryTimeout);
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

void AAILearningAgentsController::UpdateRecovery(float DeltaTime)
{
	if (!bIsRecovering || !ControlledTank)
	{
		return;
	}

	// Update recovery timer
	RecoveryTimer += DeltaTime;

	// Calculate distance moved from recovery start
	const FVector CurrentPos = ControlledTank->GetActorLocation();
	const float DistanceMoved = FVector::Dist2D(RecoveryStartPosition, CurrentPos);

	// Check if moved enough
	if (DistanceMoved >= RecoveryReverseDistance)
	{
		EndRecovery(true);
		return;
	}

	// Check rear clearance - abort if we've hit something while reversing
	const float RearClearance = GetRearClearance();
	if (RearClearance < MinRearClearanceForRecovery)
	{
		UE_LOG(LogTemp, Warning, TEXT("Recovery ABORTED - rear blocked! Clearance: %.1fcm < %.1fcm (moved %.1fcm)"),
			RearClearance, MinRearClearanceForRecovery, DistanceMoved);

		// If we moved at least some distance, consider it partial success
		if (DistanceMoved > 10.0f)
		{
			EndRecovery(true);  // Partial success - we moved some distance
		}
		else
		{
			EndRecovery(false);  // Failed - couldn't move at all
		}
		return;
	}

	// Check for timeout - if stuck too long, recovery has failed
	if (RecoveryTimer >= RecoveryTimeout)
	{
		UE_LOG(LogTemp, Warning, TEXT("Recovery TIMEOUT after %.1fs (moved only %.1fcm of %.1fcm)"),
			RecoveryTimer, DistanceMoved, RecoveryReverseDistance);
		EndRecovery(false);
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

	// Reset all stuck/recovery state to allow future detection
	RecoveryAttemptCount = 0;
	bIsStuck = false;       // BUG FIX: Allow future stuck detection
	StuckTimer = 0.0f;      // BUG FIX: Give tank time to respond to new waypoints (1.5s before next detection)
	bIsRecovering = false;  // Ensure recovery flag is cleared

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

	// Check if we should use direct aiming (skip interpolation entirely)
	// When tank is steering hard during combat, tank rotation changes target angle faster
	// than interpolation can track. In this case, use instant aiming in both AI controller
	// AND turret component for immediate response.
	const float AbsSteering = FMath::Abs(CurrentSteering);
	const bool bUseDirectAiming = bIsTargetingEnemy && AbsSteering > TurretCompensationSteeringThreshold;

	// Tell turret component to use instant aiming (skip its interpolation too)
	Turret->SetInstantAiming(bUseDirectAiming);

	if (bUseDirectAiming)
	{
		// Direct aiming - skip interpolation in AI controller
		// Turret component will handle smooth rotation
		CurrentTurretYaw = TargetTurretYaw;
		CurrentTurretPitch = TargetTurretPitch;
	}
	else
	{
		// Normal interpolation for smooth turret movement
		const FRotator InterpolatedRot = FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, TurretRotationInterpSpeed);
		CurrentTurretYaw = InterpolatedRot.Yaw;
		CurrentTurretPitch = InterpolatedRot.Pitch;
	}

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

		// Check if direct aiming is active (skips AI controller interpolation)
		const float DebugAbsSteering = FMath::Abs(CurrentSteering);
		const bool bDirectAimActive = bIsTargetingEnemy && DebugAbsSteering > TurretCompensationSteeringThreshold;

		UE_LOG(LogTemp, Log, TEXT("AI Turret [%s]: Yaw %.1f (err:%.1f) | Steering=%.2f%s"),
			*TargetInfo,
			CurrentTurretYaw, YawError, CurrentSteering,
			bDirectAimActive ? TEXT(" [DIRECT AIM]") : TEXT(""));
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

// ========== COMBAT MANEUVER EVENT HANDLERS ==========

void AAILearningAgentsController::OnCombatStateChangedHandler(ECombatState OldState, ECombatState NewState)
{
	UE_LOG(LogTemp, Log, TEXT("AI Controller: Combat state changed - %s -> %s"),
		*CombatManeuverUtils::GetCombatStateName(OldState),
		*CombatManeuverUtils::GetCombatStateName(NewState));

	// Enter combat mode when transitioning to Combat or Reposition states
	if ((NewState == ECombatState::Combat || NewState == ECombatState::Reposition || NewState == ECombatState::Disengage)
		&& !bInCombatMode)
	{
		EnterCombatMode();
	}
	// Exit combat mode when returning to Patrol
	else if (NewState == ECombatState::Patrol && bInCombatMode)
	{
		ExitCombatMode();
	}

	// Show on-screen debug message
	if (bNotifyDetectedEnemyHUD && GEngine)
	{
		FColor Color;
		switch (NewState)
		{
		case ECombatState::Alert:     Color = FColor::Yellow; break;
		case ECombatState::Combat:    Color = FColor::Red; break;
		case ECombatState::Disengage: Color = FColor::Orange; break;
		case ECombatState::Reposition: Color = FColor::Cyan; break;
		default:                      Color = FColor::White; break;
		}

		FString Message = FString::Printf(TEXT("COMBAT: %s"),
			*CombatManeuverUtils::GetCombatStateName(NewState));
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, Color, Message, true, FVector2D(1.5f, 1.5f));
	}
}

void AAILearningAgentsController::OnManeuverStartedHandler(const FCombatManeuver& Maneuver)
{
	UE_LOG(LogTemp, Log, TEXT("AI Controller: Maneuver STARTED - %s with %d waypoints"),
		*CombatManeuverUtils::GetManeuverTypeName(Maneuver.ManeuverType),
		Maneuver.Waypoints.Num());

	// Show on-screen debug message
	if (bNotifyDetectedEnemyHUD && GEngine)
	{
		FColor Color = CombatManeuverUtils::GetManeuverDebugColor(Maneuver.ManeuverType);
		FString Message = FString::Printf(TEXT("MANEUVER: %s"),
			*CombatManeuverUtils::GetManeuverTypeName(Maneuver.ManeuverType));
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, Color, Message, true, FVector2D(1.5f, 1.5f));
	}
}

void AAILearningAgentsController::OnManeuverCompletedHandler(const FCombatManeuver& Maneuver, bool bSuccess)
{
	UE_LOG(LogTemp, Log, TEXT("AI Controller: Maneuver COMPLETED - %s (%s)"),
		*CombatManeuverUtils::GetManeuverTypeName(Maneuver.ManeuverType),
		bSuccess ? TEXT("Success") : TEXT("Cancelled"));
}

void AAILearningAgentsController::OnCombatWaypointAdvancedHandler(int32 NewIndex, const FCombatWaypoint& Waypoint)
{
	UE_LOG(LogTemp, Log, TEXT("AI Controller: Combat waypoint advanced to %d - Fire=%d, Reverse=%d, Speed=%.2f"),
		NewIndex, Waypoint.bShouldFire ? 1 : 0, Waypoint.bReverseMovement ? 1 : 0, Waypoint.DesiredSpeed);
}

// ========== COMBAT MODE CONTROL ==========

void AAILearningAgentsController::EnterCombatMode()
{
	if (bInCombatMode)
	{
		return;
	}

	bInCombatMode = true;

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("AI Controller: ENTERING COMBAT MODE"));
	UE_LOG(LogTemp, Warning, TEXT("  -> Switching from patrol to combat waypoints"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	// Combat waypoints are managed by CombatManeuverComponent
	// It will push waypoints to WaypointComponent automatically
}

void AAILearningAgentsController::ExitCombatMode()
{
	if (!bInCombatMode)
	{
		return;
	}

	bInCombatMode = false;

	UE_LOG(LogTemp, Warning, TEXT("AI Controller: EXITING COMBAT MODE -> Returning to patrol"));

	// Generate new random patrol waypoint
	// Note: GenerateRandomTarget() already calls GenerateWaypointsToTarget() internally
	if (WaypointComponent)
	{
		WaypointComponent->GenerateRandomTarget();
	}
}

const FCombatWaypoint* AAILearningAgentsController::GetCurrentCombatWaypoint() const
{
	if (CombatManeuverComponent && CombatManeuverComponent->IsExecutingManeuver())
	{
		return CombatManeuverComponent->GetCurrentWaypoint();
	}
	return nullptr;
}

// ========== AI SHOOTING ==========

void AAILearningAgentsController::SetShootingDifficulty(EAIDifficulty NewDifficulty)
{
	if (ShootingComponent)
	{
		ShootingComponent->ApplyDifficultyPreset(NewDifficulty);
	}
}

EAIDifficulty AAILearningAgentsController::GetShootingDifficulty() const
{
	if (ShootingComponent)
	{
		return ShootingComponent->Difficulty;
	}
	return EAIDifficulty::Medium;
}

void AAILearningAgentsController::UpdateShooting(float DeltaTime)
{
	if (!bEnableShooting || !ShootingComponent || !ControlledTank)
	{
		return;
	}

	// Check if we have a valid enemy target
	if (bIsTargetingEnemy && CurrentTurretTarget.IsValid() && EnemyDetectionComponent)
	{
		// Get enemy info for shooting component
		FDetectedEnemyInfo EnemyInfo;
		if (EnemyDetectionComponent->IsActorDetected(CurrentTurretTarget.Get(), EnemyInfo))
		{
			// Update shooting component with current target
			ShootingComponent->SetTarget(CurrentTurretTarget.Get(), EnemyInfo);

			// NOTE: Do NOT override turret aim here - let UpdateTurretAimToWaypoint handle smooth rotation
			// The shooting component's spread affects hit/miss calculation, not actual turret aiming
		}
		else
		{
			// Target no longer detected - clear shooting
			ShootingComponent->ClearTarget(EEngagementEndReason::TargetLost);
		}
	}
	else
	{
		// No target - clear shooting
		if (ShootingComponent->HasTarget())
		{
			ShootingComponent->ClearTarget(EEngagementEndReason::TargetLost);
		}
	}

	// Execute firing commands
	ExecuteShootingCommands();
}

void AAILearningAgentsController::ExecuteShootingCommands()
{
	if (!ShootingComponent || !ControlledTank)
	{
		return;
	}

	// Get fire commands from shooting component
	const bool bShouldFirePrimary = ShootingComponent->ShouldFirePrimary();
	const bool bShouldFireSecondary = ShootingComponent->ShouldFireSecondary();

	// Execute primary fire
	ControlledTank->Server_PrimaryFire_Implementation(bShouldFirePrimary);

	// Execute secondary fire
	ControlledTank->Server_SecondaryFire_Implementation(bShouldFireSecondary);
}

// ========== LEARNING AGENTS STANDALONE INFERENCE ==========

bool AAILearningAgentsController::DoTrainedPolicyFilesExist() const
{
	const FString BasePath = FPaths::ProjectSavedDir() / TEXT("LearningAgents") / TEXT("Policies") / TEXT("TankPolicy");
	const FString PolicyFilePath = BasePath + TEXT("_policy.bin");
	return FPaths::FileExists(PolicyFilePath);
}

void AAILearningAgentsController::InitializeLearningAgentsForInference()
{
	if (bRegisteredWithSharedManager)
	{
		return;  // Already registered
	}

	if (!ControlledTank)
	{
		UE_LOG(LogTemp, Error, TEXT("AILearningAgentsController: Cannot initialize - no controlled tank!"));
		return;
	}

	// Check if trained policy exists
	if (!DoTrainedPolicyFilesExist())
	{
		UE_LOG(LogTemp, Warning, TEXT("AILearningAgentsController: No trained policy found - using simple waypoint navigation"));
		UE_LOG(LogTemp, Warning, TEXT("  -> Expected path: Saved/LearningAgents/Policies/TankPolicy_*.bin"));
		return;
	}

	// ========================================================================
	// SHARED COMPONENTS: First controller creates them, others reuse
	// ========================================================================
	if (!bSharedLearningAgentsInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("========================================"));
		UE_LOG(LogTemp, Warning, TEXT("AILearningAgentsController: Creating SHARED Learning Agents"));
		UE_LOG(LogTemp, Warning, TEXT("========================================"));

		// 1. Create shared Manager
		SharedManager = NewObject<ULearningAgentsManager>(GetWorld(), ULearningAgentsManager::StaticClass(), TEXT("SharedLearningAgentsManager"));
		if (!SharedManager)
		{
			UE_LOG(LogTemp, Error, TEXT("  -> Failed to create SharedManager!"));
			return;
		}
		SharedManager->AddToRoot();  // Prevent garbage collection
		SharedManager->RegisterComponent();
		SharedManager->SetMaxAgentNum(32);  // Support multiple tanks
		UE_LOG(LogTemp, Log, TEXT("  -> SharedManager created"));

		// 2. Create shared Interactor
		SharedInteractor = Cast<UTankLearningAgentsInteractor>(
			ULearningAgentsInteractor::MakeInteractor(
				SharedManager,
				UTankLearningAgentsInteractor::StaticClass(),
				TEXT("SharedTankInteractor")));

		if (!SharedInteractor)
		{
			UE_LOG(LogTemp, Error, TEXT("  -> Failed to create SharedInteractor!"));
			return;
		}
		SharedInteractor->AddToRoot();  // Prevent garbage collection
		UE_LOG(LogTemp, Log, TEXT("  -> SharedInteractor created"));

		// 3. Create shared Policy (must match training settings)
		FLearningAgentsPolicySettings PolicySettings;
		PolicySettings.HiddenLayerNum = 3;
		PolicySettings.HiddenLayerSize = 128;
		PolicySettings.bUseMemory = false;
		PolicySettings.MemoryStateSize = 0;
		PolicySettings.InitialEncodedActionScale = 0.7f;
		PolicySettings.ActivationFunction = ELearningAgentsActivationFunction::ELU;
		PolicySettings.bUseParallelEvaluation = true;

		// MakePolicy requires references to pointers
		ULearningAgentsManager* ManagerRef = SharedManager;
		ULearningAgentsInteractor* InteractorRef = SharedInteractor;

		SharedPolicy = ULearningAgentsPolicy::MakePolicy(
			ManagerRef,
			InteractorRef,
			ULearningAgentsPolicy::StaticClass(),
			TEXT("SharedTankPolicy"),
			nullptr, nullptr, nullptr,
			true, true, true,
			PolicySettings,
			1234);

		if (!SharedPolicy)
		{
			UE_LOG(LogTemp, Error, TEXT("  -> Failed to create SharedPolicy!"));
			return;
		}
		SharedPolicy->AddToRoot();  // Prevent garbage collection
		UE_LOG(LogTemp, Log, TEXT("  -> SharedPolicy created"));

		// 4. Load trained weights (only once)
		LoadTrainedPolicy();

		bSharedLearningAgentsInitialized = true;

		UE_LOG(LogTemp, Warning, TEXT("========================================"));
		UE_LOG(LogTemp, Warning, TEXT("SHARED LEARNING AGENTS READY"));
		UE_LOG(LogTemp, Warning, TEXT("========================================"));
	}

	// ========================================================================
	// Register THIS tank with shared manager
	// ========================================================================
	LocalAgentId = SharedManager->AddAgent(ControlledTank);
	if (LocalAgentId == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("AILearningAgentsController: Failed to register tank as agent!"));
		return;
	}

	bRegisteredWithSharedManager = true;

	UE_LOG(LogTemp, Log, TEXT("AILearningAgentsController: Tank %s registered (AgentId: %d)"),
		*ControlledTank->GetName(), LocalAgentId);
}

void AAILearningAgentsController::LoadTrainedPolicy()
{
	if (!SharedPolicy)
	{
		return;
	}

	const FString BasePath = FPaths::ProjectSavedDir() / TEXT("LearningAgents") / TEXT("Policies") / TEXT("TankPolicy");
	int32 LoadedCount = 0;

	// Load Encoder
	if (ULearningAgentsNeuralNetwork* Encoder = SharedPolicy->GetEncoderNetworkAsset())
	{
		FString Path = BasePath + TEXT("_encoder.bin");
		if (FPaths::FileExists(Path))
		{
			FFilePath FilePath; FilePath.FilePath = Path;
			Encoder->LoadNetworkFromSnapshot(FilePath);
			LoadedCount++;
		}
	}

	// Load Policy
	if (ULearningAgentsNeuralNetwork* PolicyNetwork = SharedPolicy->GetPolicyNetworkAsset())
	{
		FString Path = BasePath + TEXT("_policy.bin");
		if (FPaths::FileExists(Path))
		{
			FFilePath FilePath; FilePath.FilePath = Path;
			PolicyNetwork->LoadNetworkFromSnapshot(FilePath);
			LoadedCount++;
		}
	}

	// Load Decoder
	if (ULearningAgentsNeuralNetwork* Decoder = SharedPolicy->GetDecoderNetworkAsset())
	{
		FString Path = BasePath + TEXT("_decoder.bin");
		if (FPaths::FileExists(Path))
		{
			FFilePath FilePath; FilePath.FilePath = Path;
			Decoder->LoadNetworkFromSnapshot(FilePath);
			LoadedCount++;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("AILearningAgentsController: Loaded %d/3 network files"), LoadedCount);
}

void AAILearningAgentsController::ReloadTrainedPolicy()
{
	if (!bSharedLearningAgentsInitialized)
	{
		InitializeLearningAgentsForInference();
		return;
	}
	LoadTrainedPolicy();
}

void AAILearningAgentsController::RunLearningAgentsInference()
{
	if (!bSharedLearningAgentsInitialized || !SharedPolicy || !bRegisteredWithSharedManager || LocalAgentId == INDEX_NONE)
	{
		return;
	}

	// Ensure RunInference is called only ONCE per frame (not once per AI tank)
	// Multiple AI tanks share the same SharedPolicy, so we use frame counter to deduplicate
	static uint64 LastInferenceFrameNumber = 0;
	const uint64 CurrentFrame = GFrameCounter;

	if (LastInferenceFrameNumber == CurrentFrame)
	{
		return;  // Already ran inference this frame
	}
	LastInferenceFrameNumber = CurrentFrame;

	// RunInference processes ALL registered agents in one call
	// Each agent gets: GatherObservations -> EvaluatePolicy -> PerformActions
	// PerformActions calls SetThrottleFromAI/SetSteeringFromAI via Interactor
	//
	// IMPORTANT: RunInference is designed to handle multiple agents efficiently
	// It batches all observations, runs single neural network forward pass,
	// then distributes actions to all agents
	SharedPolicy->RunInference(0.0f);
}
