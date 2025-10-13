// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseTankAIController.h"
#include "WR_Tank_Pawn.h"
#include "WR_ControlsInterface.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "DrawDebugHelpers.h"
#include "Kismet/KismetMathLibrary.h"

ABaseTankAIController::ABaseTankAIController()
{
	PrimaryActorTick.bCanEverTick = true;

	// Initialize state
	CurrentThrottle = 0.0f;
	CurrentSteering = 0.0f;
	CurrentBrake = 0.0f;
	CurrentTurretRotation = FRotator::ZeroRotator;
}

void ABaseTankAIController::BeginPlay()
{
	Super::BeginPlay();

	// Initialize line trace distances array
	LineTraceDistances.SetNum(NumLineTraces);
	for (int32 i = 0; i < NumLineTraces; i++)
	{
		LineTraceDistances[i] = 1.0f; // Initialize to max distance (normalized)
	}
}

void ABaseTankAIController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (ControlledTank)
	{
		// Perform line traces for obstacle detection
		PerformLineTraces();
	}
}

void ABaseTankAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Cache tank reference
	ControlledTank = Cast<AWR_Tank_Pawn>(InPawn);

	if (ControlledTank)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s::OnPossess - Successfully possessed tank: %s (Class: %s)"),
			*GetName(), *ControlledTank->GetName(), *ControlledTank->GetClass()->GetName());

		// Check if tank implements WR_ControlsInterface
		if (ControlledTank->GetClass()->ImplementsInterface(UWR_ControlsInterface::StaticClass()))
		{
			UE_LOG(LogTemp, Log, TEXT("  -> Tank implements IWR_ControlsInterface: YES"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("  -> Tank implements IWR_ControlsInterface: NO"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("%s::OnPossess - Failed to possess tank - invalid pawn type (Pawn: %s)"),
			*GetName(), InPawn ? *InPawn->GetName() : TEXT("NULL"));
	}
}

void ABaseTankAIController::PerformLineTraces()
{
	if (!ControlledTank)
	{
		return;
	}

	// Get trace start location (tank center)
	const FVector TraceStart = ControlledTank->GetActorLocation();
	const FRotator TankRotation = ControlledTank->GetActorRotation();

	// Generate ellipse trace points
	TArray<FVector> TracePoints = GenerateEllipseTracePoints();

	// Setup trace parameters
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(ControlledTank);
	QueryParams.bTraceComplex = false;

	// Perform traces
	for (int32 i = 0; i < TracePoints.Num(); i++)
	{
		// Transform local point to world space
		const FVector LocalPoint = TracePoints[i];
		const FVector WorldPoint = TraceStart + TankRotation.RotateVector(LocalPoint);

		// Perform line trace
		FHitResult HitResult;
		bool bHit = GetWorld()->LineTraceSingleByChannel(
			HitResult,
			TraceStart,
			WorldPoint,
			ECC_Visibility,
			QueryParams
		);

		// Calculate normalized distance
		float Distance;
		if (bHit)
		{
			Distance = FVector::Dist(TraceStart, HitResult.ImpactPoint);
		}
		else
		{
			Distance = MaxTraceDistance;
		}

		// Normalize distance (0 = obstacle at tank, 1 = no obstacle within max distance)
		LineTraceDistances[i] = FMath::Clamp(Distance / MaxTraceDistance, 0.0f, 1.0f);

		// Debug visualization
		if (bDrawDebugTraces)
		{
			FColor DebugColor = bHit ? FColor::Red : FColor::Green;
			DrawDebugLine(GetWorld(), TraceStart, bHit ? HitResult.ImpactPoint : WorldPoint, DebugColor, false, -1.0f, 0, 2.0f);
		}
	}
}

TArray<FVector> ABaseTankAIController::GenerateEllipseTracePoints() const
{
	TArray<FVector> Points;
	Points.Reserve(NumLineTraces);

	for (int32 i = 0; i < NumLineTraces; i++)
	{
		// Calculate angle for this trace (evenly distributed around ellipse)
		const float Angle = (2.0f * PI * i) / NumLineTraces;

		// Calculate ellipse point in local space (X = forward, Y = right)
		const float X = EllipseMajorAxis * FMath::Cos(Angle);
		const float Y = EllipseMinorAxis * FMath::Sin(Angle);

		Points.Add(FVector(X, Y, 0.0f));
	}

	return Points;
}

void ABaseTankAIController::ApplyMovementToTank(float Throttle, float Steering)
{
	if (!ControlledTank)
	{
		return;
	}

	// Update current values
	CurrentThrottle = Throttle;
	CurrentSteering = Steering;

	// Apply to tank via interface
	ControlledTank->MoveForward_Implementation(Throttle);
	ControlledTank->MoveRight_Implementation(Steering);
}

void ABaseTankAIController::ApplyTurretRotationToTank(const FRotator& TurretRotation)
{
	if (!ControlledTank)
	{
		return;
	}

	// Update current value
	CurrentTurretRotation = TurretRotation;

	// Apply turret rotation to tank via WR_ControlsInterface
	// LookUp controls turret pitch (vertical), LookRight controls turret yaw (horizontal)

	// Get turret actor to calculate delta rotation
	AActor* Turret = ControlledTank->GetTurret_Implementation();
	if (Turret)
	{
		// Calculate delta rotation needed
		FRotator CurrentRot = Turret->GetActorRotation();
		FRotator DeltaRot = TurretRotation - CurrentRot;
		DeltaRot.Normalize();

		// Apply delta rotation via interface
		// Scale down the delta to avoid overshooting
		const float RotationSpeed = 2.0f;
		ControlledTank->LookUp_Implementation(DeltaRot.Pitch * RotationSpeed);
		ControlledTank->LookRight_Implementation(DeltaRot.Yaw * RotationSpeed);
	}
}

FVector ABaseTankAIController::GetTankVelocity() const
{
	if (!ControlledTank)
	{
		return FVector::ZeroVector;
	}

	return ControlledTank->GetVelocity();
}

FRotator ABaseTankAIController::GetTankRotation() const
{
	if (!ControlledTank)
	{
		return FRotator::ZeroRotator;
	}

	return ControlledTank->GetActorRotation();
}

float ABaseTankAIController::GetForwardSpeed() const
{
	if (!ControlledTank)
	{
		return 0.0f;
	}

	const FVector Velocity = ControlledTank->GetVelocity();
	const FVector Forward = ControlledTank->GetActorForwardVector();

	// Project velocity onto forward vector
	return FVector::DotProduct(Velocity, Forward);
}

FRotator ABaseTankAIController::GetTurretRotation() const
{
	if (!ControlledTank)
	{
		return FRotator::ZeroRotator;
	}

	// Get turret actor from tank
	AActor* Turret = ControlledTank->GetTurret_Implementation();
	if (Turret)
	{
		return Turret->GetActorRotation();
	}

	return FRotator::ZeroRotator;
}
