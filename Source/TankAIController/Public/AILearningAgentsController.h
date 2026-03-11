// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseTankAIController.h"
#include "EnemyDetectionComponent.h"
#include "CombatManeuverComponent.h"
#include "AIShootingComponent.h"
#include "AILearningAgentsController.generated.h"


// Forward declarations for Learning Agents (standalone inference)
class ULearningAgentsManager;
class ULearningAgentsPolicy;
class UTankLearningAgentsInteractor;

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
 * - Combat maneuver system with tactical waypoint generation
 *
 * NAVIGATION MODES:
 * - PATROL: Uses random waypoints (trained AI movement)
 * - COMBAT: Uses tactical maneuver waypoints (flanking, retreat, etc.)
 * - Automatic switching based on enemy detection
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

	/** Enable/disable AI movement. When false, AI tank will not move (but turret still aims). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|General")
	bool bAIMovementEnabled = false;

	/** Enable AI movement */
	UFUNCTION(BlueprintCallable, Category = "AI|General")
	void SetAIMovementEnabled(bool bEnabled);

	/** Check if AI movement is enabled */
	UFUNCTION(BlueprintPure, Category = "AI|General")
	bool IsAIMovementEnabled() const { return bAIMovementEnabled; }

	/** Throttle value for autonomous waypoint movement (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|General", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float AutonomousThrottle = 0.6f;

	/** Steering sensitivity for autonomous waypoint movement */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|General", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float AutonomousSteeringSensitivity = 1.5f;

	// ========== LEARNING AGENTS INFERENCE ==========

	/** Use trained neural network for movement (true) or simple waypoint following (false).
	 *  When true, AI uses trained policy from Saved/LearningAgents/Policies/
	 *  When false, AI uses simple UpdateAutonomousMovement() as fallback */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|LearningAgents")
	bool bUseLearningAgentsInference = true;

	/** Check if Learning Agents inference is ready and active */
	UFUNCTION(BlueprintPure, Category = "AI|LearningAgents")
	bool IsLearningAgentsInferenceActive() const { return bRegisteredWithSharedManager && bUseLearningAgentsInference; }

	/** Manually trigger policy reload from disk */
	UFUNCTION(BlueprintCallable, Category = "AI|LearningAgents")
	void ReloadTrainedPolicy();

	// ========== STUCK DETECTION SETTINGS ==========

	/** Enable stuck detection and automatic recovery */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|StuckDetection")
	bool bEnableStuckDetection = true;

	/** Time with low velocity before considered stuck (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|StuckDetection", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float StuckTimeThreshold = 2.5f;

	/** Velocity below this is considered "not moving" (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|StuckDetection", meta = (ClampMin = "5.0", ClampMax = "50.0"))
	float StuckVelocityThreshold = 15.0f;

	/** Minimum throttle to trigger stuck detection (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|StuckDetection", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float StuckThrottleThreshold = 0.02f;

	/** Steering above this is considered "turning" and prevents stuck detection (0-1).
	 *  When tank is actively turning, low forward speed is expected and not a stuck condition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|StuckDetection", meta = (ClampMin = "0.1", ClampMax = "0.9"))
	float StuckSteeringThreshold = 0.3f;

	// ========== DAMAGE RESPONSE SETTINGS ==========

	/** Enable turret smooth rotation toward instigator when tank receives damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|DamageResponse")
	bool bEnableDamageResponse = true;

	/** How long the turret prioritizes the damage instigator location (seconds).
	 *  After this time, turret returns to normal targeting (waypoint/detected enemy). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|DamageResponse", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float DamageResponseDuration = 3.0f;

	/** Check if turret is currently reacting to damage instigator */
	UFUNCTION(BlueprintPure, Category = "AI|DamageResponse")
	bool IsRespondingToDamage() const { return bIsRespondingToDamage; }

	// ========== TURRET CONTROL SETTINGS ==========

	/** Enable automatic turret aiming at waypoint/target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Turret")
	bool bEnableTurretAiming = true;

	/** Enable turret targeting of detected enemies (prioritizes enemies over waypoints) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Turret")
	bool bEnableEnemyTargeting = true;

	/** Minimum awareness state required to target enemy (Combat = only fully visible, Suspicious = any detection) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Turret")
	EAwarenessState MinAwarenessForTargeting = EAwarenessState::Alerted;

	/** Maximum angle from forward to engage enemy (degrees). 90 = front hemisphere only */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Turret", meta = (ClampMin = "30.0", ClampMax = "180.0"))
	float EnemyEngageAngleLimit = 90.0f;

	/** Time to wait after losing target before returning to waypoint (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Turret", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float ReturnToWaypointDelay = 0.5f;

	/** Turret rotation interpolation speed (higher = faster rotation).
	 *  Uses RInterpTo with quaternion interpolation for smooth wraparound handling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Turret", meta = (ClampMin = "1.0", ClampMax = "20.0"))
	float TurretRotationInterpSpeed = 8.0f;

	/** Turret speed multiplier when targeting enemy AND tank is steering.
	 *  Compensates for tank rotation pushing turret off-target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Turret", meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float CombatTurretSpeedMultiplier = 2.5f;

	/** Minimum steering value to trigger combat turret speed boost (0-1).
	 *  Below this, normal turret speed is used even in combat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Turret", meta = (ClampMin = "0.1", ClampMax = "0.8"))
	float TurretCompensationSteeringThreshold = 0.2f;

	/** Height above ground to aim at when tracking waypoints (cm). 50 = aim at 50cm above ground */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Turret", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float WaypointAimHeight = 50.0f;

	/** Check if turret is currently targeting an enemy */
	UFUNCTION(BlueprintPure, Category = "AI|Turret")
	bool IsTargetingEnemy() const { return bIsTargetingEnemy; }

	/** Get current turret target actor (enemy or nullptr if targeting waypoint) */
	UFUNCTION(BlueprintPure, Category = "AI|Turret")
	AActor* GetCurrentTurretTarget() const { return CurrentTurretTarget.Get(); }

	// ========== ENEMY DETECTION ==========

	/** Enemy detection component - handles visibility checks and awareness tracking */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Detection")
	TObjectPtr<UEnemyDetectionComponent> EnemyDetectionComponent;

	/** Get enemy detection component */
	UFUNCTION(BlueprintPure, Category = "AI|Detection")
	UEnemyDetectionComponent* GetEnemyDetectionComponent() const { return EnemyDetectionComponent; }

	/** Enable HUD notifications when enemies are detected (for debugging/spectating) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Detection")
	bool bNotifyDetectedEnemyHUD = true;

	/** Enable debug visualization for enemy detection (draws FOV cone, detection rays, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Detection")
	bool bEnableDetectionDebug = false;

	// ========== COMBAT MANEUVER ==========

	/** Combat maneuver component - handles tactical decision making and waypoint generation */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Combat")
	TObjectPtr<UCombatManeuverComponent> CombatManeuverComponent;

	/** Get combat maneuver component */
	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	UCombatManeuverComponent* GetCombatManeuverComponent() const { return CombatManeuverComponent; }

	/** Enable combat maneuver system (switches to tactical waypoints during combat) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Combat")
	bool bEnableCombatManeuvers = true;

	/** Enable debug visualization for combat maneuvers */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Combat")
	bool bEnableCombatDebug = false;

	/** Check if currently in combat navigation mode */
	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	bool IsInCombatMode() const { return bInCombatMode; }

	/** Get current combat waypoint (if in combat mode) */
	const FCombatWaypoint* GetCurrentCombatWaypoint() const;

	// ========== AI SHOOTING ==========

	/** AI Shooting component - handles human-like shooting behavior */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Shooting")
	TObjectPtr<UAIShootingComponent> ShootingComponent;

	/** Get shooting component */
	UFUNCTION(BlueprintPure, Category = "AI|Shooting")
	UAIShootingComponent* GetShootingComponent() const { return ShootingComponent; }

	/** Enable AI shooting (when targeting enemies) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Shooting")
	bool bEnableShooting = true;

	/** Enable debug visualization for shooting */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Shooting")
	bool bEnableShootingDebug = false;

	/** Set AI shooting difficulty */
	UFUNCTION(BlueprintCallable, Category = "AI|Shooting")
	void SetShootingDifficulty(EAIDifficulty NewDifficulty);

	/** Get current AI shooting difficulty */
	UFUNCTION(BlueprintPure, Category = "AI|Shooting")
	EAIDifficulty GetShootingDifficulty() const;

