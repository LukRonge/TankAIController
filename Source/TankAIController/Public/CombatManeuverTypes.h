// Copyright ArenaBreakers. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CombatManeuverTypes.generated.h"

// Forward declarations
class AActor;

// ============================================================================
// COMBAT STATE ENUM
// ============================================================================

/**
 * Combat state for AI tank state machine.
 * Determines the overall behavior mode of the AI.
 */
UENUM(BlueprintType)
enum class ECombatState : uint8
{
	/** Normal navigation, no enemy awareness */
	Patrol      UMETA(DisplayName = "Patrol"),

	/** Enemy detected but not engaged (Suspicious/Alerted awareness) */
	Alert       UMETA(DisplayName = "Alert"),

	/** Active combat engagement (Combat awareness) */
	Combat      UMETA(DisplayName = "Combat"),

	/** Retreating from combat due to low health or tactical disadvantage */
	Disengage   UMETA(DisplayName = "Disengage"),

	/** Tactical repositioning (flanking, hull-down, etc.) */
	Reposition  UMETA(DisplayName = "Reposition")
};

// ============================================================================
// COMBAT MANEUVER TYPE ENUM
// ============================================================================

/**
 * Types of combat maneuvers the AI can execute.
 * Each maneuver has associated waypoint generation logic and scoring weights.
 */
UENUM(BlueprintType)
enum class ECombatManeuverType : uint8
{
	/** No maneuver - default state */
	None            UMETA(DisplayName = "None"),

	/** Flank attack - move to enemy's side for weaker armor */
	Flanking        UMETA(DisplayName = "Flanking"),

	/** Tactical retreat - reverse away keeping frontal armor to enemy */
	TacticalRetreat UMETA(DisplayName = "Tactical Retreat"),

	/** Hull-down position - use cover to expose only turret */
	HullDown        UMETA(DisplayName = "Hull Down"),

	/** Zigzag evasion - unpredictable movement to avoid fire */
	ZigzagEvade     UMETA(DisplayName = "Zigzag Evade"),

	/** Shoot and scoot - fire then relocate before counter-fire */
	ShootAndScoot   UMETA(DisplayName = "Shoot and Scoot"),

	/** Charge attack - aggressive close-range engagement */
	ChargeAttack    UMETA(DisplayName = "Charge Attack"),

	/** Circle strafe - circular movement while engaging */
	CircleStrafe    UMETA(DisplayName = "Circle Strafe"),

	/** Hidden enum value for iteration */
	MAX             UMETA(Hidden)
};

// ============================================================================
// MANEUVER SCORING STRUCTS
// ============================================================================

/**
 * Weights for scoring a single maneuver.
 * Positive weight = factor supports this maneuver.
 * Negative weight = factor discourages this maneuver.
 *
 * Score = Sum(FactorWeight * FactorValue) where FactorValue is 0 or 1.
 */
USTRUCT(BlueprintType)
struct TANKAICONTROLLER_API FManeuverScoreWeights
{
	GENERATED_BODY()

	// ===== DISTANCE FACTORS (mutually exclusive) =====

	/** Weight when enemy is at close range (< CloseRangeDistance) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance")
	float EnemyDistanceClose = 0.0f;

	/** Weight when enemy is at medium range (CloseRange to LongRange) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance")
	float EnemyDistanceMedium = 0.0f;

	/** Weight when enemy is at long range (> LongRangeDistance) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance")
	float EnemyDistanceFar = 0.0f;

	// ===== TERRAIN FACTORS =====

	/** Weight when cover is available nearby */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	float CoverNearby = 0.0f;

	/** Weight when terrain is open (low obstacle density) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	float OpenTerrain = 0.0f;

	// ===== HEALTH FACTORS (mutually exclusive) =====

	/** Weight when own health is low (< LowHealthThreshold) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health")
	float LowHealth = 0.0f;

	/** Weight when own health is high (> HighHealthThreshold) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health")
	float HighHealth = 0.0f;

	// ===== THREAT FACTORS =====

	/** Weight when multiple enemies are detected (2+) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threat")
	float MultipleEnemies = 0.0f;

	/** Weight when enemy is facing away from us */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threat")
	float EnemyFacingAway = 0.0f;

