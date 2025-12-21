// Copyright Epic Games, Inc. All Rights Reserved.

#include "TankTrainingGameMode.h"
#include "WR_Tank_Pawn.h"
#include "HumanPlayerController.h"
#include "AILearningAgentsController.h"
#include "TankLearningAgentsManager.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

ATankTrainingGameMode::ATankTrainingGameMode()
{
	// Set default player controller class to HumanPlayerController
	PlayerControllerClass = AHumanPlayerController::StaticClass();

	// Try to load BP_WR_Tank_Pawn blueprint from WeaponPlugin
	// This BP should be based on AWR_Tank_Pawn C++ class
	static ConstructorHelpers::FClassFinder<AWR_Tank_Pawn> TankBPClass(TEXT("/WeaponPlugin/Blueprints/BP_WR_Tank_Pawn.BP_WR_Tank_Pawn_C"));
	if (TankBPClass.Class != nullptr)
	{
		TrainerTankClass = TankBPClass.Class;
		AITankClass = TankBPClass.Class; // Both use same BP class
		UE_LOG(LogTemp, Warning, TEXT("TankTrainingGameMode: Tank classes set to BP_WR_Tank_Pawn"));
	}
	else
	{
		// ERROR: Cannot use C++ class directly - it needs BP configuration!
		UE_LOG(LogTemp, Error, TEXT("TankTrainingGameMode: BP_WR_Tank_Pawn not found at /WeaponPlugin/Blueprints/BP_WR_Tank_Pawn!"));
		UE_LOG(LogTemp, Error, TEXT("TankTrainingGameMode: Cannot use AWR_Tank_Pawn C++ class directly - it requires blueprint setup!"));
		TrainerTankClass = nullptr;
		AITankClass = nullptr;
	}

	// IMPORTANT: Set DefaultPawnClass to nullptr so GameMode doesn't auto-spawn a pawn
	// We will manually spawn trainer and AI tanks in BeginPlay
	DefaultPawnClass = nullptr;

	UE_LOG(LogTemp, Warning, TEXT("TankTrainingGameMode: Constructor - PlayerControllerClass set to HumanPlayerController"));
}

