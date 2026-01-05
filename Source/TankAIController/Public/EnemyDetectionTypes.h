// Copyright ArenaBreakers. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EnemyDetectionTypes.generated.h"

/**
 * Awareness state enum for enemy detection
 * Gradual detection system instead of binary yes/no
 */
UENUM(BlueprintType)
enum class EAwarenessState : uint8
{
	Unaware = 0     UMETA(DisplayName = "Unaware"),      // No knowledge of enemy
	Suspicious = 1  UMETA(DisplayName = "Suspicious"),   // Something detected, investigating
	Alerted = 2     UMETA(DisplayName = "Alerted"),      // Enemy partially visible, preparing
	Combat = 3      UMETA(DisplayName = "Combat")        // Full engagement
};

/**
 * Detection socket configuration for multi-raycast visibility checks
 * Multiple sockets allow detecting partially visible enemies (half behind cover)
 */
USTRUCT(BlueprintType)
struct TANKAICONTROLLER_API FDetectionSocket
{
	GENERATED_BODY()

	/** Socket name on the mesh (NAME_None uses offset from actor center) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection")
	FName SocketName = NAME_None;

	/** Offset from socket/actor location in local space */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection")
	FVector Offset = FVector::ZeroVector;

	/** Weight for visibility calculation (higher = more important for detection) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float Weight = 1.0f;

	FDetectionSocket() = default;

	FDetectionSocket(FName InSocket, FVector InOffset = FVector::ZeroVector, float InWeight = 1.0f)
		: SocketName(InSocket), Offset(InOffset), Weight(InWeight)
	{
	}
};

/**
 * Information about a detected enemy
 * Replicated to owning client for UI feedback
 */
USTRUCT(BlueprintType)
struct TANKAICONTROLLER_API FDetectedEnemyInfo
{
	GENERATED_BODY()

	/** The detected enemy actor */
	UPROPERTY(BlueprintReadOnly, Category = "Detection")
	TWeakObjectPtr<AActor> Enemy;

	/** Current visibility percentage (0.0 - 1.0) based on multi-raycast hits */
	UPROPERTY(BlueprintReadOnly, Category = "Detection")
	float VisibilityPercent = 0.0f;

	/** Current awareness level (0.0 - 1.0), fills up over time when seeing enemy */
	UPROPERTY(BlueprintReadOnly, Category = "Detection")
	float AwarenessLevel = 0.0f;

	/** Current awareness state derived from AwarenessLevel */
	UPROPERTY(BlueprintReadOnly, Category = "Detection")
	EAwarenessState AwarenessState = EAwarenessState::Unaware;

	/** Last known world location of enemy (center of actor) */
	UPROPERTY(BlueprintReadOnly, Category = "Detection")
	FVector LastKnownLocation = FVector::ZeroVector;

	/** Best visible point on enemy - use this for turret aiming (not LastKnownLocation) */
	UPROPERTY(BlueprintReadOnly, Category = "Detection")
	FVector BestVisibleLocation = FVector::ZeroVector;

	/** Last known velocity of enemy (for prediction) */
	UPROPERTY(BlueprintReadOnly, Category = "Detection")
	FVector LastKnownVelocity = FVector::ZeroVector;

	/** Time since enemy was last seen (seconds) */
	UPROPERTY(BlueprintReadOnly, Category = "Detection")
	float TimeSinceLastSeen = 0.0f;

	/** Distance to enemy in cm */
	UPROPERTY(BlueprintReadOnly, Category = "Detection")
	float Distance = 0.0f;

	/** Angle to enemy relative to forward vector (degrees, 0 = directly ahead) */
	UPROPERTY(BlueprintReadOnly, Category = "Detection")
	float AngleToEnemy = 0.0f;

	/** Is enemy currently within firing cone? */
	UPROPERTY(BlueprintReadOnly, Category = "Detection")
	bool bInFiringCone = false;

	/** Which sockets are currently visible (bitmask for debugging) */
	UPROPERTY(BlueprintReadOnly, Category = "Detection")
	uint8 VisibleSocketsMask = 0;

	// Network serialization for bandwidth optimization
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

	FDetectedEnemyInfo() = default;

