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
		UE_LOG(LogTemp, Warning, TEXT("HumanPlayerController: InputComponent is valid, binding axes..."));

		// Bind axis inputs - classic input system
		InputComponent->BindAxis("MoveForward", this, &AHumanPlayerController::MoveForward);
		UE_LOG(LogTemp, Warning, TEXT("  - Bound MoveForward"));

		InputComponent->BindAxis("MoveRight", this, &AHumanPlayerController::MoveRight);
		UE_LOG(LogTemp, Warning, TEXT("  - Bound MoveRight"));

		InputComponent->BindAxis("LookUp", this, &AHumanPlayerController::LookUp);
		UE_LOG(LogTemp, Warning, TEXT("  - Bound LookUp"));

		InputComponent->BindAxis("LookRight", this, &AHumanPlayerController::LookRight);
		UE_LOG(LogTemp, Warning, TEXT("  - Bound LookRight"));

		// Bind action inputs for recording and training control
		InputComponent->BindAction("StartStopRecording", IE_Pressed, this, &AHumanPlayerController::StartStopRecording);
		UE_LOG(LogTemp, Warning, TEXT("  - Bound StartStopRecording"));

		InputComponent->BindAction("StartStopTraining", IE_Pressed, this, &AHumanPlayerController::StartStopTraining);
		UE_LOG(LogTemp, Warning, TEXT("  - Bound StartStopTraining"));

		UE_LOG(LogTemp, Warning, TEXT("HumanPlayerController: Input bindings setup complete - 4 axes + 2 actions bound"));
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
	// Apply current inputs to tank using base class method
	if (ControlledTank)
	{
		ApplyMovementToTank(CurrentThrottle, CurrentSteering);
	}
}

// ========== INPUT HANDLERS ==========

void AHumanPlayerController::MoveForward(float AxisValue)
{
	if (FMath::Abs(AxisValue) > 0.01f)
	{
		UE_LOG(LogTemp, Log, TEXT("HumanPlayerController::MoveForward called with AxisValue: %f"), AxisValue);
	}

	// Only store the value - Tick will apply it
	CurrentThrottle = AxisValue;
}

void AHumanPlayerController::MoveRight(float AxisValue)
{
	if (FMath::Abs(AxisValue) > 0.01f)
	{
		UE_LOG(LogTemp, Log, TEXT("HumanPlayerController::MoveRight called with AxisValue: %f"), AxisValue);
	}

	// Only store the value - Tick will apply it
	CurrentSteering = AxisValue;
}

void AHumanPlayerController::LookUp(float AxisValue)
{
	// Invert Y axis for turret pitch (mouse up = look up)
	AxisValue *= -1.0f;

	if (FMath::Abs(AxisValue) > 0.01f)
	{
		UE_LOG(LogTemp, Warning, TEXT("HumanPlayerController::LookUp called with AxisValue: %f (inverted), ControlledTank: %s, IsLocallyControlled: %d"),
			AxisValue, ControlledTank ? *ControlledTank->GetName() : TEXT("NULL"), IsLocalPlayerController());
	}

	// Apply turret pitch control via interface _Implementation
	if (ControlledTank)
	{
		// Call _Implementation method (WR_ControlsInterface uses BlueprintNativeEvent)
		ControlledTank->LookUp_Implementation(AxisValue);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("HumanPlayerController::LookUp - ControlledTank is NULL!"));
	}

	// Update current turret rotation for observation data
	CurrentTurretRotation.Pitch += AxisValue;
}

void AHumanPlayerController::LookRight(float AxisValue)
{
	if (FMath::Abs(AxisValue) > 0.01f)
	{
		UE_LOG(LogTemp, Warning, TEXT("HumanPlayerController::LookRight called with AxisValue: %f, ControlledTank: %s, IsLocallyControlled: %d"),
			AxisValue, ControlledTank ? *ControlledTank->GetName() : TEXT("NULL"), IsLocalPlayerController());
	}

	// Apply turret yaw control via interface _Implementation
	if (ControlledTank)
	{
		// Call _Implementation method (WR_ControlsInterface uses BlueprintNativeEvent)
		ControlledTank->LookRight_Implementation(AxisValue);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("HumanPlayerController::LookRight - ControlledTank is NULL!"));
	}

	// Update current turret rotation for observation data
	CurrentTurretRotation.Yaw += AxisValue;
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