protected:
	// ========== COMBAT STATE ==========

	/** Currently using combat waypoints instead of patrol waypoints */
	bool bInCombatMode = false;

	// ========== COMBAT EVENT HANDLERS ==========

	/** Called when combat state changes */
	UFUNCTION()
	void OnCombatStateChangedHandler(ECombatState OldState, ECombatState NewState);

	/** Called when a new maneuver starts */
	UFUNCTION()
	void OnManeuverStartedHandler(const FCombatManeuver& Maneuver);

	/** Called when a maneuver completes */
	UFUNCTION()
	void OnManeuverCompletedHandler(const FCombatManeuver& Maneuver, bool bSuccess);

	/** Called when advancing to next combat waypoint */
	UFUNCTION()
	void OnCombatWaypointAdvancedHandler(int32 NewIndex, const FCombatWaypoint& Waypoint);

	// ========== COMBAT METHODS ==========

	/** Enter combat navigation mode */
	void EnterCombatMode();

	/** Exit combat navigation mode and return to patrol */
	void ExitCombatMode();

	// ========== DETECTION EVENT HANDLERS ==========

	/** Called when enemy is first detected */
	UFUNCTION()
	void OnEnemyDetectedHandler(AActor* Enemy, const FDetectedEnemyInfo& Info);

	/** Called when enemy awareness state changes */
	UFUNCTION()
	void OnAwarenessStateChangedHandler(AActor* Enemy, EAwarenessState OldState, EAwarenessState NewState);

	/** Called when enemy is lost */
	UFUNCTION()
	void OnEnemyLostHandler(AActor* Enemy);

	/** Bind/unbind damage response delegate to controlled tank (for subclasses that bypass OnPossess) */
	void BindDamageResponseDelegate();
	void UnbindDamageResponseDelegate();

