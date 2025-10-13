// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TankLearningAgentsManager.generated.h"

class UTankLearningAgentsInteractor;
class UTankLearningAgentsTrainer;
class ULearningAgentsPolicy;
class ULearningAgentsImitationTrainer;
class ULearningAgentsRecorder;
class ULearningAgentsManager;
class AWR_Tank_Pawn;

/**
 * Tank Learning Agents Manager Actor
 * Manages the tank AI training process using Learning Agents plugin
 * Handles agent registration and training loop
 * Place this actor in the level for AI training to work
 */
UCLASS(BlueprintType, Blueprintable)
class TANKAICONTROLLER_API ATankLearningAgentsManager : public AActor
{
	GENERATED_BODY()

public:
	ATankLearningAgentsManager();

	// ========== SETUP ==========

	/** Initialize the manager and setup Learning Agents components */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents")
	void InitializeManager();

	/** Add a tank agent to be managed */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents")
	int32 AddTankAgent(AActor* TankActor);

	/** Remove a tank agent */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents")
	void RemoveTankAgent(int32 AgentId);

	// ========== COMPONENTS ==========

	/** Base Learning Agents Manager component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Learning Agents")
	TObjectPtr<ULearningAgentsManager> Manager;

	/** Interactor component - handles observations and actions */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Learning Agents")
	TObjectPtr<UTankLearningAgentsInteractor> Interactor;

	/** Policy component - contains the neural network */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Learning Agents")
	TObjectPtr<ULearningAgentsPolicy> Policy;

	/** Trainer component - handles training and rewards (not used in imitation learning) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Learning Agents")
	TObjectPtr<UTankLearningAgentsTrainer> Trainer;

	/** Imitation Trainer component - learns from human demonstrations */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Learning Agents")
	TObjectPtr<ULearningAgentsImitationTrainer> ImitationTrainer;

	/** Recorder component - records demonstrations for imitation learning */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Learning Agents")
	TObjectPtr<ULearningAgentsRecorder> Recorder;

	// ========== CONFIGURATION ==========

	/** Maximum number of agents to manage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Learning Agents|Config")
	int32 MaxAgents = 10;

	/** Whether to automatically start training */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Learning Agents|Config")
	bool bAutoStartTraining = false;

	// ========== GETTERS ==========

	/** Get the interactor component */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents")
	UTankLearningAgentsInteractor* GetInteractor() const { return Interactor; }

	/** Get the policy component */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents")
	ULearningAgentsPolicy* GetPolicy() const { return Policy; }

	/** Get the trainer component */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents")
	UTankLearningAgentsTrainer* GetTrainer() const { return Trainer; }

	/** Get the imitation trainer component */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents")
	ULearningAgentsImitationTrainer* GetImitationTrainer() const { return ImitationTrainer; }

	/** Get the recorder component */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents")
	ULearningAgentsRecorder* GetRecorder() const { return Recorder; }

	/** Get the base manager component */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents")
	ULearningAgentsManager* GetManager() const { return Manager; }

	/** Get the trainer tank reference */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents")
	AWR_Tank_Pawn* GetTrainerTank() const { return TrainerTank; }

	// ========== TRAINING WORKFLOW ==========

	/** Start recording demonstrations from the human trainer tank */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents|Training")
	void StartRecording();

	/** Stop recording demonstrations */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents|Training")
	void StopRecording();

	/** Start training the AI from recorded demonstrations */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents|Training")
	void StartTraining();

	/** Stop training */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents|Training")
	void StopTraining();

	/** Check if currently recording */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents|Training")
	bool IsRecording() const;

	/** Check if currently training */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents|Training")
	bool IsTraining() const;

	// ========== REGISTRATION (GameMode → Manager) ==========

	/** Register externally spawned trainer tank (called by GameMode) */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents|Registration")
	void RegisterTrainerTank(AWR_Tank_Pawn* Tank);

	/** Register externally spawned agent tank (called by GameMode) */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents|Registration")
	void RegisterAgentTank(AWR_Tank_Pawn* Tank);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	/** Reference to spawned trainer tank */
	UPROPERTY()
	TObjectPtr<AWR_Tank_Pawn> TrainerTank;

	/** Reference to spawned agent tank */
	UPROPERTY()
	TObjectPtr<AWR_Tank_Pawn> AgentTank;

	/** Trainer tank agent ID */
	int32 TrainerAgentId = INDEX_NONE;

	/** Agent tank agent ID */
	int32 AgentAgentId = INDEX_NONE;
};
