// Copyright ArenaBreakers. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIShootingTypes.generated.h"

// ============================================================================
// ENUMS
// ============================================================================

/**
 * Weapon slot selection for AI firing
 */
UENUM(BlueprintType)
enum class EWeaponSlot : uint8
{
	Primary		UMETA(DisplayName = "Primary Weapon"),		// Machine gun, cannon - general use
	Secondary	UMETA(DisplayName = "Secondary Weapon")		// Grenade launcher, rockets - limited ammo
};

/**
 * AI difficulty presets affecting shooting behavior
 */
UENUM(BlueprintType)
enum class EAIDifficulty : uint8
{
	Easy		UMETA(DisplayName = "Easy"),		// Slow reactions, high spread, frequent misses
	Medium		UMETA(DisplayName = "Medium"),		// Balanced gameplay
	Hard		UMETA(DisplayName = "Hard"),		// Fast reactions, accurate
	Veteran		UMETA(DisplayName = "Veteran"),		// Near-perfect but still human-like
	Custom		UMETA(DisplayName = "Custom")		// User-defined values
};

/**
 * Type of intentional miss for human-like behavior
 */
UENUM(BlueprintType)
enum class EAIMissType : uint8
{
	None			UMETA(DisplayName = "None"),			// No miss - accurate shot
	Overshoot		UMETA(DisplayName = "Overshoot"),		// Aimed too far ahead of moving target
	Undershoot		UMETA(DisplayName = "Undershoot"),		// Didn't lead target enough
	Flinch			UMETA(DisplayName = "Flinch"),			// Jerked aim when firing
	TrackingLoss	UMETA(DisplayName = "Tracking Loss"),	// Lost track of fast-moving target
	PanicShot		UMETA(DisplayName = "Panic Shot")		// Under pressure (low HP, multiple enemies)
};

/**
 * Reason why engagement ended
 */
UENUM(BlueprintType)
enum class EEngagementEndReason : uint8
{
	TargetDestroyed		UMETA(DisplayName = "Target Destroyed"),
	TargetLost			UMETA(DisplayName = "Target Lost"),			// Lost line of sight
	TargetOutOfRange	UMETA(DisplayName = "Target Out of Range"),
	OutOfAmmo			UMETA(DisplayName = "Out of Ammo"),
	ManualStop			UMETA(DisplayName = "Manual Stop"),
	OwnerDestroyed		UMETA(DisplayName = "Owner Destroyed")
};

/**
 * Current state of the shooting system
 */
UENUM(BlueprintType)
enum class EShootingState : uint8
{
	Idle			UMETA(DisplayName = "Idle"),			// No target
	Acquiring		UMETA(DisplayName = "Acquiring"),		// Target detected, reaction time in progress
	Tracking		UMETA(DisplayName = "Tracking"),		// Aiming at target, improving accuracy
	Firing			UMETA(DisplayName = "Firing"),			// Actively shooting
	Cooldown		UMETA(DisplayName = "Cooldown"),		// Between bursts
	Reloading		UMETA(DisplayName = "Reloading")		// Weapon reloading (if applicable)
};

// ============================================================================
// STRUCTS
// ============================================================================

/**
 * Configuration for AI shooting behavior - adjustable per difficulty
 */
USTRUCT(BlueprintType)
struct FAIShootingConfig
{
	GENERATED_BODY()

	// ========== REACTION TIME ==========

	/** Minimum reaction time before first shot (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float ReactionTimeMin = 0.3f;

	/** Maximum reaction time before first shot (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float ReactionTimeMax = 0.6f;

	/** Multiplier for reaction time when target is moving fast */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction", meta = (ClampMin = "1.0", ClampMax = "2.0"))
	float ReactionTimeMovingTargetMultiplier = 1.2f;

