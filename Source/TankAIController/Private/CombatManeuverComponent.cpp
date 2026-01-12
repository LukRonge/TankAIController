// Copyright ArenaBreakers. All Rights Reserved.

#include "CombatManeuverComponent.h"
#include "EnemyDetectionComponent.h"
#include "TankWaypointComponent.h"
#include "BaseTankAIController.h"
#include "WR_Tank_Pawn.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "DrawDebugHelpers.h"

// ============================================================================
// CONSTRUCTOR & LIFECYCLE
// ============================================================================

UCombatManeuverComponent::UCombatManeuverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f; // 10Hz update rate for performance

	// Initialize configs with maneuver types
	FlankingConfig.ManeuverType = ECombatManeuverType::Flanking;
	RetreatConfig.ManeuverType = ECombatManeuverType::TacticalRetreat;
	HullDownConfig.ManeuverType = ECombatManeuverType::HullDown;
	ZigzagConfig.ManeuverType = ECombatManeuverType::ZigzagEvade;
	ShootScootConfig.ManeuverType = ECombatManeuverType::ShootAndScoot;
	ChargeConfig.ManeuverType = ECombatManeuverType::ChargeAttack;
	CircleStrafeConfig.ManeuverType = ECombatManeuverType::CircleStrafe;

	// Initialize default weights in constructor (so editor can override)
	InitializeDefaultWeights();
}

void UCombatManeuverComponent::BeginPlay()
{
	Super::BeginPlay();

	// Note: InitializeDefaultWeights() called in constructor so editor can override values
	CacheReferences();
}

void UCombatManeuverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clear cached references
	EnemyDetection.Reset();
	WaypointComponent.Reset();
	OwnerTank.Reset();
	OwnerController.Reset();

	// Reset state
	bExecutingManeuver = false;
	CurrentManeuver.Reset();

	Super::EndPlay(EndPlayReason);
}

void UCombatManeuverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bEnabled)
	{
		return;
	}

	// Update timers
	UpdateUnderFireState(DeltaTime);
	UpdateCooldowns(DeltaTime);

	// Update combat state based on enemy detection
	UpdateCombatState(DeltaTime);

	// Update maneuver execution if in combat
	if (CurrentCombatState == ECombatState::Combat || CurrentCombatState == ECombatState::Reposition)
	{
		UpdateManeuverExecution(DeltaTime);
	}

	// Debug visualization
	if (bDrawDebug)
	{
		DrawDebugVisualization();
	}
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void UCombatManeuverComponent::InitializeDefaultWeights()
{
	// FLANKING: Best at medium range, when enemy is facing away, with clear flank path
	FlankingConfig.Weights.EnemyDistanceClose = -0.5f;
	FlankingConfig.Weights.EnemyDistanceMedium = 1.0f;
	FlankingConfig.Weights.EnemyDistanceFar = 0.5f;
	FlankingConfig.Weights.CoverNearby = 0.3f;
	FlankingConfig.Weights.OpenTerrain = 0.5f;
	FlankingConfig.Weights.LowHealth = -0.5f;
	FlankingConfig.Weights.HighHealth = 0.5f;
	FlankingConfig.Weights.MultipleEnemies = -0.5f;
	FlankingConfig.Weights.EnemyFacingAway = 1.5f;
	FlankingConfig.Weights.EnemyFacingMe = -0.3f;
	FlankingConfig.Weights.UnderFire = -0.3f;
	FlankingConfig.Weights.FlankPathClear = 1.0f;
	FlankingConfig.Weights.RetreatPathClear = 0.0f;

	// RETREAT: Best when low health, under fire, with clear retreat path
	RetreatConfig.Weights.EnemyDistanceClose = 0.5f;
	RetreatConfig.Weights.EnemyDistanceMedium = 0.3f;
	RetreatConfig.Weights.EnemyDistanceFar = -0.5f;
	RetreatConfig.Weights.CoverNearby = 0.8f;
	RetreatConfig.Weights.OpenTerrain = -0.3f;
	RetreatConfig.Weights.LowHealth = 1.5f;
	RetreatConfig.Weights.HighHealth = -0.5f;
	RetreatConfig.Weights.MultipleEnemies = 1.0f;
	RetreatConfig.Weights.EnemyFacingAway = -0.5f;
	RetreatConfig.Weights.EnemyFacingMe = 0.5f;
	RetreatConfig.Weights.UnderFire = 1.0f;
	RetreatConfig.Weights.FlankPathClear = 0.0f;
	RetreatConfig.Weights.RetreatPathClear = 1.0f;

	// HULL-DOWN: Best at long range with cover available
	HullDownConfig.Weights.EnemyDistanceClose = -0.5f;
	HullDownConfig.Weights.EnemyDistanceMedium = 0.8f;
	HullDownConfig.Weights.EnemyDistanceFar = 1.0f;
	HullDownConfig.Weights.CoverNearby = 2.0f;  // Requires cover
	HullDownConfig.Weights.OpenTerrain = -0.5f;
	HullDownConfig.Weights.LowHealth = 0.5f;
	HullDownConfig.Weights.HighHealth = 0.3f;
	HullDownConfig.Weights.MultipleEnemies = 0.5f;
	HullDownConfig.Weights.EnemyFacingAway = 0.3f;
	HullDownConfig.Weights.EnemyFacingMe = 0.5f;
	HullDownConfig.Weights.UnderFire = 0.5f;
	HullDownConfig.Weights.FlankPathClear = 0.0f;
	HullDownConfig.Weights.RetreatPathClear = 0.0f;

	// ZIGZAG: Best when under fire in open terrain
	ZigzagConfig.Weights.EnemyDistanceClose = 0.3f;
	ZigzagConfig.Weights.EnemyDistanceMedium = 0.5f;
	ZigzagConfig.Weights.EnemyDistanceFar = 0.3f;
	ZigzagConfig.Weights.CoverNearby = -0.5f;
	ZigzagConfig.Weights.OpenTerrain = 1.5f;  // Best in open terrain
	ZigzagConfig.Weights.LowHealth = 1.0f;
	ZigzagConfig.Weights.HighHealth = 0.3f;
	ZigzagConfig.Weights.MultipleEnemies = 0.5f;
	ZigzagConfig.Weights.EnemyFacingAway = -0.3f;
	ZigzagConfig.Weights.EnemyFacingMe = 1.0f;
	ZigzagConfig.Weights.UnderFire = 1.5f;  // Very good when under fire
	ZigzagConfig.Weights.FlankPathClear = 0.0f;
	ZigzagConfig.Weights.RetreatPathClear = 0.0f;

	// SHOOT-SCOOT: Best at medium range with cover for relocation
	ShootScootConfig.Weights.EnemyDistanceClose = -0.3f;
	ShootScootConfig.Weights.EnemyDistanceMedium = 1.0f;
	ShootScootConfig.Weights.EnemyDistanceFar = 0.8f;
	ShootScootConfig.Weights.CoverNearby = 1.0f;
	ShootScootConfig.Weights.OpenTerrain = 0.3f;
	ShootScootConfig.Weights.LowHealth = 0.3f;
	ShootScootConfig.Weights.HighHealth = 0.5f;
	ShootScootConfig.Weights.MultipleEnemies = 0.3f;
	ShootScootConfig.Weights.EnemyFacingAway = 0.5f;
	ShootScootConfig.Weights.EnemyFacingMe = 0.5f;
	ShootScootConfig.Weights.UnderFire = 0.8f;
	ShootScootConfig.Weights.FlankPathClear = 0.0f;
	ShootScootConfig.Weights.RetreatPathClear = 0.0f;

	// CHARGE: Best at close range with high health against single enemy
	ChargeConfig.Weights.EnemyDistanceClose = 1.5f;
	ChargeConfig.Weights.EnemyDistanceMedium = -0.3f;
	ChargeConfig.Weights.EnemyDistanceFar = -1.0f;
	ChargeConfig.Weights.CoverNearby = -0.3f;
	ChargeConfig.Weights.OpenTerrain = 0.5f;
	ChargeConfig.Weights.LowHealth = -1.0f;  // Never charge with low health
	ChargeConfig.Weights.HighHealth = 1.0f;
	ChargeConfig.Weights.MultipleEnemies = -1.5f;  // Never charge multiple enemies
	ChargeConfig.Weights.EnemyFacingAway = 1.0f;
	ChargeConfig.Weights.EnemyFacingMe = -0.5f;
	ChargeConfig.Weights.UnderFire = -0.5f;
	ChargeConfig.Weights.FlankPathClear = 0.0f;
	ChargeConfig.Weights.RetreatPathClear = 0.0f;

	// CIRCLE-STRAFE: Best at close-medium range in open terrain against single enemy
	CircleStrafeConfig.Weights.EnemyDistanceClose = 1.0f;
	CircleStrafeConfig.Weights.EnemyDistanceMedium = 0.5f;
	CircleStrafeConfig.Weights.EnemyDistanceFar = -0.5f;
	CircleStrafeConfig.Weights.CoverNearby = -0.5f;
	CircleStrafeConfig.Weights.OpenTerrain = 1.0f;  // Needs space to circle
	CircleStrafeConfig.Weights.LowHealth = -0.3f;
	CircleStrafeConfig.Weights.HighHealth = 0.8f;
	CircleStrafeConfig.Weights.MultipleEnemies = -1.0f;  // Bad against multiple
	CircleStrafeConfig.Weights.EnemyFacingAway = 0.5f;
	CircleStrafeConfig.Weights.EnemyFacingMe = 0.3f;
	CircleStrafeConfig.Weights.UnderFire = 0.3f;
	CircleStrafeConfig.Weights.FlankPathClear = 0.5f;
	CircleStrafeConfig.Weights.RetreatPathClear = 0.0f;
}

