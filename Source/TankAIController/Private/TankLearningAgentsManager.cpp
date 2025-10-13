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
#include "WR_Tank_Pawn.h"

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

	// Create Policy using factory method
	ULearningAgentsInteractor* InteractorRef = Interactor;
	Policy = ULearningAgentsPolicy::MakePolicy(
		ManagerRef,
		InteractorRef,
		ULearningAgentsPolicy::StaticClass(),
		TEXT("TankPolicy"));

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

	// Begin recording demonstrations from all agents
	Recorder->BeginRecording();
	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Started recording demonstrations from trainer tank."));
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

	// End recording
	Recorder->EndRecording();
	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Stopped recording demonstrations."));
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

	// Configure Imitation Trainer Settings
	FLearningAgentsImitationTrainerSettings ImitationTrainerSettings;
	ImitationTrainerSettings.TrainerCommunicationTimeout = 30.0f; // 30 seconds timeout

	// Configure Imitation Trainer Training Settings
	FLearningAgentsImitationTrainerTrainingSettings TrainingSettings;
	TrainingSettings.NumberOfIterations = 100000;        // 100k iterations
	TrainingSettings.LearningRate = 0.0005f;             // Learning rate 0.0005
	TrainingSettings.LearningRateDecay = 0.99f;          // Decay rate per 1000 iterations
	TrainingSettings.WeightDecay = 0.0001f;              // Weight decay
	TrainingSettings.BatchSize = 128;                    // Batch size
	TrainingSettings.Window = 64;                        // Sequence window
	TrainingSettings.ActionRegularizationWeight = 0.001f; // Action regularization
	TrainingSettings.ActionEntropyWeight = 0.0f;         // No entropy (deterministic)
	TrainingSettings.RandomSeed = 1234;                  // Random seed
	TrainingSettings.Device = ELearningAgentsTrainingDevice::CPU; // CPU training
	TrainingSettings.bUseTensorboard = false;            // No TensorBoard
	TrainingSettings.bSaveSnapshots = true;              // Save snapshots
	TrainingSettings.bUseMLflow = false;                 // No MLflow

	// Configure Process Path Settings (use defaults)
	FLearningAgentsTrainerProcessSettings PathSettings;

	// Begin training using recorded demonstrations with proper settings
	ImitationTrainer->BeginTraining(Recording, ImitationTrainerSettings, TrainingSettings, PathSettings);
	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Started training from recorded demonstrations."));
	UE_LOG(LogTemp, Log, TEXT("  -> Iterations: %d"), TrainingSettings.NumberOfIterations);
	UE_LOG(LogTemp, Log, TEXT("  -> Learning Rate: %.6f"), TrainingSettings.LearningRate);
	UE_LOG(LogTemp, Log, TEXT("  -> Batch Size: %d"), TrainingSettings.BatchSize);
	UE_LOG(LogTemp, Log, TEXT("  -> Device: %s"), TrainingSettings.Device == ELearningAgentsTrainingDevice::CPU ? TEXT("CPU") : TEXT("GPU"));
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
	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsManager: Stopped training."));
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
	}

	// Training loop - iterate training if active
	if (ImitationTrainer && ImitationTrainer->IsTraining())
	{
		ImitationTrainer->IterateTraining();
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
