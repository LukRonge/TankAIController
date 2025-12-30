// Copyright Epic Games, Inc. All Rights Reserved.

#include "TankLearningAgentsManager.h"
#include "TankLearningAgentsInteractor.h"
#include "TankLearningAgentsTrainer.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsPolicy.h"
#include "LearningAgentsImitationTrainer.h"
#include "LearningAgentsRecorder.h"
#include "LearningAgentsCommunicator.h"
#include "LearningAgentsTrainer.h"
#include "LearningAgentsNeuralNetwork.h"
#include "WR_Tank_Pawn.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "DrawDebugHelpers.h"
#include "Components/SphereComponent.h"
#include "Engine/StaticMeshActor.h"

ATankLearningAgentsManager::ATankLearningAgentsManager()
{
	// Set default max agents
	MaxAgents = 10;
	bAutoStartTraining = false;

	// Enable ticking
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ATankLearningAgentsManager::BeginPlay()
{
	Super::BeginPlay();

	// Create the base ULearningAgentsManager component
	Manager = NewObject<ULearningAgentsManager>(this, ULearningAgentsManager::StaticClass(), TEXT("LearningAgentsManager"));
	if (!Manager)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Failed to create base Manager component!"));
		return;
	}

	// Register the manager component
	Manager->RegisterComponent();

	// Set MaxAgentNum on the underlying Manager component to match our MaxAgents setting
	Manager->SetMaxAgentNum(MaxAgents);
	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Set Manager MaxAgentNum to %d"), MaxAgents);

	// Initialize all Learning Agents components
	InitializeManager();

	// Tanks will be spawned and registered by TankTrainingGameMode
	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Waiting for TankTrainingGameMode to spawn and register tanks..."));
}

void ATankLearningAgentsManager::InitializeManager()
{
	if (!Manager)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Manager component is null! Cannot initialize."));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager::InitializeManager: Starting initialization..."));
	UE_LOG(LogTemp, Log, TEXT("  -> MaxAgents setting: %d"), MaxAgents);

	// Get reference to manager for factory methods (they need ULearningAgentsManager*&)
	ULearningAgentsManager* ManagerRef = Manager;

	// Create Interactor using factory method
	Interactor = Cast<UTankLearningAgentsInteractor>(
		ULearningAgentsInteractor::MakeInteractor(
			ManagerRef,
			UTankLearningAgentsInteractor::StaticClass(),
			TEXT("TankInteractor")));

	if (Interactor)
	{
		UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Interactor created successfully."));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Failed to create Interactor!"));
		return;
	}

	// Create Policy using factory method with EXPLICIT settings
	// NARROW CORRIDOR OPTIMIZED (v2.0): Deeper and wider network for complex navigation
	ULearningAgentsInteractor* InteractorRef = Interactor;

	FLearningAgentsPolicySettings PolicySettings;
	PolicySettings.HiddenLayerNum = 3;          // 3 hidden layers (increased for corridor complexity)
	PolicySettings.HiddenLayerSize = 128;       // 128 neurons (increased representation capacity)
	PolicySettings.bUseMemory = false;          // DISABLE memory - reactive task doesn't need it
	PolicySettings.MemoryStateSize = 0;         // No memory state
	PolicySettings.InitialEncodedActionScale = 0.7f;  // Increased from 0.1 - allows network to output larger actions from start
	PolicySettings.ActivationFunction = ELearningAgentsActivationFunction::ELU;  // ELU handles negative inputs well
	PolicySettings.bUseParallelEvaluation = true;  // Enable for GPU performance

	UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Creating Policy with NARROW CORRIDOR settings:"));
	UE_LOG(LogTemp, Warning, TEXT("  -> HiddenLayers: %d x %d neurons (optimized for corridors)"), PolicySettings.HiddenLayerNum, PolicySettings.HiddenLayerSize);
	UE_LOG(LogTemp, Warning, TEXT("  -> Memory: DISABLED (reactive task)"));
	UE_LOG(LogTemp, Warning, TEXT("  -> Activation: ELU | Parallel: ENABLED"));

	Policy = ULearningAgentsPolicy::MakePolicy(
		ManagerRef,
		InteractorRef,
		ULearningAgentsPolicy::StaticClass(),
		TEXT("TankPolicy"),
		nullptr,  // EncoderNeuralNetworkAsset
		nullptr,  // PolicyNeuralNetworkAsset
		nullptr,  // DecoderNeuralNetworkAsset
		true,     // bReinitializeEncoderNetwork
		true,     // bReinitializePolicyNetwork
		true,     // bReinitializeDecoderNetwork
		PolicySettings,  // Custom policy settings
		1234);    // Seed

	if (Policy)
	{
		UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Policy created successfully."));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Failed to create Policy!"));
		return;
	}

	// NOTE: Controller is NOT needed for Imitation Learning workflow
	// Controller is only used for hand-crafted policies (e.g., behavior trees generating actions)
	// For Imitation Learning: Human input → Interactor → Recorder → ImitationTrainer → Policy
	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Controller not created (not needed for Imitation Learning workflow)."));

	// Create Communicator for training process (using shared memory for local development)
	// IMPORTANT: Must specify train_behavior_cloning trainer for Imitation Learning (default is train_ppo)
	FLearningAgentsTrainerProcessSettings ProcessSettings;
	ProcessSettings.TaskName = TEXT("TankImitationTraining");
	ProcessSettings.TrainerFileName = TEXT("train_behavior_cloning"); // Use Behavior Cloning (Imitation Learning), not PPO

	FLearningAgentsCommunicator Communicator = ULearningAgentsCommunicatorLibrary::MakeSharedMemoryTrainingProcess(ProcessSettings);

	// Create Recorder using factory method
	Recorder = ULearningAgentsRecorder::MakeRecorder(
		ManagerRef,
		InteractorRef,
		ULearningAgentsRecorder::StaticClass(),
		TEXT("TankRecorder"));

	if (Recorder)
	{
		UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Recorder created successfully."));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Failed to create Recorder!"));
		return;
	}

	// Create Imitation Trainer using factory method
	ULearningAgentsPolicy* PolicyRef = Policy;
	ImitationTrainer = ULearningAgentsImitationTrainer::MakeImitationTrainer(
		ManagerRef,
		InteractorRef,
		PolicyRef,
		Communicator,
		ULearningAgentsImitationTrainer::StaticClass(),
		TEXT("TankImitationTrainer"));

	if (ImitationTrainer)
	{
		UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Imitation Trainer created successfully."));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Failed to create Imitation Trainer!"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Initialization complete - All components created including Recorder and Imitation Trainer."));
}

int32 ATankLearningAgentsManager::AddTankAgent(AActor* TankActor)
{
	if (!TankActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager::AddTankAgent: Cannot add null tank actor!"));
		return INDEX_NONE;
	}

	if (!Manager)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager::AddTankAgent: Manager component is null! Cannot add agent."));
		return INDEX_NONE;
	}

	// Log detailed information about the tank being added
	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager::AddTankAgent: Attempting to add tank:"));
	UE_LOG(LogTemp, Log, TEXT("  -> Tank Actor: %s (Class: %s)"), *TankActor->GetName(), *TankActor->GetClass()->GetName());
	UE_LOG(LogTemp, Log, TEXT("  -> Manager MaxAgents: %d"), MaxAgents);

	// Check if tank has a controller
	AWR_Tank_Pawn* TankPawn = Cast<AWR_Tank_Pawn>(TankActor);
	if (TankPawn)
	{
		AController* Controller = TankPawn->GetController();
		if (Controller)
		{
			UE_LOG(LogTemp, Log, TEXT("  -> Tank Controller: %s (Class: %s)"), *Controller->GetName(), *Controller->GetClass()->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("  -> Tank has NO Controller assigned!"));
		}
	}

	// Add agent using the base manager component method
	const int32 AgentId = Manager->AddAgent(TankActor);

	if (AgentId != INDEX_NONE)
	{
		UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager::AddTankAgent: ✓ Successfully added tank agent %d for actor %s"),
			AgentId, *TankActor->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager::AddTankAgent: ✗ FAILED to add tank agent for actor %s"),
			*TankActor->GetName());
		UE_LOG(LogTemp, Error, TEXT("  -> Manager->AddAgent() returned INDEX_NONE"));
		UE_LOG(LogTemp, Error, TEXT("  -> Possible causes: MaxAgents limit reached, invalid actor, or Manager not properly initialized"));
	}

	return AgentId;
}

