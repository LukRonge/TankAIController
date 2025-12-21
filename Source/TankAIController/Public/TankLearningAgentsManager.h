// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LearningAgentsTrainer.h" // For ELearningAgentsTrainingDevice enum
#include "TankLearningAgentsManager.generated.h"

class UTankLearningAgentsInteractor;
class UTankLearningAgentsTrainer;
class ULearningAgentsPolicy;
class ULearningAgentsImitationTrainer;
class ULearningAgentsRecorder;
class ULearningAgentsManager;
class AWR_Tank_Pawn;

/**
 * Target Segment Data Structure
 * Tracks one recorded target sequence (completed or incomplete)
 */
USTRUCT(BlueprintType)
struct FTargetSegment
{
	GENERATED_BODY()

	/** Target location in world space */
	UPROPERTY(BlueprintReadOnly, Category = "Target Segment")
	FVector TargetLocation = FVector::ZeroVector;

	/** Index of first experience in this segment */
	UPROPERTY(BlueprintReadOnly, Category = "Target Segment")
	int32 StartExperienceIndex = 0;

	/** Index of last experience in this segment (-1 if incomplete) */
	UPROPERTY(BlueprintReadOnly, Category = "Target Segment")
	int32 EndExperienceIndex = -1;

	/** Was this target completed? */
	UPROPERTY(BlueprintReadOnly, Category = "Target Segment")
	bool bCompleted = false;

	/** Timestamp when target was generated */
	UPROPERTY(BlueprintReadOnly, Category = "Target Segment")
	float StartTime = 0.0f;

	/** Timestamp when target was completed */
	UPROPERTY(BlueprintReadOnly, Category = "Target Segment")
	float EndTime = 0.0f;

	/** Waypoints to reach the target (path from start to target) */
	UPROPERTY(BlueprintReadOnly, Category = "Target Segment")
	TArray<FVector> Waypoints;

	/** Number of waypoints completed */
	UPROPERTY(BlueprintReadOnly, Category = "Target Segment")
	int32 CompletedWaypointsCount = 0;

