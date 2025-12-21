// Copyright Epic Games, Inc. All Rights Reserved.

#include "TankLearningAgentsInteractor.h"
#include "WR_Tank_Pawn.h"
#include "AILearningAgentsController.h"
#include "HumanPlayerController.h"
#include "LearningAgentsManager.h"
#include "TankLearningAgentsManager.h"

// Learning Agents observation/action includes
#include "LearningAgentsObservations.h"
#include "LearningAgentsActions.h"

void UTankLearningAgentsInteractor::SpecifyAgentObservation_Implementation(
	FLearningAgentsObservationSchemaElement& OutObservationSchemaElement,
	ULearningAgentsObservationSchema* InObservationSchema)
{
	// Create a struct to hold all observations
	// SIMPLIFIED for UGV drone navigation training:
	// - LineTraces: obstacle detection (CRITICAL for avoidance)
	// - ForwardSpeed: current speed feedback
	// - RelativeCurrentWaypointPosition: direction to navigate (LOCAL SPACE)
	// - DistanceToCurrentWaypoint: how far to waypoint
	// - RelativeTargetPosition: final target direction (LOCAL SPACE)
	// - DistanceToTarget: how far to final target
	// REMOVED: Velocity, Rotation, TurretRotation, RelativePositionToTrainer
	TMap<FName, FLearningAgentsObservationSchemaElement> ObservationElements;

	// Line traces observation - array of 16 raw distances in cm (0-1500cm or max if no obstacle)
	// Scale is set to 1500.0f (max ellipse major axis) for neural network normalization
	// Result: 0.0 = obstacle at tank, 1.0 = clear path
	ObservationElements.Add(TEXT("LineTraces"),
		ULearningAgentsObservations::SpecifyContinuousObservation(InObservationSchema, 16, 1500.0f, TEXT("LineTraces")));

	// Forward speed observation - UGV drone max speed ~800-1000 cm/s (8-10 m/s)
	// Scale = 1000.0f so normalized range is approximately -1 to 1
	ObservationElements.Add(TEXT("ForwardSpeed"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 1000.0f));

	// Relative position to current waypoint (direction vector - normalized, LOCAL SPACE)
	// X > 0 = waypoint in front, Y > 0 = waypoint to the right
	ObservationElements.Add(TEXT("RelativeCurrentWaypointPosition"),
		ULearningAgentsObservations::SpecifyDirectionObservation(InObservationSchema));

	// Distance to current waypoint - scale 5000.0f (50m max reasonable distance)
	ObservationElements.Add(TEXT("DistanceToCurrentWaypoint"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 5000.0f));

	// Relative position to final target (direction vector - normalized, LOCAL SPACE)
	ObservationElements.Add(TEXT("RelativeTargetPosition"),
		ULearningAgentsObservations::SpecifyDirectionObservation(InObservationSchema));

	// Distance to final target - scale 10000.0f (100m max reasonable distance)
	ObservationElements.Add(TEXT("DistanceToTarget"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 10000.0f));

	// Combine all observations into a struct
	OutObservationSchemaElement = ULearningAgentsObservations::SpecifyStructObservation(
		InObservationSchema,
		ObservationElements);

	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsInteractor: Observation schema specified (SIMPLIFIED for UGV navigation)."));
}

void UTankLearningAgentsInteractor::SpecifyAgentAction_Implementation(
	FLearningAgentsActionSchemaElement& OutActionSchemaElement,
	ULearningAgentsActionSchema* InActionSchema)
{
	// Create a struct to hold all actions
	// SIMPLIFIED: Only Throttle and Steering for UGV drone navigation
	// Removed: Brake (not tracked), TurretYaw, TurretPitch (not training turret control)
	TMap<FName, FLearningAgentsActionSchemaElement> ActionElements;

	// Throttle action (-1 to 1), scale = 1.0
	ActionElements.Add(TEXT("Throttle"),
		ULearningAgentsActions::SpecifyFloatAction(InActionSchema, 1.0f));

	// Steering action (-1 to 1), scale = 1.0
	ActionElements.Add(TEXT("Steering"),
		ULearningAgentsActions::SpecifyFloatAction(InActionSchema, 1.0f));

	// Combine all actions into a struct
	OutActionSchemaElement = ULearningAgentsActions::SpecifyStructAction(
		InActionSchema,
		ActionElements);

	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsInteractor: Action schema specified (Throttle + Steering only)."));
}