	/** Multiplier for reaction time when surprised (target appeared suddenly) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reaction", meta = (ClampMin = "1.0", ClampMax = "2.5"))
	float ReactionTimeSurpriseMultiplier = 1.4f;

	// ========== ACCURACY / SPREAD ==========

	/** Base aim spread in degrees (initial inaccuracy) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy", meta = (ClampMin = "0.5", ClampMax = "15.0"))
	float BaseSpread = 5.0f;

	/** Minimum spread after zeroing (best accuracy achievable) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float MinSpread = 1.0f;

	/** Maximum spread (worst accuracy) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy", meta = (ClampMin = "5.0", ClampMax = "20.0"))
	float MaxSpread = 12.0f;

	/** Rate at which spread decreases while aiming at target (degrees per second) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float ZeroingRate = 2.5f;

	/** Spread increase when owner tank is moving (added to current spread) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float MovementSpreadPenalty = 2.0f;

	/** Owner velocity threshold above which movement penalty applies (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accuracy", meta = (ClampMin = "50.0", ClampMax = "500.0"))
	float MovementSpreadThreshold = 100.0f;

	// ========== INTENTIONAL MISS ==========

	/** Base chance to intentionally miss (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Miss", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float BaseMissChance = 0.12f;

	/** Additional miss chance when target is moving fast */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Miss", meta = (ClampMin = "0.0", ClampMax = "0.3"))
	float MovingTargetMissBonus = 0.08f;

	/** Additional miss chance per unit of normalized distance - reduced for better long range accuracy */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Miss", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float DistanceMissBonus = 0.02f;

	/** Additional miss chance when owner HP is below 30% */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Miss", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float PanicMissBonus = 0.1f;

	/** Maximum miss chance cap */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Miss", meta = (ClampMin = "0.1", ClampMax = "0.6"))
	float MaxMissChance = 0.35f;

	/** Angle offset in degrees when intentionally missing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Miss", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float MissAngleOffset = 4.0f;

	// ========== TARGET PREDICTION ==========

	/** How accurately AI calculates lead for moving targets (0 = no lead, 1 = perfect) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prediction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LeadAccuracy = 0.75f;

	/** Target velocity threshold for lead calculation (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prediction", meta = (ClampMin = "50.0", ClampMax = "300.0"))
	float LeadVelocityThreshold = 100.0f;

	// ========== FIRE CONTROL ==========

	/** Minimum shots in a burst */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireControl", meta = (ClampMin = "1", ClampMax = "10"))
	int32 BurstSizeMin = 3;

	/** Maximum shots in a burst */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireControl", meta = (ClampMin = "1", ClampMax = "20"))
	int32 BurstSizeMax = 6;

	/** Time between shots within a burst (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireControl", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float TimeBetweenBurstShots = 0.12f;

	/** Cooldown time between bursts (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireControl", meta = (ClampMin = "0.2", ClampMax = "2.0"))
	float BurstCooldown = 0.5f;

	/** Maximum angle from perfect aim to still fire (degrees) - keep low for accuracy */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireControl", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float MaxFireAngle = 10.0f;

	// ========== SECONDARY WEAPON SPECIFIC ==========

	/** Minimum safe distance for secondary weapon to avoid self-damage (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary", meta = (ClampMin = "500.0", ClampMax = "1500.0"))
	float SecondaryMinSafeDistance = 800.0f;

	/** Preferred minimum distance to use secondary weapon (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary", meta = (ClampMin = "1000.0", ClampMax = "3000.0"))
	float SecondaryPreferredMinDistance = 2000.0f;

	/** Maximum effective range for secondary weapon (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary", meta = (ClampMin = "5000.0", ClampMax = "15000.0"))
	float SecondaryMaxRange = 8000.0f;

	/** Minimum ammo count to consider using secondary weapon */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary", meta = (ClampMin = "1", ClampMax = "5"))
	int32 SecondaryMinAmmoThreshold = 2;

	/** Cooldown after firing secondary weapon (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float SecondaryCooldown = 3.0f;

	// ========== GRENADE ARC COMPENSATION ==========

	/** Distance at which arc compensation starts (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GrenadeArc", meta = (ClampMin = "1000.0", ClampMax = "3000.0"))
	float GrenadeArcStartDistance = 2000.0f;

	/** Maximum height offset at max range (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GrenadeArc", meta = (ClampMin = "200.0", ClampMax = "800.0"))
	float GrenadeArcMaxOffset = 500.0f;

	/** Exponent for arc curve (1.0 = linear, 1.5 = accelerating, 2.0 = quadratic) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GrenadeArc", meta = (ClampMin = "1.0", ClampMax = "2.5"))
	float GrenadeArcExponent = 1.5f;

	// ========== PRIMARY WEAPON ==========

	/** Effective range for primary weapon (cm) - matches detection range for consistent behavior */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primary", meta = (ClampMin = "3000.0", ClampMax = "15000.0"))
	float PrimaryEffectiveRange = 8000.0f;

