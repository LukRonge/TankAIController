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

private:
	/** Get reference to TankLearningAgentsManager */
	class ATankLearningAgentsManager* GetLearningAgentsManager();
};
