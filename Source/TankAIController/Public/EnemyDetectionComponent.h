// Copyright ArenaBreakers. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnemyDetectionTypes.h"
#include "EnemyDetectionComponent.generated.h"

// Forward declarations
class AWR_Tank_Pawn;

// ========== DELEGATES ==========

/** Called when a new enemy is first detected (awareness > 0) */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEnemyDetected, AActor*, Enemy, const FDetectedEnemyInfo&, Info);

/** Called when enemy is completely lost (awareness = 0 and memory expired) */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemyLost, AActor*, Enemy);

/** Called when awareness state changes for an enemy */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnAwarenessStateChanged, AActor*, Enemy, EAwarenessState, OldState, EAwarenessState, NewState);

/** Called when enemy enters/exits firing cone */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEnemyInFiringCone, AActor*, Enemy, bool, bInCone);

/**
 * Enemy Detection Component for AI Tank Drones
 *
 * Handles multi-raycast visibility checks, awareness tracking, and enemy memory.
 * Detection originates from the turret position and follows turret direction.
 *
 * FEATURES:
 * - Multi-raycast partial visibility detection (detects enemies half behind cover)
 * - Gradual awareness system (Unaware -> Suspicious -> Alerted -> Combat)
 * - FOV with peripheral vision
 * - Distance-based detection falloff
 * - Enemy memory after losing sight
 * - Turret-based detection origin and direction
 *
 * MULTIPLAYER:
 * - Server-authoritative detection (all logic runs on server)
 * - Results replicated only to owning client (bandwidth optimization)
 * - Custom NetSerialize for struct compression
 * - Uses COND_OwnerOnly replication
 *
 * PERFORMANCE OPTIMIZATIONS:
 * - Layered detection: Distance -> FOV -> Raycast (cheapest to most expensive)
 * - Temporal distribution: Not all enemies checked every frame
 * - LOD system: Closer enemies get more frequent updates
 * - Potential target caching with periodic refresh
 * - Collision channel filtering for fast raycasts
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TANKAICONTROLLER_API UEnemyDetectionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyDetectionComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	// ========== CONFIGURATION ==========

	/** Detection configuration parameters */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Config")
	FDetectionConfig DetectionConfig;

	/** Eye/sensor socket name on owning actor's mesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Config")
	FName EyeSocketName = NAME_None;

	/** Fallback eye offset if socket doesn't exist (local space) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Config")
	FVector EyeOffset = FVector(0, 0, 80);

	/** Team ID for friend/foe identification. -1 = no team (detect all other actors) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Detection|Config")
	int32 TeamID = -1;

	/** Maximum number of enemies to track simultaneously */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Config", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxTrackedEnemies = 8;

	// ========== PERFORMANCE ==========

	/** Base update interval for detection scan (seconds). 0 = every frame */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Performance", meta = (ClampMin = "0", ClampMax = "0.5"))
	float DetectionUpdateInterval = 0.1f;

	/** Interval for refreshing potential target list (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Performance", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float PotentialTargetRefreshInterval = 0.5f;

	/** Enable LOD system - closer enemies are checked more frequently */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Performance")
	bool bUseLODSystem = true;

	/** Maximum raycasts per frame (distributed across enemies) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Performance", meta = (ClampMin = "4", ClampMax = "64"))
	int32 MaxRaycastsPerFrame = 24;

	// ========== DEBUG ==========

	/** Draw debug visualization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Debug")
	bool bDrawDebug = false;

	/** Draw debug for N seconds (0 = single frame) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Debug", meta = (ClampMin = "0", ClampMax = "5"))
	float DebugDrawDuration = 0.0f;

	// ========== EVENTS ==========

	/** Fired when a new enemy is first detected */
	UPROPERTY(BlueprintAssignable, Category = "Detection|Events")
	FOnEnemyDetected OnEnemyDetected;

	/** Fired when enemy is completely lost from memory */
	UPROPERTY(BlueprintAssignable, Category = "Detection|Events")
	FOnEnemyLost OnEnemyLost;

	/** Fired when awareness state changes */
	UPROPERTY(BlueprintAssignable, Category = "Detection|Events")
	FOnAwarenessStateChanged OnAwarenessStateChanged;

	/** Fired when enemy enters or exits firing cone */
	UPROPERTY(BlueprintAssignable, Category = "Detection|Events")
	FOnEnemyInFiringCone OnEnemyInFiringCone;

	// ========== PUBLIC API ==========

	/** Get all currently detected enemies */
	UFUNCTION(BlueprintPure, Category = "Detection")
	const TArray<FDetectedEnemyInfo>& GetDetectedEnemies() const { return DetectedEnemies; }

	/** Get the highest priority target (Combat state + in firing cone + closest) */
	UFUNCTION(BlueprintPure, Category = "Detection")
	bool GetPriorityTarget(FDetectedEnemyInfo& OutInfo) const;

	/** Get all enemies currently in Combat state */
	UFUNCTION(BlueprintPure, Category = "Detection")
	TArray<FDetectedEnemyInfo> GetCombatTargets() const;

	/** Get all enemies currently in firing cone */
	UFUNCTION(BlueprintPure, Category = "Detection")
	TArray<FDetectedEnemyInfo> GetEnemiesInFiringCone() const;

	/** Check if a specific actor is detected and get info */
	UFUNCTION(BlueprintPure, Category = "Detection")
	bool IsActorDetected(AActor* Actor, FDetectedEnemyInfo& OutInfo) const;

	/** Get count of detected enemies by awareness state */
	UFUNCTION(BlueprintPure, Category = "Detection")
	int32 GetEnemyCountByState(EAwarenessState State) const;

	/** Get total detected enemy count */
	UFUNCTION(BlueprintPure, Category = "Detection")
	int32 GetDetectedEnemyCount() const { return DetectedEnemies.Num(); }

	/** Check if any enemy is in Combat state */
	UFUNCTION(BlueprintPure, Category = "Detection")
	bool HasCombatTarget() const;

	/** Check if any enemy is in firing cone */
	UFUNCTION(BlueprintPure, Category = "Detection")
	bool HasTargetInFiringCone() const;

	// ========== DETECTION CONTROL ==========

	/** Force immediate full detection update (expensive, use sparingly) */
	UFUNCTION(BlueprintCallable, Category = "Detection")
	void ForceDetectionUpdate();

	/** Manually report enemy contact (e.g., when taking damage from unknown source) */
	UFUNCTION(BlueprintCallable, Category = "Detection")
	void ReportEnemyContact(AActor* Enemy, float InitialAwareness = 0.5f);

	/** Clear all detected enemies */
	UFUNCTION(BlueprintCallable, Category = "Detection")
	void ClearAllDetections();

	/** Enable/disable detection updates */
	UFUNCTION(BlueprintCallable, Category = "Detection")
	void SetDetectionEnabled(bool bEnabled);

	/** Is detection currently enabled? */
	UFUNCTION(BlueprintPure, Category = "Detection")
	bool IsDetectionEnabled() const { return bDetectionEnabled; }

	// ========== UTILITY ==========

	/** Get eye/sensor location for detection raycasts (turret position) */
	UFUNCTION(BlueprintPure, Category = "Detection")
	FVector GetEyeLocation() const;

	/** Get look direction (turret forward vector, used for FOV checks) */
	UFUNCTION(BlueprintPure, Category = "Detection")
	FVector GetLookDirection() const;

	/** Calculate visibility to a specific target (0-1, for debugging/testing) */
	UFUNCTION(BlueprintCallable, Category = "Detection")
	float CalculateVisibilityTo(AActor* Target) const;

	// ========== AI OBSERVATION HELPERS (for Learning Agents integration) ==========

	/** Get nearest enemy distance normalized (0 = at position, 1 = at max range) */
	UFUNCTION(BlueprintPure, Category = "Detection|Observations")
	float GetNearestEnemyDistanceNormalized() const;

	/** Get nearest enemy angle normalized (-1 = left, 0 = front, 1 = right) */
	UFUNCTION(BlueprintPure, Category = "Detection|Observations")
	float GetNearestEnemyAngleNormalized() const;

	/** Get threat level in 8 directional sectors (N, NE, E, SE, S, SW, W, NW) */
	UFUNCTION(BlueprintPure, Category = "Detection|Observations")
	TArray<float> GetSectorThreatLevels() const;

	/** Get highest awareness level among all detected enemies */
	UFUNCTION(BlueprintPure, Category = "Detection|Observations")
	float GetMaxAwarenessLevel() const;