	/** Weight when enemy is facing toward us */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threat")
	float EnemyFacingMe = 0.0f;

	/** Weight when currently under fire (took damage recently) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threat")
	float UnderFire = 0.0f;

	// ===== MOBILITY FACTORS =====

	/** Weight when lateral path is clear for flanking */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobility")
	float FlankPathClear = 0.0f;

	/** Weight when rear path is clear for retreat */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobility")
	float RetreatPathClear = 0.0f;

	// ===== HELPER METHODS =====

	/** Reset all weights to zero */
	void Reset()
	{
		EnemyDistanceClose = 0.0f;
		EnemyDistanceMedium = 0.0f;
		EnemyDistanceFar = 0.0f;
		CoverNearby = 0.0f;
		OpenTerrain = 0.0f;
		LowHealth = 0.0f;
		HighHealth = 0.0f;
		MultipleEnemies = 0.0f;
		EnemyFacingAway = 0.0f;
		EnemyFacingMe = 0.0f;
		UnderFire = 0.0f;
		FlankPathClear = 0.0f;
		RetreatPathClear = 0.0f;
	}
};

/**
 * Configuration for a single maneuver type.
 * Contains the maneuver type enum and its associated scoring weights.
 */
USTRUCT(BlueprintType)
struct TANKAICONTROLLER_API FManeuverScoreConfig
{
	GENERATED_BODY()

	/** The maneuver type this config applies to */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maneuver")
	ECombatManeuverType ManeuverType = ECombatManeuverType::None;

	/** Scoring weights for this maneuver */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maneuver")
	FManeuverScoreWeights Weights;

	/** Base score added before factor weights (can make some maneuvers preferred by default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maneuver")
	float BaseScore = 0.0f;

	/** Constructor */
	FManeuverScoreConfig()
		: ManeuverType(ECombatManeuverType::None)
		, BaseScore(0.0f)
	{
	}

	/** Constructor with maneuver type */
	explicit FManeuverScoreConfig(ECombatManeuverType InManeuverType)
		: ManeuverType(InManeuverType)
		, BaseScore(0.0f)
	{
	}
};

// ============================================================================
// COMBAT SITUATION ASSESSMENT
// ============================================================================

/**
 * Current combat situation assessment.
 * Gathered from EnemyDetectionComponent, LineTraces, and tank state.
 * Used as input for maneuver selection scoring.
 */
USTRUCT(BlueprintType)
struct TANKAICONTROLLER_API FCombatSituation
{
	GENERATED_BODY()

	// ===== ENEMY INFO =====

	/** Distance to primary enemy (cm) */
	UPROPERTY(BlueprintReadWrite, Category = "Enemy")
	float EnemyDistance = 0.0f;

	/** Angle to enemy relative to tank forward (-180 to 180 degrees) */
	UPROPERTY(BlueprintReadWrite, Category = "Enemy")
	float EnemyAngle = 0.0f;

	/** Enemy world position */
	UPROPERTY(BlueprintReadWrite, Category = "Enemy")
	FVector EnemyPosition = FVector::ZeroVector;

	/** Is enemy currently visible? (VisibilityMask > 0) */
	UPROPERTY(BlueprintReadWrite, Category = "Enemy")
	bool bEnemyVisible = false;

	/** Number of detected enemies */
	UPROPERTY(BlueprintReadWrite, Category = "Enemy")
	int32 EnemyCount = 0;

	/** Is enemy facing toward us? (dot product of enemy forward and direction to us > 0.5) */
	UPROPERTY(BlueprintReadWrite, Category = "Enemy")
	bool bEnemyFacingMe = false;

	/** Is enemy facing away from us? (dot product < -0.3) */
	UPROPERTY(BlueprintReadWrite, Category = "Enemy")
	bool bEnemyFacingAway = false;

	// ===== OWN STATUS =====

	/** Own health percentage (0-1) */
	UPROPERTY(BlueprintReadWrite, Category = "Self")
	float OwnHealth = 1.0f;

