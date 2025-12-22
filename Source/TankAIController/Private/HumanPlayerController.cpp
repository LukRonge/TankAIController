// Copyright Epic Games, Inc. All Rights Reserved.

#include "HumanPlayerController.h"
#include "WR_Tank_Pawn.h"
#include "WR_ControlsInterface.h"
#include "TankLearningAgentsManager.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerInput.h"
#include "Kismet/GameplayStatics.h"

AHumanPlayerController::AHumanPlayerController()
{
	// Base class already sets PrimaryActorTick.bCanEverTick = true
	// and initializes CurrentThrottle, CurrentSteering
}

void AHumanPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Enable mouse input and cursor capture for look controls
	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;

	// Set input mode to game only
	FInputModeGameOnly InputMode;
	InputMode.SetConsumeCaptureMouseDown(false);
	SetInputMode(InputMode);

	// Capture mouse cursor
	SetMouseLocation(GEngine->GameViewport->Viewport->GetSizeXY().X / 2,
	                 GEngine->GameViewport->Viewport->GetSizeXY().Y / 2);

	UE_LOG(LogTemp, Log, TEXT("HumanPlayerController: BeginPlay - Ready for training data collection"));
	UE_LOG(LogTemp, Log, TEXT("HumanPlayerController: Mouse input enabled and cursor captured"));
}

void AHumanPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UE_LOG(LogTemp, Warning, TEXT("HumanPlayerController::SetupInputComponent - START"));

	if (InputComponent)
	{
		// ========================================================================
		// NOTE: Axis bindings (MoveForward, MoveRight, etc.) are NOT bound here
		// ========================================================================
		// The tank pawn (AWR_Tank_Pawn) has its own SetupPlayerInputComponent that
		// binds the same axes. Input goes to the pawn first, so these controller
		// bindings would never fire. Instead, we READ input from the pawn in Tick().
		//
		// Only action bindings work here because they're not bound on the pawn.
		UE_LOG(LogTemp, Warning, TEXT("HumanPlayerController: NOTE - Axis inputs handled by tank pawn, reading in Tick()"));

		// Bind action inputs for recording and training control
		// These actions are NOT bound on the tank pawn, so they work here
		InputComponent->BindAction("StartStopRecording", IE_Pressed, this, &AHumanPlayerController::StartStopRecording);
		UE_LOG(LogTemp, Warning, TEXT("  - Bound StartStopRecording (NumPad8)"));

		InputComponent->BindAction("StartStopTraining", IE_Pressed, this, &AHumanPlayerController::StartStopTraining);
		UE_LOG(LogTemp, Warning, TEXT("  - Bound StartStopTraining (NumPad9)"));

		InputComponent->BindAction("EnableInference", IE_Pressed, this, &AHumanPlayerController::EnableInference);
		UE_LOG(LogTemp, Warning, TEXT("  - Bound EnableInference (NumPad7)"));

		UE_LOG(LogTemp, Warning, TEXT("HumanPlayerController: Input bindings setup complete - 3 action bindings"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("HumanPlayerController: InputComponent is NULL!"));
	}
}

void AHumanPlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Base class performs line traces

	// ========================================================================
	// Read RAW input from tank pawn for ML recording
	// ========================================================================
	// Using RAW values (0/1/-1) ensures observation-action consistency:
	// - Tank moves according to raw input
	// - We record the same raw input as action
	// - No mismatch between what tank does and what is recorded
	if (ControlledTank)
	{
		// Save previous values BEFORE updating (for temporal context in observations)
		PreviousThrottle = CurrentThrottle;
		PreviousSteering = CurrentSteering;

		// Read RAW input from tank pawn (digital 0/1/-1 values)
		CurrentThrottle = ControlledTank->GetTankThrottle_Implementation();
		CurrentSteering = ControlledTank->GetTankSteering_Implementation();

		// Read current turret rotation from tank pawn
		AActor* Turret = ControlledTank->GetTurret_Implementation();
		if (Turret)
		{
			CurrentTurretRotation = Turret->GetActorRotation();
		}

		// Note: Tank pawn handles its own movement in its Tick()
		// We do NOT call ApplyMovementToTank here - that would override pawn's input
	}
}

// ========== INPUT HANDLERS (DEPRECATED) ==========
// These methods are NO LONGER CALLED because input goes directly to tank pawn.
// Input values are now READ from the tank pawn in Tick() instead.
// Keeping these for reference but they can be removed in future cleanup.

void AHumanPlayerController::MoveForward(float AxisValue)
{
	// DEPRECATED: This method is never called - input goes to tank pawn
	// CurrentThrottle is now read from tank pawn in Tick()
}

void AHumanPlayerController::MoveRight(float AxisValue)
{
	// DEPRECATED: This method is never called - input goes to tank pawn
	// CurrentSteering is now read from tank pawn in Tick()
}

void AHumanPlayerController::LookUp(float AxisValue)
{
	// DEPRECATED: This method is never called - input goes to tank pawn
	// Turret rotation is handled by tank pawn, we read it in Tick()
}

void AHumanPlayerController::LookRight(float AxisValue)
{
	// DEPRECATED: This method is never called - input goes to tank pawn
	// Turret rotation is handled by tank pawn, we read it in Tick()
}

// ========== RECORDING & TRAINING CONTROLS ==========

void AHumanPlayerController::StartStopRecording()
{
	ATankLearningAgentsManager* Manager = GetLearningAgentsManager();
	if (!Manager)
	{
		UE_LOG(LogTemp, Error, TEXT("HumanPlayerController::StartStopRecording - Cannot find TankLearningAgentsManager!"));
		return;
	}

	// Toggle recording on/off
	if (Manager->IsRecording())
	{
		UE_LOG(LogTemp, Warning, TEXT("HumanPlayerController: Stopping recording..."));
		Manager->StopRecording();

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("Recording STOPPED"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("HumanPlayerController: Starting recording..."));
		Manager->StartRecording();

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green, TEXT("Recording STARTED"));
		}
	}
}

void AHumanPlayerController::StartStopTraining()
{
	ATankLearningAgentsManager* Manager = GetLearningAgentsManager();
	if (!Manager)
	{
		UE_LOG(LogTemp, Error, TEXT("HumanPlayerController::StartStopTraining - Cannot find TankLearningAgentsManager!"));
		return;
	}

	// Toggle training on/off
	if (Manager->IsTraining())
	{
		UE_LOG(LogTemp, Warning, TEXT("HumanPlayerController: Stopping training..."));
		Manager->StopTraining();

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("Training STOPPED"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("HumanPlayerController: Starting training..."));
		Manager->StartTraining();

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green, TEXT("Training STARTED"));
		}
	}
}

void AHumanPlayerController::EnableInference()
{
	ATankLearningAgentsManager* Manager = GetLearningAgentsManager();
	if (!Manager)
	{
		UE_LOG(LogTemp, Error, TEXT("HumanPlayerController::EnableInference - Cannot find TankLearningAgentsManager!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("HumanPlayerController: Enabling inference mode..."));
	Manager->EnableInferenceMode();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Cyan, TEXT("INFERENCE MODE ENABLED - AI is now driving!"));
	}
}

ATankLearningAgentsManager* AHumanPlayerController::GetLearningAgentsManager()
{
	if (!GetWorld())
	{
		return nullptr;
	}

	// Search for TankLearningAgentsManager actor in world
	TArray<AActor*> FoundManagers;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATankLearningAgentsManager::StaticClass(), FoundManagers);

	if (FoundManagers.Num() > 0)
	{
		return Cast<ATankLearningAgentsManager>(FoundManagers[0]);
	}

	return nullptr;
}
