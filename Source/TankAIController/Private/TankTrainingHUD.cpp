// Copyright Epic Games, Inc. All Rights Reserved.

#include "TankTrainingHUD.h"
#include "TankLearningAgentsManager.h"
#include "Kismet/GameplayStatics.h"

void UTankTrainingHUD::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(LogTemp, Warning, TEXT("TankTrainingHUD::NativeConstruct - Initializing HUD widget"));

	// Find the manager in the world
	FindManager();

	// Initialize status tracking
	if (Manager)
	{
		bLastRecordingStatus = Manager->IsRecording();
		bLastTrainingStatus = Manager->IsTraining();
		LastTrainingProgress = Manager->GetTrainingProgress();

		UE_LOG(LogTemp, Log, TEXT("  -> Initial status: Recording=%d, Training=%d, Progress=%.2f"),
			bLastRecordingStatus, bLastTrainingStatus, LastTrainingProgress);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("  -> Manager is NULL! HUD will not function!"));
	}
}

void UTankTrainingHUD::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Ensure we have a manager reference
	if (!Manager)
	{
		FindManager();
		if (!Manager)
		{
			return;
		}
	}

	// Check for recording status changes
	bool bCurrentRecordingStatus = Manager->IsRecording();
	if (bCurrentRecordingStatus != bLastRecordingStatus)
	{
		bLastRecordingStatus = bCurrentRecordingStatus;
		UE_LOG(LogTemp, Warning, TEXT("TankTrainingHUD: Recording status CHANGED to %s"),
			bCurrentRecordingStatus ? TEXT("RECORDING") : TEXT("NOT RECORDING"));

		// Show on-screen message
		if (GEngine)
		{
			FColor Color = bCurrentRecordingStatus ? FColor::Green : FColor::Red;
			FString Message = bCurrentRecordingStatus ? TEXT("RECORDING STARTED") : TEXT("RECORDING STOPPED");
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, Color, Message, true, FVector2D(2.0f, 2.0f));
		}

		OnRecordingStatusChanged(bCurrentRecordingStatus);
	}

	// Check for training status changes
	bool bCurrentTrainingStatus = Manager->IsTraining();
	if (bCurrentTrainingStatus != bLastTrainingStatus)
	{
		bLastTrainingStatus = bCurrentTrainingStatus;
		UE_LOG(LogTemp, Warning, TEXT("TankTrainingHUD: Training status CHANGED to %s"),
			bCurrentTrainingStatus ? TEXT("TRAINING") : TEXT("NOT TRAINING"));

		// Show on-screen message
		if (GEngine)
		{
			FColor Color = bCurrentTrainingStatus ? FColor::Cyan : FColor::Yellow;
			FString Message = bCurrentTrainingStatus ? TEXT("TRAINING STARTED") : TEXT("TRAINING STOPPED");
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, Color, Message, true, FVector2D(2.0f, 2.0f));
		}

		OnTrainingStatusChanged(bCurrentTrainingStatus);
	}

	// Update training progress (throttled to avoid spam)
	if (bCurrentTrainingStatus)
	{
		TimeSinceLastProgressUpdate += InDeltaTime;

		if (TimeSinceLastProgressUpdate >= ProgressUpdateInterval)
		{
			TimeSinceLastProgressUpdate = 0.0f;

			float CurrentProgress = Manager->GetTrainingProgress();
			if (FMath::Abs(CurrentProgress - LastTrainingProgress) > 0.001f) // Only update if changed
			{
				LastTrainingProgress = CurrentProgress;
				OnTrainingProgressUpdated(CurrentProgress, Manager->GetCurrentIteration(), Manager->GetCurrentIteration());
			}
		}
	}

	// Display persistent on-screen status
	if (GEngine)
	{
		// Recording status (Key 100 = persistent location)
		FColor RecordingColor = bCurrentRecordingStatus ? FColor::Green : FColor(100, 100, 100);
		FString RecordingText = FString::Printf(TEXT("Recording: %s"), bCurrentRecordingStatus ? TEXT("ON") : TEXT("OFF"));
		GEngine->AddOnScreenDebugMessage(100, 0.0f, RecordingColor, RecordingText, false, FVector2D(1.5f, 1.5f));

		// Training status (Key 101 = persistent location)
		FColor TrainingColor = bCurrentTrainingStatus ? FColor::Cyan : FColor(100, 100, 100);
		FString TrainingText = FString::Printf(TEXT("Training: %s"), bCurrentTrainingStatus ? TEXT("ON") : TEXT("OFF"));
		GEngine->AddOnScreenDebugMessage(101, 0.0f, TrainingColor, TrainingText, false, FVector2D(1.5f, 1.5f));

		// Recording progress (Key 102 = persistent location)
		if (bCurrentRecordingStatus)
		{
			int32 RecordedFrames = Manager->GetRecordedExperienceCount();
			FString RecordingProgressText = FString::Printf(TEXT("Recorded: %d frames"), RecordedFrames);
			GEngine->AddOnScreenDebugMessage(102, 0.0f, FColor::Green, RecordingProgressText, false, FVector2D(1.5f, 1.5f));
		}
		// Training progress (Key 102 = persistent location, only show when training)
		else if (bCurrentTrainingStatus || LastTrainingProgress > 0.0f)
		{
			int32 CurrentIter = Manager->GetCurrentIteration();
			int32 TotalIter = Manager->GetTotalIterations();
			float Progress = Manager->GetTrainingProgress() * 100.0f;
			FString ProgressText = FString::Printf(TEXT("Progress: %d/%d (%.1f%%)"), CurrentIter, TotalIter, Progress);
			GEngine->AddOnScreenDebugMessage(102, 0.0f, FColor::White, ProgressText, false, FVector2D(1.5f, 1.5f));
		}

		// Instructions (Key 103)
		GEngine->AddOnScreenDebugMessage(103, 0.0f, FColor(150, 150, 150), TEXT("R = Record | T = Train"), false, FVector2D(1.2f, 1.2f));
	}
}

