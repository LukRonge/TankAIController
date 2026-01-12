// Copyright ArenaBreakers. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AIShootingTypes.h"
#include "AIShootingComponent.generated.h"

// Forward declarations
class AWR_Tank_Pawn;
class AWR_Turret;
class UEnemyDetectionComponent;
struct FDetectedEnemyInfo;

// ============================================================================
// DELEGATES
// ============================================================================

/** Fired when AI starts engaging a target */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEngagementStarted, AActor*, Target, EWeaponSlot, Weapon);

/** Fired when AI stops engaging */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEngagementEnded, AActor*, Target, EEngagementEndReason, Reason);

/** Fired when weapon is switched */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeaponSwitched, EWeaponSlot, OldWeapon, EWeaponSlot, NewWeapon);

/** Fired when AI fires a shot */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnShotFired, EWeaponSlot, Weapon, FVector, AimLocation, bool, bIntentionalMiss);

/**
 * AI Shooting Component - Human-like shooting behavior for AI tanks
 *
 * Handles weapon selection, accuracy simulation, reaction times, and target prediction
 * to create believable combat encounters.
 *
 * FEATURES:
 * - Reaction time delay before first shot
 * - Accuracy spread that improves over time (zeroing)
 * - Intentional miss system for human-like errors
 * - Smart weapon selection (primary vs secondary)
 * - Target prediction (lead calculation) for moving targets
 * - Grenade arc compensation for ballistic weapons
 * - Difficulty presets (Easy to Veteran)
 * - Burst fire control
 *
 * USAGE:
 * 1. Add component to AI controller
 * 2. Call SetTarget() when enemy is detected
 * 3. Call Update() each tick
 * 4. Query ShouldFirePrimary() and ShouldFireSecondary() for fire commands
 * 5. Use GetAdjustedAimLocation() for turret targeting
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TANKAICONTROLLER_API UAIShootingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAIShootingComponent();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	// ============================================================================
	// CONFIGURATION
	// ============================================================================

	/** Difficulty preset - affects all shooting parameters */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting|Config")
	EAIDifficulty Difficulty = EAIDifficulty::Medium;

	/** Shooting configuration - auto-populated from difficulty preset unless Custom */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting|Config")
	FAIShootingConfig Config;

	/** Enable shooting system */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting|Config")
	bool bShootingEnabled = true;

	/** Enable secondary weapon usage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting|Config")
	bool bUseSecondaryWeapon = true;

	/** Enable target lead prediction for moving targets */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting|Config")
	bool bUseTargetPrediction = true;

	/** Enable grenade arc compensation for secondary weapon */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting|Config")
	bool bUseGrenadeArcCompensation = true;

	// ============================================================================
	// DEBUG
	// ============================================================================

	/** Draw debug visualization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting|Debug")
	bool bDrawDebug = false;

	/** Debug draw duration (0 = single frame) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shooting|Debug", meta = (ClampMin = "0", ClampMax = "5"))
	float DebugDrawDuration = 0.0f;

	// ============================================================================
	// EVENTS
	// ============================================================================

	/** Fired when engagement starts */
	UPROPERTY(BlueprintAssignable, Category = "Shooting|Events")
	FOnEngagementStarted OnEngagementStarted;

	/** Fired when engagement ends */
	UPROPERTY(BlueprintAssignable, Category = "Shooting|Events")
	FOnEngagementEnded OnEngagementEnded;

	/** Fired when weapon is switched */
	UPROPERTY(BlueprintAssignable, Category = "Shooting|Events")
	FOnWeaponSwitched OnWeaponSwitched;

	/** Fired when a shot is fired */
	UPROPERTY(BlueprintAssignable, Category = "Shooting|Events")
	FOnShotFired OnShotFired;

	// ============================================================================
	// PUBLIC API - TARGET MANAGEMENT
	// ============================================================================

	/** Set current target - call when enemy detection acquires target */
	UFUNCTION(BlueprintCallable, Category = "Shooting")
	void SetTarget(AActor* NewTarget, const FDetectedEnemyInfo& EnemyInfo);

	/** Clear current target - call when target is lost */
	UFUNCTION(BlueprintCallable, Category = "Shooting")
	void ClearTarget(EEngagementEndReason Reason = EEngagementEndReason::TargetLost);

	/** Check if currently has a target */
	UFUNCTION(BlueprintPure, Category = "Shooting")
	bool HasTarget() const { return CurrentTarget.IsValid(); }

	/** Get current target actor */
	UFUNCTION(BlueprintPure, Category = "Shooting")
	AActor* GetCurrentTarget() const { return CurrentTarget.Get(); }

	// ============================================================================
	// PUBLIC API - FIRE CONTROL
	// ============================================================================

	/** Should AI fire primary weapon this frame? */
	UFUNCTION(BlueprintPure, Category = "Shooting")
	bool ShouldFirePrimary() const { return ShootingState.bIsFiringPrimary; }

	/** Should AI fire secondary weapon this frame? */
	UFUNCTION(BlueprintPure, Category = "Shooting")
	bool ShouldFireSecondary() const { return ShootingState.bIsFiringSecondary; }

	/** Get adjusted aim location (includes spread, lead, arc compensation) */
	UFUNCTION(BlueprintPure, Category = "Shooting")
	FVector GetAdjustedAimLocation() const { return ShootingState.AdjustedAimLocation; }

	/** Get currently selected weapon */
	UFUNCTION(BlueprintPure, Category = "Shooting")
	EWeaponSlot GetSelectedWeapon() const { return ShootingState.SelectedWeapon; }

	/** Force weapon switch */
	UFUNCTION(BlueprintCallable, Category = "Shooting")
	void SetSelectedWeapon(EWeaponSlot NewWeapon);

	// ============================================================================
	// PUBLIC API - STATE QUERIES
	// ============================================================================

	/** Get current shooting state */
	UFUNCTION(BlueprintPure, Category = "Shooting")
	EShootingState GetShootingState() const { return ShootingState.State; }

	/** Get full shooting state struct */
	UFUNCTION(BlueprintPure, Category = "Shooting")
	const FAIShootingState& GetShootingStateData() const { return ShootingState; }

	/** Get current accuracy spread in degrees */
	UFUNCTION(BlueprintPure, Category = "Shooting")
	float GetCurrentSpread() const { return ShootingState.CurrentSpread; }

	/** Get remaining reaction time */
	UFUNCTION(BlueprintPure, Category = "Shooting")
	float GetReactionTimeRemaining() const { return ShootingState.ReactionTimeRemaining; }

	/** Has target been acquired (reaction time elapsed)? */
	UFUNCTION(BlueprintPure, Category = "Shooting")
	bool IsTargetAcquired() const { return ShootingState.bTargetAcquired; }

	/** Get time spent tracking current target */
	UFUNCTION(BlueprintPure, Category = "Shooting")
	float GetTimeOnTarget() const { return ShootingState.TimeOnTarget; }

	/** Get distance to current target */
	UFUNCTION(BlueprintPure, Category = "Shooting")
	float GetDistanceToTarget() const { return ShootingState.DistanceToTarget; }

	// ============================================================================
	// PUBLIC API - CONFIGURATION
	// ============================================================================

	/** Apply difficulty preset */
	UFUNCTION(BlueprintCallable, Category = "Shooting")
	void ApplyDifficultyPreset(EAIDifficulty NewDifficulty);

	/** Enable/disable shooting */
	UFUNCTION(BlueprintCallable, Category = "Shooting")
	void SetShootingEnabled(bool bEnabled);

	/** Set reference to owner tank (called by controller) */
	UFUNCTION(BlueprintCallable, Category = "Shooting")
	void SetOwnerTank(AWR_Tank_Pawn* Tank);

	/** Set reference to enemy detection component (for ammo/context info) */
	UFUNCTION(BlueprintCallable, Category = "Shooting")
	void SetEnemyDetectionComponent(UEnemyDetectionComponent* DetectionComp);