void UTankLearningAgentsInteractor::GatherAgentObservation_Implementation(
	FLearningAgentsObservationObjectElement& OutObservationObjectElement,
	ULearningAgentsObservationObject* InObservationObject,
	const int32 AgentId)
{
	ABaseTankAIController* TankController = GetTankControllerFromAgentId(AgentId);
	if (!TankController)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsInteractor: Failed to get TankController for agent %d"), AgentId);
		return;
	}

	// Get the controlled tank for LOCAL SPACE transform
	AWR_Tank_Pawn* ControlledTank = TankController->GetControlledTank();

	// ========================================================================
	// LOCAL SPACE TRANSFORM - All directions will be relative to tank orientation
	// ========================================================================
	// Using tank's transform means:
	// - If waypoint is IN FRONT of tank → direction = (1, 0, 0)
	// - If waypoint is to the RIGHT → direction = (0, 1, 0)
	// - If waypoint is BEHIND → direction = (-1, 0, 0)
	// This makes learning MUCH easier because the AI doesn't need to learn
	// the relationship between world orientation and actions.
	FTransform TankTransform = ControlledTank ? ControlledTank->GetActorTransform() : FTransform::Identity;

	// Gather SIMPLIFIED observation data for UGV drone navigation
	TMap<FName, FLearningAgentsObservationObjectElement> ObservationElements;

	// ========== 1. LINE TRACES (CRITICAL for obstacle avoidance) ==========
	// 16 distances in cm, normalized by schema scale 1500.0f
	// Result: 0.0 = obstacle at tank, 1.0 = clear path
	const TArray<float>& LineTraces = TankController->GetLineTraceDistances();
	ObservationElements.Add(TEXT("LineTraces"),
		ULearningAgentsObservations::MakeContinuousObservationFromArrayView(InObservationObject, LineTraces, TEXT("LineTraces")));

	// ========== 2. FORWARD SPEED ==========
	// Scalar speed in cm/s, normalized by schema scale 1000.0f
	const float ForwardSpeed = TankController->GetForwardSpeed();
	ObservationElements.Add(TEXT("ForwardSpeed"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, ForwardSpeed));

	// ========== 3. WAYPOINT NAVIGATION (PRIMARY) ==========
	FVector DirectionToWaypoint = FVector::ZeroVector;
	float DistanceToWaypoint = 0.0f;

	ULearningAgentsManager* AgentManager = GetAgentManager();
	if (AgentManager)
	{
		ATankLearningAgentsManager* TankManager = Cast<ATankLearningAgentsManager>(AgentManager->GetOwner());
		if (TankManager && TankManager->IsWaypointPathFollowingEnabled() && TankManager->bHasActiveTarget)
		{
			if (ControlledTank)
			{
				// Get current waypoint location (world space)
				FVector WaypointLocation = TankManager->GetCurrentWaypointLocation();
				FVector AgentLocation = ControlledTank->GetActorLocation();

				FVector DeltaToWaypoint = WaypointLocation - AgentLocation;
				DistanceToWaypoint = DeltaToWaypoint.Size();
				DirectionToWaypoint = DeltaToWaypoint.GetSafeNormal();
			}
		}
	}

	// Direction to waypoint - LOCAL SPACE (X > 0 = front, Y > 0 = right)
	ObservationElements.Add(TEXT("RelativeCurrentWaypointPosition"),
		ULearningAgentsObservations::MakeDirectionObservation(InObservationObject, DirectionToWaypoint, TankTransform));

	// Distance to waypoint - normalized by schema scale 5000.0f
	ObservationElements.Add(TEXT("DistanceToCurrentWaypoint"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, DistanceToWaypoint));

	// ========== 4. TARGET NAVIGATION (SECONDARY) ==========
	FVector DirectionToTarget = FVector::ZeroVector;
	float DistanceToTarget = 0.0f;

	if (AgentManager)
	{
		ATankLearningAgentsManager* TankManager = Cast<ATankLearningAgentsManager>(AgentManager->GetOwner());
		if (TankManager && TankManager->IsTargetBasedRecordingEnabled() && TankManager->bHasActiveTarget)
		{
			if (ControlledTank)
			{
				// Get target location (world space)
				FVector TargetLocation = TankManager->GetCurrentTargetLocation();
				FVector AgentLocation = ControlledTank->GetActorLocation();

				FVector DeltaToTarget = TargetLocation - AgentLocation;
				DistanceToTarget = DeltaToTarget.Size();
				DirectionToTarget = DeltaToTarget.GetSafeNormal();
			}
		}
	}

	// Direction to target - LOCAL SPACE
	ObservationElements.Add(TEXT("RelativeTargetPosition"),
		ULearningAgentsObservations::MakeDirectionObservation(InObservationObject, DirectionToTarget, TankTransform));

	// Distance to target - normalized by schema scale 10000.0f
	ObservationElements.Add(TEXT("DistanceToTarget"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, DistanceToTarget));

	// ========== DEBUG LOGGING ==========
	// Transform direction to local space for logging
	FVector LocalWaypointDir = TankTransform.InverseTransformVector(DirectionToWaypoint);

	static int32 ObsLogCounter = 0;
	if (++ObsLogCounter % 120 == 0)  // Every 2 seconds
	{
		// Log LineTraces summary (front, right, back, left)
		if (LineTraces.Num() >= 16)
		{
			UE_LOG(LogTemp, Log, TEXT("Agent %d LineTraces: Front=%.0f Right=%.0f Back=%.0f Left=%.0f (cm, max=1500)"),
				AgentId, LineTraces[0], LineTraces[4], LineTraces[8], LineTraces[12]);
		}

		UE_LOG(LogTemp, Log, TEXT("Agent %d Navigation: WP_Dir=(%.2f,%.2f) WP_Dist=%.1fm Speed=%.0fcm/s"),
			AgentId, LocalWaypointDir.X, LocalWaypointDir.Y, DistanceToWaypoint / 100.0f, ForwardSpeed);

		// Warning if no navigation goal
		if (DirectionToWaypoint.IsNearlyZero())
		{
			UE_LOG(LogTemp, Warning, TEXT("Agent %d: No waypoint direction! Check target/waypoint system."), AgentId);
		}
	}

	// Combine all observations into a struct
	OutObservationObjectElement = ULearningAgentsObservations::MakeStructObservation(
		InObservationObject,
		ObservationElements);
}

