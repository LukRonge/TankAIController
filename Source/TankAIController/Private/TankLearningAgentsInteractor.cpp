// Copyright Epic Games, Inc. All Rights Reserved.

#include "TankLearningAgentsInteractor.h"
#include "WR_Tank_Pawn.h"
#include "AILearningAgentsController.h"
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
	TMap<FName, FLearningAgentsObservationSchemaElement> ObservationElements;

	// Line traces observation - array of 16 normalized distances
	ObservationElements.Add(TEXT("LineTraces"),
		ULearningAgentsObservations::SpecifyContinuousObservation(InObservationSchema, 16, 1.0f, TEXT("LineTraces")));

	// Velocity observation
	ObservationElements.Add(TEXT("Velocity"),
		ULearningAgentsObservations::SpecifyVelocityObservation(InObservationSchema));

	// Rotation observation
	ObservationElements.Add(TEXT("Rotation"),
		ULearningAgentsObservations::SpecifyRotationObservation(InObservationSchema));

	// Forward speed observation
	ObservationElements.Add(TEXT("ForwardSpeed"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema));

	// Turret rotation observation
	ObservationElements.Add(TEXT("TurretRotation"),
		ULearningAgentsObservations::SpecifyRotationObservation(InObservationSchema));

	// Relative position to trainer observation (direction vector)
	ObservationElements.Add(TEXT("RelativePositionToTrainer"),
		ULearningAgentsObservations::SpecifyDirectionObservation(InObservationSchema));

	// Combine all observations into a struct
	OutObservationSchemaElement = ULearningAgentsObservations::SpecifyStructObservation(
		InObservationSchema,
		ObservationElements);

	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsInteractor: Observation schema specified."));
}