void UCombatManeuverComponent::CacheReferences()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// Try to get controller (component is typically added to AIController)
	OwnerController = Cast<ABaseTankAIController>(Owner);
	if (OwnerController.IsValid())
	{
		// Get controlled pawn
		OwnerTank = Cast<AWR_Tank_Pawn>(OwnerController->GetPawn());

		// Get waypoint component from controller
		WaypointComponent = OwnerController->GetWaypointComponent();
	}

	// Try to find EnemyDetectionComponent - check controller FIRST (where it typically lives)
	// as per AAILearningAgentsController architecture
	EnemyDetection = Owner->FindComponentByClass<UEnemyDetectionComponent>();

	// Fallback: check on pawn if not found on controller
	if (!EnemyDetection.IsValid() && OwnerTank.IsValid())
	{
		EnemyDetection = OwnerTank->FindComponentByClass<UEnemyDetectionComponent>();
	}
}

void UCombatManeuverComponent::SetReferences(UEnemyDetectionComponent* InEnemyDetection, UTankWaypointComponent* InWaypointComponent)
{
	EnemyDetection = InEnemyDetection;
	WaypointComponent = InWaypointComponent;
}

// ============================================================================
// UPDATE METHODS
// ============================================================================

void UCombatManeuverComponent::UpdateCombatState(float DeltaTime)
{
	if (!EnemyDetection.IsValid())
	{
		// No enemy detection, stay in patrol
		if (CurrentCombatState != ECombatState::Patrol)
		{
			TransitionToState(ECombatState::Patrol);
		}
		return;
	}

	// Get highest awareness state from detected enemies
	EAwarenessState HighestAwareness = EAwarenessState::Unaware;
	const TArray<FDetectedEnemyInfo>& DetectedEnemies = EnemyDetection->GetDetectedEnemies();

	for (const FDetectedEnemyInfo& EnemyInfo : DetectedEnemies)
	{
		if (EnemyInfo.AwarenessState > HighestAwareness)
		{
			HighestAwareness = EnemyInfo.AwarenessState;
		}
	}

	// Determine appropriate combat state
	ECombatState DesiredState = CurrentCombatState;

	if (HighestAwareness >= MinAwarenessForCombat)
	{
		DesiredState = ECombatState::Combat;
	}
	else if (HighestAwareness >= MinAwarenessForAlert)
	{
		DesiredState = ECombatState::Alert;
	}
	else if (CurrentCombatState != ECombatState::Reposition)
	{
		DesiredState = ECombatState::Patrol;
	}

	// Check for disengage conditions
	if (DesiredState == ECombatState::Combat)
	{
		FCombatSituation Situation = AssessCurrentSituation();
		if (Situation.OwnHealth < LowHealthThreshold && Situation.bUnderFire)
		{
			DesiredState = ECombatState::Disengage;
		}
	}

	// Transition if needed
	if (DesiredState != CurrentCombatState)
	{
		TransitionToState(DesiredState);
	}
}

void UCombatManeuverComponent::UpdateManeuverExecution(float DeltaTime)
{
	// Update re-evaluation timer
	ManeuverReevaluationTimer += DeltaTime;

	// Check if we should re-evaluate maneuver selection
	if (ManeuverReevaluationTimer >= ManeuverReevaluationInterval)
	{
		ManeuverReevaluationTimer = 0.0f;
		EvaluateAndSelectManeuver();
	}

	// If not executing a maneuver, try to start one
	if (!bExecutingManeuver)
	{
		EvaluateAndSelectManeuver();
		return;
	}

	// Update waypoint wait timer if at waypoint
	if (IsCurrentWaypointReached())
	{
		// Reset path regeneration timer since we reached the waypoint
		PathRegenerationTimer = 0.0f;

		const FCombatWaypoint* CurrentWP = GetCurrentWaypoint();
		if (CurrentWP && CurrentWP->WaitTime > 0.0f)
		{
			WaypointWaitTimer += DeltaTime;
			if (WaypointWaitTimer < CurrentWP->WaitTime)
			{
				return; // Still waiting
			}
		}

		// Advance to next waypoint
		AdvanceToNextWaypoint();
	}
	else
	{
		// Waypoint not reached - check if NavMesh path is stuck (completed but not at waypoint)
		if (WaypointComponent.IsValid() && WaypointComponent->AreAllWaypointsCompleted())
		{
			// NavMesh path completed but combat waypoint not reached - need to regenerate
			PathRegenerationTimer += DeltaTime;

			if (PathRegenerationTimer >= PathRegenerationInterval)
			{
				PathRegenerationTimer = 0.0f;

				// Re-push current combat waypoint to navigation system
				if (bLogManeuverSelection)
				{
					UE_LOG(LogTemp, Warning, TEXT("[CombatManeuver] Path stuck! Regenerating NavMesh path to waypoint..."));
				}
				PushCurrentWaypointToNavigation();
			}
		}
		else
		{
			// NavMesh still in progress - reset timer
			PathRegenerationTimer = 0.0f;
		}
	}
}

void UCombatManeuverComponent::UpdateUnderFireState(float DeltaTime)
{
	if (UnderFireTimer > 0.0f)
	{
		UnderFireTimer -= DeltaTime;
	}
}

void UCombatManeuverComponent::UpdateCooldowns(float DeltaTime)
{
	// Decrease all cooldown timers
	TArray<ECombatManeuverType> ExpiredCooldowns;

	for (auto& Pair : ManeuverCooldowns)
	{
		Pair.Value -= DeltaTime;
		if (Pair.Value <= 0.0f)
		{
			ExpiredCooldowns.Add(Pair.Key);
		}
	}

	// Remove expired cooldowns
	for (ECombatManeuverType Type : ExpiredCooldowns)
	{
		ManeuverCooldowns.Remove(Type);
	}
}

bool UCombatManeuverComponent::IsManeuverOnCooldown(ECombatManeuverType ManeuverType) const
{
	const float* Cooldown = ManeuverCooldowns.Find(ManeuverType);
	return Cooldown && *Cooldown > 0.0f;
}