	bool IsValid() const { return Enemy.IsValid(); }
	AActor* GetEnemy() const { return Enemy.Get(); }
};

template <>
struct TStructOpsTypeTraits<FDetectedEnemyInfo> : public TStructOpsTypeTraitsBase2<FDetectedEnemyInfo>
{
	enum
	{
		WithNetSerializer = true
	};
};

/**
 * Detection configuration - tunable parameters
 */
USTRUCT(BlueprintType)
struct TANKAICONTROLLER_API FDetectionConfig
{
	GENERATED_BODY()

	// ========== RANGE & FOV ==========

	/** Maximum detection range in cm (50m default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Range", meta = (ClampMin = "100", ClampMax = "100000"))
	float MaxDetectionRange = 5000.0f;

	/** Close range where detection is instant (5m default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Range", meta = (ClampMin = "50", ClampMax = "1000"))
	float InstantDetectionRange = 500.0f;

	/** Field of view half-angle in degrees (45 = 90 total FOV) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|FOV", meta = (ClampMin = "10", ClampMax = "90"))
	float DetectionFOVHalfAngle = 45.0f;

	/** Peripheral vision extension beyond main FOV (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|FOV", meta = (ClampMin = "0", ClampMax = "90"))
	float PeripheralVisionAngle = 30.0f;

	/** Effectiveness multiplier in peripheral zone (0.3 = 30% as effective) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|FOV", meta = (ClampMin = "0", ClampMax = "1"))
	float PeripheralEffectiveness = 0.3f;

	// ========== AWARENESS TIMING ==========

	/** Awareness gain rate per second at 100% visibility */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Awareness", meta = (ClampMin = "0.1", ClampMax = "10"))
	float AwarenessGainRate = 1.5f;

	/** Awareness decay rate per second when not seeing enemy */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Awareness", meta = (ClampMin = "0.05", ClampMax = "5"))
	float AwarenessDecayRate = 0.25f;

	/** Time to remember enemy location after losing sight (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Awareness", meta = (ClampMin = "1", ClampMax = "60"))
	float MemoryDuration = 15.0f;

	// ========== COMBAT ==========

	/** Firing cone half-angle for "in firing solution" check (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Combat", meta = (ClampMin = "1", ClampMax = "45"))
	float FiringConeHalfAngle = 5.0f;

	// ========== TARGET SOCKETS ==========

	/** Detection sockets to check on target tanks (multi-raycast) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection|Sockets")
	TArray<FDetectionSocket> TargetSockets;

	FDetectionConfig()
	{
		// Default UGV tank sockets - optimized for partial visibility detection
		// Turret top has highest weight (most visible point)
		TargetSockets.Add(FDetectionSocket(NAME_None, FVector(0, 0, 60), 2.0f));    // Turret top
		TargetSockets.Add(FDetectionSocket(NAME_None, FVector(60, 0, 25), 1.0f));   // Hull front center
		TargetSockets.Add(FDetectionSocket(NAME_None, FVector(-50, 0, 25), 0.8f));  // Hull rear center
		TargetSockets.Add(FDetectionSocket(NAME_None, FVector(30, 45, 15), 0.6f));  // Left track front
		TargetSockets.Add(FDetectionSocket(NAME_None, FVector(30, -45, 15), 0.6f)); // Right track front
		TargetSockets.Add(FDetectionSocket(NAME_None, FVector(-30, 45, 15), 0.5f)); // Left track rear
		TargetSockets.Add(FDetectionSocket(NAME_None, FVector(-30, -45, 15), 0.5f));// Right track rear
	}
};

/**
 * Awareness state thresholds
 */
namespace AwarenessThresholds
{
	constexpr float Suspicious = 0.15f;  // 15% awareness = suspicious
	constexpr float Alerted = 0.45f;     // 45% awareness = alerted
	constexpr float Combat = 0.75f;      // 75% awareness = combat
}

/**
 * Detection update priority levels for LOD system
 */
UENUM(BlueprintType)
enum class EDetectionPriority : uint8
{
	Critical = 0,  // Update every frame (very close enemies)
	High = 1,      // Update every 2 frames
	Normal = 2,    // Update every 4 frames
	Low = 3        // Update every 8 frames
};