void ATankLearningAgentsManager::RemoveTankAgent(int32 AgentId)
{
	if (AgentId == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Invalid agent ID!"));
		return;
	}

	if (!Manager)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Manager component is null! Cannot remove agent."));
		return;
	}

	// Remove agent using the base manager component method
	Manager->RemoveAgent(AgentId);

	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Removed tank agent %d"), AgentId);
}

void ATankLearningAgentsManager::StartRecording()
{
	if (!Recorder)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Cannot start recording - Recorder not initialized!"));
		return;
	}

	if (IsRecording())
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Already recording!"));
		return;
	}

	// Reset recorded experiences counter
	RecordedExperiencesCount = 0;

	// Begin recording demonstrations from all agents
	Recorder->BeginRecording();
	UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Started recording demonstrations from trainer tank."));

	// Target-based recording: Generate first target
	if (bUseTargetBasedRecording)
	{
		// Clear previous target segments
		TargetSegments.Empty();
		bHasActiveTarget = false;

		// Generate first target
		GenerateNewTarget();

		if (bHasActiveTarget)
		{
			UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Target-based recording enabled - navigate to target!"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Failed to generate first target - recording will continue without targets"));
		}
	}
}

void ATankLearningAgentsManager::StopRecording()
{
	if (!Recorder)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Cannot stop recording - Recorder not initialized!"));
		return;
	}

	if (!IsRecording())
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Not currently recording!"));
		return;
	}

	// Target-based recording: Handle incomplete target
	if (bUseTargetBasedRecording && bHasActiveTarget)
	{
		// Check if current target was reached before stopping
		if (IsTargetReached())
		{
			// Complete the final target before stopping
			UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Completing final target before stopping recording..."));
			OnTargetReached();
		}
		else
		{
			// Incomplete target - do NOT add to completed segments
			const int32 IncompleteExperiences = RecordedExperiencesCount - CurrentSegment.StartExperienceIndex;
			UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Recording stopped with INCOMPLETE target!"));
			UE_LOG(LogTemp, Warning, TEXT("  -> Incomplete target experiences: %d frames (will be included in training)"), IncompleteExperiences);
			UE_LOG(LogTemp, Warning, TEXT("  -> Note: Incomplete target data is minimal compared to completed targets"));

			// Clear incomplete target state
			bHasActiveTarget = false;
		}

		// Print target-based recording statistics
		const int32 CompletedTargets = GetCompletedTargetsCount();
		int32 TotalValidExperiences = 0;
		for (const FTargetSegment& Segment : TargetSegments)
		{
			if (Segment.bCompleted)
			{
				TotalValidExperiences += (Segment.EndExperienceIndex - Segment.StartExperienceIndex + 1);
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Target-based recording statistics:"));
		UE_LOG(LogTemp, Warning, TEXT("  -> Completed targets: %d"), CompletedTargets);
		UE_LOG(LogTemp, Warning, TEXT("  -> Total targets attempted: %d"), TargetSegments.Num());
		UE_LOG(LogTemp, Warning, TEXT("  -> Valid experiences (completed targets): %d frames"), TotalValidExperiences);
		UE_LOG(LogTemp, Warning, TEXT("  -> Total experiences (all): %d frames"), RecordedExperiencesCount);

		// Destroy visualization
		DestroyTargetVisualization();
	}

	// End recording
	Recorder->EndRecording();

	// Final recording summary
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: RECORDING COMPLETE"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("  -> Total frames recorded: %d"), RecordedExperiencesCount);
	UE_LOG(LogTemp, Warning, TEXT("  -> Data stored in: In-memory buffer (ready for training)"));
	UE_LOG(LogTemp, Warning, TEXT("  -> Next step: Call StartTraining() to train the AI"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

void ATankLearningAgentsManager::StartTraining()
{
	if (!ImitationTrainer)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Cannot start training - ImitationTrainer not initialized!"));
		return;
	}

	if (!Recorder)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Cannot start training - Recorder not initialized!"));
		return;
	}

	if (IsTraining())
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Already training!"));
		return;
	}

	// Get the recording from the recorder
	const ULearningAgentsRecording* Recording = Recorder->GetRecordingAsset();
	if (!Recording)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Cannot start training - No recording available! Record demonstrations first."));
		return;
	}

	// Validate recording has data
	if (RecordedExperiencesCount <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Cannot start training - Recording is empty!"));
		UE_LOG(LogTemp, Error, TEXT("  -> Recorded experiences: %d"), RecordedExperiencesCount);
		UE_LOG(LogTemp, Error, TEXT("  -> Please record demonstrations first using StartRecording/StopRecording"));
		return;
	}

	// Log recording data summary
	UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Training data validation:"));
	UE_LOG(LogTemp, Warning, TEXT("  -> Recorded experiences: %d frames"), RecordedExperiencesCount);
	UE_LOG(LogTemp, Warning, TEXT("  -> Completed targets: %d"), GetCompletedTargetsCount());
	UE_LOG(LogTemp, Warning, TEXT("  -> Data source: In-memory recording buffer"));

	// Configure Imitation Trainer Settings
	FLearningAgentsImitationTrainerSettings ImitationTrainerSettings;
	ImitationTrainerSettings.TrainerCommunicationTimeout = 30.0f; // 30 seconds timeout

	// Configure Imitation Trainer Training Settings
	// NARROW CORRIDOR OPTIMIZED (v2.0): Smaller batch, more iterations, adaptive LR
	FLearningAgentsImitationTrainerTrainingSettings TrainingSettings;

	// IMPROVED: Adaptive iteration count based on epochs through dataset
	// Target: ~40 epochs through the dataset
	const int32 BatchSize = 32;  // Smaller batch = 2x more gradient updates
	const int32 BatchesPerEpoch = FMath::Max(1, RecordedExperiencesCount / BatchSize);
	const int32 TargetEpochs = 40;  // 40 epochs through dataset

	const int32 AdaptiveIterations = FMath::Clamp(
		BatchesPerEpoch * TargetEpochs,  // Iterations based on epochs
		1500,                            // Minimum: 1500 iterations (increased from 500)
		20000                            // Maximum: 20000 iterations (increased from 5000)
	);

	// ADAPTIVE LEARNING RATE: Lower for larger datasets
	float AdaptiveLearningRate = 0.001f;
	if (RecordedExperiencesCount > 5000)
	{
		AdaptiveLearningRate = 0.0005f;  // Lower for large datasets
	}
	if (RecordedExperiencesCount > 10000)
	{
		AdaptiveLearningRate = 0.0003f;  // Even lower for very large datasets
	}

	TrainingSettings.NumberOfIterations = AdaptiveIterations;
	TrainingSettings.LearningRate = AdaptiveLearningRate;    // Adaptive learning rate
	TrainingSettings.LearningRateDecay = 0.9995f;            // Slower decay (was 0.999)
	TrainingSettings.WeightDecay = 0.0001f;                  // Weight decay for regularization
	TrainingSettings.BatchSize = BatchSize;                  // Smaller batch = more gradient updates
	TrainingSettings.Window = 1;                             // Single frame - direct obs->action mapping
	TrainingSettings.ActionRegularizationWeight = 0.001f;    // Reduced - was penalizing large throttle values too much
	TrainingSettings.ActionEntropyWeight = 0.0f;             // No entropy (deterministic actions)
	TrainingSettings.RandomSeed = 1234;                      // Random seed
	TrainingSettings.Device = TrainingDevice;                // GPU/CPU (blueprint configurable)
	TrainingSettings.bUseTensorboard = false;                // No TensorBoard
	TrainingSettings.bSaveSnapshots = true;                  // Save snapshots
	TrainingSettings.bUseMLflow = false;                     // No MLflow

	UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: NARROW CORRIDOR training settings:"));
	UE_LOG(LogTemp, Warning, TEXT("  -> Samples: %d"), RecordedExperiencesCount);
	UE_LOG(LogTemp, Warning, TEXT("  -> BatchesPerEpoch: %d | TargetEpochs: %d"), BatchesPerEpoch, TargetEpochs);
	UE_LOG(LogTemp, Warning, TEXT("  -> Iterations: %d (adaptive, clamped 1500-20000)"), AdaptiveIterations);
	UE_LOG(LogTemp, Warning, TEXT("  -> BatchSize: %d (smaller = more gradient updates)"), TrainingSettings.BatchSize);
	UE_LOG(LogTemp, Warning, TEXT("  -> LearningRate: %.4f (adaptive by dataset size)"), TrainingSettings.LearningRate);
	UE_LOG(LogTemp, Warning, TEXT("  -> ActionRegularization: %.3f | LRDecay: %.4f"), TrainingSettings.ActionRegularizationWeight, TrainingSettings.LearningRateDecay);

	// Configure Process Path Settings (use defaults)
	FLearningAgentsTrainerProcessSettings PathSettings;

	// Store total iterations for progress tracking
	TotalIterations = TrainingSettings.NumberOfIterations;
	CurrentIteration = 0;
	CurrentLoss = 0.0f;

	// Begin training using recorded demonstrations with proper settings
	ImitationTrainer->BeginTraining(Recording, ImitationTrainerSettings, TrainingSettings, PathSettings);
	UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Started training from recorded demonstrations."));
	UE_LOG(LogTemp, Warning, TEXT("  -> Iterations: %d"), TrainingSettings.NumberOfIterations);
	UE_LOG(LogTemp, Warning, TEXT("  -> Learning Rate: %.6f"), TrainingSettings.LearningRate);
	UE_LOG(LogTemp, Warning, TEXT("  -> Batch Size: %d"), TrainingSettings.BatchSize);
	UE_LOG(LogTemp, Warning, TEXT("  -> Device: %s"), TrainingSettings.Device == ELearningAgentsTrainingDevice::CPU ? TEXT("CPU") : TEXT("GPU"));
}

