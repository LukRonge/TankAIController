// Copyright Epic Games, Inc. All Rights Reserved.

#include "TankLearningAgentsInteractor.h"
#include "WR_Tank_Pawn.h"
#include "AILearningAgentsController.h"
#include "BaseTankAIController.h"
#include "LearningAgentsManager.h"
#include "TankLearningAgentsManager.h"

// Learning Agents observation/action includes
#include "LearningAgentsObservations.h"
#include "LearningAgentsActions.h"

void UTankLearningAgentsInteractor::SpecifyAgentObservation_Implementation(
	FLearningAgentsObservationSchemaElement& OutObservationSchemaElement,
	ULearningAgentsObservationSchema* InObservationSchema)
{
	// Create a struct to hold all observations
	// NARROW CORRIDOR OPTIMIZED (v2.0):
	// - LineTraces: 24 traces (increased from 16) for better wall detection
	// - ForwardSpeed: current speed feedback
	// - RelativeCurrentWaypointPosition: direction to navigate (LOCAL SPACE)
	// - DistanceToCurrentWaypoint: how far to waypoint
	// - RelativeTargetPosition: final target direction (LOCAL SPACE)
	// - DistanceToTarget: how far to final target
	// - AngularVelocityZ: yaw rate for smooth steering
	// - LeftClearance/RightClearance: lateral wall detection
	// - MinObstacleDistance: closest obstacle awareness
	TMap<FName, FLearningAgentsObservationSchemaElement> ObservationElements;

	// Line traces observation - array of 24 raw distances in cm (0-600cm or max if no obstacle)
	// Scale is set to 600.0f (max ellipse major axis - REDUCED for narrow corridors)
	// Result: 0.0 = obstacle at tank, 1.0 = clear path
	ObservationElements.Add(TEXT("LineTraces"),
		ULearningAgentsObservations::SpecifyContinuousObservation(InObservationSchema, 24, 600.0f, TEXT("LineTraces")));

	// Forward speed observation - UGV drone max speed ~800-1000 cm/s (8-10 m/s)
	// Scale = 1000.0f so normalized range is approximately -1 to 1
	ObservationElements.Add(TEXT("ForwardSpeed"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 1000.0f));

	// Relative position to current waypoint (direction vector - normalized, LOCAL SPACE)
	// X > 0 = waypoint in front, Y > 0 = waypoint to the right
	// MANUAL ENCODING: Using 3 separate floats instead of MakeDirectionObservation
	// because MakeDirectionObservation may use different transform logic
	ObservationElements.Add(TEXT("WaypointDirX"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 1.0f));
	ObservationElements.Add(TEXT("WaypointDirY"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 1.0f));
	ObservationElements.Add(TEXT("WaypointDirZ"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 1.0f));

	// Distance to current waypoint - scale 5000.0f (50m max reasonable distance)
	ObservationElements.Add(TEXT("DistanceToCurrentWaypoint"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 5000.0f));

	// Relative position to final target (direction vector - normalized, LOCAL SPACE)
	// MANUAL ENCODING: Using 3 separate floats
	ObservationElements.Add(TEXT("TargetDirX"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 1.0f));
	ObservationElements.Add(TEXT("TargetDirY"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 1.0f));
	ObservationElements.Add(TEXT("TargetDirZ"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 1.0f));

	// Distance to final target - scale 10000.0f (100m max reasonable distance)
	ObservationElements.Add(TEXT("DistanceToTarget"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 10000.0f));

	// ========== NARROW CORRIDOR OBSERVATIONS ==========

	// Angular velocity Z (yaw rate) - helps AI learn smooth steering without oscillation
	// Scale = 180.0f (max reasonable rotation rate in deg/sec)
	ObservationElements.Add(TEXT("AngularVelocityZ"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 180.0f));

	// Left wall clearance - dedicated lateral distance for corridor centering
	// Scale = 400.0f (max lateral trace distance in cm)
	ObservationElements.Add(TEXT("LeftClearance"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 400.0f));

	// Right wall clearance - dedicated lateral distance for corridor centering
	// Scale = 400.0f (max lateral trace distance in cm)
	ObservationElements.Add(TEXT("RightClearance"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 400.0f));

	// Minimum obstacle distance - closest danger from all traces
	// Scale = 600.0f (same as line trace max)
	ObservationElements.Add(TEXT("MinObstacleDistance"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 600.0f));

	// ========== VELOCITY ALIGNMENT (v4.0 - CRITICAL for navigation quality) ==========
	// Dot product of velocity direction with waypoint direction
	// +1.0 = moving directly toward waypoint (perfect)
	// 0.0 = moving perpendicular to waypoint
	// -1.0 = moving away from waypoint (bad)
	// This helps the AI understand if its current movement is productive
	ObservationElements.Add(TEXT("VelocityWaypointAlignment"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 1.0f));

	// Velocity magnitude relative to max speed (0-1 normalized)
	// Helps AI correlate throttle input with actual movement
	ObservationElements.Add(TEXT("NormalizedSpeed"),
		ULearningAgentsObservations::SpecifyFloatObservation(InObservationSchema, 1.0f));

	// Combine all observations into a struct
	OutObservationSchemaElement = ULearningAgentsObservations::SpecifyStructObservation(
		InObservationSchema,
		ObservationElements);

	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsInteractor: Observation schema specified (39 features: 24 traces + 1 speed + 3 wp_dir + 1 wp_dist + 3 target_dir + 1 target_dist + 4 corridor + 2 velocity)."));
}