void UTankLearningAgentsInteractor::PerformAgentAction_Implementation(
	const ULearningAgentsActionObject* InActionObject,
	const FLearningAgentsActionObjectElement& InActionObjectElement,
	const int32 AgentId)
{
	AAILearningAgentsController* AIController = Cast<AAILearningAgentsController>(GetTankControllerFromAgentId(AgentId));
	if (!AIController)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsInteractor: Failed to get AIController for agent %d"), AgentId);
		return;
	}

	// Get the struct element containing all actions
	TMap<FName, FLearningAgentsActionObjectElement> ActionElements;
	if (!ULearningAgentsActions::GetStructAction(ActionElements, InActionObject, InActionObjectElement))
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsInteractor: Failed to get struct action for agent %d"), AgentId);
		return;
	}

	// Extract SIMPLIFIED actions (Throttle + Steering only for UGV navigation)
	float Throttle = 0.0f;
	if (ULearningAgentsActions::GetFloatAction(Throttle, InActionObject, ActionElements[TEXT("Throttle")]))
	{
		AIController->SetThrottleFromAI(Throttle);
	}

	float Steering = 0.0f;
	if (ULearningAgentsActions::GetFloatAction(Steering, InActionObject, ActionElements[TEXT("Steering")]))
	{
		AIController->SetSteeringFromAI(Steering);
	}

	// DEBUG: Log AI actions every 60 frames (1 second)
	static int32 ActionLogCounter = 0;
	if (++ActionLogCounter % 60 == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("AI ACTIONS [Agent %d]: Throttle=%.3f | Steering=%.3f"),
			AgentId, Throttle, Steering);
	}
}

