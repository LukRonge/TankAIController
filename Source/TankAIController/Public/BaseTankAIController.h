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
	// NARROW CORRIDOR OPTIMIZED: Increased density, reduced range

	/** Number of line traces in ellipse pattern (24 for narrow corridors) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|LineTraces")
	int32 NumLineTraces = 24;

	/** Major axis of ellipse (forward/backward) in cm - 6 meters for faster reaction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|LineTraces")
	float EllipseMajorAxis = 600.0f;

	/** Minor axis of ellipse (left/right) in cm - 3.5 meters for close wall detection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|LineTraces")
	float EllipseMinorAxis = 350.0f;

	/** Line trace results - raw distance in cm to obstacle, or max distance if no obstacle */
	UPROPERTY(BlueprintReadOnly, Category = "Tank|LineTraces")
	TArray<float> LineTraceDistances;

	/** Whether to draw debug lines for traces */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|LineTraces")
	bool bDrawDebugTraces = true;

	// ========== LATERAL CLEARANCE (NARROW CORRIDOR SPECIFIC) ==========

	/** Distance for lateral (left/right) clearance traces in cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|LineTraces")
	float LateralTraceDistance = 400.0f;

	/** Left wall clearance distance in cm */
	UPROPERTY(BlueprintReadOnly, Category = "Tank|LineTraces")
	float LeftClearance = 400.0f;

	/** Right wall clearance distance in cm */
	UPROPERTY(BlueprintReadOnly, Category = "Tank|LineTraces")
	float RightClearance = 400.0f;

	// ========== ANGULAR VELOCITY (FOR SMOOTH STEERING) ==========

	/** Current angular velocity around Z axis (yaw rate) in degrees/second */
	UPROPERTY(BlueprintReadOnly, Category = "Tank|AngularVelocity")
	float CurrentAngularVelocityZ = 0.0f;

	/** Previous frame's yaw for angular velocity calculation */
	float PreviousYaw = 0.0f;

	// ========== TEMPORAL CONTEXT (for ML training) ==========

	/** Previous frame's throttle value (for temporal context in ML observations) */
	UPROPERTY(BlueprintReadOnly, Category = "Tank|TemporalContext")
	float PreviousThrottle = 0.0f;

	/** Previous frame's steering value (for temporal context in ML observations) */
	UPROPERTY(BlueprintReadOnly, Category = "Tank|TemporalContext")
	float PreviousSteering = 0.0f;

	// ========== METHODS ==========

	/** Perform line traces in ellipse pattern around tank */
	void PerformLineTraces();

	/** Perform dedicated lateral traces for left/right clearance */
	void PerformLateralTraces();

	/** Update angular velocity from yaw change */
	void UpdateAngularVelocity(float DeltaTime);

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

	// ========== TEMPORAL CONTEXT GETTERS ==========

	/** Get previous frame's throttle value (for ML temporal context) */
	UFUNCTION(BlueprintPure, Category = "Tank|TemporalContext")
	float GetPreviousThrottle() const { return PreviousThrottle; }

	/** Get previous frame's steering value (for ML temporal context) */
	UFUNCTION(BlueprintPure, Category = "Tank|TemporalContext")
	float GetPreviousSteering() const { return PreviousSteering; }

	// ========== NARROW CORRIDOR OBSERVATION GETTERS ==========

	/** Get angular velocity around Z axis (yaw rate) in degrees/second */
	UFUNCTION(BlueprintPure, Category = "Tank|AngularVelocity")
	float GetAngularVelocityZ() const { return CurrentAngularVelocityZ; }

	/** Get left wall clearance distance in cm */
	UFUNCTION(BlueprintPure, Category = "Tank|Observations")
	float GetLeftClearance() const { return LeftClearance; }

	/** Get right wall clearance distance in cm */
	UFUNCTION(BlueprintPure, Category = "Tank|Observations")
	float GetRightClearance() const { return RightClearance; }

	/** Get lateral trace distance setting in cm */
	UFUNCTION(BlueprintPure, Category = "Tank|Observations")
	float GetLateralTraceDistance() const { return LateralTraceDistance; }
};