void UCombatManeuverComponent::EvaluateAndSelectManeuver()
{
	// Assess current situation
	LastSituation = AssessCurrentSituation();

	// No enemy, no maneuver
	if (!LastSituation.HasValidEnemy())
	{
		if (bExecutingManeuver)
		{
			CompleteManeuver(true);
		}
		return;
	}

	// Select best maneuver
	FManeuverSelectionResult Result = SelectBestManeuver(LastSituation);
	LastSelectionResult = Result;

	if (bLogManeuverSelection)
	{
		LogManeuverSelection(Result);
	}

	if (!Result.IsValid())
	{
		return;
	}

	// Check if we should switch maneuvers
	if (bExecutingManeuver)
	{
		float ScoreDifference = Result.SelectedScore - CurrentManeuver.SelectionScore;

		// Keep current maneuver if:
		// - Score difference is not significant enough, OR
		// - Current maneuver is not interruptible (e.g., defensive maneuvers)
		if (ScoreDifference < ManeuverSwitchThreshold || !CurrentManeuver.bInterruptible)
		{
			return; // Keep current maneuver
		}

		// Cancel current maneuver
		CancelManeuver();
	}

	// Generate waypoints for selected maneuver
	TArray<FCombatWaypoint> Waypoints = GenerateManeuverWaypoints(Result.SelectedManeuver, LastSituation.EnemyPosition);

	if (Waypoints.Num() == 0)
	{
		return;
	}

	// Create and start maneuver
	FCombatManeuver NewManeuver(Result.SelectedManeuver);
	NewManeuver.Waypoints = MoveTemp(Waypoints);
	NewManeuver.SelectionScore = Result.SelectedScore;
	NewManeuver.StartTime = GetWorld()->GetTimeSeconds();

	// Set interruptibility based on maneuver type
	NewManeuver.bInterruptible = !CombatManeuverUtils::IsDefensiveManeuver(Result.SelectedManeuver);

	StartManeuver(NewManeuver);
}

// ============================================================================
// STATE TRANSITIONS
// ============================================================================

void UCombatManeuverComponent::TransitionToState(ECombatState NewState)
{
	if (NewState == CurrentCombatState)
	{
		return;
	}

	ECombatState OldState = CurrentCombatState;
	CurrentCombatState = NewState;

	// Cancel any active maneuver when leaving combat
	if (OldState == ECombatState::Combat && NewState == ECombatState::Patrol)
	{
		if (bExecutingManeuver)
		{
			CompleteManeuver(false);
		}
	}

	// Broadcast state change
	OnCombatStateChanged.Broadcast(OldState, NewState);

	if (bLogManeuverSelection)
	{
		UE_LOG(LogTemp, Log, TEXT("[CombatManeuver] State: %s -> %s"),
			*CombatManeuverUtils::GetCombatStateName(OldState),
			*CombatManeuverUtils::GetCombatStateName(NewState));
	}
}

void UCombatManeuverComponent::SetCombatState(ECombatState NewState)
{
	TransitionToState(NewState);
}

void UCombatManeuverComponent::StartManeuver(const FCombatManeuver& Maneuver)
{
	CurrentManeuver = Maneuver;
	bExecutingManeuver = true;
	CurrentWaypointIndex = 0;
	WaypointWaitTimer = 0.0f;
	PathRegenerationTimer = 0.0f;

	// Push first waypoint to navigation
	PushCurrentWaypointToNavigation();

	// Broadcast event
	OnManeuverStarted.Broadcast(CurrentManeuver);

	if (bLogManeuverSelection)
	{
		UE_LOG(LogTemp, Log, TEXT("[CombatManeuver] Started: %s with %d waypoints"),
			*CombatManeuverUtils::GetManeuverTypeName(Maneuver.ManeuverType),
			Maneuver.Waypoints.Num());
	}
}

void UCombatManeuverComponent::CompleteManeuver(bool bSuccess)
{
	if (!bExecutingManeuver)
	{
		return;
	}

	FCombatManeuver CompletedManeuver = CurrentManeuver;

	bExecutingManeuver = false;
	CurrentManeuver.Reset();
	CurrentWaypointIndex = 0;
	WaypointWaitTimer = 0.0f;
	PathRegenerationTimer = 0.0f;

	// Start cooldown for this maneuver type (only on successful completion)
	// This prevents immediately re-selecting the same maneuver
	if (bSuccess && ManeuverCooldownDuration > 0.0f)
	{
		ManeuverCooldowns.Add(CompletedManeuver.ManeuverType, ManeuverCooldownDuration);

		if (bLogManeuverSelection)
		{
			UE_LOG(LogTemp, Log, TEXT("[CombatManeuver] Started %.1fs cooldown for %s"),
				ManeuverCooldownDuration,
				*CombatManeuverUtils::GetManeuverTypeName(CompletedManeuver.ManeuverType));
		}
	}

	// Broadcast event
	OnManeuverCompleted.Broadcast(CompletedManeuver, bSuccess);

	if (bLogManeuverSelection)
	{
		UE_LOG(LogTemp, Log, TEXT("[CombatManeuver] Completed: %s (%s)"),
			*CombatManeuverUtils::GetManeuverTypeName(CompletedManeuver.ManeuverType),
			bSuccess ? TEXT("Success") : TEXT("Cancelled"));
	}
}

void UCombatManeuverComponent::CancelManeuver()
{
	CompleteManeuver(false);
}

void UCombatManeuverComponent::AdvanceToNextWaypoint()
{
	WaypointWaitTimer = 0.0f;
	PathRegenerationTimer = 0.0f;
	CurrentWaypointIndex++;

	if (CurrentWaypointIndex >= CurrentManeuver.Waypoints.Num())
	{
		// Maneuver complete
		CompleteManeuver(true);
		return;
	}

	// Push new waypoint to navigation
	PushCurrentWaypointToNavigation();

	// Broadcast event
	const FCombatWaypoint* NewWP = GetCurrentWaypoint();
	if (NewWP)
	{
		OnWaypointAdvanced.Broadcast(CurrentWaypointIndex, *NewWP);
	}
}

bool UCombatManeuverComponent::IsCurrentWaypointReached() const
{
	const FCombatWaypoint* CurrentWP = GetCurrentWaypoint();
	if (!CurrentWP)
	{
		return true;
	}

	// Check distance to combat waypoint first
	FVector CurrentLocation = GetOwnerLocation();
	float Distance = FVector::Dist(CurrentLocation, CurrentWP->Location);
	bool bReached = Distance <= CurrentWP->ReachRadius;

	// If within combat waypoint reach radius, consider it reached
	// regardless of NavMesh path state. This prevents getting stuck
	// when NavMesh waypoints don't align perfectly with combat waypoint.
	if (bReached)
	{
		if (bLogManeuverSelection)
		{
			UE_LOG(LogTemp, Log, TEXT("[CombatManeuver] WP REACHED: Dist=%.0fcm <= Radius=%.0fcm"),
				Distance, CurrentWP->ReachRadius);
		}
		return true;
	}

	// Not within reach radius - check if NavMesh path is still in progress
	// Only wait for NavMesh if we're far from combat waypoint
	if (WaypointComponent.IsValid() && !WaypointComponent->AreAllWaypointsCompleted())
	{
		if (bLogManeuverSelection)
		{
			UE_LOG(LogTemp, Log, TEXT("[CombatManeuver] WP Check: Dist=%.0fcm > Radius=%.0fcm, NavMesh in progress"),
				Distance, CurrentWP->ReachRadius);
		}
		return false;
	}

	// NavMesh completed but still not within reach radius
	if (bLogManeuverSelection)
	{
		UE_LOG(LogTemp, Log, TEXT("[CombatManeuver] WP Check: Dist=%.0fcm > Radius=%.0fcm, NavMesh done but not reached"),
			Distance, CurrentWP->ReachRadius);
	}

	return false;
}

void UCombatManeuverComponent::PushCurrentWaypointToNavigation()
{
	const FCombatWaypoint* CurrentWP = GetCurrentWaypoint();
	if (!CurrentWP || !WaypointComponent.IsValid())
	{
		return;
	}

	// Set target on waypoint component
	// Note: SetTarget() already calls GenerateWaypointsToTarget() internally
	WaypointComponent->SetTarget(CurrentWP->Location);
}

const FCombatWaypoint* UCombatManeuverComponent::GetCurrentWaypoint() const
{
	return CurrentManeuver.GetWaypoint(CurrentWaypointIndex);
}

// ============================================================================
// PUBLIC API
// ============================================================================

