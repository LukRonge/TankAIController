// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TankTrainingHUD.generated.h"

class ATankLearningAgentsManager;

/**
 * Tank Training HUD Widget
 * Displays training status, recording status, and progress information
 * This is a base C++ class that should be extended in Blueprint for UI design
 */
UCLASS(BlueprintType, Blueprintable)
class TANKAICONTROLLER_API UTankTrainingHUD : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Initialize the HUD and find the manager */
	virtual void NativeConstruct() override;

	/** Update HUD every frame */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ========== BLUEPRINT IMPLEMENTABLE EVENTS ==========

	/** Called when recording status changes */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Training HUD")
	void OnRecordingStatusChanged(bool bIsRecording);

	/** Called when training status changes */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Training HUD")
	void OnTrainingStatusChanged(bool bIsTraining);

	/** Called when training progress updates */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Training HUD")
	void OnTrainingProgressUpdated(float Progress, int32 CurrentIter, int32 TotalIter);

	// ========== BLUEPRINT CALLABLE GETTERS ==========

	/** Get recording status */
	UFUNCTION(BlueprintPure, Category = "Tank Training HUD")
	bool IsRecording() const;

	/** Get training status */
	UFUNCTION(BlueprintPure, Category = "Tank Training HUD")
	bool IsTraining() const;

	/** Get training progress (0.0 to 1.0) */
	UFUNCTION(BlueprintPure, Category = "Tank Training HUD")
	float GetTrainingProgress() const;

	/** Get current training iteration */
	UFUNCTION(BlueprintPure, Category = "Tank Training HUD")
	int32 GetCurrentIteration() const;

	/** Get total training iterations */
	UFUNCTION(BlueprintPure, Category = "Tank Training HUD")
	int32 GetTotalIterations() const;

	/** Get current training loss */
	UFUNCTION(BlueprintPure, Category = "Tank Training HUD")
	float GetCurrentLoss() const;

	/** Get number of recorded experiences (frames) */
	UFUNCTION(BlueprintPure, Category = "Tank Training HUD")
	int32 GetRecordedExperienceCount() const;

	/** Get recording progress text (e.g., "500 frames recorded") */
	UFUNCTION(BlueprintPure, Category = "Tank Training HUD")
	FText GetRecordingProgressText() const;

	/** Get recording status text */
	UFUNCTION(BlueprintPure, Category = "Tank Training HUD")
	FText GetRecordingStatusText() const;

	/** Get training status text */
	UFUNCTION(BlueprintPure, Category = "Tank Training HUD")
	FText GetTrainingStatusText() const;

	/** Get training progress text (e.g., "5000/100000") */
	UFUNCTION(BlueprintPure, Category = "Tank Training HUD")
	FText GetTrainingProgressText() const;

	/** Get recording status color */
	UFUNCTION(BlueprintPure, Category = "Tank Training HUD")
	FLinearColor GetRecordingStatusColor() const;

	/** Get training status color */
	UFUNCTION(BlueprintPure, Category = "Tank Training HUD")
	FLinearColor GetTrainingStatusColor() const;

protected:
	/** Reference to the Tank Learning Agents Manager */
	UPROPERTY(BlueprintReadOnly, Category = "Tank Training HUD")
	TObjectPtr<ATankLearningAgentsManager> Manager;

	/** Find and cache the manager reference */
	void FindManager();

private:
	/** Last known recording status for change detection */
	bool bLastRecordingStatus = false;

	/** Last known training status for change detection */
	bool bLastTrainingStatus = false;

	/** Last known training progress for throttled updates */
	float LastTrainingProgress = 0.0f;

	/** Update interval for progress events (seconds) */
	float ProgressUpdateInterval = 0.5f;

	/** Time since last progress update */
	float TimeSinceLastProgressUpdate = 0.0f;
};
