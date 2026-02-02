#include "WR_AITankController.h"
#include "WR_Tank_Pawn.h"
#include "WR_MapConfiguration.h"
#include "WR_TeamTypeEnum.h"
#include "WR_CollectableInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/GameStateBase.h"

AWR_AITankController::AWR_AITankController()
{
	// Default AI player name with random number
	AIPlayerName = FString::Printf(TEXT("Bot_%d"), FMath::RandRange(1, 999));

	// Enable replication
	bReplicates = true;

	// NOTE: Tank spawning and possession is handled by GameMode.
	// GameMode spawns tank, then calls this->Possess(Tank).
	// Learning Agents inference is automatically initialized in OnPossess.
}

void AWR_AITankController::BeginPlay()
{
	Super::BeginPlay();
}

void AWR_AITankController::OnPossess(APawn* InPawn)
{
	// Skip AAILearningAgentsController::OnPossess - we don't want auto-enable of movement/inference
	// Call grandparent directly
	ABaseTankAIController::OnPossess(InPawn);

	// Setup AI turret control (same as parent would do)
	if (ControlledTank)
	{
		ControlledTank->bUseAITurretControl = true;
	}

	// NOTE: Initialize_PlayerNetID is NOT called here!
	// The PlayerNetID is actually the index in GameState->LoggedPlayers array.
	// BP GameMode must:
	// 1. Add AI to LoggedPlayers (using MakePlayerInfoStruct from this controller)
	// 2. Get the AI's index in LoggedPlayers array
	// 3. Call Initialize_PlayerNetID(index) on ControlledTank
	// 4. Call SetPlayerIndex(index) on this controller
	// This matches how human players are initialized in BP_PlayerController.
}

void AWR_AITankController::OnUnPossess()
{
	// Get pawn before calling Super (which clears it)
	APawn* PreviousPawn = GetPawn();

	if (PreviousPawn)
	{
		// Call DestroyAttachments on the pawn (turret, weapons, etc.)
		if (PreviousPawn->GetClass()->ImplementsInterface(UWR_ControlsInterface::StaticClass()))
		{
			IWR_ControlsInterface::Execute_DestroyAttachments(PreviousPawn);
		}
	}

	Super::OnUnPossess();
}

AWR_MapConfiguration* AWR_AITankController::FindMapConfigurationActor()
{
	if (!GetWorld())
	{
		return nullptr;
	}

	// Search for MapConfiguration actor in world
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AWR_MapConfiguration::StaticClass(), FoundActors);

	if (FoundActors.Num() > 0)
	{
		return Cast<AWR_MapConfiguration>(FoundActors[0]);
	}

	return nullptr;
}

void AWR_AITankController::PrepareStartRound()
{
	// Server-only: Only execute on authority
	if (!HasAuthority())
	{
		return;
	}

	// Set game start flag to false
	bIsGameStart = false;

	// NOTE: Do NOT disable AI movement here!
	// The Blueprint calls PrepareStartRound() but never calls StartRound(),
	// so if we disable movement here, the AI will never move.
}

void AWR_AITankController::StartRound()
{
	// Server-only
	if (!HasAuthority())
	{
		return;
	}

	// Set game state flags
	bIsGameStart = true;
	bIsGameEnd = false;

	// Initialize Learning Agents inference now (deferred from OnPossess)
	if (bUseLearningAgentsInference && !bRegisteredWithSharedManager && ControlledTank)
	{
		InitializeLearningAgentsForInference();
	}

	// Enable AI movement
	SetAIMovementEnabled(true);
}

void AWR_AITankController::EndRound()
{
	// Server-only: Only execute on authority
	if (!HasAuthority())
	{
		return;
	}

	// Set game state flags
	bIsGameStart = false;
	bIsGameEnd = true;

	// Disable AI movement
	SetAIMovementEnabled(false);
}

void AWR_AITankController::RespawnTankAfterDestroy_Implementation()
{
	// Server-only: Only execute on authority
	if (!HasAuthority())
	{
		return;
	}

	// Find MapConfiguration actor if not cached
	if (!MapConfigurationActor)
	{
		MapConfigurationActor = FindMapConfigurationActor();
	}

	// Get respawn transform from MapConfiguration
	if (MapConfigurationActor)
	{
		// Get spawn transform from map configuration (randomized spawn point)
		FTransform RespawnTransform = MapConfigurationActor->MakeTankDeathMatchTransform();

		// Get controlled tank pawn
		APawn* ControlledPawn = GetPawn();
		if (ControlledPawn && ControlledPawn->GetClass()->ImplementsInterface(UWR_ControlsInterface::StaticClass()))
		{
			// Call RespawnTank on the controlled pawn
			IWR_ControlsInterface::Execute_RespawnTank(ControlledPawn, RespawnTransform, true);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("AWR_AITankController::RespawnTankAfterDestroy - %s has no valid pawn"), *AIPlayerName);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("AWR_AITankController::RespawnTankAfterDestroy - No MapConfiguration actor found!"));
	}
}