void ATankLearningAgentsManager::StopTraining()
{
	if (!ImitationTrainer)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Cannot stop training - ImitationTrainer not initialized!"));
		return;
	}

	if (!IsTraining())
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Not currently training!"));
		return;
	}

	// End training
	ImitationTrainer->EndTraining();

	// Log training summary
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: TRAINING COMPLETE"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("  -> Final iteration: %d/%d"), CurrentIteration, TotalIterations);
	UE_LOG(LogTemp, Warning, TEXT("  -> Training progress: %.1f%%"), GetTrainingProgress() * 100.0f);
	UE_LOG(LogTemp, Warning, TEXT("  -> Recorded experiences used: %d frames"), RecordedExperiencesCount);

	// Automatically save trained policy
	UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Auto-saving trained policy..."));
	SavePolicy();

	// Automatically enable inference mode after training completes
	UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Enabling inference mode for AI tank..."));
	EnableInferenceMode();

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: AI tank is now ready for testing!"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

bool ATankLearningAgentsManager::IsRecording() const
{
	if (!Recorder)
	{
		return false;
	}

	return Recorder->IsRecording();
}

bool ATankLearningAgentsManager::IsTraining() const
{
	if (!ImitationTrainer)
	{
		return false;
	}

	return ImitationTrainer->IsTraining();
}

void ATankLearningAgentsManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Recording loop - capture human demonstrations when recording is active
	if (Recorder && Recorder->IsRecording() && Interactor && TrainerAgentId != INDEX_NONE)
	{
		// 1. Gather observations from environment
		Interactor->GatherObservations();

		// 2. Encode human actions from HumanPlayerController's current input state
		Interactor->EncodeHumanActionsForAgent(TrainerAgentId);

		// 3. Add the observation+action pair to the recording buffer
		Recorder->AddExperience();

		// 4. Increment recorded experiences counter
		RecordedExperiencesCount++;

		// 5. Log progress every 60 frames (1 second at 60 fps)
		if (RecordedExperiencesCount % 60 == 0)
		{
			UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Recording in progress... %d frames recorded"), RecordedExperiencesCount);
		}

		// 6. Waypoint-based path following: Check waypoint completion
		if (bUseWaypointPathFollowing && bUseTargetBasedRecording && bHasActiveTarget)
		{
			if (IsCurrentWaypointReached())
			{
				AdvanceToNextWaypoint();
			}
		}

		// 7. Target-based recording: Check target completion
		if (bUseTargetBasedRecording && bHasActiveTarget)
		{
			// If using waypoints, check if all waypoints are completed first
			const bool bWaypointsCompleted = !bUseWaypointPathFollowing || AreAllWaypointsCompleted();

			// CRITICAL LOG: Check target reach status every 2 seconds (120 frames at 60fps)
			static int32 TargetCheckLogCounter = 0;
			if (bWaypointsCompleted && ++TargetCheckLogCounter % 120 == 0)
			{
				if (TrainerTank)
				{
					const float CurrentDistance = FVector::Dist2D(
						TrainerTank->GetActorLocation(),
						CurrentTargetLocation
					) / 100.0f;

					UE_LOG(LogTemp, Log, TEXT("TARGET CHECK: Distance=%.2fm | ReachRadius=%.2fm | WaypointsCompleted=%s"),
						CurrentDistance,
						TargetReachRadius / 100.0f,
						bWaypointsCompleted ? TEXT("YES") : TEXT("NO"));
				}
			}

			if (bWaypointsCompleted && IsTargetReached())
			{
				// CRITICAL LOG: Target reached
				UE_LOG(LogTemp, Warning, TEXT("========================================"));
				UE_LOG(LogTemp, Warning, TEXT(">>> TARGET REACHED! <<<"));
				UE_LOG(LogTemp, Warning, TEXT("  Calling OnTargetReached()..."));
				UE_LOG(LogTemp, Warning, TEXT("========================================"));

				OnTargetReached();
			}
		}
	}

	// Training loop - iterate training if active
	if (ImitationTrainer && ImitationTrainer->IsTraining())
	{
		// Perform one training iteration
		ImitationTrainer->IterateTraining();

		// Update metrics
		CurrentIteration++;

		// Get loss from trainer (if available via stats)
		// Note: UE Learning Agents doesn't expose loss directly in UE 5.6
		// We can only track iterations for now
		CurrentLoss = 0.0f; // TODO: Get actual loss when API supports it

		// Log progress at intervals
		if (CurrentIteration % LogInterval == 0)
		{
			float Progress = GetTrainingProgress();
			UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Training Progress: %d/%d iterations (%.1f%%)"),
				CurrentIteration, TotalIterations, Progress * 100.0f);
		}

		// Save checkpoint at intervals
		if (CurrentIteration % CheckpointInterval == 0)
		{
			FString CheckpointName = FString::Printf(TEXT("TankPolicy_Iteration_%d"), CurrentIteration);
			SavePolicyCheckpoint(CheckpointName);
		}
	}

	// Inference loop - run AI policy if agent is registered and not training
	if (Policy && Interactor && AgentAgentId != INDEX_NONE && !IsTraining())
	{
		// UE 5.6 API: RunInference() does complete pipeline:
		// 1. GatherObservations (via Interactor)
		// 2. EncodeObservations (Policy)
		// 3. EvaluatePolicy (Policy)
		// 4. DecodeAndSampleActions (Policy)
		// 5. PerformActions (via Interactor)
		Policy->RunInference(0.0f);  // 0.0f = no noise, deterministic actions

		// CRITICAL: Track AI tank's waypoint/target progress during inference
		// Without this, AI has no new targets to navigate to after reaching current one
		if (bUseTargetBasedRecording && bHasActiveTarget && AgentTank)
		{
			// Check waypoint progress (using AgentTank instead of TrainerTank)
			if (bUseWaypointPathFollowing && CurrentWaypoints.Num() > 0 && CurrentWaypointIndex < CurrentWaypoints.Num())
			{
				const FVector AILocation = AgentTank->GetActorLocation();
				const FVector WaypointLocation = CurrentWaypoints[CurrentWaypointIndex];
				const float DistanceToWaypoint = FVector::Dist2D(AILocation, WaypointLocation);

				if (DistanceToWaypoint <= WaypointReachRadius)
				{
					UE_LOG(LogTemp, Warning, TEXT("AI INFERENCE: Waypoint #%d reached!"), CurrentWaypointIndex);
					CurrentWaypointIndex++;

					if (CurrentWaypointIndex >= CurrentWaypoints.Num())
					{
						UE_LOG(LogTemp, Warning, TEXT("AI INFERENCE: All waypoints completed, heading to target"));
					}
				}
			}

			// Check if AI reached target (after all waypoints completed)
			const bool bWaypointsCompleted = !bUseWaypointPathFollowing || CurrentWaypointIndex >= CurrentWaypoints.Num();
			if (bWaypointsCompleted)
			{
				const FVector AILocation = AgentTank->GetActorLocation();
				const float DistanceToTarget = FVector::Dist2D(AILocation, CurrentTargetLocation);

				if (DistanceToTarget <= TargetReachRadius)
				{
					UE_LOG(LogTemp, Warning, TEXT("AI INFERENCE: Target reached! Generating new target..."));

					// Generate new target using AI tank as origin
					AWR_Tank_Pawn* OriginalTrainer = TrainerTank;
					TrainerTank = AgentTank;

					GenerateNewTarget();

					TrainerTank = OriginalTrainer;

					if (bHasActiveTarget)
					{
						UE_LOG(LogTemp, Warning, TEXT("AI INFERENCE: New target generated at %s"), *CurrentTargetLocation.ToString());
					}
				}
			}
		}
	}

	// Waypoint visualization - draw waypoint spheres and path lines
	if (bUseWaypointPathFollowing && bUseTargetBasedRecording && bHasActiveTarget && bShowWaypointVisualization && GetWorld())
	{
		for (int32 i = 0; i < CurrentWaypoints.Num(); ++i)
		{
			const FVector WaypointLocation = CurrentWaypoints[i];
			const bool bIsCurrentWaypoint = (i == CurrentWaypointIndex);
			const bool bIsCompleted = (i < CurrentWaypointIndex);

			// Determine color: Gray (completed), Yellow (current), Blue (pending)
			FColor WaypointColor;
			if (bIsCompleted)
			{
				WaypointColor = FColor(128, 128, 128);  // Gray
			}
			else if (bIsCurrentWaypoint)
			{
				WaypointColor = FColor::Yellow;  // Current waypoint
			}
			else
			{
				WaypointColor = FColor(0, 100, 255);  // Blue (pending)
			}

			// Draw waypoint sphere (smaller than target)
			DrawDebugSphere(
				GetWorld(),
				WaypointLocation,
				WaypointReachRadius,  // Use waypoint reach radius
				12,  // Segments
				WaypointColor,
				false,  // Persistent = false
				-1.0f,  // Lifetime
				0,  // Depth priority
				3.0f  // Thickness
			);

			// Draw line to next waypoint (or to target if last waypoint)
			if (i < CurrentWaypoints.Num() - 1)
			{
				// Line to next waypoint
				DrawDebugLine(
					GetWorld(),
					WaypointLocation,
					CurrentWaypoints[i + 1],
					FColor::Green,
					false,  // Persistent = false
					-1.0f,  // Lifetime
					0,  // Depth priority
					1.5f  // Thickness
				);
			}
			else
			{
				// Line from last waypoint to final target
				DrawDebugLine(
					GetWorld(),
					WaypointLocation,
					CurrentTargetLocation,
					FColor::Green,
					false,  // Persistent = false
					-1.0f,  // Lifetime
					0,  // Depth priority
					1.5f  // Thickness
				);
			}
		}

		// Draw line from trainer to first waypoint (or to target if no waypoints)
		if (TrainerTank)
		{
			const FVector TrainerLocation = TrainerTank->GetActorLocation();
			const FVector FirstPoint = (CurrentWaypoints.Num() > 0) ?
				GetCurrentWaypointLocation() : CurrentTargetLocation;

			DrawDebugLine(
				GetWorld(),
				TrainerLocation,
				FirstPoint,
				FColor::Cyan,
				false,  // Persistent = false
				-1.0f,  // Lifetime
				0,  // Depth priority
				2.0f  // Thickness
			);

			// Draw distance text to current waypoint (or target)
			const float Distance = FVector::Dist2D(TrainerLocation, FirstPoint);
			const FString DistanceText = FString::Printf(TEXT("%.1fm"), Distance / 100.0f);
			const FVector TextLocation = FirstPoint + FVector(0.0f, 0.0f, 80.0f);
			DrawDebugString(
				GetWorld(),
				TextLocation,
				DistanceText,
				nullptr,
				FColor::White,
				0.0f,  // Duration (0 = one frame)
				true  // Draw shadow
			);

			// Draw waypoint progress text
			if (CurrentWaypoints.Num() > 0)
			{
				const FString ProgressText = FString::Printf(TEXT("WP %d/%d"), CurrentWaypointIndex + 1, CurrentWaypoints.Num());
				const FVector ProgressTextLocation = FirstPoint + FVector(0.0f, 0.0f, 120.0f);
				DrawDebugString(
					GetWorld(),
					ProgressTextLocation,
					ProgressText,
					nullptr,
					FColor::Yellow,
					0.0f,
					true
				);
			}
		}
	}

	// Target visualization - draw debug sphere
	if (bUseTargetBasedRecording && bHasActiveTarget && bShowTargetVisualization && GetWorld())
	{
		// Draw target sphere (green = active, yellow = close to target)
		const FColor SphereColor = IsTargetReached() ? FColor::Yellow : FColor::Green;
		DrawDebugSphere(
			GetWorld(),
			CurrentTargetLocation,
			TargetReachRadius,  // Use reach radius as sphere size
			16,  // Segments
			SphereColor,
			false,  // Persistent lines = false (draw every frame)
			-1.0f,  // Lifetime
			0,  // Depth priority
			5.0f  // Thickness
		);

		// Draw distance line from trainer to target
		if (TrainerTank)
		{
			const FVector TrainerLocation = TrainerTank->GetActorLocation();
			DrawDebugLine(
				GetWorld(),
				TrainerLocation,
				CurrentTargetLocation,
				FColor::Cyan,
				false,  // Persistent = false
				-1.0f,  // Lifetime
				0,  // Depth priority
				2.0f  // Thickness
			);

			// Draw distance text
			const float Distance = FVector::Dist2D(TrainerLocation, CurrentTargetLocation);
			const FString DistanceText = FString::Printf(TEXT("%.1fm"), Distance / 100.0f);
			const FVector TextLocation = CurrentTargetLocation + FVector(0.0f, 0.0f, 100.0f);
			DrawDebugString(
				GetWorld(),
				TextLocation,
				DistanceText,
				nullptr,
				FColor::White,
				0.0f,  // Duration (0 = one frame)
				true  // Draw shadow
			);
		}
	}
}

void ATankLearningAgentsManager::RegisterTrainerTank(AWR_Tank_Pawn* Tank)
{
	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager::RegisterTrainerTank: Called"));

	if (!Tank)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager::RegisterTrainerTank: Tank is null!"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("  -> Tank: %s (Class: %s)"), *Tank->GetName(), *Tank->GetClass()->GetName());

	// Store reference
	TrainerTank = Tank;

	// Register as agent
	UE_LOG(LogTemp, Log, TEXT("  -> Calling AddTankAgent for Trainer tank..."));
	TrainerAgentId = AddTankAgent(Tank);

	if (TrainerAgentId != INDEX_NONE)
	{
		UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Trainer tank registered successfully (AgentId: %d, Tank: %s)"),
			TrainerAgentId, *Tank->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Failed to register trainer tank as agent!"));
	}
}

void ATankLearningAgentsManager::RegisterAgentTank(AWR_Tank_Pawn* Tank)
{
	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager::RegisterAgentTank: Called"));

	if (!Tank)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager::RegisterAgentTank: Tank is null!"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("  -> Tank: %s (Class: %s)"), *Tank->GetName(), *Tank->GetClass()->GetName());

	// Store reference
	AgentTank = Tank;

	// Register as agent
	UE_LOG(LogTemp, Log, TEXT("  -> Calling AddTankAgent for Agent tank..."));
	AgentAgentId = AddTankAgent(Tank);

	if (AgentAgentId != INDEX_NONE)
	{
		UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Agent tank registered successfully (AgentId: %d, Tank: %s)"),
			AgentAgentId, *Tank->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Failed to register agent tank as agent!"));
	}
}

