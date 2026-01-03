// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseTankAIController.h"
#include "WR_Tank_Pawn.h"
#include "WR_Turret.h"
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

	// Get tank center location and rotation
	const FVector TankCenter = ControlledTank->GetActorLocation();
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
		// Calculate angle for this trace
		const float Angle = (2.0f * PI * i) / NumLineTraces;

		// Calculate offset from center to tank surface (v8.5)
		float SurfaceOffset = 0.0f;
		if (bOffsetTracesFromSurface)
		{
			SurfaceOffset = CalculateTankSurfaceOffset(Angle);
		}

		// Calculate trace start point (either center or surface)
		const FVector LocalOffset = FVector(
			SurfaceOffset * FMath::Cos(Angle),
			SurfaceOffset * FMath::Sin(Angle),
			0.0f
		);
		const FVector TraceStart = TankCenter + TankRotation.RotateVector(LocalOffset);

		// Transform ellipse end point to world space
		const FVector LocalEndPoint = TracePoints[i];
		const FVector TraceEnd = TankCenter + TankRotation.RotateVector(LocalEndPoint);

		// Perform line trace
		FHitResult HitResult;
		bool bHit = GetWorld()->LineTraceSingleByChannel(
			HitResult,
			TraceStart,
			TraceEnd,
			ECC_Visibility,
			QueryParams
		);

		// Store distance - now represents actual clearance from tank surface!
		if (bHit)
		{
			// Distance from trace start (surface) to obstacle
			float Distance = FVector::Dist(TraceStart, HitResult.ImpactPoint);
			LineTraceDistances[i] = Distance;
		}
		else
		{
			// No obstacle - max distance minus the offset (actual clearance)
			// For ellipse: the max distance in this direction minus surface offset
			float MaxDistanceInDirection = FVector::Dist(TankCenter, TraceEnd);
			LineTraceDistances[i] = MaxDistanceInDirection - SurfaceOffset;
		}

		// Debug visualization with color coding (v8.3)
		if (bDrawDebugTraces)
		{
			FColor DebugColor;
			float LineThickness = 2.0f;

			// Color coding by trace direction:
			// Forward (0) = Blue, Back (12) = Magenta
			// Corners (3, 9, 15, 21) = Yellow/Orange
			// Lateral (6, 18) = Cyan
			// Others = Green/Red based on hit
			if (i == 0)
			{
				// Forward - Blue
				DebugColor = bHit ? FColor(100, 100, 255) : FColor::Blue;
				LineThickness = 4.0f;
			}
			else if (i == 12)
			{
				// Back - Magenta
				DebugColor = bHit ? FColor(200, 0, 200) : FColor::Magenta;
				LineThickness = 3.0f;
			}
			else if (i == 3 || i == 9 || i == 15 || i == 21)
			{
				// Corners - Yellow/Orange
				DebugColor = bHit ? FColor::Orange : FColor::Yellow;
				LineThickness = 3.5f;
			}
			else if (i == 6 || i == 18)
			{
				// Lateral - Cyan
				DebugColor = bHit ? FColor(0, 150, 150) : FColor::Cyan;
				LineThickness = 3.0f;
			}
			else
			{
				// Other traces - Green/Red
				DebugColor = bHit ? FColor::Red : FColor::Green;
			}

			DrawDebugLine(GetWorld(), TraceStart, bHit ? HitResult.ImpactPoint : TraceEnd, DebugColor, false, -1.0f, 0, LineThickness);
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

float ABaseTankAIController::CalculateTankSurfaceOffset(float AngleRad) const
{
	// Calculate distance from tank center to surface for rectangular tank
	// Tank is approximated as a rectangle with dimensions TankHalfLength x TankHalfWidth
	//
	// For a ray from center at angle AngleRad:
	// - If ray hits front/back face: offset = HalfLength / |cos(angle)|
	// - If ray hits left/right face: offset = HalfWidth / |sin(angle)|
	// The actual offset is the minimum of these two (first intersection)

	const float CosA = FMath::Cos(AngleRad);
	const float SinA = FMath::Sin(AngleRad);
	const float AbsCosA = FMath::Abs(CosA);
	const float AbsSinA = FMath::Abs(SinA);

	// Avoid division by zero for exact cardinal directions
	constexpr float Epsilon = 0.0001f;

	float OffsetLength = TankHalfLength;  // Default for pure forward/backward
	float OffsetWidth = TankHalfWidth;    // Default for pure left/right

	if (AbsCosA > Epsilon)
	{
		OffsetLength = TankHalfLength / AbsCosA;
	}
	else
	{
		OffsetLength = TankHalfWidth;  // Pure sideways, use width
	}

	if (AbsSinA > Epsilon)
	{
		OffsetWidth = TankHalfWidth / AbsSinA;
	}
	else
	{
		OffsetWidth = TankHalfLength;  // Pure forward/back, use length
	}

	// Return minimum (first intersection with rectangle boundary)
	return FMath::Min(OffsetLength, OffsetWidth);
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

	// Get turret actor from tank and cast to AWR_Turret to access YawComponent/PitchComponent
	AWR_Turret* Turret = Cast<AWR_Turret>(ControlledTank->GetTurret_Implementation());
	if (Turret)
	{
		// The turret rotation is composed of:
		// - YawComponent: relative yaw rotation (horizontal)
		// - PitchComponent: relative pitch rotation (vertical)
		// We need to combine them with the turret actor's base rotation

		FRotator TurretBaseRot = Turret->GetActorRotation();

		// Get YawComponent rotation (horizontal turret rotation)
		if (Turret->YawComponent)
		{
			FRotator YawRot = Turret->YawComponent->GetRelativeRotation();
			TurretBaseRot.Yaw += YawRot.Yaw;
		}

		// Get PitchComponent rotation (vertical turret rotation)
		if (Turret->PitchComponent)
		{
			FRotator PitchRot = Turret->PitchComponent->GetRelativeRotation();
			TurretBaseRot.Pitch = PitchRot.Pitch;
		}

		return TurretBaseRot;
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