bool UCombatManeuverComponent::RequestManeuver(ECombatManeuverType ManeuverType, AActor* TargetActor)
{
	FCombatSituation Situation = AssessCurrentSituation();

	if (!IsManeuverValid(ManeuverType, Situation))
	{
		return false;
	}

	// Cancel current maneuver
	if (bExecutingManeuver)
	{
		CancelManeuver();
	}

	// Generate waypoints
	FVector TargetPosition = Situation.EnemyPosition;
	if (TargetActor)
	{
		TargetPosition = TargetActor->GetActorLocation();
	}

	TArray<FCombatWaypoint> Waypoints = GenerateManeuverWaypoints(ManeuverType, TargetPosition);
	if (Waypoints.Num() == 0)
	{
		return false;
	}

	// Create and start maneuver
	FCombatManeuver NewManeuver(ManeuverType);
	NewManeuver.Waypoints = MoveTemp(Waypoints);
	NewManeuver.TargetActor = TargetActor;
	NewManeuver.StartTime = GetWorld()->GetTimeSeconds();

	StartManeuver(NewManeuver);
	return true;
}

void UCombatManeuverComponent::ForceReevaluation()
{
	ManeuverReevaluationTimer = ManeuverReevaluationInterval;
}

void UCombatManeuverComponent::NotifyDamageTaken(float DamageAmount, AActor* DamageSource)
{
	UnderFireTimer = UnderFireDuration;

	// Force re-evaluation after taking damage
	ForceReevaluation();
}

// ============================================================================
// SITUATION ASSESSMENT
// ============================================================================

FCombatSituation UCombatManeuverComponent::AssessCurrentSituation() const
{
	FCombatSituation Situation;

	// Own position and orientation
	Situation.OwnPosition = GetOwnerLocation();
	Situation.OwnForward = GetOwnerForward();

	// Under fire state
	Situation.bUnderFire = (UnderFireTimer > 0.0f);

	// Get line trace data for terrain assessment
	const TArray<float>& Traces = GetLineTraceDistances();

	if (Traces.Num() >= 24)
	{
		// Calculate average distance for open terrain check
		float TotalDistance = 0.0f;
		for (float Dist : Traces)
		{
			TotalDistance += Dist;
		}
		Situation.AverageObstacleDistance = TotalDistance / Traces.Num();
		Situation.bOpenTerrain = (Situation.AverageObstacleDistance > OpenTerrainThreshold);

		// Check flank paths (indices 6 = right 90deg, 18 = left 90deg in 24-trace ellipse)
		float RightClearance = Traces[6];
		float LeftClearance = Traces[18];

		Situation.bFlankPathClear = (RightClearance > FlankPathClearDistance) ||
		                           (LeftClearance > FlankPathClearDistance);
		Situation.bRightFlankClearer = (RightClearance >= LeftClearance);

		// Check retreat path (index 12 = rear)
		Situation.bRetreatPathClear = (Traces[12] > RetreatPathClearDistance);
	}

	// Get enemy info from EnemyDetectionComponent
	if (EnemyDetection.IsValid())
	{
		FDetectedEnemyInfo PriorityTarget;
		if (EnemyDetection->GetPriorityTarget(PriorityTarget))
		{
			Situation.EnemyPosition = PriorityTarget.LastKnownLocation;
			Situation.EnemyDistance = PriorityTarget.Distance;
			Situation.EnemyAngle = PriorityTarget.AngleToEnemy;
			Situation.bEnemyVisible = (PriorityTarget.VisibleSocketsMask > 0);

			// Calculate enemy facing based on angle to enemy
			// Note: This is angle FROM us TO enemy, not enemy's facing direction
			// For accurate enemy facing detection, would need enemy forward vector
			// Using simplified heuristic: if enemy is directly ahead of us, they might be facing us
			Situation.bEnemyFacingMe = (FMath::Abs(PriorityTarget.AngleToEnemy) < 45.0f);
			Situation.bEnemyFacingAway = (FMath::Abs(PriorityTarget.AngleToEnemy) > 135.0f);
		}

		Situation.EnemyCount = EnemyDetection->GetDetectedEnemyCount();
	}

	// Get own health from tank pawn
	if (OwnerTank.IsValid())
	{
		// TODO: Get actual health from tank
		// Situation.OwnHealth = OwnerTank->GetHealthPercentage();
		Situation.OwnHealth = 1.0f; // Default to full health for now
		Situation.OwnAmmo = 1.0f;
	}

	// Find cover
	if (Situation.HasValidEnemy())
	{
		FVector ThreatDir = Situation.GetDirectionToEnemy();
		FVector CoverPos = FindNearestCover(Situation.OwnPosition, ThreatDir);
		Situation.bCoverAvailable = !CoverPos.IsNearlyZero();
		if (Situation.bCoverAvailable)
		{
			Situation.CoverPosition = CoverPos;
			Situation.CoverDistance = FVector::Dist(Situation.OwnPosition, CoverPos);
			Situation.CoverDirection = (CoverPos - Situation.OwnPosition).GetSafeNormal();
		}
	}

	return Situation;
}

// ============================================================================
// MANEUVER SELECTION
// ============================================================================

FManeuverSelectionResult UCombatManeuverComponent::SelectBestManeuver(const FCombatSituation& Situation)
{
	FManeuverSelectionResult Result;
	Result.EvaluatedSituation = Situation;

	float BestScore = -FLT_MAX;
	ECombatManeuverType BestManeuver = ECombatManeuverType::None;

	// Evaluate all maneuver types
	TArray<ECombatManeuverType> AllTypes = {
		ECombatManeuverType::Flanking,
		ECombatManeuverType::TacticalRetreat,
		ECombatManeuverType::HullDown,
		ECombatManeuverType::ZigzagEvade,
		ECombatManeuverType::ShootAndScoot,
		ECombatManeuverType::ChargeAttack,
		ECombatManeuverType::CircleStrafe
	};

	for (ECombatManeuverType ManeuverType : AllTypes)
	{
		float Score = CalculateManeuverScore(ManeuverType, Situation);
		Result.AllScores.Add(ManeuverType, Score);

		bool bValid = IsManeuverValid(ManeuverType, Situation);
		if (bValid)
		{
			Result.ValidManeuvers.Add(ManeuverType);

			if (Score > BestScore)
			{
				BestScore = Score;
				BestManeuver = ManeuverType;
			}
		}
	}

	Result.SelectedManeuver = BestManeuver;
	Result.SelectedScore = BestScore;

	return Result;
}

float UCombatManeuverComponent::CalculateManeuverScore(ECombatManeuverType ManeuverType, const FCombatSituation& Situation) const
{
	const FManeuverScoreConfig& Config = GetConfigForManeuver(ManeuverType);
	return CalculateScoreInternal(Config, Situation);
}

float UCombatManeuverComponent::CalculateScoreInternal(const FManeuverScoreConfig& Config, const FCombatSituation& Situation) const
{
	float Score = Config.BaseScore;
	const FManeuverScoreWeights& W = Config.Weights;

	// Distance factors (mutually exclusive)
	if (Situation.EnemyDistance < CloseRangeDistance)
	{
		Score += W.EnemyDistanceClose;
	}
	else if (Situation.EnemyDistance < LongRangeDistance)
	{
		Score += W.EnemyDistanceMedium;
	}
	else
	{
		Score += W.EnemyDistanceFar;
	}

	// Cover factor
	if (Situation.bCoverAvailable)
	{
		Score += W.CoverNearby;
	}

	// Terrain factor
	if (Situation.bOpenTerrain)
	{
		Score += W.OpenTerrain;
	}

	// Health factors (mutually exclusive)
	if (Situation.OwnHealth < LowHealthThreshold)
	{
		Score += W.LowHealth;
	}
	else if (Situation.OwnHealth > HighHealthThreshold)
	{
		Score += W.HighHealth;
	}

	// Enemy count factor
	if (Situation.EnemyCount > 1)
	{
		Score += W.MultipleEnemies;
	}

	// Enemy facing factors (mutually exclusive)
	if (Situation.bEnemyFacingAway)
	{
		Score += W.EnemyFacingAway;
	}
	else if (Situation.bEnemyFacingMe)
	{
		Score += W.EnemyFacingMe;
	}

	// Under fire factor
	if (Situation.bUnderFire)
	{
		Score += W.UnderFire;
	}

	// Path clearance factors
	if (Situation.bFlankPathClear)
	{
		Score += W.FlankPathClear;
	}
	if (Situation.bRetreatPathClear)
	{
		Score += W.RetreatPathClear;
	}

	return Score;
}