void UTankLearningAgentsInteractor::SpecifyAgentAction_Implementation(
	FLearningAgentsActionSchemaElement& OutActionSchemaElement,
	ULearningAgentsActionSchema* InActionSchema)
{
	// Create a struct to hold all actions
	TMap<FName, FLearningAgentsActionSchemaElement> ActionElements;

	// Throttle action (-1 to 1), scale = 1.0
	ActionElements.Add(TEXT("Throttle"),
		ULearningAgentsActions::SpecifyFloatAction(InActionSchema, 1.0f, TEXT("Throttle")));

	// Steering action (-1 to 1), scale = 1.0
	ActionElements.Add(TEXT("Steering"),
		ULearningAgentsActions::SpecifyFloatAction(InActionSchema, 1.0f, TEXT("Steering")));

	// Brake action (0 to 1), scale = 1.0
	ActionElements.Add(TEXT("Brake"),
		ULearningAgentsActions::SpecifyFloatAction(InActionSchema, 1.0f, TEXT("Brake")));

	// Turret yaw action (-180 to 180 degrees), scale = 180.0
	ActionElements.Add(TEXT("TurretYaw"),
		ULearningAgentsActions::SpecifyFloatAction(InActionSchema, 180.0f, TEXT("TurretYaw")));

	// Turret pitch action (-90 to 90 degrees), scale = 90.0
	ActionElements.Add(TEXT("TurretPitch"),
		ULearningAgentsActions::SpecifyFloatAction(InActionSchema, 90.0f, TEXT("TurretPitch")));

	// Combine all actions into a struct
	OutActionSchemaElement = ULearningAgentsActions::SpecifyStructAction(
		InActionSchema,
		ActionElements);

	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsInteractor: Action schema specified."));
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

	// Gather all observation data from controller
	TMap<FName, FLearningAgentsObservationObjectElement> ObservationElements;

	// Line traces observation
	const TArray<float>& LineTraces = TankController->GetLineTraceDistances();
	ObservationElements.Add(TEXT("LineTraces"),
		ULearningAgentsObservations::MakeContinuousObservationFromArrayView(InObservationObject, LineTraces, TEXT("LineTraces")));

	// Velocity observation
	const FVector Velocity = TankController->GetTankVelocity();
	ObservationElements.Add(TEXT("Velocity"),
		ULearningAgentsObservations::MakeVelocityObservation(InObservationObject, Velocity, FTransform::Identity));

	// Rotation observation
	const FRotator Rotation = TankController->GetTankRotation();
	ObservationElements.Add(TEXT("Rotation"),
		ULearningAgentsObservations::MakeRotationObservation(InObservationObject, Rotation, FRotator::ZeroRotator, TEXT("Rotation")));

	// Forward speed observation
	const float ForwardSpeed = TankController->GetForwardSpeed();
	ObservationElements.Add(TEXT("ForwardSpeed"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, ForwardSpeed));

	// Turret rotation observation
	const FRotator TurretRotation = TankController->GetTurretRotation();
	ObservationElements.Add(TEXT("TurretRotation"),
		ULearningAgentsObservations::MakeRotationObservation(InObservationObject, TurretRotation, FRotator::ZeroRotator, TEXT("TurretRotation")));

	// Relative position to trainer observation
	FVector DirectionToTrainer = FVector::ZeroVector;

	// Get trainer tank from manager
	// The manager is now owned by an ATankLearningAgentsManager actor
	ULearningAgentsManager* AgentManager = GetAgentManager();
	if (AgentManager)
	{
		// Get the owning actor (which should be ATankLearningAgentsManager)
		ATankLearningAgentsManager* TankManager = Cast<ATankLearningAgentsManager>(AgentManager->GetOwner());
		if (TankManager)
		{
			AWR_Tank_Pawn* TrainerTank = TankManager->GetTrainerTank();
			AWR_Tank_Pawn* AgentTank = TankController->GetControlledTank();

			if (TrainerTank && AgentTank && TrainerTank != AgentTank)
			{
				// Calculate direction vector from agent to trainer
				FVector TrainerLocation = TrainerTank->GetActorLocation();
				FVector AgentLocation = AgentTank->GetActorLocation();
				DirectionToTrainer = (TrainerLocation - AgentLocation).GetSafeNormal();
			}
		}
	}

	ObservationElements.Add(TEXT("RelativePositionToTrainer"),
		ULearningAgentsObservations::MakeDirectionObservation(InObservationObject, DirectionToTrainer, FTransform::Identity));

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

	// Extract individual actions
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

	float Brake = 0.0f;
	if (ULearningAgentsActions::GetFloatAction(Brake, InActionObject, ActionElements[TEXT("Brake")]))
	{
		AIController->SetBrakeFromAI(Brake);
	}

	float TurretYaw = 0.0f;
	float TurretPitch = 0.0f;
	ULearningAgentsActions::GetFloatAction(TurretYaw, InActionObject, ActionElements[TEXT("TurretYaw")]);
	ULearningAgentsActions::GetFloatAction(TurretPitch, InActionObject, ActionElements[TEXT("TurretPitch")]);
	AIController->SetTurretRotationFromAI(TurretYaw, TurretPitch);
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
	// Get the controller for this agent
	ABaseTankAIController* TankController = GetTankControllerFromAgentId(AgentId);
	if (!TankController)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsInteractor::EncodeHumanActionsForAgent: Failed to get TankController for agent %d"), AgentId);
		return;
	}

	// Read current input state from controller
	const float Throttle = TankController->GetCurrentThrottle();
	const float Steering = TankController->GetCurrentSteering();
	const float Brake = TankController->GetCurrentBrake();
	const FRotator TurretRotation = TankController->GetCurrentTurretRotation();

	// Create action elements from current input
	TMap<FName, FLearningAgentsActionObjectElement> ActionElements;

	// Throttle action
	ActionElements.Add(TEXT("Throttle"),
		ULearningAgentsActions::MakeFloatAction(GetActionObject(), Throttle, TEXT("Throttle")));

	// Steering action
	ActionElements.Add(TEXT("Steering"),
		ULearningAgentsActions::MakeFloatAction(GetActionObject(), Steering, TEXT("Steering")));

	// Brake action
	ActionElements.Add(TEXT("Brake"),
		ULearningAgentsActions::MakeFloatAction(GetActionObject(), Brake, TEXT("Brake")));

	// Turret yaw action
	ActionElements.Add(TEXT("TurretYaw"),
		ULearningAgentsActions::MakeFloatAction(GetActionObject(), TurretRotation.Yaw, TEXT("TurretYaw")));

	// Turret pitch action
	ActionElements.Add(TEXT("TurretPitch"),
		ULearningAgentsActions::MakeFloatAction(GetActionObject(), TurretRotation.Pitch, TEXT("TurretPitch")));

	// Combine all actions into a struct
	FLearningAgentsActionObjectElement ActionElement = ULearningAgentsActions::MakeStructAction(
		GetActionObject(),
		ActionElements);

	// Convert the action object element to an action vector in the interactor's action buffer
	// This is done using the internal UE::Learning::Action::SetVectorFromObject function
	// which is the same method used by ULearningAgentsController to encode actions
	UE::Learning::Action::SetVectorFromObject(
		GetActionVectorsArrayView()[AgentId],
		GetActionSchema()->ActionSchema,
		GetActionSchemaElement().SchemaElement,
		GetActionObject()->ActionObject,
		ActionElement.ObjectElement);

	// Increment the action iteration counter so the Recorder knows this action vector is valid
	// This is necessary for AddExperience() to accept the data (it checks iteration numbers)
	GetActionVectorIterationArrayView()[AgentId]++;

	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsInteractor::EncodeHumanActionsForAgent: Encoded actions for agent %d (Throttle: %.2f, Steering: %.2f)"),
		AgentId, Throttle, Steering);
}