	/** Total number of waypoints generated */
	UPROPERTY(BlueprintReadOnly, Category = "Target Segment")
	int32 TotalWaypointsCount = 0;
};

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

	/** Training device - GPU is much faster than CPU (requires CUDA) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Learning Agents|Config")
	ELearningAgentsTrainingDevice TrainingDevice = ELearningAgentsTrainingDevice::GPU;

	/** Policy save file path (relative to project Saved directory) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Learning Agents|Config")
	FString PolicySavePath = TEXT("LearningAgents/Policies/TankPolicy.policy");

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

	// ========== TRAINING METRICS ==========

	/** Get current training iteration count */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents|Metrics")
	int32 GetCurrentIteration() const { return CurrentIteration; }

	/** Get current training loss value */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents|Metrics")
	float GetCurrentLoss() const { return CurrentLoss; }

	/** Get training progress (0.0 to 1.0) */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents|Metrics")
	float GetTrainingProgress() const;

	/** Get total training iterations target */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents|Metrics")
	int32 GetTotalIterations() const { return TotalIterations; }

	/** Get number of recorded experiences */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents|Metrics")
	int32 GetRecordedExperienceCount() const;

	/** Save policy to file */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents|Training")
	void SavePolicy();

	/** Load policy from file */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents|Training")
	void LoadPolicy();

	/** Save policy checkpoint with custom name */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents|Training")
	void SavePolicyCheckpoint(const FString& CheckpointName);

	// ========== REGISTRATION (GameMode → Manager) ==========

	/** Register externally spawned trainer tank (called by GameMode) */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents|Registration")
	void RegisterTrainerTank(AWR_Tank_Pawn* Tank);

	/** Register externally spawned agent tank (called by GameMode) */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents|Registration")
	void RegisterAgentTank(AWR_Tank_Pawn* Tank);

	/** Set agent tank reference WITHOUT registering (for delayed inference) */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents|Registration")
	void SetAgentTank(AWR_Tank_Pawn* Tank);

	// ========== INFERENCE MODE ==========

	/**
	 * Enable inference mode - registers AI tank and starts policy inference
	 * Call this AFTER training is complete to test the trained AI policy
	 * This should NOT be called during recording phase (causes warnings)
	 */
	UFUNCTION(BlueprintCallable, Category = "Tank Learning Agents|Inference")
	void EnableInferenceMode();

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

	// ========== TRAINING METRICS TRACKING ==========

	/** Current training iteration */
	int32 CurrentIteration = 0;

	/** Current training loss value */
	float CurrentLoss = 0.0f;

	/** Total number of iterations for training */
	int32 TotalIterations = 100000;

	/** Log interval for training progress (every N iterations) */
	UPROPERTY(EditAnywhere, Category = "Tank Learning Agents|Metrics")
	int32 LogInterval = 100;

	/** Checkpoint save interval (every N iterations) */
	UPROPERTY(EditAnywhere, Category = "Tank Learning Agents|Metrics")
	int32 CheckpointInterval = 1000;

	// ========== RECORDING METRICS TRACKING ==========

	/** Number of recorded experiences (frames) - incremented on each AddExperience() */
	int32 RecordedExperiencesCount = 0;

	// ========== TARGET-BASED RECORDING SYSTEM ==========

	/** Enable target-based recording (vs. free-form recording) */
	UPROPERTY(EditAnywhere, Category = "Tank Learning Agents|Target System")
	bool bUseTargetBasedRecording = true;

	/** Minimum distance for target generation (cm) */
	UPROPERTY(EditAnywhere, Category = "Tank Learning Agents|Target System", meta = (EditCondition = "bUseTargetBasedRecording"))
	float MinTargetDistance = 1000.0f;  // 10m

	/** Maximum distance for target generation (cm) */
	UPROPERTY(EditAnywhere, Category = "Tank Learning Agents|Target System", meta = (EditCondition = "bUseTargetBasedRecording"))
	float MaxTargetDistance = 3000.0f;  // 30m

	/** Radius to consider target "reached" (cm) */
	UPROPERTY(EditAnywhere, Category = "Tank Learning Agents|Target System", meta = (EditCondition = "bUseTargetBasedRecording"))
	float TargetReachRadius = 200.0f;  // 2m

	/** Enable debug visualization of target */
	UPROPERTY(EditAnywhere, Category = "Tank Learning Agents|Target System", meta = (EditCondition = "bUseTargetBasedRecording"))
	bool bShowTargetVisualization = true;

	// ========== WAYPOINT SYSTEM CONFIGURATION ==========

	/** Enable waypoint-based path following (requires target-based recording) */
	UPROPERTY(EditAnywhere, Category = "Tank Learning Agents|Waypoint System", meta = (EditCondition = "bUseTargetBasedRecording"))
	bool bUseWaypointPathFollowing = true;

	/** Radius to consider waypoint "reached" (cm) */
	UPROPERTY(EditAnywhere, Category = "Tank Learning Agents|Waypoint System", meta = (EditCondition = "bUseWaypointPathFollowing"))
	float WaypointReachRadius = 100.0f;  // 1m

	/** Enable debug visualization of waypoints */
	UPROPERTY(EditAnywhere, Category = "Tank Learning Agents|Waypoint System", meta = (EditCondition = "bUseWaypointPathFollowing"))
	bool bShowWaypointVisualization = true;

	// ========== TARGET & WAYPOINT RUNTIME STATE ==========

	/** Current active target location */
	FVector CurrentTargetLocation = FVector::ZeroVector;

	/** Waypoints to current target (path from trainer to target) */
	TArray<FVector> CurrentWaypoints;

	/** Current waypoint index being followed */
	int32 CurrentWaypointIndex = 0;

	/** List of all target segments (completed and incomplete) */
	TArray<FTargetSegment> TargetSegments;

	/** Current segment being recorded */
	FTargetSegment CurrentSegment;

	/** Visual representation of current target (debug sphere) */
	UPROPERTY()
	TObjectPtr<AActor> TargetVisualizationActor;

	// ========== TARGET SYSTEM METHODS ==========

	/** Generate new random target on NavMesh */
	void GenerateNewTarget();

	/** Check if trainer tank reached current target */
	bool IsTargetReached() const;

	/** Called when target is reached - complete segment and generate new target */
	void OnTargetReached();

	/** Create visual representation of target in world */
	void CreateTargetVisualization();

	/** Update target visualization position */
	void UpdateTargetVisualization();

	/** Destroy target visualization actor */
	void DestroyTargetVisualization();

	// ========== WAYPOINT SYSTEM METHODS ==========

	/** Generate waypoints from trainer to target using NavMesh pathfinding */
	void GenerateWaypointsToTarget();

	/** Check if current waypoint is reached */
	bool IsCurrentWaypointReached() const;

	/** Advance to next waypoint (mark current as completed) */
	void AdvanceToNextWaypoint();

	/** Check if all waypoints are completed */
	bool AreAllWaypointsCompleted() const;

public:
	// ========== TARGET SYSTEM GETTERS ==========

	/** Get current target location */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents|Target System")
	FVector GetCurrentTargetLocation() const { return CurrentTargetLocation; }

	/** Check if target system is enabled */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents|Target System")
	bool IsTargetBasedRecordingEnabled() const { return bUseTargetBasedRecording; }

	/** Get number of completed targets */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents|Target System")
	int32 GetCompletedTargetsCount() const;

	/** Get total number of target segments (completed + incomplete) */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents|Target System")
	int32 GetTotalTargetsCount() const { return TargetSegments.Num(); }

	// ========== WAYPOINT SYSTEM GETTERS ==========

	/** Check if waypoint system is enabled */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents|Waypoint System")
	bool IsWaypointPathFollowingEnabled() const { return bUseWaypointPathFollowing && bUseTargetBasedRecording; }

	/** Get number of waypoints in current path */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents|Waypoint System")
	int32 GetCurrentWaypointCount() const { return CurrentWaypoints.Num(); }

	/** Get current waypoint index */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents|Waypoint System")
	int32 GetCurrentWaypointIndex() const { return CurrentWaypointIndex; }

	/** Get number of completed waypoints in current segment */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents|Waypoint System")
	int32 GetCompletedWaypointsInCurrentSegment() const { return CurrentWaypointIndex; }

	/** Get current waypoint location (or target if no waypoints/all completed) */
	UFUNCTION(BlueprintPure, Category = "Tank Learning Agents|Waypoint System")
	FVector GetCurrentWaypointLocation() const;

	/** Is there an active target? (public for Interactor access) */
	bool bHasActiveTarget = false;
};