bool UCombatManeuverComponent::IsManeuverValid(ECombatManeuverType ManeuverType, const FCombatSituation& Situation) const
{
	// Check cooldown first
	if (IsManeuverOnCooldown(ManeuverType))
	{
		return false;
	}

	// All maneuvers require an enemy
	if (!Situation.HasValidEnemy())
	{
		return false;
	}

	switch (ManeuverType)
	{
	case ECombatManeuverType::Flanking:
		// Need clear path to side
		return Situation.bFlankPathClear;

	case ECombatManeuverType::TacticalRetreat:
		// Need clear path behind
		return Situation.bRetreatPathClear;

	case ECombatManeuverType::HullDown:
		// Need cover available
		return Situation.bCoverAvailable;

	case ECombatManeuverType::CircleStrafe:
		// Need open terrain and ideally single enemy
		return Situation.bOpenTerrain;

	case ECombatManeuverType::ChargeAttack:
		// Need sufficient health and not too far
		return Situation.OwnHealth > 0.5f && Situation.EnemyDistance < LongRangeDistance;

	case ECombatManeuverType::ZigzagEvade:
	case ECombatManeuverType::ShootAndScoot:
		// Always valid if enemy exists
		return true;

	default:
		return true;
	}
}

const FManeuverScoreConfig& UCombatManeuverComponent::GetConfigForManeuver(ECombatManeuverType ManeuverType) const
{
	switch (ManeuverType)
	{
	case ECombatManeuverType::Flanking:        return FlankingConfig;
	case ECombatManeuverType::TacticalRetreat: return RetreatConfig;
	case ECombatManeuverType::HullDown:        return HullDownConfig;
	case ECombatManeuverType::ZigzagEvade:     return ZigzagConfig;
	case ECombatManeuverType::ShootAndScoot:   return ShootScootConfig;
	case ECombatManeuverType::ChargeAttack:    return ChargeConfig;
	case ECombatManeuverType::CircleStrafe:    return CircleStrafeConfig;
	default:                                   return FlankingConfig;
	}
}

// ============================================================================
// WAYPOINT GENERATION
// ============================================================================

TArray<FCombatWaypoint> UCombatManeuverComponent::GenerateManeuverWaypoints(ECombatManeuverType ManeuverType, FVector EnemyPosition)
{
	switch (ManeuverType)
	{
	case ECombatManeuverType::Flanking:        return GenerateFlankingWaypoints(EnemyPosition);
	case ECombatManeuverType::TacticalRetreat: return GenerateRetreatWaypoints(EnemyPosition);
	case ECombatManeuverType::HullDown:        return GenerateHullDownWaypoints(EnemyPosition);
	case ECombatManeuverType::ZigzagEvade:     return GenerateZigzagWaypoints(EnemyPosition);
	case ECombatManeuverType::ShootAndScoot:   return GenerateShootScootWaypoints(EnemyPosition);
	case ECombatManeuverType::ChargeAttack:    return GenerateChargeWaypoints(EnemyPosition);
	case ECombatManeuverType::CircleStrafe:    return GenerateCircleStrafeWaypoints(EnemyPosition);
	default:                                   return TArray<FCombatWaypoint>();
	}
}

TArray<FCombatWaypoint> UCombatManeuverComponent::GenerateFlankingWaypoints(FVector EnemyPosition)
{
	TArray<FCombatWaypoint> Waypoints;

	FVector TankPos = GetOwnerLocation();
	FVector ToEnemy = (EnemyPosition - TankPos).GetSafeNormal();
	FVector RightDir = FVector::CrossProduct(ToEnemy, FVector::UpVector).GetSafeNormal();

	// Decide flank direction based on obstacles
	bool bGoRight = EvaluateFlankDirection();
	FVector FlankDir = bGoRight ? RightDir : -RightDir;

	// Calculate dynamic lateral distance based on clearance
	float DynamicLateralDistance = CalculateDynamicDistance(FlankDir, FlankingLateralDistance);

	if (bLogManeuverSelection)
	{
		UE_LOG(LogTemp, Log, TEXT("[Flanking] Direction=%s, LateralDist=%.0fcm"),
			bGoRight ? TEXT("RIGHT") : TEXT("LEFT"), DynamicLateralDistance);
	}

	// Waypoint 1: Initial lateral movement
	FCombatWaypoint WP1;
	WP1.Location = ProjectToNavMesh(TankPos + FlankDir * DynamicLateralDistance);
	WP1.ManeuverType = ECombatManeuverType::Flanking;
	WP1.bShouldFire = false;
	WP1.DesiredSpeed = 0.8f;
	WP1.LookAtTarget = EnemyPosition;
	WP1.bTrackTarget = true;
	WP1.ReachRadius = 200.0f;
	Waypoints.Add(WP1);

	// Waypoint 2: Arc toward enemy flank
	FVector FlankPosition = EnemyPosition + FlankDir * FlankingApproachDistance;
	FCombatWaypoint WP2;
	WP2.Location = ProjectToNavMesh(FlankPosition);
	WP2.ManeuverType = ECombatManeuverType::Flanking;
	WP2.bShouldFire = true;
	WP2.DesiredSpeed = 0.6f;
	WP2.LookAtTarget = EnemyPosition;
	WP2.bTrackTarget = true;
	WP2.WaitTime = 2.0f;  // Engage from flank
	WP2.ReachRadius = 150.0f;
	Waypoints.Add(WP2);

	return Waypoints;
}

TArray<FCombatWaypoint> UCombatManeuverComponent::GenerateRetreatWaypoints(FVector EnemyPosition)
{
	TArray<FCombatWaypoint> Waypoints;

	FVector TankPos = GetOwnerLocation();

	// Direction away from enemy
	FVector AwayFromEnemy = (TankPos - EnemyPosition).GetSafeNormal();
	AwayFromEnemy.Z = 0.0f;
	AwayFromEnemy.Normalize();

	// Calculate diagonal retreat direction
	// Instead of going straight back (180°), go diagonally (145-150°)
	// This allows neural network to navigate with smaller turn angle
	FVector RightDir = FVector::CrossProduct(FVector::UpVector, AwayFromEnemy).GetSafeNormal();

	// Choose side based on which flank has more clearance
	const TArray<float>& Traces = GetLineTraceDistances();
	bool bGoRight = true;
	if (Traces.Num() >= 24)
	{
		float RightClearance = Traces[6];   // Right 90°
		float LeftClearance = Traces[18];   // Left 90°
		bGoRight = (RightClearance >= LeftClearance);
	}

	// Apply angle offset to create diagonal retreat
	// RetreatAngleOffset = 35° means we go 145° instead of 180° (relative to enemy direction)
	float AngleOffsetRad = FMath::DegreesToRadians(RetreatAngleOffset);
	FVector DiagonalDir = AwayFromEnemy * FMath::Cos(AngleOffsetRad) +
	                      (bGoRight ? RightDir : -RightDir) * FMath::Sin(AngleOffsetRad);
	DiagonalDir.Normalize();

	// Calculate dynamic distance based on clearance in retreat direction
	float DynamicRetreatDistance = CalculateDynamicDistance(DiagonalDir, RetreatDistance);
	float SegmentDistance = DynamicRetreatDistance / RetreatWaypointCount;

	// CRITICAL FIX: ReachRadius must be smaller than SegmentDistance to prevent immediate waypoint reaching
	// Scale ReachRadius based on available segment distance (70% of segment, clamped between 50-200cm)
	const float MaxReachRadius = 200.0f;
	const float MinReachRadius = 50.0f;
	float DynamicReachRadius = FMath::Clamp(SegmentDistance * 0.7f, MinReachRadius, MaxReachRadius);

	if (bLogManeuverSelection)
	{
		UE_LOG(LogTemp, Log, TEXT("[Retreat] Diagonal %s, AngleOffset=%.0f°, Distance=%.0fcm (%.0fcm per segment, ReachRadius=%.0fcm)"),
			bGoRight ? TEXT("RIGHT") : TEXT("LEFT"), RetreatAngleOffset, DynamicRetreatDistance, SegmentDistance, DynamicReachRadius);
	}

	for (int32 i = 1; i <= RetreatWaypointCount; i++)
	{
		FVector WPPos = TankPos + DiagonalDir * SegmentDistance * i;

		FCombatWaypoint WP;
		WP.Location = ProjectToNavMesh(WPPos);
		WP.ManeuverType = ECombatManeuverType::TacticalRetreat;
		WP.bShouldFire = (i == 1);  // Fire while starting retreat
		WP.bReverseMovement = false;  // NO REVERSE - neural network drives forward to diagonal waypoint
		WP.DesiredSpeed = 0.8f;  // Higher speed for quick repositioning
		WP.LookAtTarget = EnemyPosition;
		WP.bTrackTarget = true;
		WP.ReachRadius = DynamicReachRadius;  // Dynamic radius based on available space

		Waypoints.Add(WP);
	}

	return Waypoints;
}