protected:
	// ========== REPLICATED STATE ==========

	/** Currently detected enemies - replicated to owning client only */
	UPROPERTY(ReplicatedUsing = OnRep_DetectedEnemies)
	TArray<FDetectedEnemyInfo> DetectedEnemies;

	UFUNCTION()
	void OnRep_DetectedEnemies();

	// ========== INTERNAL STATE ==========

	/** Is detection currently enabled - DISABLED by default, enabled when inference mode starts (NumPad 7) */
	bool bDetectionEnabled = false;

	/** Time accumulator for detection updates */
	float DetectionUpdateTimer = 0.0f;

	/** Cached potential targets (refreshed periodically) */
	TArray<TWeakObjectPtr<AActor>> CachedPotentialTargets;

	/** Timer for potential target refresh */
	float PotentialTargetRefreshTimer = 0.0f;

	/** Frame counter for LOD distribution */
	uint32 FrameCounter = 0;

	/** Raycast budget remaining this frame */
	int32 RemainingRaycastBudget = 0;

	/** Index for round-robin enemy checking when budget limited */
	int32 EnemyCheckIndex = 0;

	// ========== INTERNAL METHODS ==========

	/** Main detection update loop (server only) */
	void UpdateDetection(float DeltaTime);

	/** Refresh list of potential targets within range */
	void RefreshPotentialTargets();

	/** Calculate visibility to target using multi-raycast */
	float CalculateVisibilityToTarget(AActor* Target, FVector& OutBestVisibleLocation, uint8& OutVisibleMask) const;

	/** Get socket location on target for visibility check */
	FVector GetTargetSocketLocation(AActor* Target, const FDetectionSocket& Socket) const;

	/** Check if target location is within detection FOV */
	bool IsInDetectionFOV(const FVector& TargetLocation, float& OutAngle, float& OutEffectiveness) const;

	/** Update awareness for a detected enemy */
	void UpdateEnemyAwareness(FDetectedEnemyInfo& Info, float Visibility, float DeltaTime);

	/** Convert awareness level to state enum */
	EAwarenessState GetAwarenessStateFromLevel(float AwarenessLevel) const;

	/** Get detection priority (LOD) for an enemy based on distance */
	EDetectionPriority GetDetectionPriority(float Distance) const;

	/** Should this enemy be checked this frame based on LOD? */
	bool ShouldCheckEnemyThisFrame(EDetectionPriority Priority) const;

	/** Check if actor is enemy (different team) */
	bool IsEnemy(AActor* Actor) const;

	/** Find existing entry or create new one for enemy */
	FDetectedEnemyInfo* FindOrCreateEnemyInfo(AActor* Enemy);

	/** Remove enemy from detected list */
	void RemoveEnemy(AActor* Enemy);

	/** Cleanup invalid/expired entries */
	void CleanupStaleEntries(float DeltaTime);

	/** Draw debug visualization */
	void DrawDebugVisualization() const;

	/** Get cached tank pawn reference */
	AWR_Tank_Pawn* GetOwnerTank() const;

	/** Get both eye location and look direction in a single call (more efficient) */
	void GetEyeLocationAndDirection(FVector& OutLocation, FVector& OutDirection) const;

private:
	/** Cached owner tank reference */
	mutable TWeakObjectPtr<AWR_Tank_Pawn> CachedOwnerTank;

	/** Cached turret reference for detection origin/direction */
	mutable TWeakObjectPtr<AActor> CachedTurret;

	/** Has turret setup been logged (one-time log) */
	mutable bool bTurretSetupLogged = false;

	/** Ensure turret is cached and return it. Returns nullptr if turret not found. */
	class AWR_Turret* EnsureTurretCached() const;
};
