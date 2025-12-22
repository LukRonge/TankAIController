// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILearningAgentsController.h"
#include "WR_Tank_Pawn.h"

AAILearningAgentsController::AAILearningAgentsController()
{
	// Base class already initializes everything
}

void AAILearningAgentsController::BeginPlay()
{
	Super::BeginPlay();
	// Base class handles tank possession
}

void AAILearningAgentsController::Tick(float DeltaTime)
{
	// ========================================================================
	// TEMPORAL CONTEXT: Update Previous at START of frame
	// ========================================================================
	// CurrentThrottle/Steering was set by SetThrottleFromAI in PREVIOUS frame.
	// Save it as Previous so GatherObs (in Manager::Tick later) sees correct value.
	//
	// Timeline:
	// Frame N-1: SetThrottleFromAI(A) sets Current = A
	// Frame N:   Tick: Previous = A, then GatherObs sees Previous = A ✓
	PreviousThrottle = CurrentThrottle;
	PreviousSteering = CurrentSteering;

	Super::Tick(DeltaTime);

	// Apply AI inputs to tank using base class method
	ApplyMovementToTank(CurrentThrottle, CurrentSteering);
}

// ========== AI ACTION API IMPLEMENTATION ==========

void AAILearningAgentsController::SetThrottleFromAI(float Value)
{
	// Just set current value - Previous is updated in Tick() at start of NEXT frame
	CurrentThrottle = FMath::Clamp(Value, -1.0f, 1.0f);
}

void AAILearningAgentsController::SetSteeringFromAI(float Value)
{
	// Just set current value - Previous is updated in Tick() at start of NEXT frame
	CurrentSteering = FMath::Clamp(Value, -1.0f, 1.0f);
}

void AAILearningAgentsController::SetBrakeFromAI(float Value)
{
	CurrentBrake = FMath::Clamp(Value, 0.0f, 1.0f);
}

void AAILearningAgentsController::SetTurretRotationFromAI(float Yaw, float Pitch)
{
	CurrentTurretRotation = FRotator(Pitch, Yaw, 0.0f);
	ApplyTurretRotationToTank(CurrentTurretRotation);
}