void ATankLearningAgentsManager::SetAgentTank(AWR_Tank_Pawn* Tank)
{
	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager::SetAgentTank: Setting agent tank reference"));

	if (!Tank)
	{
		UE_LOG(LogTemp, Error, TEXT("SetAgentTank: Tank is null!"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("  -> Tank: %s (Class: %s)"), *Tank->GetName(), *Tank->GetClass()->GetName());

	// Store reference WITHOUT registering as agent
	// This allows EnableInferenceMode() to register it later
	AgentTank = Tank;

	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Agent tank reference stored (NOT registered yet)"));
	UE_LOG(LogTemp, Log, TEXT("  → Tank will be registered when EnableInferenceMode() is called"));
}

void ATankLearningAgentsManager::EnableInferenceMode()
{
	UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager::EnableInferenceMode - Registering AI tank for inference"));

	// Check if AI tank is available
	if (!AgentTank)
	{
		UE_LOG(LogTemp, Error, TEXT("EnableInferenceMode: Agent tank not set! Cannot enable inference."));
		UE_LOG(LogTemp, Error, TEXT("  → Make sure GameMode has spawned and cached the AI tank reference."));
		UE_LOG(LogTemp, Error, TEXT("  → GameMode should call Manager->SetAgentTank(AgentTank) after spawning."));
		return;
	}

	// Check if already registered
	if (AgentAgentId != INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("EnableInferenceMode: Agent tank already registered (AgentId: %d)"), AgentAgentId);
		return;
	}

	// CRITICAL: Unregister Trainer Tank before inference
	// RunInference() processes ALL registered agents, but Trainer Tank has HumanPlayerController
	// which cannot receive AI actions. This causes "Failed to get AIController" spam.
	if (TrainerAgentId != INDEX_NONE && Manager)
	{
		UE_LOG(LogTemp, Warning, TEXT("EnableInferenceMode: Unregistering Trainer Tank (AgentId: %d) to prevent inference conflicts..."), TrainerAgentId);
		Manager->RemoveAgent(TrainerAgentId);
		TrainerAgentId = INDEX_NONE;
	}

	// Register AI tank for inference
	RegisterAgentTank(AgentTank);

	UE_LOG(LogTemp, Warning, TEXT("EnableInferenceMode: AI tank registered successfully!"));
	UE_LOG(LogTemp, Warning, TEXT("  → AI will now receive policy actions from trained neural network"));
	UE_LOG(LogTemp, Warning, TEXT("  → Make sure training is complete and policy is loaded"));

	// CRITICAL: Generate target and waypoints for AI inference
	// Without this, the AI receives zero vectors for waypoint observations
	if (bUseTargetBasedRecording)
	{
		UE_LOG(LogTemp, Warning, TEXT("EnableInferenceMode: Generating initial target and waypoints for AI..."));

		// Temporarily set TrainerTank to AgentTank for target generation
		// (GenerateNewTarget uses TrainerTank as origin)
		AWR_Tank_Pawn* OriginalTrainer = TrainerTank;
		TrainerTank = AgentTank;

		GenerateNewTarget();

		// Restore original trainer reference
		TrainerTank = OriginalTrainer;

		if (bHasActiveTarget)
		{
			UE_LOG(LogTemp, Warning, TEXT("EnableInferenceMode: Target and waypoints generated for AI!"));
			UE_LOG(LogTemp, Warning, TEXT("  → Target: %s"), *CurrentTargetLocation.ToString());
			UE_LOG(LogTemp, Warning, TEXT("  → Waypoints: %d"), CurrentWaypoints.Num());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("EnableInferenceMode: Failed to generate target for AI!"));
		}
	}
}

// ========== TRAINING METRICS IMPLEMENTATION ==========

float ATankLearningAgentsManager::GetTrainingProgress() const
{
	if (TotalIterations <= 0)
	{
		return 0.0f;
	}

	return FMath::Clamp(static_cast<float>(CurrentIteration) / static_cast<float>(TotalIterations), 0.0f, 1.0f);
}

int32 ATankLearningAgentsManager::GetRecordedExperienceCount() const
{
	// Return the manually tracked counter
	// This tracks AddExperience() calls during active recording
	return RecordedExperiencesCount;
}

void ATankLearningAgentsManager::SavePolicy()
{
	if (!Policy)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Cannot save policy - Policy not initialized!"));
		return;
	}

	// Base path for all policy networks
	FString BasePath = FPaths::ProjectSavedDir() / TEXT("LearningAgents") / TEXT("Policies") / TEXT("TankPolicy");

	// Create directory if it doesn't exist
	FString DirectoryPath = FPaths::GetPath(BasePath);
	if (!FPaths::DirectoryExists(DirectoryPath))
	{
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*DirectoryPath);
		UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Created policy directory: %s"), *DirectoryPath);
	}

	UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Saving trained policy..."));

	// Save all three neural network components
	int32 SavedCount = 0;

	// Save Encoder Network
	ULearningAgentsNeuralNetwork* EncoderNetwork = Policy->GetEncoderNetworkAsset();
	if (EncoderNetwork)
	{
		FFilePath EncoderPath;
		EncoderPath.FilePath = BasePath + TEXT("_encoder.bin");
		EncoderNetwork->SaveNetworkToSnapshot(EncoderPath);
		UE_LOG(LogTemp, Log, TEXT("  -> Encoder saved: %s"), *EncoderPath.FilePath);
		SavedCount++;
	}

	// Save Policy Network (main network)
	ULearningAgentsNeuralNetwork* PolicyNetwork = Policy->GetPolicyNetworkAsset();
	if (PolicyNetwork)
	{
		FFilePath PolicyPath;
		PolicyPath.FilePath = BasePath + TEXT("_policy.bin");
		PolicyNetwork->SaveNetworkToSnapshot(PolicyPath);
		UE_LOG(LogTemp, Log, TEXT("  -> Policy saved: %s"), *PolicyPath.FilePath);
		SavedCount++;
	}

	// Save Decoder Network
	ULearningAgentsNeuralNetwork* DecoderNetwork = Policy->GetDecoderNetworkAsset();
	if (DecoderNetwork)
	{
		FFilePath DecoderPath;
		DecoderPath.FilePath = BasePath + TEXT("_decoder.bin");
		DecoderNetwork->SaveNetworkToSnapshot(DecoderPath);
		UE_LOG(LogTemp, Log, TEXT("  -> Decoder saved: %s"), *DecoderPath.FilePath);
		SavedCount++;
	}

	if (SavedCount == 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Policy saved successfully! (%d/3 networks)"), SavedCount);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Policy save incomplete! Only %d/3 networks saved."), SavedCount);
	}
}