	/** Projectile speed for lead calculation - primary weapon (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primary", meta = (ClampMin = "10000.0", ClampMax = "100000.0"))
	float PrimaryProjectileSpeed = 50000.0f;

	/** Projectile speed for lead calculation - secondary weapon (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Secondary", meta = (ClampMin = "5000.0", ClampMax = "50000.0"))
	float SecondaryProjectileSpeed = 15000.0f;
};

/**
 * Runtime state of the shooting system
 */
USTRUCT(BlueprintType)
struct FAIShootingState
{
	GENERATED_BODY()

	/** Current shooting state */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	EShootingState State = EShootingState::Idle;

	/** Currently selected weapon */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	EWeaponSlot SelectedWeapon = EWeaponSlot::Primary;

	/** Time spent on current target (for zeroing) */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	float TimeOnTarget = 0.0f;

	/** Current accuracy spread (degrees) */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	float CurrentSpread = 5.0f;

	/** Remaining reaction time before can fire */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	float ReactionTimeRemaining = 0.0f;

	/** Has reaction time elapsed for current target */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bTargetAcquired = false;

	/** Current burst shot count */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	int32 CurrentBurstShots = 0;

	/** Target burst size for current burst */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	int32 TargetBurstSize = 0;

	/** Time until next shot in burst */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	float BurstShotTimer = 0.0f;

	/** Time until next burst can start */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	float BurstCooldownTimer = 0.0f;

	/** Time until secondary weapon can fire */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	float SecondaryCooldownTimer = 0.0f;

	/** Time until weapon can be switched (hysteresis) */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	float WeaponSwitchTimer = 0.0f;

	/** Is currently in a firing burst */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bInBurst = false;

	/** Is primary weapon currently firing */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bIsFiringPrimary = false;

	/** Is secondary weapon currently firing */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bIsFiringSecondary = false;

	/** Last miss type (for debugging) */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	EAIMissType LastMissType = EAIMissType::None;

	/** Distance to current target (cm) */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	float DistanceToTarget = 0.0f;

	/** Calculated lead position for moving targets */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	FVector LeadPosition = FVector::ZeroVector;

	/** Final adjusted aim location (after spread, lead, arc) */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	FVector AdjustedAimLocation = FVector::ZeroVector;

	/** Reset state for new engagement */
	void Reset()
	{
		State = EShootingState::Idle;
		SelectedWeapon = EWeaponSlot::Primary;
		TimeOnTarget = 0.0f;
		CurrentSpread = 5.0f;
		ReactionTimeRemaining = 0.0f;
		bTargetAcquired = false;
		CurrentBurstShots = 0;
		TargetBurstSize = 0;
		BurstShotTimer = 0.0f;
		BurstCooldownTimer = 0.0f;
		bInBurst = false;
		bIsFiringPrimary = false;
		bIsFiringSecondary = false;
		LastMissType = EAIMissType::None;
		DistanceToTarget = 0.0f;
		LeadPosition = FVector::ZeroVector;
		AdjustedAimLocation = FVector::ZeroVector;
		WeaponSwitchTimer = 0.0f;
		// Note: SecondaryCooldownTimer persists across engagements
	}
};

/**
 * Context information for shooting decisions
 */
USTRUCT(BlueprintType)
struct FShootingContext
{
	GENERATED_BODY()

	/** Current target actor */
	UPROPERTY(BlueprintReadOnly, Category = "Context")
	TWeakObjectPtr<AActor> Target;

	/** Target's current location */
	UPROPERTY(BlueprintReadOnly, Category = "Context")
	FVector TargetLocation = FVector::ZeroVector;

	/** Target's velocity */
	UPROPERTY(BlueprintReadOnly, Category = "Context")
	FVector TargetVelocity = FVector::ZeroVector;

	/** Distance to target (cm) */
	UPROPERTY(BlueprintReadOnly, Category = "Context")
	float DistanceToTarget = 0.0f;

	/** Angle from turret to target (degrees) */
	UPROPERTY(BlueprintReadOnly, Category = "Context")
	float AngleToTarget = 0.0f;

	/** Is target currently visible (line of sight) */
	UPROPERTY(BlueprintReadOnly, Category = "Context")
	bool bHasLineOfSight = false;

	/** Is target stationary (velocity < threshold) */
	UPROPERTY(BlueprintReadOnly, Category = "Context")
	bool bTargetIsStationary = false;

