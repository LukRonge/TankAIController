// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TankWaypointComponent.generated.h"

/**
 * Tank Waypoint Component
 * Standalone component for waypoint generation and path following.
 * Can be attached to any tank controller (trainer or AI).
 * Uses NavMesh pathfinding to generate obstacle-aware routes.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TANKAICONTROLLER_API UTankWaypointComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTankWaypointComponent();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ========== TARGET SETTINGS ==========

	/** Radius to consider target "reached" (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Target")
	float TargetReachRadius = 200.0f;

	/** Minimum distance for random target generation (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Target")
	float MinTargetDistance = 500.0f;

	/** Maximum distance for random target generation (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Target")
	float MaxTargetDistance = 3000.0f;

	// ========== WAYPOINT SETTINGS ==========

	/** Radius to consider waypoint "reached" (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Path")
	float WaypointReachRadius = 100.0f;

	/** Enable debug visualization of waypoints */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Debug")
	bool bShowDebugVisualization = true;

	/** Color for waypoint debug spheres */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Debug")
	FColor WaypointDebugColor = FColor::Cyan;

	/** Color for target debug sphere */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Debug")
	FColor TargetDebugColor = FColor::Green;

	// ========== TARGET METHODS ==========

	/** Generate a random target on NavMesh within distance range */
	UFUNCTION(BlueprintCallable, Category = "Waypoint|Target")
	bool GenerateRandomTarget();

	/** Set a specific target location */
	UFUNCTION(BlueprintCallable, Category = "Waypoint|Target")
	void SetTarget(FVector Location);

	/** Clear the current target */
	UFUNCTION(BlueprintCallable, Category = "Waypoint|Target")
	void ClearTarget();

	/** Check if current target is reached */
	UFUNCTION(BlueprintPure, Category = "Waypoint|Target")
	bool IsTargetReached() const;

	/** Get current target location */
	UFUNCTION(BlueprintPure, Category = "Waypoint|Target")
	FVector GetTargetLocation() const { return CurrentTargetLocation; }

	/** Check if there's an active target */
	UFUNCTION(BlueprintPure, Category = "Waypoint|Target")
	bool HasActiveTarget() const { return bHasActiveTarget; }

	// ========== WAYPOINT GENERATION ==========

	/** Generate waypoints from owner's pawn to target using NavMesh */
	UFUNCTION(BlueprintCallable, Category = "Waypoint|Path")
	bool GenerateWaypointsToTarget();

	/** Regenerate waypoints from current position (for stuck recovery) */
	UFUNCTION(BlueprintCallable, Category = "Waypoint|Path")
	bool RegenerateWaypointsFromCurrentPosition();

	// ========== WAYPOINT FOLLOWING ==========

	/** Get the current waypoint location */
	UFUNCTION(BlueprintPure, Category = "Waypoint|Path")
	FVector GetCurrentWaypointLocation() const;

	/** Check if current waypoint is reached */
	UFUNCTION(BlueprintPure, Category = "Waypoint|Path")
	bool IsCurrentWaypointReached() const;

	/** Advance to the next waypoint */
	UFUNCTION(BlueprintCallable, Category = "Waypoint|Path")
	void AdvanceToNextWaypoint();

	/** Check if all waypoints are completed */
	UFUNCTION(BlueprintPure, Category = "Waypoint|Path")
	bool AreAllWaypointsCompleted() const;

	/** Get distance to current waypoint (cm) */
	UFUNCTION(BlueprintPure, Category = "Waypoint|Path")
	float GetDistanceToCurrentWaypoint() const;

	/** Get direction to current waypoint in world space */
	UFUNCTION(BlueprintPure, Category = "Waypoint|Path")
	FVector GetDirectionToCurrentWaypoint() const;

	/** Get direction to current waypoint in local space (relative to pawn) */
	UFUNCTION(BlueprintPure, Category = "Waypoint|Path")
	FVector GetLocalDirectionToCurrentWaypoint() const;

	/** Get current waypoint index */
	UFUNCTION(BlueprintPure, Category = "Waypoint|Path")
	int32 GetCurrentWaypointIndex() const { return CurrentWaypointIndex; }

	/** Get total number of waypoints */
	UFUNCTION(BlueprintPure, Category = "Waypoint|Path")
	int32 GetWaypointCount() const { return Waypoints.Num(); }

	// ========== DELEGATES ==========

	/** Called when target is reached */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTargetReached);
	UPROPERTY(BlueprintAssignable, Category = "Waypoint|Events")
	FOnTargetReached OnTargetReached;

	/** Called when waypoint is reached */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWaypointReached, int32, WaypointIndex);
	UPROPERTY(BlueprintAssignable, Category = "Waypoint|Events")
	FOnWaypointReached OnWaypointReached;

	/** Called when waypoints are regenerated */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWaypointsRegenerated);
	UPROPERTY(BlueprintAssignable, Category = "Waypoint|Events")
	FOnWaypointsRegenerated OnWaypointsRegenerated;

protected:
	// ========== INTERNAL STATE ==========

	/** Current target location */
	FVector CurrentTargetLocation = FVector::ZeroVector;

	/** Whether there's an active target */
	bool bHasActiveTarget = false;

	/** Array of waypoints to current target */
	TArray<FVector> Waypoints;

	/** Current waypoint index */
	int32 CurrentWaypointIndex = 0;

	// ========== HELPER METHODS ==========

	/** Get owner's pawn location */
	FVector GetOwnerPawnLocation() const;

	/** Get owner's pawn (cached) */
	APawn* GetOwnerPawn() const;

	/** Draw debug visualization */
	void DrawDebugVisualization() const;

private:
	/** Cached pawn reference */
	UPROPERTY()
	TWeakObjectPtr<APawn> CachedPawn;
};