	/** Own ammo percentage (0-1) */
	UPROPERTY(BlueprintReadWrite, Category = "Self")
	float OwnAmmo = 1.0f;

	/** Is tank currently under fire? (took damage within last N seconds) */
	UPROPERTY(BlueprintReadWrite, Category = "Self")
	bool bUnderFire = false;

	/** Own world position */
	UPROPERTY(BlueprintReadWrite, Category = "Self")
	FVector OwnPosition = FVector::ZeroVector;

	/** Own forward direction */
	UPROPERTY(BlueprintReadWrite, Category = "Self")
	FVector OwnForward = FVector::ForwardVector;

	// ===== TERRAIN =====

	/** Is cover available within search radius? */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	bool bCoverAvailable = false;

	/** Distance to nearest cover (cm) */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	float CoverDistance = 0.0f;

	/** Direction to nearest cover (world space, normalized) */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	FVector CoverDirection = FVector::ZeroVector;

	/** Nearest cover position (world space) */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	FVector CoverPosition = FVector::ZeroVector;

	/** Open terrain - average obstacle distance > threshold */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	bool bOpenTerrain = false;

	/** Can move to left or right for flanking? (lateral LineTraces clear) */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	bool bFlankPathClear = false;

	/** Which flank is clearer? true = right, false = left */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	bool bRightFlankClearer = true;

	/** Can move backward for retreat? (rear LineTraces clear) */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	bool bRetreatPathClear = false;

	/** Average distance to obstacles from LineTraces (cm) */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	float AverageObstacleDistance = 0.0f;

	// ===== HELPER METHODS =====

	/** Check if we have a valid enemy target */
	bool HasValidEnemy() const
	{
		return EnemyCount > 0 && EnemyDistance > 0.0f;
	}

	/** Check if situation is critical (low health + under fire) */
	bool IsCritical() const
	{
		return OwnHealth < 0.3f && bUnderFire;
	}

	/** Get direction from own position to enemy */
	FVector GetDirectionToEnemy() const
	{
		if (EnemyPosition.IsNearlyZero() || OwnPosition.IsNearlyZero())
		{
			return FVector::ZeroVector;
		}
		return (EnemyPosition - OwnPosition).GetSafeNormal();
	}
};

// ============================================================================
// COMBAT WAYPOINT
// ============================================================================

/**
 * Single waypoint in a combat maneuver path.
 * Contains position, behavior flags, and turret control data.
 */
USTRUCT(BlueprintType)
struct TANKAICONTROLLER_API FCombatWaypoint
{
	GENERATED_BODY()

	/** World position of waypoint */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint")
	FVector Location = FVector::ZeroVector;

	/** Type of maneuver this waypoint belongs to */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint")
	ECombatManeuverType ManeuverType = ECombatManeuverType::None;

	/** Should tank fire when reaching this waypoint? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Behavior")
	bool bShouldFire = false;

	/** Should tank reverse to this waypoint? (keeps front armor to enemy) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Behavior")
	bool bReverseMovement = false;

	/** Desired speed (0-1, percentage of max speed) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Behavior", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DesiredSpeed = 1.0f;

	/** Target for turret to aim at (world space) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Turret")
	FVector LookAtTarget = FVector::ZeroVector;

	/** Should turret track LookAtTarget during movement to this waypoint? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Turret")
	bool bTrackTarget = true;

	/** Time to wait at this waypoint before continuing (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Timing", meta = (ClampMin = "0.0"))
	float WaitTime = 0.0f;

	/** Radius to consider waypoint reached (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Navigation", meta = (ClampMin = "50.0"))
	float ReachRadius = 150.0f;

	/** Priority of this waypoint (for interruption decisions) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Navigation")
	int32 Priority = 0;

	/** Constructor */
	FCombatWaypoint()
		: Location(FVector::ZeroVector)
		, ManeuverType(ECombatManeuverType::None)
		, bShouldFire(false)
		, bReverseMovement(false)
		, DesiredSpeed(1.0f)
		, LookAtTarget(FVector::ZeroVector)
		, bTrackTarget(true)
		, WaitTime(0.0f)
		, ReachRadius(150.0f)
		, Priority(0)
	{
	}

