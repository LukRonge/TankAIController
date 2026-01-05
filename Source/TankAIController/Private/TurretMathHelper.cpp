// Copyright ArenaBreakers. All Rights Reserved.

#include "TurretMathHelper.h"
#include "WR_Turret.h"
#include "WR_Tank_Pawn.h"

FRotator FTurretMathHelper::GetTurretRotation(const AWR_Turret* Turret)
{
	if (!Turret)
	{
		return FRotator::ZeroRotator;
	}

	// Start with turret actor's base rotation
	FRotator TurretRotation = Turret->GetActorRotation();

	// Add YawComponent rotation (horizontal turret rotation)
	if (Turret->YawComponent)
	{
		const FRotator YawRot = Turret->YawComponent->GetRelativeRotation();
		TurretRotation.Yaw += YawRot.Yaw;
	}

	// Set pitch from PitchComponent (vertical turret rotation)
	if (Turret->PitchComponent)
	{
		const FRotator PitchRot = Turret->PitchComponent->GetRelativeRotation();
		TurretRotation.Pitch = PitchRot.Pitch;
	}

	return TurretRotation;
}

FRotator FTurretMathHelper::GetTurretRotationFromTank(const AWR_Tank_Pawn* TankPawn)
{
	if (!TankPawn)
	{
		return FRotator::ZeroRotator;
	}

	// Get turret from tank - need const_cast because GetTurret_Implementation is not const
	AWR_Tank_Pawn* MutableTank = const_cast<AWR_Tank_Pawn*>(TankPawn);
	AWR_Turret* Turret = Cast<AWR_Turret>(MutableTank->GetTurret_Implementation());

	return GetTurretRotation(Turret);
}

void FTurretMathHelper::CalculateAimAngles(
	const FVector& TurretLocation,
	const FVector& TargetLocation,
	float TankWorldYaw,
	float& OutRelativeYaw,
	float& OutPitch)
{
	const FVector ToTarget = TargetLocation - TurretLocation;

	// Calculate world yaw to target
	const float DesiredWorldYaw = FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Y, ToTarget.X));

	// Calculate relative yaw (relative to tank body)
	OutRelativeYaw = FRotator::NormalizeAxis(DesiredWorldYaw - TankWorldYaw);

	// Calculate pitch (angle from horizontal to target)
	const float HorizontalDist = FMath::Sqrt(ToTarget.X * ToTarget.X + ToTarget.Y * ToTarget.Y);
	OutPitch = FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Z, HorizontalDist));
}

void FTurretMathHelper::GetTurretLocationAndDirection(
	const AWR_Turret* Turret,
	FVector& OutLocation,
	FVector& OutDirection)
{
	if (!Turret)
	{
		OutLocation = FVector::ZeroVector;
		OutDirection = FVector::ForwardVector;
		return;
	}

	// Priority: PitchComponent > YawComponent > Actor
	if (Turret->PitchComponent)
	{
		OutLocation = Turret->PitchComponent->GetComponentLocation();
		OutDirection = Turret->PitchComponent->GetForwardVector();
	}
	else if (Turret->YawComponent)
	{
		OutLocation = Turret->GetActorLocation();
		OutDirection = Turret->YawComponent->GetForwardVector();
	}
	else
	{
		OutLocation = Turret->GetActorLocation();
		OutDirection = Turret->GetActorForwardVector();
	}
}

FVector FTurretMathHelper::AnglesToDirection(float WorldYaw, float Pitch)
{
	const float YawRad = FMath::DegreesToRadians(WorldYaw);
	const float PitchRad = FMath::DegreesToRadians(Pitch);
	const float CosPitch = FMath::Cos(PitchRad);

	return FVector(
		FMath::Cos(YawRad) * CosPitch,
		FMath::Sin(YawRad) * CosPitch,
		FMath::Sin(PitchRad)
	);
}

FVector FTurretMathHelper::ProjectTargetLocation(
	const FVector& TurretLocation,
	float WorldYaw,
	float Pitch,
	float Distance)
{
	const FVector Direction = AnglesToDirection(WorldYaw, Pitch);
	return TurretLocation + Direction * Distance;
}

float FTurretMathHelper::ClampPitch(float Pitch, float MinPitch, float MaxPitch)
{
	return FMath::Clamp(Pitch, MinPitch, MaxPitch);
}
