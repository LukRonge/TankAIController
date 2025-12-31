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
 * SAFETY FEATURES (v2.0):
 * - Obstacle proximity throttle reduction
 * - Maximum speed limiting
 */
UCLASS()
class TANKAICONTROLLER_API AAILearningAgentsController : public ABaseTankAIController
{
	GENERATED_BODY()

public:
	AAILearningAgentsController();

	// ========== SAFETY SETTINGS ==========
	// WARNING: Aggressive safety limits cause TRAINING/INFERENCE MISMATCH!
	// Human records with full throttle, but AI gets limited → AI learns wrong behavior.
	// Keep these settings minimal or disabled for best results.

	/** Enable obstacle proximity throttle reduction (slows down near walls) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Safety")
	bool bEnableObstacleProximityThrottle = false;  // DISABLED - causes distribution mismatch

	/** Distance at which throttle reduction starts (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Safety", meta = (ClampMin = "50", ClampMax = "500"))
	float ObstacleSlowdownStartDistance = 100.0f;  // Reduced from 200

	/** Distance at which throttle is fully reduced (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Safety", meta = (ClampMin = "20", ClampMax = "200"))
	float ObstacleSlowdownEndDistance = 30.0f;  // Reduced from 50

	/** Minimum throttle multiplier when close to obstacles (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Safety", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinThrottleMultiplier = 0.6f;  // Increased from 0.3

	/** Maximum throttle the AI can apply (0-1, useful for testing) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Safety", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float MaxThrottleLimit = 1.0f;  // Increased from 0.7 - allow full throttle

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Calculate throttle multiplier based on obstacle proximity */
	float CalculateObstacleProximityMultiplier() const;

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