	/** Constructor with location and maneuver type */
	FCombatWaypoint(FVector InLocation, ECombatManeuverType InManeuverType)
		: Location(InLocation)
		, ManeuverType(InManeuverType)
		, bShouldFire(false)
		, bReverseMovement(false)
		, DesiredSpeed(1.0f)
		, LookAtTarget(FVector::ZeroVector)
		, bTrackTarget(true)
		, WaitTime(0.0f)
		, ReachRadius(150.0f)
		, Priority(0)
	{
	}

	/** Check if waypoint has a valid location */
	bool IsValid() const
	{
		return !Location.IsNearlyZero() && ManeuverType != ECombatManeuverType::None;
	}
};

// ============================================================================
// COMBAT MANEUVER
// ============================================================================

/**
 * Complete combat maneuver with all waypoints.
 * Represents a tactical action from start to finish.
 */
USTRUCT(BlueprintType)
struct TANKAICONTROLLER_API FCombatManeuver
{
	GENERATED_BODY()

	/** Type of this maneuver */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maneuver")
	ECombatManeuverType ManeuverType = ECombatManeuverType::None;

	/** Sequence of waypoints to execute */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maneuver")
	TArray<FCombatWaypoint> Waypoints;

	/** Target actor this maneuver is responding to */
	UPROPERTY(BlueprintReadWrite, Category = "Maneuver")
	TWeakObjectPtr<AActor> TargetActor;

	/** Score that led to this maneuver being selected */
	UPROPERTY(BlueprintReadWrite, Category = "Maneuver")
	float SelectionScore = 0.0f;

	/** Can this maneuver be interrupted by higher priority maneuver? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maneuver")
	bool bInterruptible = true;

	/** Timestamp when maneuver was started */
	UPROPERTY(BlueprintReadWrite, Category = "Maneuver")
	float StartTime = 0.0f;

	/** Constructor */
	FCombatManeuver()
		: ManeuverType(ECombatManeuverType::None)
		, SelectionScore(0.0f)
		, bInterruptible(true)
		, StartTime(0.0f)
	{
	}

	/** Constructor with maneuver type */
	explicit FCombatManeuver(ECombatManeuverType InManeuverType)
		: ManeuverType(InManeuverType)
		, SelectionScore(0.0f)
		, bInterruptible(true)
		, StartTime(0.0f)
	{
	}

	/** Check if maneuver is valid (has waypoints) */
	bool IsValid() const
	{
		return ManeuverType != ECombatManeuverType::None && Waypoints.Num() > 0;
	}

	/** Get total number of waypoints */
	int32 GetWaypointCount() const
	{
		return Waypoints.Num();
	}

	/** Get waypoint at index (with bounds check) */
	const FCombatWaypoint* GetWaypoint(int32 Index) const
	{
		if (Waypoints.IsValidIndex(Index))
		{
			return &Waypoints[Index];
		}
		return nullptr;
	}

	/** Add waypoint to the maneuver */
	void AddWaypoint(const FCombatWaypoint& Waypoint)
	{
		Waypoints.Add(Waypoint);
	}

	/** Clear all waypoints */
	void ClearWaypoints()
	{
		Waypoints.Empty();
	}

	/** Reset maneuver to default state */
	void Reset()
	{
		ManeuverType = ECombatManeuverType::None;
		Waypoints.Empty();
		TargetActor.Reset();
		SelectionScore = 0.0f;
		bInterruptible = true;
		StartTime = 0.0f;
	}
};

// ============================================================================
// MANEUVER SELECTION RESULT
// ============================================================================

/**
 * Result of maneuver selection scoring.
 * Contains the selected maneuver and all scores for debugging.
 */
USTRUCT(BlueprintType)
struct TANKAICONTROLLER_API FManeuverSelectionResult
{
	GENERATED_BODY()

	/** The selected maneuver type */
	UPROPERTY(BlueprintReadWrite, Category = "Selection")
	ECombatManeuverType SelectedManeuver = ECombatManeuverType::None;

