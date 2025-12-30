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

	UE_LOG(LogTemp, Log, TEXT("AAILearningAgentsController: Safety features initialized"));
	UE_LOG(LogTemp, Log, TEXT("  -> ObstacleProximityThrottle: %s"), bEnableObstacleProximityThrottle ? TEXT("ENABLED") : TEXT("DISABLED"));
	UE_LOG(LogTemp, Log, TEXT("  -> MaxThrottleLimit: %.2f"), MaxThrottleLimit);
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

	// ========================================================================
	// SAFETY: Apply obstacle proximity throttle reduction
	// ========================================================================
	float SafeThrottle = CurrentThrottle;
	if (bEnableObstacleProximityThrottle && FMath::Abs(CurrentThrottle) > 0.01f)
	{
		const float ProximityMultiplier = CalculateObstacleProximityMultiplier();
		SafeThrottle = CurrentThrottle * ProximityMultiplier;
	}

	// Apply max throttle limit
	SafeThrottle = FMath::Clamp(SafeThrottle, -MaxThrottleLimit, MaxThrottleLimit);

	// Apply AI inputs to tank using base class method
	ApplyMovementToTank(SafeThrottle, CurrentSteering);
}

float AAILearningAgentsController::CalculateObstacleProximityMultiplier() const
{
	// Find minimum obstacle distance from front traces only (direction AI is moving)
	const TArray<float>& LineTraces = LineTraceDistances;

	if (LineTraces.Num() == 0)
	{
		return 1.0f;  // No traces, no reduction
	}

	// For 24 traces: indices 0-3 and 21-23 are front-facing (±30° from forward)
	// For 16 traces: indices 0-2 and 14-15 are front-facing
	float MinFrontDistance = ObstacleSlowdownStartDistance * 2.0f;  // Start with large value

	const int32 NumTraces = LineTraces.Num();
	const int32 FrontTraceCount = FMath::Max(3, NumTraces / 8);  // ~12.5% of traces are front

	// Check front traces (around index 0)
	for (int32 i = 0; i < FrontTraceCount; ++i)
	{
		MinFrontDistance = FMath::Min(MinFrontDistance, LineTraces[i]);
	}
	// Check rear-front wrap-around traces
	for (int32 i = NumTraces - FrontTraceCount; i < NumTraces; ++i)
	{
		MinFrontDistance = FMath::Min(MinFrontDistance, LineTraces[i]);
	}

	// Also check lateral clearance for tight corridors
	MinFrontDistance = FMath::Min(MinFrontDistance, LeftClearance);
	MinFrontDistance = FMath::Min(MinFrontDistance, RightClearance);

	// Calculate multiplier based on distance
	if (MinFrontDistance >= ObstacleSlowdownStartDistance)
	{
		return 1.0f;  // Far from obstacles, no reduction
	}
	else if (MinFrontDistance <= ObstacleSlowdownEndDistance)
	{
		return MinThrottleMultiplier;  // Very close, maximum reduction
	}
	else
	{
		// Linear interpolation between start and end distances
		const float Range = ObstacleSlowdownStartDistance - ObstacleSlowdownEndDistance;
		const float DistanceFromEnd = MinFrontDistance - ObstacleSlowdownEndDistance;
		const float Alpha = DistanceFromEnd / Range;
		return FMath::Lerp(MinThrottleMultiplier, 1.0f, Alpha);
	}
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
