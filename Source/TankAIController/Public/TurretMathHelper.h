// Copyright ArenaBreakers. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AWR_Turret;
class AWR_Tank_Pawn;

/**
 * Static utility class for turret-related calculations.
 * Centralizes turret math used across AI controllers and detection systems.
 *
 * USAGE:
 * - FTurretMathHelper::GetTurretRotation(Turret) - Get current turret rotation
 * - FTurretMathHelper::CalculateAimAngles(TurretLocation, TargetLocation, TankYaw) - Calculate aim angles
 * - FTurretMathHelper::GetTurretLocationAndDirection(Turret) - Get turret position and forward vector
 */
struct TANKAICONTROLLER_API FTurretMathHelper
{
	/**
	 * Get current turret rotation combining YawComponent and PitchComponent.
	 * @param Turret - The turret actor
	 * @return Combined rotation (Yaw from YawComponent, Pitch from PitchComponent)
	 */
	static FRotator GetTurretRotation(const AWR_Turret* Turret);

	/**
	 * Get turret rotation from tank pawn (convenience wrapper).
	 * @param TankPawn - The tank pawn
	 * @return Turret rotation or ZeroRotator if no turret
	 */
	static FRotator GetTurretRotationFromTank(const AWR_Tank_Pawn* TankPawn);

	/**
	 * Calculate desired yaw and pitch angles to aim at a target location.
	 * @param TurretLocation - Current turret world location
	 * @param TargetLocation - Target world location to aim at
	 * @param TankWorldYaw - Tank's world yaw rotation (for relative calculation)
	 * @param OutRelativeYaw - Output: Yaw angle relative to tank (degrees)
	 * @param OutPitch - Output: Pitch angle (degrees)
	 */
	static void CalculateAimAngles(
		const FVector& TurretLocation,
		const FVector& TargetLocation,
		float TankWorldYaw,
		float& OutRelativeYaw,
		float& OutPitch);

	/**
	 * Get turret location and forward direction from turret actor.
	 * Uses PitchComponent if available, falls back to YawComponent, then actor.
	 * @param Turret - The turret actor
	 * @param OutLocation - Output: Turret world location (barrel origin)
	 * @param OutDirection - Output: Turret forward direction (barrel direction)
	 */
	static void GetTurretLocationAndDirection(
		const AWR_Turret* Turret,
		FVector& OutLocation,
		FVector& OutDirection);

	/**
	 * Convert yaw/pitch angles back to a world direction vector.
	 * @param WorldYaw - World yaw angle (degrees)
	 * @param Pitch - Pitch angle (degrees)
	 * @return Normalized direction vector
	 */
	static FVector AnglesToDirection(float WorldYaw, float Pitch);

	/**
	 * Project a target location at a given distance along interpolated angles.
	 * Useful for smooth turret tracking.
	 * @param TurretLocation - Turret world location
	 * @param WorldYaw - World yaw angle (degrees)
	 * @param Pitch - Pitch angle (degrees)
	 * @param Distance - Distance to project (default 10000cm)
	 * @return World location of projected target point
	 */
	static FVector ProjectTargetLocation(
		const FVector& TurretLocation,
		float WorldYaw,
		float Pitch,
		float Distance = 10000.0f);

	/**
	 * Clamp pitch angle to typical turret limits.
	 * @param Pitch - Input pitch angle (degrees)
	 * @param MinPitch - Minimum pitch (default -10)
	 * @param MaxPitch - Maximum pitch (default +20)
	 * @return Clamped pitch angle
	 */
	static float ClampPitch(float Pitch, float MinPitch = -10.0f, float MaxPitch = 20.0f);
};