TArray<FCombatWaypoint> UCombatManeuverComponent::GenerateHullDownWaypoints(FVector EnemyPosition)
{
	TArray<FCombatWaypoint> Waypoints;

	FVector TankPos = GetOwnerLocation();
	FVector ThreatDir = (EnemyPosition - TankPos).GetSafeNormal();

	// Find cover position
	FVector CoverPos = FindNearestCover(TankPos, ThreatDir);

	if (CoverPos.IsNearlyZero())
	{
		// No cover found
		return Waypoints;
	}

	// Waypoint 1: Move to cover position
	FCombatWaypoint WP1;
	WP1.Location = CoverPos;
	WP1.ManeuverType = ECombatManeuverType::HullDown;
	WP1.bShouldFire = false;
	WP1.DesiredSpeed = 0.7f;
	WP1.LookAtTarget = EnemyPosition;
	WP1.bTrackTarget = true;
	WP1.ReachRadius = 150.0f;
	Waypoints.Add(WP1);

	// Waypoint 2: Hold position and engage
	FCombatWaypoint WP2;
	WP2.Location = CoverPos;
	WP2.ManeuverType = ECombatManeuverType::HullDown;
	WP2.bShouldFire = true;
	WP2.DesiredSpeed = 0.0f;
	WP2.LookAtTarget = EnemyPosition;
	WP2.bTrackTarget = true;
	WP2.WaitTime = 5.0f;  // Hold position
	WP2.ReachRadius = 100.0f;
	Waypoints.Add(WP2);

	return Waypoints;
}

TArray<FCombatWaypoint> UCombatManeuverComponent::GenerateZigzagWaypoints(FVector EnemyPosition)
{
	TArray<FCombatWaypoint> Waypoints;

	FVector TankPos = GetOwnerLocation();
	FVector AwayFromEnemy = (TankPos - EnemyPosition).GetSafeNormal();
	FVector PerpendicularDir = FVector::CrossProduct(AwayFromEnemy, FVector::UpVector).GetSafeNormal();

	// Start with side that has more clearance
	const TArray<float>& Traces = GetLineTraceDistances();
	bool bGoRight = true;
	if (Traces.Num() >= 24)
	{
		bGoRight = (Traces[6] >= Traces[18]);
	}

	FVector CurrentPos = TankPos;

	for (int32 i = 0; i < ZigzagWaypointCount; i++)
	{
		// Calculate direction for this zigzag segment
		FVector ZigDir = bGoRight ? PerpendicularDir : -PerpendicularDir;

		// Get dynamic distance based on clearance in that direction
		float MaxZigDistance = CalculateDynamicDistance(ZigDir, ZigzagMaxDistance);
		float ZigDistance = FMath::RandRange(ZigzagMinDistance, MaxZigDistance);
		float SideOffset = bGoRight ? ZigDistance : -ZigDistance;

		// Progress away from enemy - also check clearance
		float DynamicForwardDist = CalculateDynamicDistance(AwayFromEnemy, ZigzagForwardDistance);

		FVector WPPos = CurrentPos +
		                AwayFromEnemy * DynamicForwardDist +
		                PerpendicularDir * SideOffset;

		FCombatWaypoint WP;
		WP.Location = ProjectToNavMesh(WPPos);
		WP.ManeuverType = ECombatManeuverType::ZigzagEvade;
		WP.DesiredSpeed = 1.0f;  // Maximum speed for evasion
		WP.bReverseMovement = false;
		WP.LookAtTarget = EnemyPosition;
		WP.bTrackTarget = true;
		WP.ReachRadius = 250.0f;  // Larger radius for fluid movement

		Waypoints.Add(WP);
		CurrentPos = WP.Location;

		// 70% chance to change direction
		if (FMath::FRand() > 0.3f)
		{
			bGoRight = !bGoRight;
		}
	}

	return Waypoints;
}

TArray<FCombatWaypoint> UCombatManeuverComponent::GenerateShootScootWaypoints(FVector EnemyPosition)
{
	TArray<FCombatWaypoint> Waypoints;

	FVector TankPos = GetOwnerLocation();
	FVector ToEnemy = (EnemyPosition - TankPos).GetSafeNormal();
	FVector RightDir = FVector::CrossProduct(ToEnemy, FVector::UpVector).GetSafeNormal();

	// Waypoint 1: Current position - FIRE
	FCombatWaypoint WP1;
	WP1.Location = TankPos;
	WP1.ManeuverType = ECombatManeuverType::ShootAndScoot;
	WP1.bShouldFire = true;
	WP1.DesiredSpeed = 0.0f;
	WP1.LookAtTarget = EnemyPosition;
	WP1.bTrackTarget = true;
	WP1.WaitTime = ShootScootFireDuration;
	WP1.ReachRadius = 100.0f;
	Waypoints.Add(WP1);

	// Waypoint 2: Relocate (perpendicular to enemy)
	// Choose direction with more clearance
	const TArray<float>& Traces = GetLineTraceDistances();
	bool bGoRight = true;
	if (Traces.Num() >= 24)
	{
		bGoRight = (Traces[6] >= Traces[18]);
	}
	FVector RelocationDir = bGoRight ? RightDir : -RightDir;

	// Calculate dynamic relocation distance
	float DynamicRelocationDist = CalculateDynamicDistance(RelocationDir, ShootScootRelocationDistance);

	// Prefer cover if available
	TArray<FVector> CoverPositions = FindCoverPositions(EnemyPosition, 3);
	FVector RelocationPos;

	if (CoverPositions.Num() > 0)
	{
		// Choose closest cover
		RelocationPos = CoverPositions[0];
	}
	else
	{
		RelocationPos = TankPos + RelocationDir * DynamicRelocationDist;
	}

	if (bLogManeuverSelection)
	{
		UE_LOG(LogTemp, Log, TEXT("[ShootScoot] Relocation %s, Dist=%.0fcm"),
			bGoRight ? TEXT("RIGHT") : TEXT("LEFT"), DynamicRelocationDist);
	}

	FCombatWaypoint WP2;
	WP2.Location = ProjectToNavMesh(RelocationPos);
	WP2.ManeuverType = ECombatManeuverType::ShootAndScoot;
	WP2.bShouldFire = false;
	WP2.DesiredSpeed = 1.0f;  // Fast relocation
	WP2.LookAtTarget = EnemyPosition;
	WP2.bTrackTarget = true;
	WP2.ReachRadius = 150.0f;
	Waypoints.Add(WP2);

	return Waypoints;
}

