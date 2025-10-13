// Copyright Epic Games, Inc. All Rights Reserved.

#include "TankLearningAgentsTrainer.h"
#include "BaseTankAIController.h"
#include "WR_Tank_Pawn.h"
#include "LearningAgentsManager.h"

UTankLearningAgentsTrainer::UTankLearningAgentsTrainer()
{
	MaxEpisodeDuration = 300.0f;
	MinSafeDistance = 200.0f;
}

void UTankLearningAgentsTrainer::OnAgentsAdded_Implementation(const TArray<int32>& AgentIds)
{
	// Initialize episode start times for new agents
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	for (int32 AgentId : AgentIds)
	{
		EpisodeStartTimes.Add(AgentId, CurrentTime);
		UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsTrainer: Agent %d added, episode started at %.2f"), AgentId, CurrentTime);
	}
}

void UTankLearningAgentsTrainer::OnAgentsRemoved_Implementation(const TArray<int32>& AgentIds)
{
	// Clean up episode data for removed agents
	for (int32 AgentId : AgentIds)
	{
		EpisodeStartTimes.Remove(AgentId);
		UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsTrainer: Agent %d removed"), AgentId);
	}
}

void UTankLearningAgentsTrainer::OnAgentsReset_Implementation(const TArray<int32>& AgentIds)
{
	// Reset episode start times for reset agents
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	for (int32 AgentId : AgentIds)
	{
		EpisodeStartTimes.Add(AgentId, CurrentTime);
		UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsTrainer: Agent %d reset, episode restarted at %.2f"), AgentId, CurrentTime);
	}
}

void UTankLearningAgentsTrainer::UpdateEpisodes()
{
	if (!GetAgentManager())
	{
		return;
	}

	// Check all agents and reset episodes if needed
	TArray<int32> AgentsToReset;

	for (const TPair<int32, float>& Entry : EpisodeStartTimes)
	{
		if (ShouldResetEpisode(Entry.Key))
		{
			AgentsToReset.Add(Entry.Key);
		}
	}

	// Reset agents that need it
	for (int32 AgentId : AgentsToReset)
	{
		ResetEpisodeForAgent(AgentId);
	}
}

bool UTankLearningAgentsTrainer::ShouldResetEpisode(int32 AgentId) const
{
	ABaseTankAIController* TankController = GetTankControllerFromAgentId(AgentId);
	if (!TankController || !GetWorld())
	{
		return false;
	}

	// Check if episode duration exceeded
	if (EpisodeStartTimes.Contains(AgentId))
	{
		const float CurrentTime = GetWorld()->GetTimeSeconds();
		const float EpisodeDuration = CurrentTime - EpisodeStartTimes[AgentId];

		if (EpisodeDuration >= MaxEpisodeDuration)
		{
			UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsTrainer: Agent %d episode timeout (%.2fs)"), AgentId, EpisodeDuration);
			return true;
		}
	}

	// Check if tank is stuck or in collision
	const TArray<float>& LineTraces = TankController->GetLineTraceDistances();
	int32 CloseObstacleCount = 0;
	for (float Distance : LineTraces)
	{
		if (Distance < 0.1f) // Normalized distance < 0.1 means very close
		{
			CloseObstacleCount++;
		}
	}

	// If more than half the traces detect very close obstacles, reset
	if (CloseObstacleCount > LineTraces.Num() / 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("TankLearningAgentsTrainer: Agent %d stuck in collision (%d/%d traces blocked)"),
			AgentId, CloseObstacleCount, LineTraces.Num());
		return true;
	}

	return false;
}

void UTankLearningAgentsTrainer::ResetEpisodeForAgent(int32 AgentId)
{
	ABaseTankAIController* TankController = GetTankControllerFromAgentId(AgentId);
	if (!TankController || !GetWorld())
	{
		return;
	}

	AWR_Tank_Pawn* TankPawn = TankController->GetControlledTank();
	if (!TankPawn)
	{
		return;
	}

	// Reset episode start time
	EpisodeStartTimes.Add(AgentId, GetWorld()->GetTimeSeconds());

	// Reset tank position and rotation to a spawn point
	// Note: You should implement proper spawn point logic here
	// For now, we'll just reset velocity and rotation
	TankPawn->SetActorRotation(FRotator::ZeroRotator);

	UE_LOG(LogTemp, Log, TEXT("TankLearningAgentsTrainer: Reset episode for agent %d"), AgentId);
}

ABaseTankAIController* UTankLearningAgentsTrainer::GetTankControllerFromAgentId(int32 AgentId) const
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