void UTankLearningAgentsInteractor::SpecifyAgentAction_Implementation(
	FLearningAgentsActionSchemaElement& OutActionSchemaElement,
	ULearningAgentsActionSchema* InActionSchema)
{
	// Create a struct to hold all actions
	// SIMPLIFIED: Only Throttle and Steering for UGV drone navigation
	// Removed: Brake (not tracked), TurretYaw, TurretPitch (not training turret control)
	TMap<FName, FLearningAgentsActionSchemaElement> ActionElements;

	// Throttle action (-1 to 1), scale = 1.0
	ActionElements.Add(TEXT("Throttle"),
		ULearningAgentsActions::SpecifyFloatAction(InActionSchema, 1.0f));

	// Steering action (-1 to 1), scale = 1.0
	ActionElements.Add(TEXT("Steering"),
		ULearningAgentsActions::SpecifyFloatAction(InActionSchema, 1.0f));

	// Combine all actions into a struct
	OutActionSchemaElement = ULearningAgentsActions::SpecifyStructAction(
		InActionSchema,
		ActionElements);

	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsInteractor: Action schema specified (Throttle + Steering only)."));
}

void UTankLearningAgentsInteractor::GatherAgentObservation_Implementation(
	FLearningAgentsObservationObjectElement& OutObservationObjectElement,
	ULearningAgentsObservationObject* InObservationObject,
	const int32 AgentId)
{
	ABaseTankAIController* TankController = GetTankControllerFromAgentId(AgentId);
	if (!TankController)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsInteractor: Failed to get TankController for agent %d"), AgentId);
		return;
	}

	// Get the controlled tank for LOCAL SPACE transform
	AWR_Tank_Pawn* ControlledTank = TankController->GetControlledTank();

	// ========================================================================
	// LOCAL SPACE TRANSFORM - All directions will be relative to tank orientation
	// ========================================================================
	// Using tank's transform means:
	// - If waypoint is IN FRONT of tank → direction = (1, 0, 0)
	// - If waypoint is to the RIGHT → direction = (0, 1, 0)
	// - If waypoint is BEHIND → direction = (-1, 0, 0)
	// This makes learning MUCH easier because the AI doesn't need to learn
	// the relationship between world orientation and actions.
	FTransform TankTransform = ControlledTank ? ControlledTank->GetActorTransform() : FTransform::Identity;

	// Gather SIMPLIFIED observation data for UGV drone navigation
	TMap<FName, FLearningAgentsObservationObjectElement> ObservationElements;

	// ========== 1. LINE TRACES (CRITICAL for obstacle avoidance) ==========
	// 24 distances in cm, normalized by schema scale 600.0f
	// Result: 0.0 = obstacle at tank, 1.0 = clear path
	const TArray<float>& LineTraces = TankController->GetLineTraceDistances();
	ObservationElements.Add(TEXT("LineTraces"),
		ULearningAgentsObservations::MakeContinuousObservationFromArrayView(InObservationObject, LineTraces, TEXT("LineTraces")));

	// ========== 2. FORWARD SPEED ==========
	// Scalar speed in cm/s, normalized by schema scale 1000.0f
	const float ForwardSpeed = TankController->GetForwardSpeed();
	ObservationElements.Add(TEXT("ForwardSpeed"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, ForwardSpeed));

	// ========== 3. WAYPOINT NAVIGATION (PRIMARY) ==========
	FVector DirectionToWaypoint = FVector::ZeroVector;
	float DistanceToWaypoint = 0.0f;

	ULearningAgentsManager* AgentManager = GetAgentManager();
	if (AgentManager)
	{
		ATankLearningAgentsManager* TankManager = Cast<ATankLearningAgentsManager>(AgentManager->GetOwner());
		if (TankManager && TankManager->IsWaypointPathFollowingEnabled() && TankManager->bHasActiveTarget)
		{
			if (ControlledTank)
			{
				// Get current waypoint location (world space)
				FVector WaypointLocation = TankManager->GetCurrentWaypointLocation();
				FVector AgentLocation = ControlledTank->GetActorLocation();

				FVector DeltaToWaypoint = WaypointLocation - AgentLocation;
				DistanceToWaypoint = DeltaToWaypoint.Size();
				DirectionToWaypoint = DeltaToWaypoint.GetSafeNormal();
			}
		}
	}

	// Direction to waypoint - LOCAL SPACE (X > 0 = front, Y > 0 = right)
	// MANUAL: Transform to local space ourselves using InverseTransformVector
	FVector LocalWaypointDir = TankTransform.InverseTransformVector(DirectionToWaypoint);
	ObservationElements.Add(TEXT("WaypointDirX"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, LocalWaypointDir.X));
	ObservationElements.Add(TEXT("WaypointDirY"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, LocalWaypointDir.Y));
	ObservationElements.Add(TEXT("WaypointDirZ"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, LocalWaypointDir.Z));

	// Distance to waypoint - normalized by schema scale 5000.0f
	ObservationElements.Add(TEXT("DistanceToCurrentWaypoint"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, DistanceToWaypoint));

	// ========== 4. TARGET NAVIGATION (SECONDARY) ==========
	FVector DirectionToTarget = FVector::ZeroVector;
	float DistanceToTarget = 0.0f;

	if (AgentManager)
	{
		ATankLearningAgentsManager* TankManager = Cast<ATankLearningAgentsManager>(AgentManager->GetOwner());
		if (TankManager && TankManager->IsTargetBasedRecordingEnabled() && TankManager->bHasActiveTarget)
		{
			if (ControlledTank)
			{
				// Get target location (world space)
				FVector TargetLocation = TankManager->GetCurrentTargetLocation();
				FVector AgentLocation = ControlledTank->GetActorLocation();

				FVector DeltaToTarget = TargetLocation - AgentLocation;
				DistanceToTarget = DeltaToTarget.Size();
				DirectionToTarget = DeltaToTarget.GetSafeNormal();
			}
		}
	}

	// Direction to target - LOCAL SPACE
	// MANUAL: Transform to local space ourselves using InverseTransformVector
	FVector LocalTargetDir = TankTransform.InverseTransformVector(DirectionToTarget);
	ObservationElements.Add(TEXT("TargetDirX"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, LocalTargetDir.X));
	ObservationElements.Add(TEXT("TargetDirY"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, LocalTargetDir.Y));
	ObservationElements.Add(TEXT("TargetDirZ"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, LocalTargetDir.Z));

	// Distance to target - normalized by schema scale 10000.0f
	ObservationElements.Add(TEXT("DistanceToTarget"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, DistanceToTarget));

	// ========== 5. NARROW CORRIDOR OBSERVATIONS ==========

	// Angular velocity Z (yaw rate in deg/sec)
	const float AngularVelocityZ = TankController->GetAngularVelocityZ();
	ObservationElements.Add(TEXT("AngularVelocityZ"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, AngularVelocityZ));

	// Left and right wall clearance (cm)
	const float LeftClearance = TankController->GetLeftClearance();
	const float RightClearance = TankController->GetRightClearance();
	ObservationElements.Add(TEXT("LeftClearance"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, LeftClearance));
	ObservationElements.Add(TEXT("RightClearance"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, RightClearance));

	// Minimum obstacle distance (closest danger from all line traces + lateral)
	// Default to max ellipse distance (600cm) then find minimum from all traces
	float MinObstacleDistance = 600.0f;  // EllipseMajorAxis max value
	for (float TraceDist : LineTraces)
	{
		if (TraceDist < MinObstacleDistance)
		{
			MinObstacleDistance = TraceDist;
		}
	}
	// Also include lateral clearances in minimum calculation
	MinObstacleDistance = FMath::Min(MinObstacleDistance, LeftClearance);
	MinObstacleDistance = FMath::Min(MinObstacleDistance, RightClearance);
	ObservationElements.Add(TEXT("MinObstacleDistance"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, MinObstacleDistance));

	// ========== 7. VELOCITY ALIGNMENT (v4.0 - CRITICAL) ==========
	// Calculate how well the tank is moving toward the waypoint
	float VelocityWaypointAlignment = 0.0f;
	float NormalizedSpeed = 0.0f;

	if (ControlledTank)
	{
		const FVector Velocity = ControlledTank->GetVelocity();
		const float Speed = Velocity.Size();

		// Normalized speed (0-1, where 1000 cm/s = 1.0)
		const float MaxExpectedSpeed = 1000.0f;  // 10 m/s max expected
		NormalizedSpeed = FMath::Clamp(Speed / MaxExpectedSpeed, 0.0f, 1.0f);

		// Velocity alignment with waypoint direction
		if (Speed > 10.0f && !DirectionToWaypoint.IsNearlyZero())  // Only if moving and has target
		{
			const FVector VelocityDir = Velocity.GetSafeNormal();
			// Dot product: +1 = toward, 0 = perpendicular, -1 = away
			VelocityWaypointAlignment = FVector::DotProduct(VelocityDir, DirectionToWaypoint);
		}
		// If not moving or no waypoint, alignment is 0 (neutral)
	}

	ObservationElements.Add(TEXT("VelocityWaypointAlignment"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, VelocityWaypointAlignment));

	ObservationElements.Add(TEXT("NormalizedSpeed"),
		ULearningAgentsObservations::MakeFloatObservation(InObservationObject, NormalizedSpeed));

	// ========== DEBUG LOGGING ==========
	// LocalWaypointDir and LocalTargetDir already computed above for observations

	static int32 ObsLogCounter = 0;
	if (++ObsLogCounter % 30 == 0)  // Every 0.5 second for better debugging
	{
		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("====== AGENT %d OBSERVATION DEBUG ======"), AgentId);

		// Log LineTraces summary (front, right, back, left) - adjusted for 24 traces
		if (LineTraces.Num() >= 24)
		{
			// With 24 traces: 0=front, 6=right, 12=back, 18=left
			UE_LOG(LogTemp, Warning, TEXT("[OBSTACLES] Front=%.0f Right=%.0f Back=%.0f Left=%.0f (cm) [24 traces]"),
				LineTraces[0], LineTraces[6], LineTraces[12], LineTraces[18]);
		}
		else if (LineTraces.Num() >= 16)
		{
			// Fallback for 16 traces
			UE_LOG(LogTemp, Warning, TEXT("[OBSTACLES] Front=%.0f Right=%.0f Back=%.0f Left=%.0f (cm) [16 traces]"),
				LineTraces[0], LineTraces[4], LineTraces[8], LineTraces[12]);
		}

		// CRITICAL: Log LOCAL direction to waypoint
		UE_LOG(LogTemp, Warning, TEXT("[WAYPOINT] LocalDir=(%.3f, %.3f, %.3f)"),
			LocalWaypointDir.X, LocalWaypointDir.Y, LocalWaypointDir.Z);

		// Interpret direction for human readability
		const TCHAR* WpFrontBack = (LocalWaypointDir.X > 0.1f) ? TEXT("FRONT") : ((LocalWaypointDir.X < -0.1f) ? TEXT("BEHIND") : TEXT("SIDE"));
		const TCHAR* WpLeftRight = (LocalWaypointDir.Y > 0.1f) ? TEXT("RIGHT") : ((LocalWaypointDir.Y < -0.1f) ? TEXT("LEFT") : TEXT("CENTER"));
		UE_LOG(LogTemp, Warning, TEXT("[WAYPOINT] Direction: %s-%s | Distance: %.1fm"),
			WpFrontBack, WpLeftRight, DistanceToWaypoint / 100.0f);

		// Tank state
		UE_LOG(LogTemp, Warning, TEXT("[TANK] Speed=%.0f cm/s"), ForwardSpeed);

		// Narrow corridor observations
		UE_LOG(LogTemp, Warning, TEXT("[CORRIDOR] AngVelZ=%.1f deg/s | Left=%.0fcm | Right=%.0fcm | MinDist=%.0fcm"),
			AngularVelocityZ, LeftClearance, RightClearance, MinObstacleDistance);

		// Velocity alignment (v4.0)
		const TCHAR* AlignmentQuality = (VelocityWaypointAlignment > 0.7f) ? TEXT("GOOD") :
			((VelocityWaypointAlignment > 0.3f) ? TEXT("OK") :
			((VelocityWaypointAlignment > -0.3f) ? TEXT("POOR") : TEXT("WRONG WAY")));
		UE_LOG(LogTemp, Warning, TEXT("[VELOCITY] Alignment=%.2f (%s) | NormSpeed=%.2f"),
			VelocityWaypointAlignment, AlignmentQuality, NormalizedSpeed);

		// Tank orientation info
		if (ControlledTank)
		{
			FVector TankForward = ControlledTank->GetActorForwardVector();
			FVector TankLocation = ControlledTank->GetActorLocation();
			UE_LOG(LogTemp, Log, TEXT("[TANK] Location: %s"), *TankLocation.ToString());
			UE_LOG(LogTemp, Log, TEXT("[TANK] Forward (World): (%.2f, %.2f, %.2f)"),
				TankForward.X, TankForward.Y, TankForward.Z);
		}

		// World space waypoint info
		if (AgentManager)
		{
			ATankLearningAgentsManager* TankManager = Cast<ATankLearningAgentsManager>(AgentManager->GetOwner());
			if (TankManager)
			{
				UE_LOG(LogTemp, Log, TEXT("[MANAGER] bHasActiveTarget=%s | WaypointEnabled=%s"),
					TankManager->bHasActiveTarget ? TEXT("TRUE") : TEXT("FALSE"),
					TankManager->IsWaypointPathFollowingEnabled() ? TEXT("TRUE") : TEXT("FALSE"));

				if (TankManager->bHasActiveTarget)
				{
					FVector WpWorld = TankManager->GetCurrentWaypointLocation();
					UE_LOG(LogTemp, Log, TEXT("[WAYPOINT] World Location: %s"), *WpWorld.ToString());
				}
			}
		}

		// Warning if no navigation goal
		if (DirectionToWaypoint.IsNearlyZero())
		{
			UE_LOG(LogTemp, Error, TEXT(""));
			UE_LOG(LogTemp, Error, TEXT("!!! CRITICAL: NO WAYPOINT DIRECTION !!!"));
			UE_LOG(LogTemp, Error, TEXT("!!! AI cannot navigate - check bHasActiveTarget !!!"));
			UE_LOG(LogTemp, Error, TEXT(""));
		}

		UE_LOG(LogTemp, Warning, TEXT("========================================="));
	}

	// Combine all observations into a struct
	OutObservationObjectElement = ULearningAgentsObservations::MakeStructObservation(
		InObservationObject,
		ObservationElements);
}