TArray<FCombatWaypoint> UCombatManeuverComponent::GenerateCircleStrafeWaypoints(FVector EnemyPosition)
{
	TArray<FCombatWaypoint> Waypoints;

	FVector TankPos = GetOwnerLocation();
	FVector ToTank = TankPos - EnemyPosition;
	float CurrentAngle = FMath::Atan2(ToTank.Y, ToTank.X);

	// Calculate current distance to enemy
	float CurrentDistanceToEnemy = FVector::Dist2D(TankPos, EnemyPosition);

	// Determine circle direction based on clearance
	const TArray<float>& Traces = GetLineTraceDistances();
	bool bClockwise = true;
	if (Traces.Num() >= 24)
	{
		// Check right vs left clearance
		bClockwise = (Traces[6] >= Traces[18]);
	}

	// Calculate dynamic strafe radius based on average lateral clearance
	FVector RightDir = FVector::CrossProduct(FVector::UpVector, ToTank.GetSafeNormal());
	float DynamicRadius = CalculateDynamicDistance(bClockwise ? RightDir : -RightDir, CircleStrafeRadius);

	// Don't get closer to enemy than current distance
	DynamicRadius = FMath::Max(DynamicRadius, CurrentDistanceToEnemy * 0.8f);

	if (bLogManeuverSelection)
	{
		UE_LOG(LogTemp, Log, TEXT("[CircleStrafe] Direction=%s, Radius=%.0fcm"),
			bClockwise ? TEXT("CW") : TEXT("CCW"), DynamicRadius);
	}

	float TotalArc = PI * 0.6f;  // ~108 degrees
	float AngleStep = TotalArc / CircleStrafeWaypointCount;
	if (bClockwise)
	{
		AngleStep = -AngleStep;
	}

	for (int32 i = 0; i < CircleStrafeWaypointCount; i++)
	{
		float NewAngle = CurrentAngle + AngleStep * (i + 1);

		FVector WPPos;
		WPPos.X = EnemyPosition.X + DynamicRadius * FMath::Cos(NewAngle);
		WPPos.Y = EnemyPosition.Y + DynamicRadius * FMath::Sin(NewAngle);
		WPPos.Z = TankPos.Z;

		FCombatWaypoint WP;
		WP.Location = ProjectToNavMesh(WPPos);
		WP.ManeuverType = ECombatManeuverType::CircleStrafe;
		WP.bShouldFire = (i % 2 == 0);  // Fire at every other waypoint
		WP.DesiredSpeed = 0.7f;
		WP.LookAtTarget = EnemyPosition;
		WP.bTrackTarget = true;
		WP.ReachRadius = 250.0f;

		Waypoints.Add(WP);
	}

	return Waypoints;
}

TArray<FCombatWaypoint> UCombatManeuverComponent::GenerateChargeWaypoints(FVector EnemyPosition)
{
	TArray<FCombatWaypoint> Waypoints;

	FVector TankPos = GetOwnerLocation();

	// Single waypoint - charge directly at enemy
	FCombatWaypoint WP;
	WP.Location = ProjectToNavMesh(EnemyPosition);
	WP.ManeuverType = ECombatManeuverType::ChargeAttack;
	WP.bShouldFire = true;  // Fire during charge
	WP.DesiredSpeed = 1.0f;  // Maximum speed
	WP.LookAtTarget = EnemyPosition;
	WP.bTrackTarget = true;
	WP.ReachRadius = 300.0f;  // Larger radius for ramming

	Waypoints.Add(WP);

	return Waypoints;
}

// ============================================================================
// COVER DETECTION
// ============================================================================

FVector UCombatManeuverComponent::FindNearestCover(FVector FromPosition, FVector ThreatDirection) const
{
	// Cover finding with NavMesh path validation
	// Only returns cover positions that can be reached via complete NavMesh path

	UWorld* World = GetWorld();
	if (!World)
	{
		return FVector::ZeroVector;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	FVector BestCover = FVector::ZeroVector;
	float BestScore = -FLT_MAX;

	// Check potential cover positions in a circle
	const int32 NumChecks = 12;
	const float CheckRadius = CoverSearchRadius;

	for (int32 i = 0; i < NumChecks; i++)
	{
		float Angle = (2.0f * PI * i) / NumChecks;
		FVector CheckDir(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
		FVector CheckPos = FromPosition + CheckDir * CheckRadius * 0.5f;

		// Check minimum distance from current position
		// This prevents selecting cover we're already at
		float DistanceFromCurrent = FVector::Dist2D(FromPosition, CheckPos);
		if (DistanceFromCurrent < MinCoverDistance)
		{
			continue; // Too close to current position
		}

		// Check if position provides cover from threat
		if (!IsPositionInCover(CheckPos, FromPosition + ThreatDirection * 5000.0f))
		{
			continue; // Position doesn't provide cover
		}

		// CRITICAL: Verify there's a complete (non-partial) NavMesh path to cover position
		// This prevents selecting cover positions behind obstacles that can't be reached
		bool bPathComplete = false;
		if (NavSys)
		{
			UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
				World,
				FromPosition,
				CheckPos,
				nullptr,
				nullptr
			);
			bPathComplete = Path && Path->IsValid() && !Path->IsPartial();
		}

		if (!bPathComplete)
		{
			continue; // Can't reach this cover position
		}

		// Score based on angle from threat (prefer perpendicular to slightly behind)
		float DotToThreat = FVector::DotProduct(CheckDir, ThreatDirection);
		float Score = -FMath::Abs(DotToThreat) + 1.0f;  // Prefer perpendicular, +1 for reachability

		if (Score > BestScore)
		{
			BestScore = Score;
			BestCover = CheckPos;
		}
	}

	return BestCover;
}

bool UCombatManeuverComponent::IsPositionInCover(FVector Position, FVector ThreatPosition) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// Simple line trace to check if there's an obstacle between position and threat
	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(GetOwner());

	bool bHit = World->LineTraceSingleByChannel(
		Hit,
		Position + FVector(0, 0, 50),  // Raise slightly
		ThreatPosition,
		ECC_Visibility,
		Params
	);

	return bHit;
}

TArray<FVector> UCombatManeuverComponent::FindCoverPositions(FVector ThreatPosition, int32 MaxResults) const
{
	TArray<FVector> Results;

	FVector FromPosition = GetOwnerLocation();
	FVector ThreatDir = (ThreatPosition - FromPosition).GetSafeNormal();

	// Check multiple positions
	const int32 NumChecks = 16;
	TArray<TPair<float, FVector>> ScoredPositions;

	for (int32 i = 0; i < NumChecks; i++)
	{
		float Angle = (2.0f * PI * i) / NumChecks;
		FVector CheckDir(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);

		for (float Dist = 500.0f; Dist <= CoverSearchRadius; Dist += 500.0f)
		{
			FVector CheckPos = FromPosition + CheckDir * Dist;

			if (IsPositionInCover(CheckPos, ThreatPosition) && IsPositionNavigable(CheckPos))
			{
				float Score = -Dist / 100.0f;  // Prefer closer cover
				ScoredPositions.Add(TPair<float, FVector>(Score, CheckPos));
			}
		}
	}

	// Sort by score (descending)
	ScoredPositions.Sort([](const TPair<float, FVector>& A, const TPair<float, FVector>& B)
	{
		return A.Key > B.Key;
	});

	// Take top results
	for (int32 i = 0; i < FMath::Min(MaxResults, ScoredPositions.Num()); i++)
	{
		Results.Add(ScoredPositions[i].Value);
	}

	return Results;
}

// ============================================================================
// DYNAMIC DISTANCE CALCULATION
// ============================================================================

float UCombatManeuverComponent::GetDirectionalClearance(int32 DirectionIndex) const
{
	const TArray<float>& Traces = GetLineTraceDistances();

	if (Traces.Num() < 24 || DirectionIndex < 0 || DirectionIndex >= Traces.Num())
	{
		return 1000.0f; // Default fallback
	}

	return Traces[DirectionIndex];
}

float UCombatManeuverComponent::GetClearanceInDirection(FVector WorldDirection) const
{
	if (WorldDirection.IsNearlyZero())
	{
		return 1000.0f;
	}

	// Get tank's forward direction
	FVector TankForward = GetOwnerForward();
	TankForward.Z = 0.0f;
	TankForward.Normalize();

	WorldDirection.Z = 0.0f;
	WorldDirection.Normalize();

	// Calculate angle from tank forward to desired direction
	// Dot product gives cos(angle), cross product Z gives sin(angle)
	float DotProduct = FVector::DotProduct(TankForward, WorldDirection);
	FVector Cross = FVector::CrossProduct(TankForward, WorldDirection);
	float AngleRad = FMath::Atan2(Cross.Z, DotProduct);

	// Convert angle to trace index (24 traces, 0 = forward)
	// Angle: 0 = forward (index 0), PI/2 = right (index 6), PI = rear (index 12), -PI/2 = left (index 18)
	float AngleNormalized = AngleRad;
	if (AngleNormalized < 0.0f)
	{
		AngleNormalized += 2.0f * PI;
	}

	// Map angle to index: 24 traces cover 360 degrees
	int32 TraceIndex = FMath::RoundToInt(AngleNormalized / (2.0f * PI) * 24.0f) % 24;

	return GetDirectionalClearance(TraceIndex);
}