void AWR_AITankController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Stats are not replicated - GameState->LoggedPlayers is authoritative
}

EWR_TeamTypeEnum AWR_AITankController::GetTeamType() const
{
	switch (TeamIndex)
	{
	case 0:
		return EWR_TeamTypeEnum::TeamA;
	case 1:
		return EWR_TeamTypeEnum::TeamB;
	default:
		return EWR_TeamTypeEnum::TeamUndefined;
	}
}

FWR_PlayerInfoStruct AWR_AITankController::MakePlayerInfoStruct() const
{
	FWR_PlayerInfoStruct PlayerInfo;

	// AI doesn't have a real UniqueNetId, leave it empty/invalid
	// PlayerInfo.PlayerUniqueId will be default (invalid)

	// Set player name from AI name
	PlayerInfo.PlayerName = FName(*AIPlayerName);

	// Set team based on TeamIndex
	PlayerInfo.PlayerTeam = GetTeamType();

	// AI is always ready
	PlayerInfo.bIsReady = true;

	// AI is never the server
	PlayerInfo.bIsServer = false;

	// AI is not disconnected
	PlayerInfo.bIsDisconnect = false;

	// Mark as AI player
	PlayerInfo.bIsAI = true;

	// Copy stats
	PlayerInfo.Kills = Kills;
	PlayerInfo.Deaths = Deaths;
	PlayerInfo.Assistances = Assistances;
	PlayerInfo.Score = Score;

	// Set owning controller reference (server-only, not replicated)
	PlayerInfo.OwningController = const_cast<AWR_AITankController*>(this);

	return PlayerInfo;
}

void AWR_AITankController::PC_Take_Damage_Implementation(int32 DamagedPlayerIndex, int32 KillerPlayerIndex, int32 AssistantPlayerIndex)
{
	// Call Server_Take_Damage on GameState (same as human player controller Blueprint does)
	// GameState handles stats, respawn, etc.
	if (UWorld* World = GetWorld())
	{
		if (AGameStateBase* GameState = World->GetGameState<AGameStateBase>())
		{
			// Call Blueprint function Server_Take_Damage on GameState
			UFunction* ServerTakeDamageFunc = GameState->FindFunction(FName("Server_Take_Damage"));
			if (ServerTakeDamageFunc)
			{
				struct
				{
					int32 DamagedPlayerIndex;
					int32 KillerPlayerIndex;
					int32 AssistantPlayerIndex;
				} Params;

				Params.DamagedPlayerIndex = DamagedPlayerIndex;
				Params.KillerPlayerIndex = KillerPlayerIndex;
				Params.AssistantPlayerIndex = AssistantPlayerIndex;

				GameState->ProcessEvent(ServerTakeDamageFunc, &Params);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("AWR_AITankController::PC_Take_Damage - Server_Take_Damage function not found on GameState!"));
			}
		}
	}
}

void AWR_AITankController::UpdateMovementEnabled_Implementation(bool MovementEnabled)
{
	// Enable/disable AI movement (called when tank is destroyed/respawned)
	SetAIMovementEnabled(MovementEnabled);
}

void AWR_AITankController::ApplyFullHealth_Implementation()
{
	APawn* ControlledPawn = GetPawn();
	if (ControlledPawn && ControlledPawn->GetClass()->ImplementsInterface(UWR_CollectableInterface::StaticClass()))
	{
		IWR_CollectableInterface::Execute_TakeCollectableHealth(ControlledPawn);
	}
}

void AWR_AITankController::ApplyFullArmor_Implementation()
{
	APawn* ControlledPawn = GetPawn();
	if (ControlledPawn && ControlledPawn->GetClass()->ImplementsInterface(UWR_CollectableInterface::StaticClass()))
	{
		IWR_CollectableInterface::Execute_TakeCollectableArmor(ControlledPawn);
	}
}

void AWR_AITankController::ApplyFullBattery_Implementation()
{
	APawn* ControlledPawn = GetPawn();
	if (ControlledPawn && ControlledPawn->GetClass()->ImplementsInterface(UWR_CollectableInterface::StaticClass()))
	{
		IWR_CollectableInterface::Execute_TakeCollectableBattery(ControlledPawn);
	}
}

