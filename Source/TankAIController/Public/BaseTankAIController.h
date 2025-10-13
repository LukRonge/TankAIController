// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BaseTankAIController.generated.h"

class AWR_Tank_Pawn;
class UChaosWheeledVehicleMovementComponent;

/**
 * Base Tank AI Controller
 * Parent class for both HumanPlayerController (trainer) and AILearningAgentsController
 * Contains common functionality for controlling tanks and gathering observation data for AI training
 */
UCLASS(Abstract)
class TANKAICONTROLLER_API ABaseTankAIController : public APlayerController
{
	GENERATED_BODY()

public:
	ABaseTankAIController();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void OnPossess(APawn* InPawn) override;

	// ========== TANK REFERENCE ==========

	/** Cached reference to controlled tank pawn */
	UPROPERTY()
	TObjectPtr<AWR_Tank_Pawn> ControlledTank;

	// ========== CURRENT INPUT STATE ==========

	/** Current throttle value (-1.0 to 1.0) */
	UPROPERTY(BlueprintReadOnly, Category = "Tank|State")
	float CurrentThrottle;

	/** Current steering value (-1.0 to 1.0) */
	UPROPERTY(BlueprintReadOnly, Category = "Tank|State")
	float CurrentSteering;

	/** Current brake value (0.0 to 1.0) */
	UPROPERTY(BlueprintReadOnly, Category = "Tank|State")
	float CurrentBrake;

	/** Current turret rotation */
	UPROPERTY(BlueprintReadOnly, Category = "Tank|State")
	FRotator CurrentTurretRotation;

	// ========== LINE TRACES FOR OBSTACLE DETECTION ==========

	/** Number of line traces in ellipse pattern */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|LineTraces")
	int32 NumLineTraces = 16;

	/** Major axis of ellipse (forward/backward) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|LineTraces")
	float EllipseMajorAxis = 2000.0f;

	/** Minor axis of ellipse (left/right) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|LineTraces")
	float EllipseMinorAxis = 1500.0f;

	/** Maximum trace distance for normalization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|LineTraces")
	float MaxTraceDistance = 2000.0f;

	/** Line trace results - normalized distances (0-1) */
	UPROPERTY(BlueprintReadOnly, Category = "Tank|LineTraces")
	TArray<float> LineTraceDistances;

	/** Whether to draw debug lines for traces */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|LineTraces")
	bool bDrawDebugTraces = false;

	// ========== METHODS ==========

	/** Perform line traces in ellipse pattern around tank */
	void PerformLineTraces();

	/** Generate ellipse trace points in local space */
	TArray<FVector> GenerateEllipseTracePoints() const;

	/** Apply movement commands to tank */
	void ApplyMovementToTank(float Throttle, float Steering);

	/** Apply turret rotation to tank */
	void ApplyTurretRotationToTank(const FRotator& TurretRotation);

public:
	// ========== GETTERS FOR OBSERVATIONS ==========

	/** Get controlled tank reference */
	UFUNCTION(BlueprintPure, Category = "Tank|Observations")
	AWR_Tank_Pawn* GetControlledTank() const { return ControlledTank; }

	/** Get line trace distances (normalized 0-1) */
	UFUNCTION(BlueprintPure, Category = "Tank|Observations")
	const TArray<float>& GetLineTraceDistances() const { return LineTraceDistances; }

	/** Get tank velocity */
	UFUNCTION(BlueprintPure, Category = "Tank|Observations")
	FVector GetTankVelocity() const;

	/** Get tank rotation */
	UFUNCTION(BlueprintPure, Category = "Tank|Observations")
	FRotator GetTankRotation() const;

	/** Get tank forward speed (dot product of velocity and forward vector) */
	UFUNCTION(BlueprintPure, Category = "Tank|Observations")
	float GetForwardSpeed() const;

	/** Get turret rotation */
	UFUNCTION(BlueprintPure, Category = "Tank|Observations")
	FRotator GetTurretRotation() const;

	/** Get current throttle value */
	UFUNCTION(BlueprintPure, Category = "Tank|Observations")
	float GetCurrentThrottle() const { return CurrentThrottle; }

	/** Get current steering value */
	UFUNCTION(BlueprintPure, Category = "Tank|Observations")
	float GetCurrentSteering() const { return CurrentSteering; }

	/** Get current brake value */
	UFUNCTION(BlueprintPure, Category = "Tank|Observations")
	float GetCurrentBrake() const { return CurrentBrake; }

	/** Get current turret rotation value */
	UFUNCTION(BlueprintPure, Category = "Tank|Observations")
	FRotator GetCurrentTurretRotation() const { return CurrentTurretRotation; }
};