void ATankLearningAgentsManager::LoadPolicy()
{
	if (!Policy)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Cannot load policy - Policy not initialized!"));
		return;
	}

	// Base path for all policy networks
	FString BasePath = FPaths::ProjectSavedDir() / TEXT("LearningAgents") / TEXT("Policies") / TEXT("TankPolicy");

	// Check if policy files exist
	FString PolicyFilePath = BasePath + TEXT("_policy.bin");
	if (!FPaths::FileExists(PolicyFilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Policy file not found: %s"), *PolicyFilePath);
		UE_LOG(LogTemp, Warning, TEXT("  -> Train and save a policy first before loading"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Loading trained policy..."));

	int32 LoadedCount = 0;

	// Load Encoder Network
	ULearningAgentsNeuralNetwork* EncoderNetwork = Policy->GetEncoderNetworkAsset();
	FString EncoderFilePath = BasePath + TEXT("_encoder.bin");
	if (EncoderNetwork && FPaths::FileExists(EncoderFilePath))
	{
		FFilePath EncoderPath;
		EncoderPath.FilePath = EncoderFilePath;
		EncoderNetwork->LoadNetworkFromSnapshot(EncoderPath);
		UE_LOG(LogTemp, Log, TEXT("  -> Encoder loaded: %s"), *EncoderPath.FilePath);
		LoadedCount++;
	}
	else if (!FPaths::FileExists(EncoderFilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("  -> Encoder file not found: %s"), *EncoderFilePath);
	}

	// Load Policy Network (main network)
	ULearningAgentsNeuralNetwork* PolicyNetwork = Policy->GetPolicyNetworkAsset();
	if (PolicyNetwork && FPaths::FileExists(PolicyFilePath))
	{
		FFilePath PolicyPath;
		PolicyPath.FilePath = PolicyFilePath;
		PolicyNetwork->LoadNetworkFromSnapshot(PolicyPath);
		UE_LOG(LogTemp, Log, TEXT("  -> Policy loaded: %s"), *PolicyPath.FilePath);
		LoadedCount++;
	}

	// Load Decoder Network
	ULearningAgentsNeuralNetwork* DecoderNetwork = Policy->GetDecoderNetworkAsset();
	FString DecoderFilePath = BasePath + TEXT("_decoder.bin");
	if (DecoderNetwork && FPaths::FileExists(DecoderFilePath))
	{
		FFilePath DecoderPath;
		DecoderPath.FilePath = DecoderFilePath;
		DecoderNetwork->LoadNetworkFromSnapshot(DecoderPath);
		UE_LOG(LogTemp, Log, TEXT("  -> Decoder loaded: %s"), *DecoderPath.FilePath);
		LoadedCount++;
	}
	else if (!FPaths::FileExists(DecoderFilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("  -> Decoder file not found: %s"), *DecoderFilePath);
	}

	if (LoadedCount == 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Policy loaded successfully! (%d/3 networks)"), LoadedCount);
	}
	else if (LoadedCount > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager: Policy partially loaded (%d/3 networks)"), LoadedCount);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Failed to load any policy networks!"));
	}
}

void ATankLearningAgentsManager::SavePolicyCheckpoint(const FString& CheckpointName)
{
	if (!Policy)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager: Cannot save checkpoint - Policy not initialized!"));
		return;
	}

	// Base path for checkpoint
	FString BasePath = FPaths::ProjectSavedDir() / TEXT("LearningAgents") / TEXT("Checkpoints") / CheckpointName;

	// Create directory if it doesn't exist
	FString DirectoryPath = FPaths::GetPath(BasePath);
	if (!FPaths::DirectoryExists(DirectoryPath))
	{
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*DirectoryPath);
	}

	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Saving checkpoint: %s"), *CheckpointName);

	int32 SavedCount = 0;

	// Save Encoder Network
	ULearningAgentsNeuralNetwork* EncoderNetwork = Policy->GetEncoderNetworkAsset();
	if (EncoderNetwork)
	{
		FFilePath EncoderPath;
		EncoderPath.FilePath = BasePath + TEXT("_encoder.bin");
		EncoderNetwork->SaveNetworkToSnapshot(EncoderPath);
		SavedCount++;
	}

	// Save Policy Network
	ULearningAgentsNeuralNetwork* PolicyNetwork = Policy->GetPolicyNetworkAsset();
	if (PolicyNetwork)
	{
		FFilePath PolicyPath;
		PolicyPath.FilePath = BasePath + TEXT("_policy.bin");
		PolicyNetwork->SaveNetworkToSnapshot(PolicyPath);
		SavedCount++;
	}

	// Save Decoder Network
	ULearningAgentsNeuralNetwork* DecoderNetwork = Policy->GetDecoderNetworkAsset();
	if (DecoderNetwork)
	{
		FFilePath DecoderPath;
		DecoderPath.FilePath = BasePath + TEXT("_decoder.bin");
		DecoderNetwork->SaveNetworkToSnapshot(DecoderPath);
		SavedCount++;
	}

	UE_LOG(LogTemp, Log, TEXT("  -> Checkpoint saved (%d/3 networks) to: %s"), SavedCount, *DirectoryPath);
}

// ========== TARGET-BASED RECORDING SYSTEM IMPLEMENTATION ==========

void ATankLearningAgentsManager::GenerateNewTarget()
{
	if (!bUseTargetBasedRecording)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager::GenerateNewTarget: Target-based recording is disabled!"));
		return;
	}

	if (!GetWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager::GenerateNewTarget: World is null!"));
		return;
	}

	// Get NavigationSystem
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager::GenerateNewTarget: NavigationSystem not found!"));
		UE_LOG(LogTemp, Error, TEXT("  -> Make sure NavMesh is present in the level (Add Navigation Mesh Bounds Volume)"));
		return;
	}

	// Get trainer tank location as origin
	FVector OriginLocation = FVector::ZeroVector;
	if (TrainerTank)
	{
		OriginLocation = TrainerTank->GetActorLocation();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager::GenerateNewTarget: Trainer tank is null, using world origin"));
	}

	// Try multiple times to find valid NavMesh point
	const int32 MaxRetries = 10;
	bool bFoundLocation = false;
	FNavLocation NavLocation;
	FVector DesiredTargetLocation = FVector::ZeroVector;

	for (int32 Retry = 0; Retry < MaxRetries && !bFoundLocation; ++Retry)
	{
		// Generate random target within distance range
		const float RandomDistance = FMath::RandRange(MinTargetDistance, MaxTargetDistance);
		const float RandomAngle = FMath::RandRange(0.0f, 2.0f * PI);

		// Calculate desired target location (random direction at random distance)
		const FVector DesiredTargetOffset = FVector(
			FMath::Cos(RandomAngle) * RandomDistance,
			FMath::Sin(RandomAngle) * RandomDistance,
			0.0f
		);
		DesiredTargetLocation = OriginLocation + DesiredTargetOffset;

		// Project onto NavMesh to find valid reachable point
		// Use larger search extents for better chance of finding valid point
		bFoundLocation = NavSys->ProjectPointToNavigation(
			DesiredTargetLocation,
			NavLocation,
			FVector(1000.0f, 1000.0f, 500.0f)  // Search extents (10m horizontal, 5m vertical)
		);

		if (!bFoundLocation && Retry < MaxRetries - 1)
		{
			UE_LOG(LogTemp, Log, TEXT("GenerateNewTarget: Retry %d - NavMesh projection failed, trying new location..."), Retry + 1);
		}
	}

	if (bFoundLocation)
	{
		CurrentTargetLocation = NavLocation.Location;
		bHasActiveTarget = true;

		// Start new segment
		CurrentSegment = FTargetSegment();
		CurrentSegment.TargetLocation = CurrentTargetLocation;
		CurrentSegment.StartExperienceIndex = RecordedExperiencesCount;
		CurrentSegment.StartTime = GetWorld()->GetTimeSeconds();
		CurrentSegment.bCompleted = false;

		// CRITICAL LOG: Target generation
		const float DistanceFromTrainer = TrainerTank ?
			FVector::Dist(OriginLocation, CurrentTargetLocation) / 100.0f : 0.0f;
		UE_LOG(LogTemp, Warning, TEXT("========================================"));
		UE_LOG(LogTemp, Warning, TEXT("GENERATE NEW TARGET #%d"), TargetSegments.Num() + 1);
		UE_LOG(LogTemp, Warning, TEXT("  Target Location: %s"), *CurrentTargetLocation.ToString());
		UE_LOG(LogTemp, Warning, TEXT("  Distance from Trainer: %.2fm"), DistanceFromTrainer);
		UE_LOG(LogTemp, Warning, TEXT("  Reach Radius: %.2fm"), TargetReachRadius / 100.0f);
		UE_LOG(LogTemp, Warning, TEXT("  Experience Index: %d"), RecordedExperiencesCount);
		UE_LOG(LogTemp, Warning, TEXT("========================================"));

		// Generate waypoints to target (if waypoint system enabled)
		if (bUseWaypointPathFollowing)
		{
			GenerateWaypointsToTarget();
		}

		// Create or update visualization
		if (bShowTargetVisualization)
		{
			if (TargetVisualizationActor)
			{
				UpdateTargetVisualization();
			}
			else
			{
				CreateTargetVisualization();
			}
		}
	}
	else
	{
		// FALLBACK: Use desired location directly without NavMesh projection
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager::GenerateNewTarget: NavMesh projection failed after %d retries"), MaxRetries);
		UE_LOG(LogTemp, Warning, TEXT("  -> Using FALLBACK: direct target location without NavMesh validation"));

		CurrentTargetLocation = DesiredTargetLocation;
		bHasActiveTarget = true;

		// Start new segment
		CurrentSegment = FTargetSegment();
		CurrentSegment.TargetLocation = CurrentTargetLocation;
		CurrentSegment.StartExperienceIndex = RecordedExperiencesCount;
		CurrentSegment.StartTime = GetWorld()->GetTimeSeconds();
		CurrentSegment.bCompleted = false;

		// CRITICAL LOG: Target generation (fallback)
		const float DistanceFromTrainer = TrainerTank ?
			FVector::Dist(OriginLocation, CurrentTargetLocation) / 100.0f : 0.0f;
		UE_LOG(LogTemp, Warning, TEXT("========================================"));
		UE_LOG(LogTemp, Warning, TEXT("GENERATE NEW TARGET #%d (FALLBACK - no NavMesh)"), TargetSegments.Num() + 1);
		UE_LOG(LogTemp, Warning, TEXT("  Target Location: %s"), *CurrentTargetLocation.ToString());
		UE_LOG(LogTemp, Warning, TEXT("  Distance from Trainer: %.2fm"), DistanceFromTrainer);
		UE_LOG(LogTemp, Warning, TEXT("  Reach Radius: %.2fm"), TargetReachRadius / 100.0f);
		UE_LOG(LogTemp, Warning, TEXT("  Experience Index: %d"), RecordedExperiencesCount);
		UE_LOG(LogTemp, Warning, TEXT("========================================"));

		// Generate waypoints to target (if waypoint system enabled)
		if (bUseWaypointPathFollowing)
		{
			GenerateWaypointsToTarget();
		}

		// Create or update visualization
		if (bShowTargetVisualization)
		{
			if (TargetVisualizationActor)
			{
				UpdateTargetVisualization();
			}
			else
			{
				CreateTargetVisualization();
			}
		}
	}
}

