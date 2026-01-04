// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseTankAIController.h"
#include "HumanPlayerController.generated.h"

/**
 * Human Player Controller - Controls trainer tank via keyboard/mouse input
 * Used for collecting training data for AI agents
 * Inherits from BaseTankAIController to share common tank control functionality
 * Uses classic Input Axis Bindings (not Enhanced Input)
 *
 * INPUT SMOOTHING (v2.0): Applies exponential smoothing and noise injection
 * to convert digital keyboard input (0/1/-1) into gradient values for better
 * behavior cloning training data.
 */
UCLASS()
class TANKAICONTROLLER_API AHumanPlayerController : public ABaseTankAIController
{
	GENERATED_BODY()

public:
	AHumanPlayerController();

	// ========== INPUT SMOOTHING FOR ML RECORDING ==========

	/** Enable input smoothing for gradient values (for KEYBOARD only - auto-disabled for gamepad) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|InputSmoothing")
	bool bEnableInputSmoothing = true;

	/** Auto-detect analog input (gamepad) and disable smoothing (RECOMMENDED) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|InputSmoothing")
	bool bAutoDetectAnalogInput = true;

	/** Smoothing factor (0-1): Lower = smoother, Higher = more responsive */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|InputSmoothing", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float InputSmoothingAlpha = 0.3f;

	/** Noise injection scale for input diversity (0 = no noise) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|InputSmoothing", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float InputNoiseScale = 0.1f;

	/** Threshold for detecting analog vs digital input (values between -1+threshold and 1-threshold are analog) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|InputSmoothing", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float AnalogDetectionThreshold = 0.1f;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaTime) override;

	// Smoothed input values (internal state)
	float SmoothedThrottle = 0.0f;
	float SmoothedSteering = 0.0f;

	// ========== TRAINING CONTROLS ==========

	/** Toggle recording on/off */
	void StartStopRecording();

	/** Toggle training on/off */
	void StartStopTraining();

	/** Enable inference mode */
	void EnableInference();

private:
	/** Get reference to TankLearningAgentsManager */
	class ATankLearningAgentsManager* GetLearningAgentsManager();
};
