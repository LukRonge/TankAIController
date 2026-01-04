// Copyright Epic Games, Inc. All Rights Reserved.

#include "TankWaypointComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

UTankWaypointComponent::UTankWaypointComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UTankWaypointComponent::BeginPlay()
{
	Super::BeginPlay();

	// Cache the pawn reference
	if (AActor* Owner = GetOwner())
	{
		if (AController* Controller = Cast<AController>(Owner))
		{
			CachedPawn = Controller->GetPawn();
		}
		else if (APawn* Pawn = Cast<APawn>(Owner))
		{
			CachedPawn = Pawn;
		}
	}
}

void UTankWaypointComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Auto-advance waypoints when reached
	if (bHasActiveTarget && Waypoints.Num() > 0 && CurrentWaypointIndex < Waypoints.Num())
	{
		if (IsCurrentWaypointReached())
		{
			AdvanceToNextWaypoint();
		}
	}

	if (bShowDebugVisualization)
	{
		DrawDebugVisualization();
	}
}

// ========== TARGET METHODS ==========

bool UTankWaypointComponent::GenerateRandomTarget()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		UE_LOG(LogTemp, Error, TEXT("TankWaypointComponent: NavigationSystem not found!"));
		return false;
	}

	const FVector Origin = GetOwnerPawnLocation();
	if (Origin.IsZero())
	{
		UE_LOG(LogTemp, Error, TEXT("TankWaypointComponent: Owner pawn location is invalid!"));
		return false;
	}

	// Try to find a valid NavMesh point
	constexpr int32 MaxRetries = 20;
	float CurrentMinDistance = MinTargetDistance;
	float CurrentMaxDistance = MaxTargetDistance;

	for (int32 Retry = 0; Retry < MaxRetries; ++Retry)
	{
		// Reduce search range every 5 retries
		if (Retry > 0 && Retry % 5 == 0)
		{
			CurrentMaxDistance = FMath::Max(CurrentMinDistance * 1.5f, CurrentMaxDistance * 0.7f);
		}

		// Generate random direction and distance
		const float RandomAngle = FMath::RandRange(0.0f, 2.0f * PI);
		const float RandomDistance = FMath::RandRange(CurrentMinDistance, CurrentMaxDistance);

		FVector RandomOffset(
			RandomDistance * FMath::Cos(RandomAngle),
			RandomDistance * FMath::Sin(RandomAngle),
			0.0f
		);

		FVector CandidateLocation = Origin + RandomOffset;

		// Project to NavMesh
		FNavLocation ProjectedLocation;
		const FVector ProjectExtent(500.0f, 500.0f, 500.0f);

		if (NavSys->ProjectPointToNavigation(CandidateLocation, ProjectedLocation, ProjectExtent))
		{
			CurrentTargetLocation = ProjectedLocation.Location;
			bHasActiveTarget = true;

			// Generate waypoints to the new target
			GenerateWaypointsToTarget();

			UE_LOG(LogTemp, Log, TEXT("WaypointComponent: Target at %.1fm, %d waypoints"),
				FVector::Dist(Origin, CurrentTargetLocation) / 100.0f, Waypoints.Num());

			return true;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("TankWaypointComponent: Failed to generate random target after %d retries"), MaxRetries);
	return false;
}

void UTankWaypointComponent::SetTarget(FVector Location)
{
	CurrentTargetLocation = Location;
	bHasActiveTarget = true;
	GenerateWaypointsToTarget();
}

void UTankWaypointComponent::ClearTarget()
{
	bHasActiveTarget = false;
	CurrentTargetLocation = FVector::ZeroVector;
	Waypoints.Empty();
	CurrentWaypointIndex = 0;
}

bool UTankWaypointComponent::IsTargetReached() const
{
	if (!bHasActiveTarget)
	{
		return false;
	}

	const FVector PawnLocation = GetOwnerPawnLocation();
	if (PawnLocation.IsZero())
	{
		return false;
	}

	const float Distance = FVector::Dist2D(PawnLocation, CurrentTargetLocation);
	return Distance <= TargetReachRadius;
}