	/** Is target behind cover */
	UPROPERTY(BlueprintReadOnly, Category = "Context")
	bool bTargetInCover = false;

	/** Target's health percentage (0-1) */
	UPROPERTY(BlueprintReadOnly, Category = "Context")
	float TargetHealthPercent = 1.0f;

	/** Owner's health percentage (0-1) */
	UPROPERTY(BlueprintReadOnly, Category = "Context")
	float OwnerHealthPercent = 1.0f;

	/** Owner's current velocity (cm/s) */
	UPROPERTY(BlueprintReadOnly, Category = "Context")
	float OwnerSpeed = 0.0f;

	/** Number of detected enemies */
	UPROPERTY(BlueprintReadOnly, Category = "Context")
	int32 EnemyCount = 0;

	/** Primary weapon ammo count */
	UPROPERTY(BlueprintReadOnly, Category = "Context")
	int32 PrimaryAmmo = 0;

	/** Secondary weapon ammo count */
	UPROPERTY(BlueprintReadOnly, Category = "Context")
	int32 SecondaryAmmo = 0;

	/** Was target just acquired (for surprise multiplier) */
	UPROPERTY(BlueprintReadOnly, Category = "Context")
	bool bJustAcquired = false;
};

/**
 * Difficulty preset values - predefined configurations
 */
USTRUCT(BlueprintType)
struct FAIDifficultyPreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	EAIDifficulty Difficulty = EAIDifficulty::Medium;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	FAIShootingConfig Config;

	/** Get default preset for difficulty level */
	static FAIDifficultyPreset GetPreset(EAIDifficulty Difficulty)
	{
		FAIDifficultyPreset Preset;
		Preset.Difficulty = Difficulty;

		switch (Difficulty)
		{
		case EAIDifficulty::Easy:
			Preset.Config.ReactionTimeMin = 0.6f;
			Preset.Config.ReactionTimeMax = 1.2f;
			Preset.Config.BaseSpread = 10.0f;
			Preset.Config.MinSpread = 3.0f;
			Preset.Config.ZeroingRate = 1.0f;
			Preset.Config.BaseMissChance = 0.25f;
			Preset.Config.LeadAccuracy = 0.5f;
			Preset.Config.BurstSizeMin = 2;
			Preset.Config.BurstSizeMax = 4;
			Preset.Config.BurstCooldown = 0.8f;
			break;

		case EAIDifficulty::Medium:
			Preset.Config.ReactionTimeMin = 0.35f;
			Preset.Config.ReactionTimeMax = 0.7f;
			Preset.Config.BaseSpread = 6.0f;
			Preset.Config.MinSpread = 1.5f;
			Preset.Config.ZeroingRate = 2.5f;
			Preset.Config.BaseMissChance = 0.15f;
			Preset.Config.LeadAccuracy = 0.7f;
			Preset.Config.BurstSizeMin = 3;
			Preset.Config.BurstSizeMax = 6;
			Preset.Config.BurstCooldown = 0.5f;
			break;

		case EAIDifficulty::Hard:
			Preset.Config.ReactionTimeMin = 0.2f;
			Preset.Config.ReactionTimeMax = 0.4f;
			Preset.Config.BaseSpread = 3.5f;
			Preset.Config.MinSpread = 0.8f;
			Preset.Config.ZeroingRate = 4.0f;
			Preset.Config.BaseMissChance = 0.08f;
			Preset.Config.LeadAccuracy = 0.85f;
			Preset.Config.BurstSizeMin = 4;
			Preset.Config.BurstSizeMax = 8;
			Preset.Config.BurstCooldown = 0.35f;
			break;

		case EAIDifficulty::Veteran:
			Preset.Config.ReactionTimeMin = 0.12f;
			Preset.Config.ReactionTimeMax = 0.25f;
			Preset.Config.BaseSpread = 2.0f;
			Preset.Config.MinSpread = 0.3f;
			Preset.Config.ZeroingRate = 6.0f;
			Preset.Config.BaseMissChance = 0.03f;
			Preset.Config.LeadAccuracy = 0.95f;
			Preset.Config.BurstSizeMin = 5;
			Preset.Config.BurstSizeMax = 10;
			Preset.Config.BurstCooldown = 0.25f;
			break;

		case EAIDifficulty::Custom:
		default:
			// Use default FAIShootingConfig values
			break;
		}

		return Preset;
	}
};
