// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TankTrainingGameMode.generated.h"

class AWR_Tank_Pawn;
class AHumanPlayerController;
class AAILearningAgentsController;
class ATankLearningAgentsManager;
class APlayerStart;

/**
 * Tank Training Game Mode
 * Manages spawning and setup of trainer (human-controlled) and agent (AI-controlled) tanks
 *
 * SPAWNING MODES:
 * - bSpawnAIAtPlayerStarts=true: Spawns AI tank at each PlayerStart in the level
 * - bSpawnAIAtPlayerStarts=false: Spawns single AI tank at AgentSpawnLocation
 *
 * AI CONTROL:
 * - AI tanks start with movement DISABLED
 * - Press NumPad7 to start all AI tanks (enables movement)
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

	/** Spawn agent tank at specific location and assign to AI learning controller */
	AWR_Tank_Pawn* SpawnAgentTankAtLocation(const FVector& Location, const FRotator& Rotation);

	/** Spawn AI tanks at all PlayerStart locations */
	void SpawnAITanksAtPlayerStarts();

	/** Legacy: Spawn single agent tank at AgentSpawnLocation */
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

	/** Agent tank spawn location (used when bSpawnAIAtPlayerStarts=false) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Setup")
	FVector AgentSpawnLocation = FVector(500.0f, 0.0f, 100.0f);

	/** Trainer tank spawn rotation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Setup")
	FRotator TrainerSpawnRotation = FRotator::ZeroRotator;

	/** Agent tank spawn rotation (used when bSpawnAIAtPlayerStarts=false) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Setup")
	FRotator AgentSpawnRotation = FRotator::ZeroRotator;

	/** If true, spawn AI tank at each PlayerStart location in the level */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training|Setup")
	bool bSpawnAIAtPlayerStarts = true;

	// ========== RUNTIME REFERENCES ==========

	/** Reference to spawned trainer tank */
	UPROPERTY(BlueprintReadOnly, Category = "Training|Runtime")
	TObjectPtr<AWR_Tank_Pawn> TrainerTank;

	/** Reference to spawned agent tank (legacy - first AI tank) */
	UPROPERTY(BlueprintReadOnly, Category = "Training|Runtime")
	TObjectPtr<AWR_Tank_Pawn> AgentTank;

	/** Reference to human player controller */
	UPROPERTY(BlueprintReadOnly, Category = "Training|Runtime")
	TObjectPtr<AHumanPlayerController> HumanController;

	/** Reference to AI learning controller (legacy - first AI controller) */
	UPROPERTY(BlueprintReadOnly, Category = "Training|Runtime")
	TObjectPtr<AAILearningAgentsController> AIController;

	/** All spawned AI tanks */
	UPROPERTY(BlueprintReadOnly, Category = "Training|Runtime")
	TArray<TObjectPtr<AWR_Tank_Pawn>> AITanks;

	/** All AI controllers */
	UPROPERTY(BlueprintReadOnly, Category = "Training|Runtime")
	TArray<TObjectPtr<AAILearningAgentsController>> AIControllers;

	// ========== AI CONTROL ==========

	/** Start all AI tanks (enable movement). Called when NumPad7 is pressed. */
	UFUNCTION(BlueprintCallable, Category = "Training")
	void StartAllAITanks();

	/** Stop all AI tanks (disable movement) */
	UFUNCTION(BlueprintCallable, Category = "Training")
	void StopAllAITanks();

	/** Check if AI tanks are currently running */
	UFUNCTION(BlueprintPure, Category = "Training")
	bool AreAITanksRunning() const { return bAITanksRunning; }

	/** Get number of spawned AI tanks */
	UFUNCTION(BlueprintPure, Category = "Training")
	int32 GetAITankCount() const { return AITanks.Num(); }

	// ========== GETTERS ==========

	/** Get trainer tank reference */
	UFUNCTION(BlueprintPure, Category = "Training")
	AWR_Tank_Pawn* GetTrainerTank() const { return TrainerTank; }

	/** Get agent tank reference (first AI tank) */
	UFUNCTION(BlueprintPure, Category = "Training")
	AWR_Tank_Pawn* GetAgentTank() const { return AgentTank; }

	/** Get learning agents manager (searches in world) */
	UFUNCTION(BlueprintPure, Category = "Training")
	ATankLearningAgentsManager* FindLearningAgentsManager() const;

private:
	/** Register spawned tanks with Learning Agents Manager */
	void RegisterTanksWithManager();

	/** Whether AI tanks are currently running */
	bool bAITanksRunning = false;
};