// ========== WAYPOINT GENERATION ==========

bool UTankWaypointComponent::GenerateWaypointsToTarget()
{
	if (!bHasActiveTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankWaypointComponent: No active target for waypoint generation"));
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		UE_LOG(LogTemp, Error, TEXT("TankWaypointComponent: NavigationSystem not found!"));
		return false;
	}

	FVector StartLocation = GetOwnerPawnLocation();
	FVector EndLocation = CurrentTargetLocation;

	if (StartLocation.IsZero())
	{
		UE_LOG(LogTemp, Error, TEXT("TankWaypointComponent: Owner pawn location is invalid!"));
		return false;
	}

	// Project start and end to NavMesh
	FNavLocation StartProj, EndProj;
	const FVector ProjectExtent(500.0f, 500.0f, 500.0f);

	const bool bStartProjected = NavSys->ProjectPointToNavigation(StartLocation, StartProj, ProjectExtent);
	const bool bEndProjected = NavSys->ProjectPointToNavigation(EndLocation, EndProj, ProjectExtent);

	if (bStartProjected) StartLocation = StartProj.Location;
	if (bEndProjected) EndLocation = EndProj.Location;

	// Find navigation path
	UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
		World,
		StartLocation,
		EndLocation,
		nullptr,
		nullptr
	);

	Waypoints.Empty();
	CurrentWaypointIndex = 0;

	if (Path && Path->IsValid() && Path->PathPoints.Num() > 0)
	{
		// Copy all path points as waypoints
		for (const FVector& PathPoint : Path->PathPoints)
		{
			Waypoints.Add(PathPoint);
		}

		// If path is partial, ensure we still reach the target
		if (Path->IsPartial() && Waypoints.Num() > 0)
		{
			const float DistToTarget = FVector::Dist(Waypoints.Last(), EndLocation);
			if (DistToTarget > 50.0f)
			{
				Waypoints.Add(EndLocation);
			}
		}

		return true;
	}
	else
	{
		// Fallback: direct path
		Waypoints.Add(StartLocation);
		Waypoints.Add(EndLocation);
		return true;
	}
}

bool UTankWaypointComponent::RegenerateWaypointsFromCurrentPosition()
{
	if (!bHasActiveTarget)
	{
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("WaypointComponent: Regenerating waypoints"));

	const bool bSuccess = GenerateWaypointsToTarget();

	if (bSuccess)
	{
		OnWaypointsRegenerated.Broadcast();
	}

	return bSuccess;
}

// ========== WAYPOINT FOLLOWING ==========

FVector UTankWaypointComponent::GetCurrentWaypointLocation() const
{
	if (Waypoints.Num() == 0)
	{
		return CurrentTargetLocation;
	}

	if (CurrentWaypointIndex >= Waypoints.Num())
	{
		return CurrentTargetLocation;
	}

	return Waypoints[CurrentWaypointIndex];
}

bool UTankWaypointComponent::IsCurrentWaypointReached() const
{
	if (Waypoints.Num() == 0 || CurrentWaypointIndex >= Waypoints.Num())
	{
		return false;
	}

	const FVector PawnLocation = GetOwnerPawnLocation();
	if (PawnLocation.IsZero())
	{
		return false;
	}

	const FVector WaypointLocation = Waypoints[CurrentWaypointIndex];
	const float Distance = FVector::Dist2D(PawnLocation, WaypointLocation);

	return Distance <= WaypointReachRadius;
}

void UTankWaypointComponent::AdvanceToNextWaypoint()
{
	if (CurrentWaypointIndex < Waypoints.Num())
	{
		const int32 ReachedIndex = CurrentWaypointIndex;
		CurrentWaypointIndex++;

		OnWaypointReached.Broadcast(ReachedIndex);

		// Log only at key milestones
		if (AreAllWaypointsCompleted())
		{
			UE_LOG(LogTemp, Log, TEXT("WaypointComponent: All %d waypoints completed"), Waypoints.Num());
		}
	}
}

