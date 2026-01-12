// Copyright Epic Games, Inc. All Rights Reserved.

#include "TankTrainingGameMode.h"
#include "WR_Tank_Pawn.h"
#include "HumanPlayerController.h"
#include "AILearningAgentsController.h"
#include "TankLearningAgentsManager.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"

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
	Super::BeginPlay();

	// Validate tank classes
	if (!TrainerTankClass || !AITankClass)
	{
		UE_LOG(LogTemp, Error, TEXT("TankTrainingGameMode: Tank classes are not set!"));
		return;
	}

	// Check if world is valid
	if (!GetWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("TankTrainingGameMode: GetWorld() returned nullptr!"));
		return;
	}

	// 1. CLEANUP: Destroy all existing pawns and tanks from scene
	TArray<AActor*> FoundPawns;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APawn::StaticClass(), FoundPawns);

	for (AActor* Pawn : FoundPawns)
	{
		if (Pawn)
		{
			Pawn->Destroy();
		}
	}

	// 2. Spawn AI tanks
	if (bSpawnAIAtPlayerStarts)
	{
		// Spawn AI tank at each PlayerStart location
		SpawnAITanksAtPlayerStarts();
	}
	else
	{
		// Legacy: Spawn single AI tank at AgentSpawnLocation
		SpawnAgentTank();
	}

	// 3. Spawn trainer tank and possess it
	SpawnTrainerTank();

	// 4. Register tanks with Learning Agents Manager
	RegisterTanksWithManager();

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("TankTrainingGameMode: Setup complete"));
	UE_LOG(LogTemp, Warning, TEXT("  -> AI Tanks: %d"), AITanks.Num());
	UE_LOG(LogTemp, Warning, TEXT("  -> AI Movement: DISABLED (press NumPad7 to start)"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

void ATankTrainingGameMode::SpawnTrainerTank()
{
	// Get the first player controller (should be HumanPlayerController)
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (!PC)
	{
		UE_LOG(LogTemp, Error, TEXT("TankTrainingGameMode: No PlayerController found!"));
		return;
	}

	HumanController = Cast<AHumanPlayerController>(PC);
	if (!HumanController)
	{
		UE_LOG(LogTemp, Error, TEXT("TankTrainingGameMode: PlayerController is not AHumanPlayerController!"));
		return;
	}

	// Setup spawn parameters
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnParams.Name = FName(TEXT("Trainer_Tank"));

	// Spawn trainer tank
	TrainerTank = GetWorld()->SpawnActor<AWR_Tank_Pawn>(
		TrainerTankClass,
		TrainerSpawnLocation,
		TrainerSpawnRotation,
		SpawnParams
	);

	if (TrainerTank)
	{
		HumanController->Possess(TrainerTank);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TankTrainingGameMode: Failed to spawn Trainer Tank!"));
	}
}

void ATankTrainingGameMode::SpawnAgentTank()
{
	// Legacy method: Spawn single AI tank at AgentSpawnLocation
	AWR_Tank_Pawn* SpawnedTank = SpawnAgentTankAtLocation(AgentSpawnLocation, AgentSpawnRotation);

	// Store as legacy AgentTank reference (first AI tank)
	if (SpawnedTank && AITanks.Num() > 0)
	{
		AgentTank = AITanks[0];
		AIController = AIControllers.Num() > 0 ? AIControllers[0] : nullptr;
	}
}

AWR_Tank_Pawn* ATankTrainingGameMode::SpawnAgentTankAtLocation(const FVector& Location, const FRotator& Rotation)
{
	// Create AI controller for agent tank
	AAILearningAgentsController* NewController = GetWorld()->SpawnActor<AAILearningAgentsController>(
		AAILearningAgentsController::StaticClass()
	);

	if (!NewController)
	{
		UE_LOG(LogTemp, Error, TEXT("TankTrainingGameMode: Failed to spawn AILearningAgentsController!"));
		return nullptr;
	}

	// Setup spawn parameters
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnParams.Name = MakeUniqueObjectName(GetWorld(), AWR_Tank_Pawn::StaticClass(), FName(TEXT("AI_Tank")));

	// Spawn AI tank
	AWR_Tank_Pawn* NewTank = GetWorld()->SpawnActor<AWR_Tank_Pawn>(
		AITankClass,
		Location,
		Rotation,
		SpawnParams
	);

	if (NewTank)
	{
		// Possess the tank with AI controller
		NewController->Possess(NewTank);

		// AI movement starts DISABLED - will be enabled when StartAllAITanks() is called
		NewController->SetAIMovementEnabled(false);

		// Store references
		AITanks.Add(NewTank);
		AIControllers.Add(NewController);

		return NewTank;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TankTrainingGameMode: Failed to spawn AI Tank at %s!"), *Location.ToString());
		NewController->Destroy();
		return nullptr;
	}
}

void ATankTrainingGameMode::SpawnAITanksAtPlayerStarts()
{
	// Find all PlayerStart actors in the level
	TArray<AActor*> FoundPlayerStarts;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), FoundPlayerStarts);

	if (FoundPlayerStarts.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankTrainingGameMode: No PlayerStart actors found! Spawning at AgentSpawnLocation instead."));
		SpawnAgentTank();
		return;
	}

	// Spawn AI tank at each PlayerStart
	for (AActor* PlayerStartActor : FoundPlayerStarts)
	{
		APlayerStart* PlayerStart = Cast<APlayerStart>(PlayerStartActor);
		if (PlayerStart)
		{
			const FVector SpawnLocation = PlayerStart->GetActorLocation();
			const FRotator SpawnRotation = PlayerStart->GetActorRotation();

			AWR_Tank_Pawn* SpawnedTank = SpawnAgentTankAtLocation(SpawnLocation, SpawnRotation);
			if (SpawnedTank)
			{
				UE_LOG(LogTemp, Log, TEXT("TankTrainingGameMode: Spawned AI Tank at PlayerStart: %s"), *SpawnLocation.ToString());
			}
		}
	}

	// Store first tank as legacy AgentTank reference
	if (AITanks.Num() > 0)
	{
		AgentTank = AITanks[0];
		AIController = AIControllers.Num() > 0 ? AIControllers[0] : nullptr;
	}
}

void ATankTrainingGameMode::StartAllAITanks()
{
	if (bAITanksRunning)
	{
		return;
	}

	bAITanksRunning = true;

	for (AAILearningAgentsController* Controller : AIControllers)
	{
		if (Controller)
		{
			Controller->SetAIMovementEnabled(true);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("TankTrainingGameMode: Started %d AI tanks!"), AIControllers.Num());

	if (GEngine)
	{
		FString Message = FString::Printf(TEXT("AI TANKS STARTED (%d tanks)"), AIControllers.Num());
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green, Message, true, FVector2D(1.5f, 1.5f));
	}
}

void ATankTrainingGameMode::StopAllAITanks()
{
	if (!bAITanksRunning)
	{
		return;
	}

	bAITanksRunning = false;

	for (AAILearningAgentsController* Controller : AIControllers)
	{
		if (Controller)
		{
			Controller->SetAIMovementEnabled(false);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("TankTrainingGameMode: Stopped %d AI tanks!"), AIControllers.Num());

	if (GEngine)
	{
		FString Message = FString::Printf(TEXT("AI TANKS STOPPED (%d tanks)"), AIControllers.Num());
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, Message, true, FVector2D(1.5f, 1.5f));
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
