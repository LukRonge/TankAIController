// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TankTrainingGameMode.generated.h"

class AWR_Tank_Pawn;
class AHumanPlayerController;
class AAILearningAgentsController;
class ATankLearningAgentsManager;

/**
 * Tank Training Game Mode
 * Manages spawning and setup of trainer (human-controlled) and agent (AI-controlled) tanks
 * Sets up the imitation learning training process
 */
UCLASS()
class TANKAICONTROLLER_API ATankTrainingGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATankTrainingGameMode();

protected:
	virtual void BeginPlay() override;

	/** Spawn trainer tank and assign to human player controller */
	void SpawnTrainerTank();

	/** Spawn agent tank and assign to AI learning controller */
	void SpawnAgentTank();

public:
	// ========== SPAWN CONFIGURATION ==========

	/** Trainer tank class (BP_Trainer_Tank) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Training|Setup")
	TSubclassOf<AWR_Tank_Pawn> TrainerTankClass;

	/** AI Agent tank class (BP_AI_Tank) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Training|Setup")
	TSubclassOf<AWR_Tank_Pawn> AITankClass;

	/** Trainer tank spawn location */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Setup")
	FVector TrainerSpawnLocation = FVector(0.0f, 0.0f, 100.0f);

	/** Agent tank spawn location */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Setup")
	FVector AgentSpawnLocation = FVector(500.0f, 0.0f, 100.0f);

	/** Trainer tank spawn rotation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Setup")
	FRotator TrainerSpawnRotation = FRotator::ZeroRotator;

	/** Agent tank spawn rotation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Setup")
	FRotator AgentSpawnRotation = FRotator::ZeroRotator;

	// ========== RUNTIME REFERENCES ==========

	/** Reference to spawned trainer tank */
	UPROPERTY(BlueprintReadOnly, Category = "Training|Runtime")
	TObjectPtr<AWR_Tank_Pawn> TrainerTank;

	/** Reference to spawned agent tank */
	UPROPERTY(BlueprintReadOnly, Category = "Training|Runtime")
	TObjectPtr<AWR_Tank_Pawn> AgentTank;

	/** Reference to human player controller */
	UPROPERTY(BlueprintReadOnly, Category = "Training|Runtime")
	TObjectPtr<AHumanPlayerController> HumanController;

	/** Reference to AI learning controller */
	UPROPERTY(BlueprintReadOnly, Category = "Training|Runtime")
	TObjectPtr<AAILearningAgentsController> AIController;

	// ========== GETTERS ==========

	/** Get trainer tank reference */
	UFUNCTION(BlueprintPure, Category = "Training")
	AWR_Tank_Pawn* GetTrainerTank() const { return TrainerTank; }

	/** Get agent tank reference */
	UFUNCTION(BlueprintPure, Category = "Training")
	AWR_Tank_Pawn* GetAgentTank() const { return AgentTank; }

	/** Get learning agents manager (searches in world) */
	UFUNCTION(BlueprintPure, Category = "Training")
	ATankLearningAgentsManager* FindLearningAgentsManager() const;

private:
	/** Register spawned tanks with Learning Agents Manager */
	void RegisterTanksWithManager();
};