void UTankTrainingHUD::FindManager()
{
	if (!GetWorld())
	{
		return;
	}

	// Search for TankLearningAgentsManager in the world
	TArray<AActor*> FoundManagers;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATankLearningAgentsManager::StaticClass(), FoundManagers);

	if (FoundManagers.Num() > 0)
	{
		Manager = Cast<ATankLearningAgentsManager>(FoundManagers[0]);
		UE_LOG(LogTemp, Log, TEXT("TankTrainingHUD: Found Manager: %s"), Manager ? *Manager->GetName() : TEXT("NULL"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("TankTrainingHUD: No TankLearningAgentsManager found in world!"));
	}
}

// ========== BLUEPRINT CALLABLE GETTERS ==========

bool UTankTrainingHUD::IsRecording() const
{
	return Manager ? Manager->IsRecording() : false;
}

bool UTankTrainingHUD::IsTraining() const
{
	return Manager ? Manager->IsTraining() : false;
}

float UTankTrainingHUD::GetTrainingProgress() const
{
	return Manager ? Manager->GetTrainingProgress() : 0.0f;
}

int32 UTankTrainingHUD::GetCurrentIteration() const
{
	return Manager ? Manager->GetCurrentIteration() : 0;
}

int32 UTankTrainingHUD::GetTotalIterations() const
{
	// Use Manager's GetTotalIterations() API (now exposed in UE 5.6 update)
	return Manager ? Manager->GetTotalIterations() : 100000;
}

float UTankTrainingHUD::GetCurrentLoss() const
{
	return Manager ? Manager->GetCurrentLoss() : 0.0f;
}

int32 UTankTrainingHUD::GetRecordedExperienceCount() const
{
	return Manager ? Manager->GetRecordedExperienceCount() : 0;
}

FText UTankTrainingHUD::GetRecordingProgressText() const
{
	if (!Manager)
	{
		return FText::FromString(TEXT("0 frames"));
	}

	int32 RecordedFrames = Manager->GetRecordedExperienceCount();
	return FText::FromString(FString::Printf(TEXT("%d frames"), RecordedFrames));
}

FText UTankTrainingHUD::GetRecordingStatusText() const
{
	if (!Manager)
	{
		return FText::FromString(TEXT("No Manager"));
	}

	return Manager->IsRecording() ?
		FText::FromString(TEXT("RECORDING")) :
		FText::FromString(TEXT("NOT RECORDING"));
}

FText UTankTrainingHUD::GetTrainingStatusText() const
{
	if (!Manager)
	{
		return FText::FromString(TEXT("No Manager"));
	}

	return Manager->IsTraining() ?
		FText::FromString(TEXT("TRAINING")) :
		FText::FromString(TEXT("NOT TRAINING"));
}

FText UTankTrainingHUD::GetTrainingProgressText() const
{
	if (!Manager)
	{
		return FText::FromString(TEXT("0/0"));
	}

	int32 Current = Manager->GetCurrentIteration();
	int32 Total = GetTotalIterations();

	return FText::FromString(FString::Printf(TEXT("%d/%d"), Current, Total));
}

FLinearColor UTankTrainingHUD::GetRecordingStatusColor() const
{
	if (!Manager)
	{
		return FLinearColor::Gray;
	}

	// Green if recording, red if not
	return Manager->IsRecording() ? FLinearColor::Green : FLinearColor::Red;
}

FLinearColor UTankTrainingHUD::GetTrainingStatusColor() const
{
	if (!Manager)
	{
		return FLinearColor::Gray;
	}

	// Blue if training, yellow if not
	return Manager->IsTraining() ? FLinearColor::Blue : FLinearColor::Yellow;
}