bool ATankLearningAgentsManager::IsTargetReached() const
{
	if (!bHasActiveTarget || !TrainerTank)
	{
		return false;
	}

	const FVector TankLocation = TrainerTank->GetActorLocation();
	const float DistanceToTarget = FVector::Dist2D(TankLocation, CurrentTargetLocation);

	return DistanceToTarget <= TargetReachRadius;
}

void ATankLearningAgentsManager::OnTargetReached()
{
	if (!bHasActiveTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager::OnTargetReached: No active target!"));
		return;
	}

	// Complete current segment
	CurrentSegment.EndExperienceIndex = RecordedExperiencesCount - 1;
	CurrentSegment.EndTime = GetWorld()->GetTimeSeconds();
	CurrentSegment.bCompleted = true;

	// Calculate statistics
	const int32 SegmentExperiences = CurrentSegment.EndExperienceIndex - CurrentSegment.StartExperienceIndex + 1;
	const float SegmentDuration = CurrentSegment.EndTime - CurrentSegment.StartTime;

	// Store waypoint statistics (if waypoint system was used)
	if (bUseWaypointPathFollowing)
	{
		CurrentSegment.Waypoints = CurrentWaypoints;
		CurrentSegment.CompletedWaypointsCount = CurrentWaypointIndex;
		CurrentSegment.TotalWaypointsCount = CurrentWaypoints.Num();
	}

	// CRITICAL LOG: Target completion
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("TARGET #%d COMPLETED!"), TargetSegments.Num() + 1);
	UE_LOG(LogTemp, Warning, TEXT("  Duration: %.2f seconds"), SegmentDuration);
	UE_LOG(LogTemp, Warning, TEXT("  Experiences: %d frames"), SegmentExperiences);
	UE_LOG(LogTemp, Warning, TEXT("  Experience Range: [%d - %d]"),
		CurrentSegment.StartExperienceIndex,
		CurrentSegment.EndExperienceIndex);

	if (bUseWaypointPathFollowing)
	{
		UE_LOG(LogTemp, Warning, TEXT("  Waypoints Completed: %d/%d"),
			CurrentWaypointIndex, CurrentWaypoints.Num());
	}

	UE_LOG(LogTemp, Warning, TEXT("  Total Completed Targets: %d"), TargetSegments.Num() + 1);
	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	// Add to completed segments list
	TargetSegments.Add(CurrentSegment);

	// Generate new target (will create new CurrentSegment)
	UE_LOG(LogTemp, Warning, TEXT("Generating next target..."));
	GenerateNewTarget();
}

void ATankLearningAgentsManager::CreateTargetVisualization()
{
	if (!GetWorld() || !bHasActiveTarget)
	{
		return;
	}

	// Destroy old visualization if exists
	DestroyTargetVisualization();

	// Spawn debug sphere at target location
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Name = FName(TEXT("TargetVisualization"));

	// Simple approach: Use DrawDebugSphere in Tick instead of spawning actor
	// This is more lightweight and doesn't require mesh assets
	// We'll draw it in Tick() method

	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Target visualization enabled (debug sphere)"));
}

void ATankLearningAgentsManager::UpdateTargetVisualization()
{
	// Visualization is handled via DrawDebugSphere in Tick()
	// No need to update actor position
}

void ATankLearningAgentsManager::DestroyTargetVisualization()
{
	if (TargetVisualizationActor)
	{
		TargetVisualizationActor->Destroy();
		TargetVisualizationActor = nullptr;
	}
}

int32 ATankLearningAgentsManager::GetCompletedTargetsCount() const
{
	int32 Count = 0;
	for (const FTargetSegment& Segment : TargetSegments)
	{
		if (Segment.bCompleted)
		{
			Count++;
		}
	}
	return Count;
}

// ========== WAYPOINT SYSTEM IMPLEMENTATION ==========

