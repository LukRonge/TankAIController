// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LearningAgentsManagerListener.h"
#include "TankLearningAgentsTrainer.generated.h"

class ABaseTankAIController;

/**
 * Tank Learning Agents Trainer
 * Handles episode management for tank AI training
 * Works with ULearningAgentsImitationTrainer or ULearningAgentsPPOTrainer for actual training
 */
UCLASS(BlueprintType)
class TANKAICONTROLLER_API UTankLearningAgentsTrainer : public ULearningAgentsManagerListener
{
	GENERATED_BODY()

public:
	UTankLearningAgentsTrainer();

	// ========== LISTENER INTERFACE ==========

	/** Called when agents are added to the manager */
	virtual void OnAgentsAdded_Implementation(const TArray<int32>& AgentIds) override;

	/** Called when agents are removed from the manager */
	virtual void OnAgentsRemoved_Implementation(const TArray<int32>& AgentIds) override;

	/** Called when agents are reset */
	virtual void OnAgentsReset_Implementation(const TArray<int32>& AgentIds) override;

	// ========== EPISODE MANAGEMENT ==========

	/** Check if episode should be reset for an agent */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents|Training")
	bool ShouldResetEpisode(int32 AgentId) const;

	/** Reset episode for an agent */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents|Training")
	void ResetEpisodeForAgent(int32 AgentId);

	/** Update episode tracking - call this every tick */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents|Training")
	void UpdateEpisodes();

	// ========== CONFIGURATION ==========

	/** Maximum episode length in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Learning Agents|Config")
	float MaxEpisodeDuration = 300.0f;

	/** Minimum distance to obstacles before penalizing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Learning Agents|Config")
	float MinSafeDistance = 200.0f;

protected:
	// ========== HELPER FUNCTIONS ==========

	/** Get tank controller from agent ID */
	ABaseTankAIController* GetTankControllerFromAgentId(int32 AgentId) const;

private:
	/** Episode start times for each agent */
	TMap<int32, float> EpisodeStartTimes;
};
