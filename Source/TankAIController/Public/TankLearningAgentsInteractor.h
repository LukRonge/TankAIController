// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LearningAgentsInteractor.h"
#include "TankLearningAgentsInteractor.generated.h"

class ABaseTankAIController;
class AAILearningAgentsController;

/**
 * Tank Learning Agents Interactor
 * Defines observations (what the AI sees) and actions (what the AI can do)
 * Bridges between the tank pawn and the Learning Agents system
 */
UCLASS(BlueprintType)
class TANKAICONTROLLER_API UTankLearningAgentsInteractor : public ULearningAgentsInteractor
{
	GENERATED_BODY()

public:
	// ========== SETUP (UE 5.6 API) ==========

	/** Specify observation schema - defines structure of what data the agent observes */
	virtual void SpecifyAgentObservation_Implementation(
		FLearningAgentsObservationSchemaElement& OutObservationSchemaElement,
		ULearningAgentsObservationSchema* InObservationSchema) override;

	/** Specify action schema - defines structure of what actions the agent can take */
	virtual void SpecifyAgentAction_Implementation(
		FLearningAgentsActionSchemaElement& OutActionSchemaElement,
		ULearningAgentsActionSchema* InActionSchema) override;

	// ========== RUNTIME (UE 5.6 API) ==========

	/** Gather observations for a single agent - called every frame to provide current observation data */
	virtual void GatherAgentObservation_Implementation(
		FLearningAgentsObservationObjectElement& OutObservationObjectElement,
		ULearningAgentsObservationObject* InObservationObject,
		const int32 AgentId) override;

	/** Perform action for a single agent - called every frame to execute AI decisions */
	virtual void PerformAgentAction_Implementation(
		const ULearningAgentsActionObject* InActionObject,
		const FLearningAgentsActionObjectElement& InActionObjectElement,
		const int32 AgentId) override;

public:
	// ========== RECORDING HELPER ==========

	/**
	 * Encode human actions from controller's current input state into the Interactor's action buffer.
	 * This allows recording human demonstrations by directly reading input values.
	 * Call this after GatherObservations() and before Recorder->AddExperience().
	 *
	 * @param AgentId The agent to encode actions for
	 */
	void EncodeHumanActionsForAgent(int32 AgentId);

protected:
	// ========== HELPER FUNCTIONS ==========

	/** Get tank controller from agent ID */
	ABaseTankAIController* GetTankControllerFromAgentId(int32 AgentId) const;
};
