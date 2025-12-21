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
 * INPUT SMOOTHING: Keyboard inputs (0/1/-1) are smoothed to gradual values
 * for better ML training data quality.
 */
UCLASS()
class TANKAICONTROLLER_API AHumanPlayerController : public ABaseTankAIController
{
	GENERATED_BODY()

public:
	AHumanPlayerController();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaTime) override;

	// ========== INPUT SMOOTHING ==========

	/** Smoothed throttle value (for ML recording) */
	UPROPERTY(BlueprintReadOnly, Category = "Input|Smoothed")
	float SmoothedThrottle = 0.0f;

	/** Smoothed steering value (for ML recording) */
	UPROPERTY(BlueprintReadOnly, Category = "Input|Smoothed")
	float SmoothedSteering = 0.0f;

	/** Smoothing speed - higher = faster response (units per second) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Smoothing Config")
	float InputSmoothingSpeed = 4.0f;

	/** Apply input smoothing filter */
	void ApplyInputSmoothing(float DeltaTime, float RawThrottle, float RawSteering);

	// ========== INPUT HANDLERS ==========

	/** Handle MoveForward input (W/S keys) */
	void MoveForward(float AxisValue);

	/** Handle MoveRight input (A/D keys) */
	void MoveRight(float AxisValue);

	/** Handle LookUp input (Mouse Y) */
	void LookUp(float AxisValue);

	/** Handle LookRight input (Mouse X) */
	void LookRight(float AxisValue);

	/** Toggle recording on/off (Action input) */
	void StartStopRecording();

	/** Toggle training on/off (Action input) */
	void StartStopTraining();

	/** Enable inference mode (Action input) */
	void EnableInference();

public:
	// ========== SMOOTHED INPUT GETTERS (for recording) ==========

	/** Get smoothed throttle for ML recording */
	UFUNCTION(BlueprintPure, Category = "Input|Smoothed")
	float GetSmoothedThrottle() const { return SmoothedThrottle; }

	/** Get smoothed steering for ML recording */
	UFUNCTION(BlueprintPure, Category = "Input|Smoothed")
	float GetSmoothedSteering() const { return SmoothedSteering; }

private:
	/** Get reference to TankLearningAgentsManager */
	class ATankLearningAgentsManager* GetLearningAgentsManager();
};
