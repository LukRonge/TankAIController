// Copyright Epic Games, Inc. All Rights Reserved.

#include "HumanPlayerController.h"
#include "WR_Tank_Pawn.h"
#include "WR_ControlsInterface.h"
#include "TankLearningAgentsManager.h"
#include "TankTrainingGameMode.h"
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
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		SetMouseLocation(GEngine->GameViewport->Viewport->GetSizeXY().X / 2,
		                 GEngine->GameViewport->Viewport->GetSizeXY().Y / 2);
	}

	UE_LOG(LogTemp, Log, TEXT("HumanPlayerController: Ready for training"));
}

void AHumanPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		// Bind action inputs for recording and training control
		InputComponent->BindAction("StartStopRecording", IE_Pressed, this, &AHumanPlayerController::StartStopRecording);
		InputComponent->BindAction("StartStopTraining", IE_Pressed, this, &AHumanPlayerController::StartStopTraining);
		InputComponent->BindAction("EnableInference", IE_Pressed, this, &AHumanPlayerController::EnableInference);
	}
}

void AHumanPlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Base class performs line traces, lateral traces, and angular velocity update

	// ========================================================================
	// Read input from tank pawn for ML recording (with optional smoothing)
	// ========================================================================
	// INPUT SMOOTHING (v2.0): Converts digital 0/1/-1 values into gradient
	// values using exponential smoothing + noise injection for better
	// behavior cloning training data.
	//
	// AUTO-DETECTION: If input values are NOT exactly -1, 0, or 1, assume
	// gamepad (analog) input and skip smoothing to preserve raw analog values.
	if (ControlledTank)
	{
		// Read RAW input from tank pawn
		const float RawThrottle = ControlledTank->GetTankThrottle_Implementation();
		const float RawSteering = ControlledTank->GetTankSteering_Implementation();

		// ========== AUTO-DETECT ANALOG INPUT (GAMEPAD) ==========
		// Digital input (keyboard) produces exactly -1, 0, or 1
		// Analog input (gamepad) produces values like 0.45, 0.78, etc.
		bool bIsAnalogInput = false;
		if (bAutoDetectAnalogInput)
		{
			// Check if throttle is analog (not exactly -1, 0, or 1)
			const bool bThrottleIsAnalog =
				(FMath::Abs(RawThrottle) > AnalogDetectionThreshold) &&
				(FMath::Abs(RawThrottle) < (1.0f - AnalogDetectionThreshold));

			// Check if steering is analog
			const bool bSteeringIsAnalog =
				(FMath::Abs(RawSteering) > AnalogDetectionThreshold) &&
				(FMath::Abs(RawSteering) < (1.0f - AnalogDetectionThreshold));

			bIsAnalogInput = bThrottleIsAnalog || bSteeringIsAnalog;
		}

		// Apply smoothing only for digital (keyboard) input
		const bool bShouldSmooth = bEnableInputSmoothing && !bIsAnalogInput;

		if (bShouldSmooth)
		{
			// Apply exponential smoothing to create gradient values from digital input
			SmoothedThrottle = FMath::Lerp(SmoothedThrottle, RawThrottle, InputSmoothingAlpha);
			SmoothedSteering = FMath::Lerp(SmoothedSteering, RawSteering, InputSmoothingAlpha);

			// Add small noise for training data diversity
			const float ThrottleNoise = FMath::FRandRange(-InputNoiseScale, InputNoiseScale);
			const float SteeringNoise = FMath::FRandRange(-InputNoiseScale, InputNoiseScale);

			// Apply smoothed + noisy values (clamped to valid range)
			CurrentThrottle = FMath::Clamp(SmoothedThrottle + ThrottleNoise, -1.0f, 1.0f);
			CurrentSteering = FMath::Clamp(SmoothedSteering + SteeringNoise, -1.0f, 1.0f);
		}
		else
		{
			// Use raw values without smoothing (for gamepad or when smoothing disabled)
			CurrentThrottle = RawThrottle;
			CurrentSteering = RawSteering;

			// Also update smoothed values to match (prevents lag on input type switch)
			SmoothedThrottle = RawThrottle;
			SmoothedSteering = RawSteering;
		}

		// Debug: Log input type detection periodically
		static int32 InputLogCounter = 0;
		if (++InputLogCounter % 120 == 0)  // Every 2 seconds at 60fps
		{
			if (bIsAnalogInput)
			{
				UE_LOG(LogTemp, Log, TEXT("[INPUT] GAMEPAD detected: Throttle=%.3f Steering=%.3f (raw analog)"),
					CurrentThrottle, CurrentSteering);
			}
		}

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
	// Get game mode and start all AI tanks
	ATankTrainingGameMode* GameMode = Cast<ATankTrainingGameMode>(GetWorld()->GetAuthGameMode());
	if (GameMode)
	{
		// Toggle AI tanks on/off
		if (GameMode->AreAITanksRunning())
		{
			GameMode->StopAllAITanks();
		}
		else
		{
			GameMode->StartAllAITanks();
		}
		return;
	}

	// Fallback: Try Learning Agents Manager (legacy behavior)
	ATankLearningAgentsManager* Manager = GetLearningAgentsManager();
	if (Manager)
	{
		Manager->EnableInferenceMode();

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Cyan, TEXT("INFERENCE MODE ENABLED - AI is now driving!"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("HumanPlayerController::EnableInference - No GameMode or Manager found!"));
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