void UTankLearningAgentsInteractor::PerformAgentAction_Implementation(
	const ULearningAgentsActionObject* InActionObject,
	const FLearningAgentsActionObjectElement& InActionObjectElement,
	const int32 AgentId)
{
	AAILearningAgentsController* AIController = Cast<AAILearningAgentsController>(GetTankControllerFromAgentId(AgentId));
	if (!AIController)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsInteractor: Failed to get AIController for agent %d"), AgentId);
		return;
	}

	// Get the struct element containing all actions
	TMap<FName, FLearningAgentsActionObjectElement> ActionElements;
	if (!ULearningAgentsActions::GetStructAction(ActionElements, InActionObject, InActionObjectElement))
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsInteractor: Failed to get struct action for agent %d"), AgentId);
		return;
	}

	// Extract SIMPLIFIED actions (Throttle + Steering only for UGV navigation)
	float Throttle = 0.0f;
	if (ULearningAgentsActions::GetFloatAction(Throttle, InActionObject, ActionElements[TEXT("Throttle")]))
	{
		AIController->SetThrottleFromAI(Throttle);
	}

	float Steering = 0.0f;
	if (ULearningAgentsActions::GetFloatAction(Steering, InActionObject, ActionElements[TEXT("Steering")]))
	{
		AIController->SetSteeringFromAI(Steering);
	}

	// DEBUG: Log AI actions every 30 frames (0.5 second) - synchronized with observation logging
	static int32 ActionLogCounter = 0;
	if (++ActionLogCounter % 30 == 0)
	{
		// CRITICAL: Log actions with interpretation
		const TCHAR* ThrottleDir = (Throttle > 0.1f) ? TEXT("FORWARD") : ((Throttle < -0.1f) ? TEXT("BACKWARD") : TEXT("STOP"));
		const TCHAR* SteeringDir = (Steering > 0.1f) ? TEXT("RIGHT") : ((Steering < -0.1f) ? TEXT("LEFT") : TEXT("STRAIGHT"));

		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("====== AGENT %d AI ACTION OUTPUT ======"), AgentId);
		UE_LOG(LogTemp, Warning, TEXT("[ACTION] Throttle=%.3f (%s) | Steering=%.3f (%s)"),
			Throttle, ThrottleDir, Steering, SteeringDir);

		// Get tank for additional info
		AWR_Tank_Pawn* TankPawn = Cast<AWR_Tank_Pawn>(GetAgentManager()->GetAgent(AgentId));
		if (TankPawn)
		{
			FVector Velocity = TankPawn->GetVelocity();
			float Speed = Velocity.Size();
			UE_LOG(LogTemp, Warning, TEXT("[RESULT] Actual Speed: %.0f cm/s"), Speed);

			// Check if action makes sense
			// Get waypoint direction for comparison
			ATankLearningAgentsManager* TankManager = nullptr;
			if (GetAgentManager())
			{
				TankManager = Cast<ATankLearningAgentsManager>(GetAgentManager()->GetOwner());
			}

			if (TankManager && TankManager->bHasActiveTarget)
			{
				FVector WaypointLocation = TankManager->GetCurrentWaypointLocation();
				FVector TankLocation = TankPawn->GetActorLocation();
				FVector DirToWaypoint = (WaypointLocation - TankLocation).GetSafeNormal();
				FVector LocalDir = TankPawn->GetActorTransform().InverseTransformVector(DirToWaypoint);

				// Check for MISMATCH: waypoint in front but AI going backward (or vice versa)
				bool bWaypointInFront = LocalDir.X > 0.3f;
				bool bGoingForward = Throttle > 0.1f;
				bool bGoingBackward = Throttle < -0.1f;

				if (bWaypointInFront && bGoingBackward)
				{
					UE_LOG(LogTemp, Error, TEXT(""));
					UE_LOG(LogTemp, Error, TEXT("!!! BEHAVIOR MISMATCH DETECTED !!!"));
					UE_LOG(LogTemp, Error, TEXT("!!! Waypoint is IN FRONT (X=%.2f) but AI is going BACKWARD (Throttle=%.2f) !!!"), LocalDir.X, Throttle);
					UE_LOG(LogTemp, Error, TEXT("!!! This indicates BAD TRAINING DATA or WRONG POLICY !!!"));
					UE_LOG(LogTemp, Error, TEXT(""));
				}
				else if (!bWaypointInFront && LocalDir.X < -0.3f && bGoingForward)
				{
					// Waypoint behind but going forward - might be intentional (turning around)
					UE_LOG(LogTemp, Log, TEXT("[INFO] Waypoint BEHIND (X=%.2f) but going FORWARD - may be turning around"), LocalDir.X);
				}

				// Check steering direction
				bool bWaypointRight = LocalDir.Y > 0.3f;
				bool bWaypointLeft = LocalDir.Y < -0.3f;
				bool bSteeringRight = Steering > 0.3f;
				bool bSteeringLeft = Steering < -0.3f;

				if (bWaypointRight && bSteeringLeft)
				{
					UE_LOG(LogTemp, Error, TEXT("!!! STEERING MISMATCH: Waypoint RIGHT but steering LEFT !!!"));
				}
				else if (bWaypointLeft && bSteeringRight)
				{
					UE_LOG(LogTemp, Error, TEXT("!!! STEERING MISMATCH: Waypoint LEFT but steering RIGHT !!!"));
				}
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("========================================="));
	}
}

