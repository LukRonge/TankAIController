// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILearningAgentsController.h"
#include "TankWaypointComponent.h"
#include "WR_Tank_Pawn.h"

AAILearningAgentsController::AAILearningAgentsController()
{
	// Base class already initializes everything
}

void AAILearningAgentsController::BeginPlay()
{
	Super::BeginPlay();

	// Log configuration
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("AAILearningAgentsController: Configuration"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("  -> MaxThrottleLimit: %.2f"), MaxThrottleLimit);
	UE_LOG(LogTemp, Warning, TEXT("  -> StuckDetection: %s"), bEnableStuckDetection ? TEXT("ENABLED") : TEXT("DISABLED"));
	if (bEnableStuckDetection)
	{
		UE_LOG(LogTemp, Warning, TEXT("  -> StuckTimeThreshold: %.1fs"), StuckTimeThreshold);
		UE_LOG(LogTemp, Warning, TEXT("  -> StuckVelocityThreshold: %.1f cm/s"), StuckVelocityThreshold);
		UE_LOG(LogTemp, Warning, TEXT("  -> RecoveryReverseDistance: %.1f cm"), RecoveryReverseDistance);
		UE_LOG(LogTemp, Warning, TEXT("  -> RecoveryThrottle: %.2f"), RecoveryThrottle);
		UE_LOG(LogTemp, Warning, TEXT("  -> MaxRecoveryAttempts: %d"), MaxRecoveryAttempts);
	}
	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	// Create waypoint component
	WaypointComponent = NewObject<UTankWaypointComponent>(this, UTankWaypointComponent::StaticClass(), TEXT("WaypointComponent"));
	if (WaypointComponent)
	{
		WaypointComponent->RegisterComponent();
		UE_LOG(LogTemp, Log, TEXT("AAILearningAgentsController: WaypointComponent created"));
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

	// 2. If recovering, handle recovery movement and skip normal AI control
	if (bIsRecovering)
	{
		UpdateRecovery();
		return;
	}

	// 3. Normal operation - apply AI inputs with throttle limit
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

	// Check if moved enough
	if (DistanceMoved >= RecoveryReverseDistance)
	{
		EndRecovery(true);
		return;
	}

	// Apply recovery inputs (reverse + slight steering)
	// Alternate steering direction based on attempt count
	const float SteerDir = (RecoveryAttemptCount % 2 == 0) ? 1.0f : -1.0f;
	const float RecoverySteer = RecoverySteering * SteerDir;

	ApplyMovementToTank(RecoveryThrottle, RecoverySteer);
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