float UCombatManeuverComponent::CalculateDynamicDistance(FVector Direction, float ConfiguredDistance) const
{
	// Get clearance in the desired direction
	float Clearance = GetClearanceInDirection(Direction);

	// Apply safety factor
	float SafeDistance = Clearance * DynamicDistanceSafetyFactor;

	// Clamp between min and configured max
	float FinalDistance = FMath::Clamp(SafeDistance, MinManeuverDistance, ConfiguredDistance);

	if (bLogManeuverSelection)
	{
		UE_LOG(LogTemp, Log, TEXT("[DynamicDist] Clearance=%.0fcm, Safe=%.0fcm, Config=%.0fcm, Final=%.0fcm"),
			Clearance, SafeDistance, ConfiguredDistance, FinalDistance);
	}

	return FinalDistance;
}

// ============================================================================
// HELPERS
// ============================================================================

FVector UCombatManeuverComponent::GetOwnerLocation() const
{
	if (OwnerTank.IsValid())
	{
		return OwnerTank->GetActorLocation();
	}

	AActor* Owner = GetOwner();
	if (Owner)
	{
		APawn* Pawn = Cast<AController>(Owner) ? Cast<AController>(Owner)->GetPawn() : nullptr;
		if (Pawn)
		{
			return Pawn->GetActorLocation();
		}
		return Owner->GetActorLocation();
	}

	return FVector::ZeroVector;
}

FRotator UCombatManeuverComponent::GetOwnerRotation() const
{
	if (OwnerTank.IsValid())
	{
		return OwnerTank->GetActorRotation();
	}

	AActor* Owner = GetOwner();
	if (Owner)
	{
		APawn* Pawn = Cast<AController>(Owner) ? Cast<AController>(Owner)->GetPawn() : nullptr;
		if (Pawn)
		{
			return Pawn->GetActorRotation();
		}
		return Owner->GetActorRotation();
	}

	return FRotator::ZeroRotator;
}

FVector UCombatManeuverComponent::GetOwnerForward() const
{
	return GetOwnerRotation().Vector();
}

const TArray<float>& UCombatManeuverComponent::GetLineTraceDistances() const
{
	static TArray<float> EmptyTraces;

	if (OwnerController.IsValid())
	{
		return OwnerController->GetLineTraceDistances();
	}

	return EmptyTraces;
}

bool UCombatManeuverComponent::EvaluateFlankDirection() const
{
	const TArray<float>& Traces = GetLineTraceDistances();

	if (Traces.Num() >= 24)
	{
		// Compare right (index 6) vs left (index 18) clearance
		return Traces[6] >= Traces[18];
	}

	// Default to right
	return true;
}

bool UCombatManeuverComponent::IsPositionNavigable(FVector Position) const
{
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys)
	{
		return true; // Assume navigable if no nav system
	}

	FNavLocation NavLocation;
	return NavSys->ProjectPointToNavigation(Position, NavLocation, FVector(100.0f, 100.0f, 100.0f));
}

FVector UCombatManeuverComponent::ProjectToNavMesh(FVector Position) const
{
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys)
	{
		return Position;
	}

	FNavLocation NavLocation;
	if (NavSys->ProjectPointToNavigation(Position, NavLocation, FVector(500.0f, 500.0f, 500.0f)))
	{
		return NavLocation.Location;
	}

	return Position;
}

// ============================================================================
// DEBUG
// ============================================================================

void UCombatManeuverComponent::DrawDebugVisualization() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector TankPos = GetOwnerLocation();

	// Draw combat state
	FString StateStr = FString::Printf(TEXT("Combat: %s"), *CombatManeuverUtils::GetCombatStateName(CurrentCombatState));
	DrawDebugString(World, TankPos + FVector(0, 0, 300), StateStr, nullptr, FColor::White, DebugDrawDuration);

	// Draw current maneuver
	if (bExecutingManeuver)
	{
		FString ManeuverStr = FString::Printf(TEXT("Maneuver: %s [%d/%d]"),
			*CombatManeuverUtils::GetManeuverTypeName(CurrentManeuver.ManeuverType),
			CurrentWaypointIndex + 1,
			CurrentManeuver.Waypoints.Num());
		DrawDebugString(World, TankPos + FVector(0, 0, 280), ManeuverStr, nullptr,
			CombatManeuverUtils::GetManeuverDebugColor(CurrentManeuver.ManeuverType), DebugDrawDuration);

		// Draw waypoints
		DrawDebugWaypoints();
	}

	// Draw scores
	DrawDebugScores(LastSituation);
}

void UCombatManeuverComponent::DrawDebugScores(const FCombatSituation& Situation) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector TankPos = GetOwnerLocation();
	float YOffset = 0.0f;

	for (const auto& ScorePair : LastSelectionResult.AllScores)
	{
		bool bValid = LastSelectionResult.ValidManeuvers.Contains(ScorePair.Key);
		bool bSelected = (ScorePair.Key == LastSelectionResult.SelectedManeuver);

		FString Text = FString::Printf(TEXT("%s: %.2f %s"),
			*CombatManeuverUtils::GetManeuverTypeName(ScorePair.Key),
			ScorePair.Value,
			bValid ? TEXT("") : TEXT("[INVALID]"));

		FColor Color = bSelected ? FColor::Green : (bValid ? FColor::White : FColor::Red);

		DrawDebugString(World, TankPos + FVector(200, 0, 200 - YOffset), Text, nullptr, Color, DebugDrawDuration);
		YOffset += 15.0f;
	}
}

void UCombatManeuverComponent::DrawDebugWaypoints() const
{
	UWorld* World = GetWorld();
	if (!World || !bExecutingManeuver)
	{
		return;
	}

	FColor ManeuverColor = CombatManeuverUtils::GetManeuverDebugColor(CurrentManeuver.ManeuverType);

	for (int32 i = 0; i < CurrentManeuver.Waypoints.Num(); i++)
	{
		const FCombatWaypoint& WP = CurrentManeuver.Waypoints[i];

		// Draw waypoint sphere
		FColor WPColor = (i == CurrentWaypointIndex) ? FColor::Yellow : ManeuverColor;
		DrawDebugSphere(World, WP.Location, WP.ReachRadius, 8, WPColor, false, DebugDrawDuration);

		// Draw line to next waypoint
		if (i < CurrentManeuver.Waypoints.Num() - 1)
		{
			DrawDebugLine(World, WP.Location, CurrentManeuver.Waypoints[i + 1].Location,
				ManeuverColor, false, DebugDrawDuration);
		}

		// Draw look-at target line
		if (WP.bTrackTarget && !WP.LookAtTarget.IsNearlyZero())
		{
			DrawDebugLine(World, WP.Location, WP.LookAtTarget, FColor::Cyan, false, DebugDrawDuration);
		}

		// Draw fire indicator
		if (WP.bShouldFire)
		{
			DrawDebugSphere(World, WP.Location + FVector(0, 0, 50), 20.0f, 4, FColor::Red, false, DebugDrawDuration);
		}
	}
}

void UCombatManeuverComponent::LogManeuverSelection(const FManeuverSelectionResult& Result) const
{
	UE_LOG(LogTemp, Log, TEXT("[CombatManeuver] Selection Result:"));
	UE_LOG(LogTemp, Log, TEXT("  Selected: %s (Score: %.2f)"),
		*CombatManeuverUtils::GetManeuverTypeName(Result.SelectedManeuver),
		Result.SelectedScore);

	UE_LOG(LogTemp, Log, TEXT("  Situation: Enemy=%.0fcm, Health=%.0f%%, UnderFire=%d, Cover=%d, Open=%d"),
		Result.EvaluatedSituation.EnemyDistance,
		Result.EvaluatedSituation.OwnHealth * 100.0f,
		Result.EvaluatedSituation.bUnderFire ? 1 : 0,
		Result.EvaluatedSituation.bCoverAvailable ? 1 : 0,
		Result.EvaluatedSituation.bOpenTerrain ? 1 : 0);

	UE_LOG(LogTemp, Log, TEXT("  All Scores:"));
	for (const auto& ScorePair : Result.AllScores)
	{
		bool bValid = Result.ValidManeuvers.Contains(ScorePair.Key);
		UE_LOG(LogTemp, Log, TEXT("    %s: %.2f %s"),
			*CombatManeuverUtils::GetManeuverTypeName(ScorePair.Key),
			ScorePair.Value,
			bValid ? TEXT("") : TEXT("[INVALID]"));
	}
}