void ATankLearningAgentsManager::GenerateWaypointsToTarget()
{
	if (!bUseWaypointPathFollowing || !bUseTargetBasedRecording)
	{
		UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager::GenerateWaypointsToTarget: Waypoint system disabled"));
		return;
	}

	if (!GetWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager::GenerateWaypointsToTarget: World is null!"));
		return;
	}

	// Get NavigationSystem
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager::GenerateWaypointsToTarget: NavigationSystem not found!"));
		return;
	}

	// Get trainer tank location as start point
	if (!TrainerTank)
	{
		UE_LOG(LogTemp, Error, TEXT("TankLearningAgentsManager::GenerateWaypointsToTarget: Trainer tank is null!"));
		return;
	}

	FVector StartLocation = TrainerTank->GetActorLocation();
	FVector EndLocation = CurrentTargetLocation;

	// Project start and end points to NavMesh to ensure valid navigation locations
	FNavLocation StartProj, EndProj;
	const FVector ProjectExtent(500.0f, 500.0f, 500.0f);  // 5m search radius

	const bool bStartProjected = NavSys->ProjectPointToNavigation(StartLocation, StartProj, ProjectExtent);
	const bool bEndProjected = NavSys->ProjectPointToNavigation(EndLocation, EndProj, ProjectExtent);

	if (bStartProjected) StartLocation = StartProj.Location;
	if (bEndProjected) EndLocation = EndProj.Location;

	// DEBUG LOG: NavMesh projection
	UE_LOG(LogTemp, Warning, TEXT("========== WAYPOINT PATHFINDING (v2) =========="));
	UE_LOG(LogTemp, Warning, TEXT("  Start: %s (Projected: %s)"), *StartLocation.ToString(), bStartProjected ? TEXT("YES") : TEXT("NO"));
	UE_LOG(LogTemp, Warning, TEXT("  End: %s (Projected: %s)"), *EndLocation.ToString(), bEndProjected ? TEXT("YES") : TEXT("NO"));
	UE_LOG(LogTemp, Warning, TEXT("  Distance: %.2fm"), FVector::Dist(StartLocation, EndLocation) / 100.0f);

	// Find navigation path using UE 5.6 Recast API
	UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
		GetWorld(),
		StartLocation,
		EndLocation,
		nullptr,  // Querier (can be nullptr)
		nullptr   // Filter class (can be nullptr for default)
	);

	// Accept both complete AND partial paths - partial paths still provide useful waypoints
	if (Path && Path->IsValid() && Path->PathPoints.Num() > 0)
	{
		// Extract waypoints from path - USE FULL RANGE from NavMesh (NO optimization)
		CurrentWaypoints.Empty();

		// UNavigationPath::PathPoints is TArray<FVector> (not TArray<FNavPathPoint>)
		const TArray<FVector>& PathPoints = Path->PathPoints;

		// Log if path is partial (for debugging, but still use it)
		if (Path->IsPartial())
		{
			UE_LOG(LogTemp, Warning, TEXT("  NOTE: Path is PARTIAL - using available waypoints anyway"));
		}

		// CRITICAL: Include ALL points from NavMesh pathfinding result
		// DO NOT skip start or end points - use complete NavMesh path data
		for (int32 i = 0; i < PathPoints.Num(); ++i)
		{
			CurrentWaypoints.Add(PathPoints[i]);  // FVector already is location
		}

		// If path is partial, add the final target as last waypoint to ensure we reach it
		if (Path->IsPartial() && CurrentWaypoints.Num() > 0)
		{
			const float DistToTarget = FVector::Dist(CurrentWaypoints.Last(), EndLocation);
			if (DistToTarget > 50.0f)  // Only add if not already close to target
			{
				CurrentWaypoints.Add(EndLocation);
				UE_LOG(LogTemp, Warning, TEXT("  Added final target as last waypoint (partial path)"));
			}
		}

		// Reset waypoint tracking
		CurrentWaypointIndex = 0;

		// CRITICAL LOG: Waypoint generation
		UE_LOG(LogTemp, Warning, TEXT("GENERATE WAYPOINTS TO TARGET"));
		UE_LOG(LogTemp, Warning, TEXT("  Total Waypoints: %d (%s path)"), CurrentWaypoints.Num(),
			Path->IsPartial() ? TEXT("partial") : TEXT("complete"));
		UE_LOG(LogTemp, Warning, TEXT("  Start Location: %s"), *StartLocation.ToString());
		UE_LOG(LogTemp, Warning, TEXT("  Target Location: %s"), *EndLocation.ToString());

		if (CurrentWaypoints.Num() > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("  First Waypoint [0]: %s"), *CurrentWaypoints[0].ToString());
			UE_LOG(LogTemp, Warning, TEXT("  Last Waypoint [%d]: %s"),
				CurrentWaypoints.Num() - 1, *CurrentWaypoints.Last().ToString());

			// Check if last waypoint matches target
			const float LastWpToTargetDist = FVector::Dist(CurrentWaypoints.Last(), EndLocation);
			UE_LOG(LogTemp, Warning, TEXT("  Distance: Last WP to Target = %.2fm"), LastWpToTargetDist / 100.0f);
		}

		// Log all waypoint positions with distances
		for (int32 i = 0; i < CurrentWaypoints.Num(); ++i)
		{
			const float DistanceToNext = (i < CurrentWaypoints.Num() - 1) ?
				FVector::Dist(CurrentWaypoints[i], CurrentWaypoints[i + 1]) / 100.0f : 0.0f;
			UE_LOG(LogTemp, Log, TEXT("  WP[%d]: %s (%.2fm to next)"),
				i, *CurrentWaypoints[i].ToString(), DistanceToNext);
		}
		UE_LOG(LogTemp, Warning, TEXT("========== WAYPOINTS GENERATED SUCCESS =========="));
	}
	else
	{
		// DEBUG: Log why pathfinding failed
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsManager::GenerateWaypointsToTarget: Failed to find path!"));
		UE_LOG(LogTemp, Warning, TEXT("  Path: %s"), Path ? TEXT("Valid") : TEXT("NULL"));
		if (Path)
		{
			UE_LOG(LogTemp, Warning, TEXT("  Path->IsValid(): %s"), Path->IsValid() ? TEXT("YES") : TEXT("NO"));
			UE_LOG(LogTemp, Warning, TEXT("  Path->IsPartial(): %s"), Path->IsPartial() ? TEXT("YES") : TEXT("NO"));
			UE_LOG(LogTemp, Warning, TEXT("  Path Points: %d"), Path->PathPoints.Num());
		}

		// FALLBACK: Create direct waypoint to target when pathfinding fails
		UE_LOG(LogTemp, Warning, TEXT("  -> Creating DIRECT path to target (fallback)"));
		CurrentWaypoints.Empty();
		CurrentWaypoints.Add(StartLocation);  // Current position
		CurrentWaypoints.Add(EndLocation);    // Target position
		CurrentWaypointIndex = 0;

		UE_LOG(LogTemp, Warning, TEXT("  -> Fallback waypoints: Start -> Target (2 points)"));
		UE_LOG(LogTemp, Warning, TEXT("=========================================="));
	}
}

bool ATankLearningAgentsManager::IsCurrentWaypointReached() const
{
	if (!bUseWaypointPathFollowing || CurrentWaypoints.Num() == 0)
	{
		return false;
	}

	if (CurrentWaypointIndex >= CurrentWaypoints.Num())
	{
		// All waypoints completed
		return false;
	}

	if (!TrainerTank)
	{
		return false;
	}

	const FVector TankLocation = TrainerTank->GetActorLocation();
	const FVector WaypointLocation = CurrentWaypoints[CurrentWaypointIndex];
	const float Distance = FVector::Dist2D(TankLocation, WaypointLocation);

	return Distance <= WaypointReachRadius;
}

void ATankLearningAgentsManager::AdvanceToNextWaypoint()
{
	if (CurrentWaypointIndex < CurrentWaypoints.Num())
	{
		// CRITICAL LOG: Waypoint reached
		UE_LOG(LogTemp, Warning, TEXT(">>> WAYPOINT #%d REACHED! <<<"), CurrentWaypointIndex);
		UE_LOG(LogTemp, Warning, TEXT("  Waypoint Location: %s"), *CurrentWaypoints[CurrentWaypointIndex].ToString());

		CurrentWaypointIndex++;

		if (CurrentWaypointIndex < CurrentWaypoints.Num())
		{
			const FVector NextWaypoint = CurrentWaypoints[CurrentWaypointIndex];
			const float DistanceToNext = TrainerTank ?
				FVector::Dist(TrainerTank->GetActorLocation(), NextWaypoint) / 100.0f : 0.0f;
			UE_LOG(LogTemp, Warning, TEXT("  -> Advancing to waypoint #%d (%.2fm away)"),
				CurrentWaypointIndex, DistanceToNext);
			UE_LOG(LogTemp, Warning, TEXT("  -> Progress: %d/%d waypoints completed"),
				CurrentWaypointIndex, CurrentWaypoints.Num());
		}
		else
		{
			// CRITICAL: All waypoints completed
			UE_LOG(LogTemp, Warning, TEXT("========================================"));
			UE_LOG(LogTemp, Warning, TEXT("ALL WAYPOINTS COMPLETED!"));
			UE_LOG(LogTemp, Warning, TEXT("  Total Waypoints: %d"), CurrentWaypoints.Num());
			UE_LOG(LogTemp, Warning, TEXT("  Now heading to FINAL TARGET"));

			if (TrainerTank)
			{
				const float DistanceToTarget = FVector::Dist2D(
					TrainerTank->GetActorLocation(),
					CurrentTargetLocation
				) / 100.0f;
				UE_LOG(LogTemp, Warning, TEXT("  Distance to Target: %.2fm"), DistanceToTarget);
				UE_LOG(LogTemp, Warning, TEXT("  Reach Radius Required: %.2fm"), TargetReachRadius / 100.0f);
			}
			UE_LOG(LogTemp, Warning, TEXT("========================================"));
		}
	}
}

bool ATankLearningAgentsManager::AreAllWaypointsCompleted() const
{
	if (!bUseWaypointPathFollowing)
	{
		return true; // No waypoints = considered completed
	}

	return CurrentWaypointIndex >= CurrentWaypoints.Num();
}

FVector ATankLearningAgentsManager::GetCurrentWaypointLocation() const
{
	if (!bUseWaypointPathFollowing || CurrentWaypoints.Num() == 0)
	{
		// No waypoints - return target location
		return CurrentTargetLocation;
	}

	if (CurrentWaypointIndex >= CurrentWaypoints.Num())
	{
		// All waypoints completed - return final target
		return CurrentTargetLocation;
	}

	return CurrentWaypoints[CurrentWaypointIndex];
}