ABaseTankAIController* UTankLearningAgentsInteractor::GetTankControllerFromAgentId(int32 AgentId) const
{
	if (!GetAgentManager())
	{
		return nullptr;
	}

	UObject* AgentObject = GetAgentManager()->GetAgent(AgentId);
	AWR_Tank_Pawn* TankPawn = Cast<AWR_Tank_Pawn>(AgentObject);
	if (!TankPawn)
	{
		return nullptr;
	}

	return Cast<ABaseTankAIController>(TankPawn->GetController());
}

void UTankLearningAgentsInteractor::EncodeHumanActionsForAgent(int32 AgentId)
{
	// ========================================================================
	// WARNING: This method uses INTERNAL UE::Learning:: API
	// ========================================================================
	// SIMPLIFIED for UGV drone navigation: Only Throttle + Steering
	// USES SMOOTHED VALUES from HumanPlayerController for better ML training
	// Keyboard digital inputs (0/1/-1) are smoothed to gradual values
	// ========================================================================

	// Get the controller for this agent
	ABaseTankAIController* TankController = GetTankControllerFromAgentId(AgentId);
	if (!TankController)
	{
		UE_LOG(LogTemp, Warning, TEXT("EncodeHumanActionsForAgent: Failed to get TankController for agent %d"), AgentId);
		return;
	}

	// Try to get HumanPlayerController for smoothed values
	AHumanPlayerController* HumanController = Cast<AHumanPlayerController>(TankController);

	float Throttle = 0.0f;
	float Steering = 0.0f;

	if (HumanController)
	{
		// Use SMOOTHED values for better ML training data
		Throttle = HumanController->GetSmoothedThrottle();
		Steering = HumanController->GetSmoothedSteering();
	}
	else
	{
		// Fallback to raw values from tank pawn
		AWR_Tank_Pawn* TankPawn = TankController->GetControlledTank();
		if (TankPawn)
		{
			Throttle = TankPawn->GetTankThrottle_Implementation();
			Steering = TankPawn->GetTankSteering_Implementation();
		}
	}

	// Create action elements (Throttle + Steering only)
	TMap<FName, FLearningAgentsActionObjectElement> ActionElements;

	ActionElements.Add(TEXT("Throttle"),
		ULearningAgentsActions::MakeFloatAction(GetActionObject(), Throttle));

	ActionElements.Add(TEXT("Steering"),
		ULearningAgentsActions::MakeFloatAction(GetActionObject(), Steering));

	// Combine actions into a struct
	FLearningAgentsActionObjectElement ActionElement = ULearningAgentsActions::MakeStructAction(
		GetActionObject(),
		ActionElements);

	// Encode to internal action buffer using UE internal API
	UE::Learning::Action::SetVectorFromObject(
		GetActionVectorsArrayView()[AgentId],
		GetActionSchema()->ActionSchema,
		GetActionSchemaElement().SchemaElement,
		GetActionObject()->ActionObject,
		ActionElement.ObjectElement);

	// Increment action iteration counter for Recorder validation
	GetActionVectorIterationArrayView()[AgentId]++;

	// Debug log every 60 frames
	static int32 EncodeLogCounter = 0;
	if (++EncodeLogCounter % 60 == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("RECORDING [Agent %d]: Throttle=%.3f | Steering=%.3f (smoothed)"),
			AgentId, Throttle, Steering);
	}
}