public:
	// ========== CIRCLE/NO-PROGRESS DETECTION ==========

	/** Enable detection of circling behavior (moving but not approaching waypoint) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|CircleDetection")
	bool bEnableCircleDetection = true;

	/** Time without waypoint progress before considered circling (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|CircleDetection", meta = (ClampMin = "2.0", ClampMax = "15.0"))
	float CircleDetectionTime = 5.0f;

	/** Minimum distance closer to waypoint required per check interval to count as progress (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|CircleDetection", meta = (ClampMin = "10.0", ClampMax = "200.0"))
	float MinProgressDistance = 50.0f;

	/** How often to sample waypoint distance for progress check (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|CircleDetection", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float ProgressCheckInterval = 1.0f;

	/** Steering bias threshold (0-1). If average steering over time exceeds this, tank is turning one way.
	 *  Lower = more sensitive. 0.3 means average 30% steering in one direction triggers faster detection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|CircleDetection", meta = (ClampMin = "0.1", ClampMax = "0.8"))
	float CircleSteeringBiasThreshold = 0.3f;

	/** Check if tank is circling (moving but not making progress) */
	UFUNCTION(BlueprintPure, Category = "AI|CircleDetection")
	bool IsCircling() const { return bIsCircling; }

	// ========== RECOVERY SETTINGS ==========

	/** Distance to reverse during recovery (cm). Lower = easier to succeed, higher = more displacement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Recovery", meta = (ClampMin = "10.0", ClampMax = "150.0"))
	float RecoveryReverseDistance = 25.0f;

	/** Throttle during recovery (negative = reverse) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Recovery", meta = (ClampMin = "-1.0", ClampMax = "-0.2"))
	float RecoveryThrottle = -0.5f;

	/** Maximum recovery attempts before regenerating waypoints */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Recovery", meta = (ClampMin = "1", ClampMax = "5"))
	int32 MaxRecoveryAttempts = 3;

	/** Maximum time for a single recovery attempt (seconds). If exceeded, recovery fails. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Recovery", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float RecoveryTimeout = 3.0f;

	/** Minimum rear clearance required to attempt reverse recovery (cm).
	 *  If rear obstacle is closer than this, recovery is skipped. Even 30cm helps for turning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Recovery", meta = (ClampMin = "10.0", ClampMax = "200.0"))
	float MinRearClearanceForRecovery = 30.0f;

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

	/** Get current turret yaw relative to tank (degrees) */
	UFUNCTION(BlueprintPure, Category = "AI|Turret")
	float GetCurrentTurretYaw() const { return CurrentTurretYaw; }

	/** Get current turret pitch (degrees) */
	UFUNCTION(BlueprintPure, Category = "AI|Turret")
	float GetCurrentTurretPitch() const { return CurrentTurretPitch; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void OnPossess(APawn* InPawn) override;

	// ========== STUCK DETECTION METHODS ==========

	/** Check if tank is stuck (low velocity despite throttle) */
	void UpdateStuckDetection(float DeltaTime);

	/** Start recovery sequence */
	void StartRecovery();

	/** Update recovery progress (distance-based with timeout) */
	void UpdateRecovery(float DeltaTime);

	/** End recovery sequence */
	void EndRecovery(bool bSuccess);

	/** Called when all recovery attempts fail */
	void OnRecoveryFailed();

	/** Get rear clearance from line traces (index 12 = rear in 24-trace ellipse) */
	float GetRearClearance() const;

	/** Check if tank is circling (moving but not approaching waypoint) and handle recovery */
	void UpdateCircleDetection(float DeltaTime);

	/** Reset circle detection state (called when waypoints change) */
	void ResetCircleDetection();


	// ========== DAMAGE RESPONSE METHODS ==========

	/** Handler for tank damage received delegate */
	UFUNCTION()
	void OnTankDamageReceivedHandler(const FVector& HitLocation, float DamageAmount, int32 KillerPlayerControllerIndex);

	/** Update damage response timer and clear state when expired */
	void UpdateDamageResponse(float DeltaTime);

	/** Find attacker pawn by player controller index */
	AActor* FindAttackerPawnByIndex(int32 PlayerControllerIndex) const;

	// ========== TURRET CONTROL METHODS ==========

	/** Update turret aim toward waypoint/target with smooth rotation */
	void UpdateTurretAimToWaypoint(float DeltaTime);

	/** Get target location for turret aiming (waypoint or final target) */
	FVector GetTurretAimTargetLocation() const;

	// ========== SHOOTING METHODS ==========

	/** Update AI shooting system - processes targeting and firing */
	void UpdateShooting(float DeltaTime);

	/** Execute primary/secondary fire based on shooting component decisions */
	void ExecuteShootingCommands();

	// ========== AUTONOMOUS MOVEMENT ==========

	/** Update autonomous waypoint-following movement (fallback when neural network not available) */
	void UpdateAutonomousMovement(float DeltaTime);

	// ========== LEARNING AGENTS INFERENCE (Standalone) ==========

	/** Initialize Learning Agents components for standalone inference (no Manager required) */
	void InitializeLearningAgentsForInference();

	/** Load trained policy weights from disk */
	void LoadTrainedPolicy();

	/** Run neural network inference to get movement actions */
	void RunLearningAgentsInference();

	/** Check if trained policy files exist on disk */
	bool DoTrainedPolicyFilesExist() const;

	// ========== SHARED LEARNING AGENTS (Static - shared across all AI controllers) ==========

	/** Shared Learning Agents Manager (for agent tracking) - created by first controller */
	static ULearningAgentsManager* SharedManager;

	/** Shared Interactor (handles observations and actions) */
	static UTankLearningAgentsInteractor* SharedInteractor;

	/** Shared Policy (neural network for inference) */
	static ULearningAgentsPolicy* SharedPolicy;

	/** Whether shared Learning Agents components are initialized */
	static bool bSharedLearningAgentsInitialized;

	/** Agent ID for THIS controller's tank in shared manager */
	int32 LocalAgentId = INDEX_NONE;

	/** Whether THIS controller has registered its tank with shared manager */
	bool bRegisteredWithSharedManager = false;

	// ========== TURRET STATE ==========

	/** Current turret yaw in degrees (relative to tank) */
	float CurrentTurretYaw = 0.0f;

	/** Current turret pitch in degrees */
	float CurrentTurretPitch = 0.0f;

	/** Target turret yaw in degrees (relative to tank) */
	float TargetTurretYaw = 0.0f;

	/** Is turret currently tracking an enemy? */
	bool bIsTargetingEnemy = false;

	/** Current enemy target (if any) */
	TWeakObjectPtr<AActor> CurrentTurretTarget;

	/** Timer for return-to-waypoint delay after losing enemy target */
	float ReturnToWaypointTimer = 0.0f;

	/** Last known enemy target location (for smooth transition) */
	FVector LastEnemyTargetLocation = FVector::ZeroVector;

	/** Target turret pitch in degrees */
	float TargetTurretPitch = 0.0f;

	// ========== CACHED PRIORITY TARGET (perf optimization) ==========

	/** Cached target location from GetTurretAimTargetLocation - updated by events, not every frame */
	FVector CachedTurretTargetLocation = FVector::ZeroVector;

	/** Whether cached target needs recalculation */
	bool bTurretTargetDirty = true;

	/** Mark turret target cache as dirty (called on awareness/detection events) */
	void InvalidateTurretTargetCache() { bTurretTargetDirty = true; }

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

	/** Timer tracking how long current recovery has been running (seconds) */
	float RecoveryTimer = 0.0f;

	// ========== DAMAGE RESPONSE STATE ==========

	/** Currently responding to damage (turret tracking instigator) */
	bool bIsRespondingToDamage = false;

	/** Timer for damage response duration */
	float DamageResponseTimer = 0.0f;

	/** Location of the damage instigator (attacker's pawn position) */
	FVector DamageInstigatorLocation = FVector::ZeroVector;

	/** Weak reference to damage instigator actor (for tracking moving targets) */
	TWeakObjectPtr<AActor> DamageInstigatorActor;

	// ========== CIRCLE DETECTION STATE ==========

	/** Whether tank is circling (moving but not making progress toward waypoint) */
	bool bIsCircling = false;

	/** Timer tracking time without waypoint progress */
	float NoProgressTimer = 0.0f;

	/** Timer for periodic distance sampling */
	float ProgressSampleTimer = 0.0f;

	/** Last sampled distance to current waypoint (for progress comparison) */
	float LastSampledDistanceToWaypoint = 0.0f;

	/** Whether we have a valid initial distance sample */
	bool bHasDistanceSample = false;

	/** Accumulated steering values for bias detection (positive = right, negative = left) */
	float SteeringAccumulator = 0.0f;

	/** Total time accumulated for steering bias calculation */
	float SteeringAccumulatorTime = 0.0f;


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