void ATankTrainingGameMode::BeginPlay()
{
	UE_LOG(LogTemp, Warning, TEXT("TankTrainingGameMode: BeginPlay called!"));
	Super::BeginPlay();

	// Validate tank classes
	if (!TrainerTankClass || !AITankClass)
	{
		UE_LOG(LogTemp, Error, TEXT("TankTrainingGameMode: Tank classes are not set!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("TankTrainingGameMode: TrainerTankClass: %s, AITankClass: %s"),
		*TrainerTankClass->GetName(), *AITankClass->GetName());

	// Check if world is valid
	if (!GetWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("TankTrainingGameMode: GetWorld() returned nullptr!"));
		return;
	}

	// 1. CLEANUP: Destroy all existing pawns and tanks from scene
	UE_LOG(LogTemp, Warning, TEXT("TankTrainingGameMode: Cleaning up existing pawns and tanks from scene"));

	TArray<AActor*> FoundPawns;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APawn::StaticClass(), FoundPawns);

	for (AActor* Pawn : FoundPawns)
	{
		if (Pawn)
		{
			UE_LOG(LogTemp, Warning, TEXT("  Destroying pawn: %s (Class: %s)"), *Pawn->GetName(), *Pawn->GetClass()->GetName());
			Pawn->Destroy();
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("TankTrainingGameMode: Destroyed %d pawn(s) from scene"), FoundPawns.Num());

	// 2. Spawn AI agent tank first
	SpawnAgentTank();

	// 3. Spawn trainer tank and possess it
	SpawnTrainerTank();

	// 4. Register tanks with Learning Agents Manager
	RegisterTanksWithManager();

	UE_LOG(LogTemp, Warning, TEXT("TankTrainingGameMode: Training setup complete."));
}

void ATankTrainingGameMode::SpawnTrainerTank()
{
	// Get the first player controller (should be HumanPlayerController)
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (PC)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankTrainingGameMode: Found PlayerController: %s"), *PC->GetClass()->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TankTrainingGameMode: No PlayerController found!"));
		return;
	}

	HumanController = Cast<AHumanPlayerController>(PC);

	if (!HumanController)
	{
		UE_LOG(LogTemp, Error, TEXT("TankTrainingGameMode: PlayerController is not AHumanPlayerController! It is: %s"), *PC->GetClass()->GetName());
		return;
	}

	// Use configured spawn location
	const FVector SpawnLocation = TrainerSpawnLocation;

	// Setup spawn parameters
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnParams.Name = FName(TEXT("BP_Trainer_Tank"));

	// Spawn trainer tank using BP_Trainer_Tank class
	TrainerTank = GetWorld()->SpawnActor<AWR_Tank_Pawn>(
		TrainerTankClass,
		SpawnLocation,
		TrainerSpawnRotation,
		SpawnParams
	);

	if (TrainerTank)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankTrainingGameMode: BP_Trainer_Tank spawned successfully"));
		UE_LOG(LogTemp, Warning, TEXT("  -> Tank Name: %s"), *TrainerTank->GetName());
		UE_LOG(LogTemp, Warning, TEXT("  -> Tank Class: %s"), *TrainerTank->GetClass()->GetName());
		UE_LOG(LogTemp, Warning, TEXT("  -> Tank Location: %s"), *SpawnLocation.ToString());

		// Possess the trainer tank with human controller
		UE_LOG(LogTemp, Warning, TEXT("  -> Possessing tank with HumanPlayerController: %s"), *HumanController->GetName());
		HumanController->Possess(TrainerTank);

		// Verify possession
		if (HumanController->GetPawn() == TrainerTank)
		{
			UE_LOG(LogTemp, Warning, TEXT("  -> Possession SUCCESSFUL - Controller->GetPawn() == TrainerTank"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("  -> Possession FAILED - Controller->GetPawn() != TrainerTank"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TankTrainingGameMode: Failed to spawn BP_Trainer_Tank!"));
	}
}

void ATankTrainingGameMode::SpawnAgentTank()
{
	// Create AI controller for agent tank
	AIController = GetWorld()->SpawnActor<AAILearningAgentsController>(
		AAILearningAgentsController::StaticClass()
	);

	if (!AIController)
	{
		UE_LOG(LogTemp, Error, TEXT("TankTrainingGameMode: Failed to spawn AILearningAgentsController!"));
		return;
	}

	// Use configured spawn location
	const FVector SpawnLocation = AgentSpawnLocation;

	// Setup spawn parameters
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnParams.Name = FName(TEXT("BP_AI_Tank"));

	// Spawn AI agent tank using BP_AI_Tank class
	AgentTank = GetWorld()->SpawnActor<AWR_Tank_Pawn>(
		AITankClass,
		SpawnLocation,
		AgentSpawnRotation,
		SpawnParams
	);

	if (AgentTank)
	{
		// Possess the agent tank with AI controller
		AIController->Possess(AgentTank);

		UE_LOG(LogTemp, Warning, TEXT("TankTrainingGameMode: BP_AI_Tank spawned and possessed at location: %s"),
			*SpawnLocation.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TankTrainingGameMode: Failed to spawn BP_AI_Tank!"));
	}
}

ATankLearningAgentsManager* ATankTrainingGameMode::FindLearningAgentsManager() const
{
	if (!GetWorld())
	{
		return nullptr;
	}

	// Search for Learning Agents Manager Actor in world
	TArray<AActor*> FoundManagers;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATankLearningAgentsManager::StaticClass(), FoundManagers);

	if (FoundManagers.Num() > 0)
	{
		return Cast<ATankLearningAgentsManager>(FoundManagers[0]);
	}

	return nullptr;
}

void ATankTrainingGameMode::RegisterTanksWithManager()
{
	// Find Learning Agents Manager
	ATankLearningAgentsManager* Manager = FindLearningAgentsManager();

	if (!Manager)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankTrainingGameMode: No ATankLearningAgentsManager actor found in world!"));
		UE_LOG(LogTemp, Warning, TEXT("  → Tanks spawned but not registered with Learning Agents system."));
		UE_LOG(LogTemp, Warning, TEXT("  → Place an ATankLearningAgentsManager actor in the level to enable AI training."));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("TankTrainingGameMode: Found Learning Agents Manager, registering tanks..."));

	// Register ONLY trainer tank for recording phase
	if (TrainerTank)
	{
		Manager->RegisterTrainerTank(TrainerTank);
		UE_LOG(LogTemp, Log, TEXT("  → Trainer tank registered: %s"), *TrainerTank->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("  → Trainer tank is null, cannot register!"));
	}

	// DO NOT register agent tank during recording phase
	// This prevents "Agent 1 has not made observations and taken actions" warnings (25k+)
	// Instead, just STORE the reference in Manager so EnableInferenceMode() can use it later
	if (AgentTank)
	{
		Manager->SetAgentTank(AgentTank);
		UE_LOG(LogTemp, Log, TEXT("  → Agent tank reference stored in Manager (NOT registered yet)"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("  → Agent tank is null, cannot store reference!"));
	}

	UE_LOG(LogTemp, Warning, TEXT("TankTrainingGameMode: Agent tank stored but NOT registered (prevents recording warnings)"));
	UE_LOG(LogTemp, Warning, TEXT("  → AI tank will be automatically registered when training stops"));
	UE_LOG(LogTemp, Log, TEXT("TankTrainingGameMode: Tank registration complete (Trainer only)."));
}