bool UTankWaypointComponent::AreAllWaypointsCompleted() const
{
	return CurrentWaypointIndex >= Waypoints.Num();
}

float UTankWaypointComponent::GetDistanceToCurrentWaypoint() const
{
	const FVector PawnLocation = GetOwnerPawnLocation();
	if (PawnLocation.IsZero())
	{
		return 0.0f;
	}

	const FVector WaypointLocation = GetCurrentWaypointLocation();
	return FVector::Dist(PawnLocation, WaypointLocation);
}

FVector UTankWaypointComponent::GetDirectionToCurrentWaypoint() const
{
	const FVector PawnLocation = GetOwnerPawnLocation();
	if (PawnLocation.IsZero())
	{
		return FVector::ForwardVector;
	}

	const FVector WaypointLocation = GetCurrentWaypointLocation();
	FVector Direction = WaypointLocation - PawnLocation;
	Direction.Normalize();

	return Direction;
}

FVector UTankWaypointComponent::GetLocalDirectionToCurrentWaypoint() const
{
	APawn* Pawn = GetOwnerPawn();
	if (!Pawn)
	{
		return FVector::ForwardVector;
	}

	const FVector WorldDirection = GetDirectionToCurrentWaypoint();
	const FRotator PawnRotation = Pawn->GetActorRotation();

	// Transform world direction to local space
	return PawnRotation.UnrotateVector(WorldDirection);
}

// ========== HELPER METHODS ==========

FVector UTankWaypointComponent::GetOwnerPawnLocation() const
{
	APawn* Pawn = GetOwnerPawn();
	if (Pawn)
	{
		return Pawn->GetActorLocation();
	}
	return FVector::ZeroVector;
}

APawn* UTankWaypointComponent::GetOwnerPawn() const
{
	// Try cached pawn first
	if (CachedPawn.IsValid())
	{
		return CachedPawn.Get();
	}

	// Try to get from owner
	if (AActor* Owner = GetOwner())
	{
		if (AController* Controller = Cast<AController>(Owner))
		{
			return Controller->GetPawn();
		}
		else if (APawn* Pawn = Cast<APawn>(Owner))
		{
			return Pawn;
		}
	}

	return nullptr;
}

void UTankWaypointComponent::DrawDebugVisualization() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Draw target
	if (bHasActiveTarget)
	{
		DrawDebugSphere(World, CurrentTargetLocation, TargetReachRadius, 12, TargetDebugColor, false, -1.0f, 0, 2.0f);
	}

	// Draw waypoints
	for (int32 i = 0; i < Waypoints.Num(); ++i)
	{
		const FVector& WP = Waypoints[i];

		// Color based on status
		FColor Color;
		if (i < CurrentWaypointIndex)
		{
			Color = FColor(100, 100, 100);  // Completed - gray
		}
		else if (i == CurrentWaypointIndex)
		{
			Color = FColor::Yellow;  // Current - yellow
		}
		else
		{
			Color = WaypointDebugColor;  // Future - cyan
		}

		DrawDebugSphere(World, WP, WaypointReachRadius * 0.5f, 8, Color, false, -1.0f, 0, 1.5f);

		// Draw line to next waypoint
		if (i < Waypoints.Num() - 1)
		{
			DrawDebugLine(World, WP, Waypoints[i + 1], Color, false, -1.0f, 0, 1.0f);
		}
	}

	// Draw line from current position to current waypoint
	const FVector PawnLocation = GetOwnerPawnLocation();
	if (!PawnLocation.IsZero() && CurrentWaypointIndex < Waypoints.Num())
	{
		DrawDebugLine(World, PawnLocation, Waypoints[CurrentWaypointIndex], FColor::Yellow, false, -1.0f, 0, 2.0f);
	}
}