protected:
	// ============================================================================
	// INTERNAL STATE
	// ============================================================================

	/** Current shooting state */
	FAIShootingState ShootingState;

	/** Current shooting context */
	FShootingContext Context;

	/** Current target */
	TWeakObjectPtr<AActor> CurrentTarget;

	/** Cached visibility from EnemyDetectionComponent (0.0-1.0) - avoids redundant LOS check */
	float CachedTargetVisibility = 0.0f;

	/** Last known target position (for when target is briefly occluded) */
	FVector LastKnownTargetPosition = FVector::ZeroVector;

	/** Reference to owner tank pawn */
	TWeakObjectPtr<AWR_Tank_Pawn> OwnerTank;

	/** Reference to owner's turret */
	TWeakObjectPtr<AWR_Turret> OwnerTurret;

	/** Reference to enemy detection component */
	TWeakObjectPtr<UEnemyDetectionComponent> EnemyDetection;

	/** Has target just switched (for spread reset) */
	bool bJustSwitchedTarget = false;

	// ============================================================================
	// INTERNAL METHODS - MAIN UPDATE
	// ============================================================================

	/** Main update loop */
	void UpdateShooting(float DeltaTime);

	/** Update shooting context from current target */
	void UpdateContext();

	/** Update reaction time countdown */
	void UpdateReactionTime(float DeltaTime);

	/** Update accuracy (zeroing) */
	void UpdateAccuracy(float DeltaTime);

	/** Update weapon selection logic */
	void UpdateWeaponSelection();

	/** Update fire control (burst, cooldowns) */
	void UpdateFireControl(float DeltaTime);

	/** Execute firing if conditions met */
	void ExecuteFiring();

	// ============================================================================
	// INTERNAL METHODS - CALCULATIONS
	// ============================================================================

	/** Calculate perfect aim position (target center or predicted position) */
	FVector CalculatePerfectAimPosition() const;

	/** Calculate lead position for moving target */
	FVector CalculateLeadPosition(const FVector& TargetPos, const FVector& TargetVel, float ProjectileSpeed) const;

	/** Calculate grenade arc height offset */
	float CalculateGrenadeArcOffset(float Distance) const;

	/** Apply accuracy spread to aim direction */
	FVector ApplySpreadToAim(const FVector& PerfectAim) const;

	/** Calculate current spread based on state */
	float CalculateCurrentSpread() const;

	/** Calculate miss chance based on context */
	float CalculateMissChance() const;

	/** Determine miss type if miss occurs */
	EAIMissType DetermineMissType() const;

	/** Apply intentional miss offset */
	FVector ApplyIntentionalMiss(const FVector& AimPos, EAIMissType MissType) const;

	/** Generate random reaction time for new target */
	float GenerateReactionTime() const;

	/** Generate random burst size */
	int32 GenerateBurstSize() const;

	// ============================================================================
	// INTERNAL METHODS - WEAPON SELECTION
	// ============================================================================

	/** Determine best weapon for current situation */
	EWeaponSlot SelectBestWeapon() const;

	/** Check if can use secondary weapon */
	bool CanUseSecondaryWeapon() const;

	/** Check if should prefer secondary weapon */
	bool ShouldPreferSecondary() const;

	// ============================================================================
	// INTERNAL METHODS - FIRE CONDITIONS
	// ============================================================================

	/** Check if can fire selected weapon */
	bool CanFire(EWeaponSlot Weapon) const;

	/** Check if turret is on target (within MaxFireAngle) */
	bool IsTurretOnTarget() const;

	/** Perform actual line trace to check if path to target is clear */
	bool CheckLineOfSight(const FVector& FromLocation, const FVector& ToLocation) const;

	/** Get ammo count for weapon */
	int32 GetAmmoCount(EWeaponSlot Weapon) const;

	// ============================================================================
	// INTERNAL METHODS - UTILITY
	// ============================================================================

	/** Get owner tank pawn */
	AWR_Tank_Pawn* GetOwnerTank() const;

	/** Get owner turret */
	AWR_Turret* GetOwnerTurret() const;

	/** Get turret muzzle location */
	FVector GetMuzzleLocation() const;

	/** Get turret forward direction */
	FVector GetTurretDirection() const;

	/** Draw debug visualization */
	void DrawDebugVisualization() const;

	/** Log shooting event */
	void LogShootingEvent(const FString& Event) const;
};
