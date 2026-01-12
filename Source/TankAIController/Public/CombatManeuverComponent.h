// Copyright ArenaBreakers. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CombatManeuverTypes.h"
#include "EnemyDetectionTypes.h"
#include "CombatManeuverComponent.generated.h"

// Forward declarations
class UEnemyDetectionComponent;
class UTankWaypointComponent;
class AWR_Tank_Pawn;
class ABaseTankAIController;

// ============================================================================
// DELEGATES
// ============================================================================

/** Called when combat state changes */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCombatStateChanged, ECombatState, OldState, ECombatState, NewState);

/** Called when a new maneuver starts */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnManeuverStarted, const FCombatManeuver&, Maneuver);

/** Called when a maneuver completes (success or failure) */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnManeuverCompleted, const FCombatManeuver&, Maneuver, bool, bSuccess);

/** Called when advancing to next waypoint in maneuver */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWaypointAdvanced, int32, NewIndex, const FCombatWaypoint&, Waypoint);

/**
 * Combat Maneuver Component
 *
 * Handles tactical decision making using rule-based scoring system.
 * Selects appropriate combat maneuvers based on situational factors
 * and generates waypoints for execution.
 *
 * FEATURES:
 * - Rule-based maneuver selection (no ML required)
 * - Configurable weight matrix for each maneuver
 * - Integration with EnemyDetectionComponent
 * - Combat waypoint generation
 * - Debug visualization
 *
 * MULTIPLAYER:
 * - Server-authoritative: All combat logic runs on server only
 * - Combat state is NOT replicated (use events for client feedback if needed)
 * - Works with server-controlled AI tanks
 *
 * USAGE:
 * 1. Add component to AAILearningAgentsController
 * 2. Configure maneuver weights in editor (defaults set in constructor)
 * 3. Component automatically assesses situation and selects maneuvers
 * 4. Generated waypoints are pushed to TankWaypointComponent
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TANKAICONTROLLER_API UCombatManeuverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatManeuverComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	// ========================================================================
	// CONFIGURATION
	// ========================================================================

	/** Enable combat maneuver system */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config")
	bool bEnabled = true;

	/** Minimum enemy awareness to trigger Alert state */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config")
	EAwarenessState MinAwarenessForAlert = EAwarenessState::Suspicious;

	/** Minimum enemy awareness to trigger Combat state */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config")
	EAwarenessState MinAwarenessForCombat = EAwarenessState::Alerted;

	/** How often to re-evaluate maneuver selection (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float ManeuverReevaluationInterval = 2.0f;

	/** Minimum score difference to switch maneuvers mid-execution */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ManeuverSwitchThreshold = 0.5f;

	/** Time after taking damage to consider "under fire" (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float UnderFireDuration = 3.0f;

	/** Cooldown time after completing a maneuver before it can be selected again (seconds).
	 *  Prevents the same maneuver from being immediately re-selected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float ManeuverCooldownDuration = 10.0f;

	// ========================================================================
	// DISTANCE THRESHOLDS
	// ========================================================================

	/** Distance considered "close range" (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Thresholds")
	float CloseRangeDistance = 1500.0f;

	/** Distance considered "long range" (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Thresholds")
	float LongRangeDistance = 4000.0f;

	/** Threshold for "open terrain" - average trace distance (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Thresholds")
	float OpenTerrainThreshold = 800.0f;

	/** Minimum clear distance for flank path (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Thresholds")
	float FlankPathClearDistance = 500.0f;

	/** Minimum clear distance for retreat path (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Thresholds")
	float RetreatPathClearDistance = 400.0f;

	/** Health threshold for "low health" (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Thresholds")
	float LowHealthThreshold = 0.3f;

	/** Health threshold for "high health" (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Thresholds")
	float HighHealthThreshold = 0.7f;

	// ========================================================================
	// MANEUVER WEIGHTS (Editor Configurable)
	// ========================================================================

	/** Score weights for Flanking maneuver */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Weights")
	FManeuverScoreConfig FlankingConfig;

	/** Score weights for Tactical Retreat maneuver */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Weights")
	FManeuverScoreConfig RetreatConfig;

	/** Score weights for Hull-Down maneuver */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Weights")
	FManeuverScoreConfig HullDownConfig;

	/** Score weights for Zigzag Evasion maneuver */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Weights")
	FManeuverScoreConfig ZigzagConfig;

	/** Score weights for Shoot and Scoot maneuver */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Weights")
	FManeuverScoreConfig ShootScootConfig;

	/** Score weights for Charge Attack maneuver */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Weights")
	FManeuverScoreConfig ChargeConfig;

	/** Score weights for Circle Strafe maneuver */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Weights")
	FManeuverScoreConfig CircleStrafeConfig;

	// ========================================================================
	// MANEUVER GENERATION PARAMETERS
	// ========================================================================

	/** Lateral distance for flanking maneuver (cm) - will be dynamically adjusted */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Flanking")
	float FlankingLateralDistance = 600.0f;

	/** Final approach distance for flanking (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Flanking")
	float FlankingApproachDistance = 400.0f;

	/** Minimum zigzag lateral distance (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Zigzag")
	float ZigzagMinDistance = 300.0f;

	/** Maximum zigzag lateral distance (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Zigzag")
	float ZigzagMaxDistance = 600.0f;

	/** Forward progress per zigzag segment (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Zigzag")
	float ZigzagForwardDistance = 400.0f;

	/** Number of zigzag waypoints to generate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Zigzag", meta = (ClampMin = "3", ClampMax = "10"))
	int32 ZigzagWaypointCount = 5;

	/** Circle strafe radius (cm) - will be dynamically adjusted based on available space */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|CircleStrafe")
	float CircleStrafeRadius = 800.0f;

	/** Number of waypoints in circle strafe arc */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|CircleStrafe", meta = (ClampMin = "4", ClampMax = "12"))
	int32 CircleStrafeWaypointCount = 6;

	/** Shoot and scoot relocation distance (cm) - will be dynamically adjusted */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|ShootScoot")
	float ShootScootRelocationDistance = 500.0f;

	/** Time to fire before scooting (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|ShootScoot")
	float ShootScootFireDuration = 3.0f;

	/** Cover search radius (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Cover")
	float CoverSearchRadius = 3000.0f;

	/** Minimum distance from current position for cover to be considered (cm).
	 *  Prevents selecting cover that is too close or the same location tank is already at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Cover")
	float MinCoverDistance = 300.0f;

	/** Total retreat distance (cm) - will be dynamically adjusted based on clearance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Retreat")
	float RetreatDistance = 800.0f;

	/** Number of retreat waypoints */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Retreat", meta = (ClampMin = "2", ClampMax = "5"))
	int32 RetreatWaypointCount = 3;

	/** Retreat angle offset from directly behind (degrees).
	 *  0 = straight back (180°), 30 = diagonal back (150°/210°).
	 *  Using diagonal allows neural network to navigate without 180° turn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Retreat", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float RetreatAngleOffset = 35.0f;

	// ========================================================================
	// DYNAMIC DISTANCE SETTINGS
	// ========================================================================

	/** Safety factor for dynamic distance calculation (0.7 = use 70% of available clearance) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|DynamicDistance", meta = (ClampMin = "0.5", ClampMax = "0.95"))
	float DynamicDistanceSafetyFactor = 0.75f;

	/** Minimum maneuver distance regardless of clearance (cm).
	 *  Should be at least 300cm to ensure waypoints aren't placed too close together. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|DynamicDistance", meta = (ClampMin = "200.0", ClampMax = "600.0"))
	float MinManeuverDistance = 300.0f;

	// ========================================================================
	// DEBUG
	// ========================================================================

	/** Draw debug visualization of maneuvers and waypoints */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Debug")
	bool bDrawDebug = true;

	/** Log maneuver selection decisions */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Debug")
	bool bLogManeuverSelection = true;

	/** Duration for debug drawing (0 = single frame) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Debug")
	float DebugDrawDuration = 0.0f;

	// ========================================================================
	// EVENTS
	// ========================================================================

	/** Called when combat state changes */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnCombatStateChanged OnCombatStateChanged;

	/** Called when a new maneuver starts */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnManeuverStarted OnManeuverStarted;

	/** Called when a maneuver completes */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnManeuverCompleted OnManeuverCompleted;

	/** Called when advancing to next waypoint */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Events")
	FOnWaypointAdvanced OnWaypointAdvanced;

	// ========================================================================
	// PUBLIC API - STATE
	// ========================================================================

	/** Get current combat state */
	UFUNCTION(BlueprintPure, Category = "Combat|State")
	ECombatState GetCombatState() const { return CurrentCombatState; }

	/** Get current maneuver being executed */
	UFUNCTION(BlueprintPure, Category = "Combat|State")
	const FCombatManeuver& GetCurrentManeuver() const { return CurrentManeuver; }

	/** Check if actively executing a maneuver */
	UFUNCTION(BlueprintPure, Category = "Combat|State")
	bool IsExecutingManeuver() const { return bExecutingManeuver; }

	/** Get current waypoint index in active maneuver */
	UFUNCTION(BlueprintPure, Category = "Combat|State")
	int32 GetCurrentWaypointIndex() const { return CurrentWaypointIndex; }

	/** Get current waypoint (or nullptr if none) */
	const FCombatWaypoint* GetCurrentWaypoint() const;

	/** Force transition to specific combat state */
	UFUNCTION(BlueprintCallable, Category = "Combat|State")
	void SetCombatState(ECombatState NewState);

	// ========================================================================
	// PUBLIC API - MANEUVER CONTROL
	// ========================================================================

	/** Request specific maneuver (for testing or manual override) */
	UFUNCTION(BlueprintCallable, Category = "Combat|Control")
	bool RequestManeuver(ECombatManeuverType ManeuverType, AActor* TargetActor = nullptr);

	/** Cancel current maneuver */
	UFUNCTION(BlueprintCallable, Category = "Combat|Control")
	void CancelManeuver();

	/** Force re-evaluation of maneuver selection */
	UFUNCTION(BlueprintCallable, Category = "Combat|Control")
	void ForceReevaluation();

	/** Notify component that damage was taken (triggers "under fire" state) */
	UFUNCTION(BlueprintCallable, Category = "Combat|Control")
	void NotifyDamageTaken(float DamageAmount, AActor* DamageSource);

	// ========================================================================
	// PUBLIC API - SITUATION ASSESSMENT
	// ========================================================================

	/** Assess current combat situation from available data */
	UFUNCTION(BlueprintPure, Category = "Combat|Assessment")
	FCombatSituation AssessCurrentSituation() const;

	/** Get last assessed situation (cached) */
	UFUNCTION(BlueprintPure, Category = "Combat|Assessment")
	const FCombatSituation& GetLastSituation() const { return LastSituation; }

	// ========================================================================
	// PUBLIC API - MANEUVER SELECTION
	// ========================================================================

	/** Select best maneuver using scoring system */
	UFUNCTION(BlueprintCallable, Category = "Combat|Selection")
	FManeuverSelectionResult SelectBestManeuver(const FCombatSituation& Situation);

	/** Calculate score for a specific maneuver */
	UFUNCTION(BlueprintPure, Category = "Combat|Selection")
	float CalculateManeuverScore(ECombatManeuverType ManeuverType, const FCombatSituation& Situation) const;

	/** Check if maneuver is valid for current situation */
	UFUNCTION(BlueprintPure, Category = "Combat|Selection")
	bool IsManeuverValid(ECombatManeuverType ManeuverType, const FCombatSituation& Situation) const;

	// ========================================================================
	// PUBLIC API - WAYPOINT GENERATION
	// ========================================================================

	/** Generate waypoints for specified maneuver type */
	UFUNCTION(BlueprintCallable, Category = "Combat|Waypoints")
	TArray<FCombatWaypoint> GenerateManeuverWaypoints(ECombatManeuverType ManeuverType, FVector EnemyPosition);

	UFUNCTION(BlueprintCallable, Category = "Combat|Waypoints")
	TArray<FCombatWaypoint> GenerateFlankingWaypoints(FVector EnemyPosition);

	UFUNCTION(BlueprintCallable, Category = "Combat|Waypoints")
	TArray<FCombatWaypoint> GenerateRetreatWaypoints(FVector EnemyPosition);

	UFUNCTION(BlueprintCallable, Category = "Combat|Waypoints")
	TArray<FCombatWaypoint> GenerateHullDownWaypoints(FVector EnemyPosition);

	UFUNCTION(BlueprintCallable, Category = "Combat|Waypoints")
	TArray<FCombatWaypoint> GenerateZigzagWaypoints(FVector EnemyPosition);

	UFUNCTION(BlueprintCallable, Category = "Combat|Waypoints")
	TArray<FCombatWaypoint> GenerateShootScootWaypoints(FVector EnemyPosition);

	UFUNCTION(BlueprintCallable, Category = "Combat|Waypoints")
	TArray<FCombatWaypoint> GenerateCircleStrafeWaypoints(FVector EnemyPosition);

	UFUNCTION(BlueprintCallable, Category = "Combat|Waypoints")
	TArray<FCombatWaypoint> GenerateChargeWaypoints(FVector EnemyPosition);

	// ========================================================================
	// PUBLIC API - COVER DETECTION
	// ========================================================================

	/** Find nearest cover position from threat */
	UFUNCTION(BlueprintPure, Category = "Combat|Cover")
	FVector FindNearestCover(FVector FromPosition, FVector ThreatDirection) const;

	/** Check if position provides cover from threat */
	UFUNCTION(BlueprintPure, Category = "Combat|Cover")
	bool IsPositionInCover(FVector Position, FVector ThreatPosition) const;

	/** Find multiple cover positions */
	UFUNCTION(BlueprintPure, Category = "Combat|Cover")
	TArray<FVector> FindCoverPositions(FVector ThreatPosition, int32 MaxResults = 5) const;

	// ========================================================================
	// PUBLIC API - DYNAMIC DISTANCE CALCULATION
	// ========================================================================

	/**
	 * Get clearance distance in a specific direction from line traces.
	 * @param DirectionIndex - 0=front, 6=right, 12=rear, 18=left (0-23 for 24-trace pattern)
	 * @return Clearance distance in cm, or max trace distance if no obstacle
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Distance")
	float GetDirectionalClearance(int32 DirectionIndex) const;

	/**
	 * Get clearance in a world direction by finding nearest trace index.
	 * @param WorldDirection - Direction vector in world space
	 * @return Clearance distance in cm
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Distance")
	float GetClearanceInDirection(FVector WorldDirection) const;

	/**
	 * Calculate dynamic maneuver distance based on environment.
	 * Returns MIN(configured distance, available clearance * safety factor).
	 *
	 * @param ManeuverType - Type of maneuver
	 * @param Direction - World direction for the maneuver
	 * @param ConfiguredDistance - Default/configured distance for this maneuver
	 * @return Safe maneuver distance in cm
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Distance")
	float CalculateDynamicDistance(FVector Direction, float ConfiguredDistance) const;

	// ========================================================================
	// PUBLIC API - REFERENCES
	// ========================================================================

	/** Set external references (called by owning controller) */
	UFUNCTION(BlueprintCallable, Category = "Combat|Setup")
	void SetReferences(UEnemyDetectionComponent* InEnemyDetection, UTankWaypointComponent* InWaypointComponent);