ABaseTankAIController* UTankLearningAgentsInteractor::GetTankControllerFromAgentId(int32 AgentId) const
{
	if (!GetAgentManager())
	{
		return nullptr;
	}

	UObject* AgentObject = GetAgentManager()->GetAgent(AgentId);
	AWR_Tank_Pawn* TankPawn = Cast<AWR_Tank_Pawn>(AgentObject);
	if (!TankPawn)
	{
		return nullptr;
	}

	return Cast<ABaseTankAIController>(TankPawn->GetController());
}

void UTankLearningAgentsInteractor::EncodeHumanActionsForAgent(int32 AgentId)
{
	// ========================================================================
	// WARNING: This method uses INTERNAL UE::Learning:: API
	// ========================================================================
	// SIMPLIFIED for UGV drone navigation: Only Throttle + Steering
	// USES RAW VALUES to ensure observation-action consistency:
	// - Tank moves according to raw input from player
	// - We record the same raw input as action
	// - No mismatch between what tank does and what is recorded
	// ========================================================================

	// Get the controller for this agent
	ABaseTankAIController* TankController = GetTankControllerFromAgentId(AgentId);
	if (!TankController)
	{
		UE_LOG(LogTemp, Warning, TEXT("EncodeHumanActionsForAgent: Failed to get TankController for agent %d"), AgentId);
		return;
	}

	// Use RAW values from controller (read from tank pawn in controller's Tick)
	float Throttle = TankController->GetCurrentThrottle();
	float Steering = TankController->GetCurrentSteering();

	// Create action elements (Throttle + Steering only)
	TMap<FName, FLearningAgentsActionObjectElement> ActionElements;

	ActionElements.Add(TEXT("Throttle"),
		ULearningAgentsActions::MakeFloatAction(GetActionObject(), Throttle));

	ActionElements.Add(TEXT("Steering"),
		ULearningAgentsActions::MakeFloatAction(GetActionObject(), Steering));

	// Combine actions into a struct
	FLearningAgentsActionObjectElement ActionElement = ULearningAgentsActions::MakeStructAction(
		GetActionObject(),
		ActionElements);

	// Encode to internal action buffer using UE internal API
	UE::Learning::Action::SetVectorFromObject(
		GetActionVectorsArrayView()[AgentId],
		GetActionSchema()->ActionSchema,
		GetActionSchemaElement().SchemaElement,
		GetActionObject()->ActionObject,
		ActionElement.ObjectElement);

	// Increment action iteration counter for Recorder validation
	GetActionVectorIterationArrayView()[AgentId]++;

	// Debug log every 60 frames
	static int32 EncodeLogCounter = 0;
	if (++EncodeLogCounter % 60 == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("RECORDING [Agent %d]: Throttle=%.3f | Steering=%.3f (raw)"),
			AgentId, Throttle, Steering);
	}
}
