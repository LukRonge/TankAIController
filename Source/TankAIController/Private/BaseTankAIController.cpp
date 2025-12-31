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
		// FIXED: Initialize to max distance (clear space) instead of -1.0f
		// UE Learning Agents will normalize this by EllipseMajorAxis (350.0f) to get 1.0
		LineTraceDistances[i] = EllipseMajorAxis;
	}
}

void ABaseTankAIController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (ControlledTank)
	{
		// Perform line traces for obstacle detection
		PerformLineTraces();

		// Perform lateral traces for corridor wall detection
		PerformLateralTraces();

		// Update angular velocity for smooth steering observation
		UpdateAngularVelocity(DeltaTime);
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

		// Store raw distance to obstacle (in cm), or max distance if no obstacle
		if (bHit)
		{
			// Calculate real distance to obstacle in cm
			float Distance = FVector::Dist(TraceStart, HitResult.ImpactPoint);
			LineTraceDistances[i] = Distance;
		}
		else
		{
			// FIXED: No obstacle detected - set to max ellipse distance (clear space)
			// UE Learning Agents will normalize this by EllipseMajorAxis to get 1.0
			// Previous: -1.0f caused incorrect normalization (-1.0 / 350.0 = -0.003)
			LineTraceDistances[i] = EllipseMajorAxis;
		}

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

	// ========================================================================
	// FIXED: Use SetAIMovementInput instead of MoveForward/MoveRight
	// ========================================================================
	// MoveForward_Implementation and MoveRight_Implementation call MoveForward()
	// and MoveRight() which check IsLocallyControlled(). For AI-controlled tanks,
	// this returns false and movement values are reset to 0.
	// SetAIMovementInput bypasses this check and applies movement directly.
	ControlledTank->SetAIMovementInput(Throttle, Steering);
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

// ========== NARROW CORRIDOR METHODS ==========

void ABaseTankAIController::PerformLateralTraces()
{
	if (!ControlledTank || !GetWorld())
	{
		return;
	}

	const FVector Origin = ControlledTank->GetActorLocation();
	const FRotator Rotation = ControlledTank->GetActorRotation();

	// Calculate left and right directions (perpendicular to forward)
	const FVector LeftDir = Rotation.RotateVector(FVector(0.0f, -1.0f, 0.0f));
	const FVector RightDir = Rotation.RotateVector(FVector(0.0f, 1.0f, 0.0f));

	// Setup trace parameters
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(ControlledTank);
	QueryParams.bTraceComplex = false;

	FHitResult HitResult;

	// Left trace
	LeftClearance = LateralTraceDistance;
	if (GetWorld()->LineTraceSingleByChannel(
		HitResult,
		Origin,
		Origin + LeftDir * LateralTraceDistance,
		ECC_Visibility,
		QueryParams))
	{
		LeftClearance = HitResult.Distance;
	}

	// Right trace
	RightClearance = LateralTraceDistance;
	if (GetWorld()->LineTraceSingleByChannel(
		HitResult,
		Origin,
		Origin + RightDir * LateralTraceDistance,
		ECC_Visibility,
		QueryParams))
	{
		RightClearance = HitResult.Distance;
	}

	// Debug visualization
	if (bDrawDebugTraces)
	{
		const FColor LeftColor = (LeftClearance < LateralTraceDistance) ? FColor::Orange : FColor::Cyan;
		const FColor RightColor = (RightClearance < LateralTraceDistance) ? FColor::Orange : FColor::Cyan;

		DrawDebugLine(GetWorld(), Origin, Origin + LeftDir * LeftClearance, LeftColor, false, -1.0f, 0, 3.0f);
		DrawDebugLine(GetWorld(), Origin, Origin + RightDir * RightClearance, RightColor, false, -1.0f, 0, 3.0f);
	}
}

void ABaseTankAIController::UpdateAngularVelocity(float DeltaTime)
{
	if (!ControlledTank || DeltaTime <= 0.0f)
	{
		return;
	}

	const float CurrentYaw = ControlledTank->GetActorRotation().Yaw;

	// Calculate yaw delta (handles wrap-around at ±180)
	float YawDelta = CurrentYaw - PreviousYaw;

	// Normalize to -180 to 180 range
	while (YawDelta > 180.0f) YawDelta -= 360.0f;
	while (YawDelta < -180.0f) YawDelta += 360.0f;

	// Calculate angular velocity in degrees per second
	CurrentAngularVelocityZ = YawDelta / DeltaTime;

	// Clamp to reasonable range (±360 deg/sec max)
	CurrentAngularVelocityZ = FMath::Clamp(CurrentAngularVelocityZ, -360.0f, 360.0f);

	// Store current yaw for next frame
	PreviousYaw = CurrentYaw;
}