	/** Score of the selected maneuver */
	UPROPERTY(BlueprintReadWrite, Category = "Selection")
	float SelectedScore = 0.0f;

	/** All maneuver scores for debugging */
	UPROPERTY(BlueprintReadWrite, Category = "Selection")
	TMap<ECombatManeuverType, float> AllScores;

	/** Which maneuvers were valid (passed validity checks) */
	UPROPERTY(BlueprintReadWrite, Category = "Selection")
	TArray<ECombatManeuverType> ValidManeuvers;

	/** Situation that was evaluated */
	UPROPERTY(BlueprintReadWrite, Category = "Selection")
	FCombatSituation EvaluatedSituation;

	/** Was selection successful? (found a valid maneuver) */
	bool IsValid() const
	{
		return SelectedManeuver != ECombatManeuverType::None;
	}
};

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

namespace CombatManeuverUtils
{
	/** Get display name for combat state */
	inline FString GetCombatStateName(ECombatState State)
	{
		switch (State)
		{
		case ECombatState::Patrol:     return TEXT("Patrol");
		case ECombatState::Alert:      return TEXT("Alert");
		case ECombatState::Combat:     return TEXT("Combat");
		case ECombatState::Disengage:  return TEXT("Disengage");
		case ECombatState::Reposition: return TEXT("Reposition");
		default:                       return TEXT("Unknown");
		}
	}

	/** Get display name for maneuver type */
	inline FString GetManeuverTypeName(ECombatManeuverType Type)
	{
		switch (Type)
		{
		case ECombatManeuverType::None:            return TEXT("None");
		case ECombatManeuverType::Flanking:        return TEXT("Flanking");
		case ECombatManeuverType::TacticalRetreat: return TEXT("Tactical Retreat");
		case ECombatManeuverType::HullDown:        return TEXT("Hull Down");
		case ECombatManeuverType::ZigzagEvade:     return TEXT("Zigzag Evade");
		case ECombatManeuverType::ShootAndScoot:   return TEXT("Shoot and Scoot");
		case ECombatManeuverType::ChargeAttack:    return TEXT("Charge Attack");
		case ECombatManeuverType::CircleStrafe:    return TEXT("Circle Strafe");
		default:                                   return TEXT("Unknown");
		}
	}

	/** Get color for debug visualization of maneuver type */
	inline FColor GetManeuverDebugColor(ECombatManeuverType Type)
	{
		switch (Type)
		{
		case ECombatManeuverType::Flanking:        return FColor::Yellow;
		case ECombatManeuverType::TacticalRetreat: return FColor::Red;
		case ECombatManeuverType::HullDown:        return FColor::Blue;
		case ECombatManeuverType::ZigzagEvade:     return FColor::Orange;
		case ECombatManeuverType::ShootAndScoot:   return FColor::Purple;
		case ECombatManeuverType::ChargeAttack:    return FColor::Green;
		case ECombatManeuverType::CircleStrafe:    return FColor::Cyan;
		default:                                   return FColor::White;
		}
	}

	/** Check if maneuver is offensive (moves toward enemy) */
	inline bool IsOffensiveManeuver(ECombatManeuverType Type)
	{
		return Type == ECombatManeuverType::Flanking ||
		       Type == ECombatManeuverType::ChargeAttack ||
		       Type == ECombatManeuverType::CircleStrafe;
	}

	/** Check if maneuver is defensive (moves away from enemy or to cover) */
	inline bool IsDefensiveManeuver(ECombatManeuverType Type)
	{
		return Type == ECombatManeuverType::TacticalRetreat ||
		       Type == ECombatManeuverType::HullDown ||
		       Type == ECombatManeuverType::ZigzagEvade;
	}

	/** Check if maneuver requires cover */
	inline bool RequiresCover(ECombatManeuverType Type)
	{
		return Type == ECombatManeuverType::HullDown ||
		       Type == ECombatManeuverType::ShootAndScoot;
	}

	/** Check if maneuver uses reverse movement */
	inline bool UsesReverseMovement(ECombatManeuverType Type)
	{
		return Type == ECombatManeuverType::TacticalRetreat;
	}
}
