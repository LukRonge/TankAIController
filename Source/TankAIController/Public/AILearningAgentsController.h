// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseTankAIController.h"
#include "AILearningAgentsController.generated.h"

/**
 * AI Learning Agents Controller - Controls agent tank via Learning Agents Interactor
 * Receives actions from AI and applies them to the tank
 * Inherits from BaseTankAIController to share common tank control functionality
 *
 * STANDALONE: Works independently without TankLearningAgentsManager
 * Inherits WaypointComponent from BaseTankAIController for navigation
 *
 * FEATURES:
 * - Stuck detection with velocity-based monitoring
 * - Distance-based recovery (reverse ~100cm when stuck)
 * - Waypoint regeneration on recovery failure
 */
UCLASS()
class TANKAICONTROLLER_API AAILearningAgentsController : public ABaseTankAIController
{
	GENERATED_BODY()

public:
	AAILearningAgentsController();

	// ========== GENERAL SETTINGS ==========

	/** Maximum throttle the AI can apply (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|General", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float MaxThrottleLimit = 1.0f;

	// ========== STUCK DETECTION SETTINGS ==========

	/** Enable stuck detection and automatic recovery */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|StuckDetection")
	bool bEnableStuckDetection = true;

	/** Time with low velocity before considered stuck (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|StuckDetection", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float StuckTimeThreshold = 1.5f;

	/** Velocity below this is considered "not moving" (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|StuckDetection", meta = (ClampMin = "5.0", ClampMax = "50.0"))
	float StuckVelocityThreshold = 15.0f;

	/** Minimum throttle to trigger stuck detection (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|StuckDetection", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float StuckThrottleThreshold = 0.15f;

	// ========== RECOVERY SETTINGS ==========

	/** Distance to reverse during recovery (cm) - max 100cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Recovery", meta = (ClampMin = "50.0", ClampMax = "150.0"))
	float RecoveryReverseDistance = 100.0f;

	/** Throttle during recovery (negative = reverse) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Recovery", meta = (ClampMin = "-1.0", ClampMax = "-0.2"))
	float RecoveryThrottle = -0.5f;

	/** Maximum recovery attempts before regenerating waypoints */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Recovery", meta = (ClampMin = "1", ClampMax = "5"))
	int32 MaxRecoveryAttempts = 3;

	// ========== STATE GETTERS ==========

	/** Check if tank is currently stuck */
	UFUNCTION(BlueprintPure, Category = "AI|StuckDetection")
	bool IsStuck() const { return bIsStuck; }

	/** Check if tank is currently in recovery mode */
	UFUNCTION(BlueprintPure, Category = "AI|StuckDetection")
	bool IsRecovering() const { return bIsRecovering; }

	/** Get current stuck timer value */
	UFUNCTION(BlueprintPure, Category = "AI|StuckDetection")
	float GetStuckTimer() const { return StuckTimer; }

	/** Get current recovery attempt count */
	UFUNCTION(BlueprintPure, Category = "AI|StuckDetection")
	int32 GetRecoveryAttemptCount() const { return RecoveryAttemptCount; }

	/** Get recovery progress (0.0 to 1.0) */
	UFUNCTION(BlueprintPure, Category = "AI|StuckDetection")
	float GetRecoveryProgress() const;

	/** Get current heading error to waypoint (-1 to +1, 0 = facing waypoint) */
	UFUNCTION(BlueprintPure, Category = "AI|Navigation")
	float GetHeadingErrorToWaypoint() const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// ========== STUCK DETECTION METHODS ==========

	/** Check if tank is stuck (low velocity despite throttle) */
	void UpdateStuckDetection(float DeltaTime);

	/** Start recovery sequence */
	void StartRecovery();

	/** Update recovery progress (distance-based) */
	void UpdateRecovery();

	/** End recovery sequence */
	void EndRecovery(bool bSuccess);

	/** Called when all recovery attempts fail */
	void OnRecoveryFailed();

	// ========== STUCK STATE ==========

	/** Timer for stuck detection */
	float StuckTimer = 0.0f;

	/** Whether tank is currently stuck */
	bool bIsStuck = false;

	/** Whether tank is in recovery mode */
	bool bIsRecovering = false;

	/** Position when recovery started */
	FVector RecoveryStartPosition = FVector::ZeroVector;

	/** Number of recovery attempts for current stuck situation */
	int32 RecoveryAttemptCount = 0;

public:
	// ========== AI ACTION API ==========

	/** Set throttle from AI (-1.0 to 1.0) */
	UFUNCTION(BlueprintCallable, Category = "AI|Actions")
	void SetThrottleFromAI(float Value);

	/** Set steering from AI (-1.0 to 1.0) */
	UFUNCTION(BlueprintCallable, Category = "AI|Actions")
	void SetSteeringFromAI(float Value);

	/** Set brake from AI (0.0 to 1.0) */
	UFUNCTION(BlueprintCallable, Category = "AI|Actions")
	void SetBrakeFromAI(float Value);

	/** Set turret rotation from AI (Yaw and Pitch in degrees) */
	UFUNCTION(BlueprintCallable, Category = "AI|Actions")
	void SetTurretRotationFromAI(float Yaw, float Pitch);
};