protected:
	// ========================================================================
	// STATE (Server-only, not replicated)
	// ========================================================================

	/** Current combat state */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	ECombatState CurrentCombatState = ECombatState::Patrol;

	/** Current maneuver being executed */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	FCombatManeuver CurrentManeuver;

	/** Is a maneuver currently being executed? */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	bool bExecutingManeuver = false;

	/** Current waypoint index in active maneuver */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	int32 CurrentWaypointIndex = 0;

	/** Timer for maneuver re-evaluation */
	float ManeuverReevaluationTimer = 0.0f;

	/** Timer for "under fire" state */
	float UnderFireTimer = 0.0f;

	/** Timer for current waypoint wait */
	float WaypointWaitTimer = 0.0f;

	/** Timer for path re-generation when NavMesh completes but waypoint not reached */
	float PathRegenerationTimer = 0.0f;

	/** Maximum time to wait before regenerating path (seconds) */
	static constexpr float PathRegenerationInterval = 1.0f;

	/** Last assessed situation */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	FCombatSituation LastSituation;

	/** Last selection result for debugging */
	UPROPERTY(BlueprintReadOnly, Category = "Combat|State")
	FManeuverSelectionResult LastSelectionResult;

	/** Cooldown timers per maneuver type (seconds remaining) */
	TMap<ECombatManeuverType, float> ManeuverCooldowns;

	// ========================================================================
	// CACHED REFERENCES
	// ========================================================================

	/** Enemy detection component reference */
	UPROPERTY()
	TWeakObjectPtr<UEnemyDetectionComponent> EnemyDetection;

	/** Waypoint component reference */
	UPROPERTY()
	TWeakObjectPtr<UTankWaypointComponent> WaypointComponent;

	/** Owner tank pawn reference */
	UPROPERTY()
	TWeakObjectPtr<AWR_Tank_Pawn> OwnerTank;

	/** Owner controller reference */
	UPROPERTY()
	TWeakObjectPtr<ABaseTankAIController> OwnerController;

	// ========================================================================
	// INTERNAL METHODS - INITIALIZATION
	// ========================================================================

	/** Initialize default weights for all maneuvers */
	void InitializeDefaultWeights();

	/** Cache references to related components */
	void CacheReferences();

	// ========================================================================
	// INTERNAL METHODS - UPDATE
	// ========================================================================

	/** Update combat state based on enemy detection */
	void UpdateCombatState(float DeltaTime);

	/** Update maneuver execution */
	void UpdateManeuverExecution(float DeltaTime);

	/** Update "under fire" timer */
	void UpdateUnderFireState(float DeltaTime);

	/** Update maneuver cooldown timers */
	void UpdateCooldowns(float DeltaTime);

	/** Check if a maneuver type is on cooldown */
	bool IsManeuverOnCooldown(ECombatManeuverType ManeuverType) const;

	/** Evaluate situation and potentially select new maneuver */
	void EvaluateAndSelectManeuver();

	// ========================================================================
	// INTERNAL METHODS - STATE TRANSITIONS
	// ========================================================================

	/** Transition to new combat state */
	void TransitionToState(ECombatState NewState);

	/** Start executing a maneuver */
	void StartManeuver(const FCombatManeuver& Maneuver);

	/** Complete current maneuver */
	void CompleteManeuver(bool bSuccess);

	/** Advance to next waypoint in current maneuver */
	void AdvanceToNextWaypoint();

	/** Check if current waypoint is reached */
	bool IsCurrentWaypointReached() const;

	/** Push current waypoint to navigation system */
	void PushCurrentWaypointToNavigation();

	// ========================================================================
	// INTERNAL METHODS - HELPERS
	// ========================================================================

	/** Get owner location */
	FVector GetOwnerLocation() const;

	/** Get owner rotation */
	FRotator GetOwnerRotation() const;

	/** Get owner forward vector */
	FVector GetOwnerForward() const;

	/** Get line trace distances from controller */
	const TArray<float>& GetLineTraceDistances() const;

	/** Get config for specific maneuver type */
	const FManeuverScoreConfig& GetConfigForManeuver(ECombatManeuverType ManeuverType) const;

	/** Calculate score using config and situation */
	float CalculateScoreInternal(const FManeuverScoreConfig& Config, const FCombatSituation& Situation) const;

	/** Evaluate which flank direction is clearer - returns true if right is clearer */
	bool EvaluateFlankDirection() const;

	/** Check if position is navigable (on NavMesh) */
	bool IsPositionNavigable(FVector Position) const;

	/** Project position to NavMesh */
	FVector ProjectToNavMesh(FVector Position) const;

	// ========================================================================
	// INTERNAL METHODS - DEBUG
	// ========================================================================

	/** Draw debug visualization */
	void DrawDebugVisualization() const;

	/** Draw debug scores for all maneuvers */
	void DrawDebugScores(const FCombatSituation& Situation) const;

	/** Draw debug waypoints for current maneuver */
	void DrawDebugWaypoints() const;

	/** Log maneuver selection to output */
	void LogManeuverSelection(const FManeuverSelectionResult& Result) const;
};
