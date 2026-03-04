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

	UE_LOG(LogTemp, Log, TEXT("[AIFlow] AWR_AITankController CONSTRUCTED: %s"), *AIPlayerName);

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
	UE_LOG(LogTemp, Warning, TEXT("[AIFlow] === AWR_AITankController::OnPossess START === AI: %s | Pawn: %s"),
		*AIPlayerName, InPawn ? *InPawn->GetName() : TEXT("NULL"));

	// Skip AAILearningAgentsController::OnPossess - we don't want auto-enable of movement/inference
	// Call grandparent directly
	ABaseTankAIController::OnPossess(InPawn);

	// Setup AI turret control (same as parent would do)
	if (ControlledTank)
	{
		ControlledTank->bUseAITurretControl = true;
		UE_LOG(LogTemp, Log, TEXT("[AIFlow]   OnPossess: Tank possessed OK, bUseAITurretControl=true | Tank: %s | PlayerIndex: %d"),
			*ControlledTank->GetName(), PlayerIndex);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[AIFlow]   OnPossess: FAILED - ControlledTank is NULL after ABaseTankAIController::OnPossess!"));
	}

	UE_LOG(LogTemp, Warning, TEXT("[AIFlow] === AWR_AITankController::OnPossess END === AI: %s | GameStart: %d | GameEnd: %d"),
		*AIPlayerName, bIsGameStart, bIsGameEnd);

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

	UE_LOG(LogTemp, Warning, TEXT("[AIFlow] === AWR_AITankController::OnUnPossess === AI: %s | PlayerIndex: %d | Pawn: %s | bRegisteredWithSharedManager: %d"),
		*AIPlayerName, PlayerIndex, PreviousPawn ? *PreviousPawn->GetName() : TEXT("NULL"), bRegisteredWithSharedManager);

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

	UE_LOG(LogTemp, Warning, TEXT("[AIFlow] PrepareStartRound: AI: %s | PlayerIndex: %d | bIsGameStart was: %d"),
		*AIPlayerName, PlayerIndex, bIsGameStart);

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

	UE_LOG(LogTemp, Warning, TEXT("[AIFlow] === StartRound === AI: %s | PlayerIndex: %d | Tank: %s | bRegisteredWithSharedManager: %d"),
		*AIPlayerName, PlayerIndex, ControlledTank ? *ControlledTank->GetName() : TEXT("NULL"), bRegisteredWithSharedManager);

	// Set game state flags
	bIsGameStart = true;
	bIsGameEnd = false;

	// Initialize Learning Agents inference now (deferred from OnPossess)
	if (bUseLearningAgentsInference && !bRegisteredWithSharedManager && ControlledTank)
	{
		UE_LOG(LogTemp, Log, TEXT("[AIFlow]   StartRound: Initializing Learning Agents inference (deferred)..."));
		InitializeLearningAgentsForInference();
	}

	// Enable AI movement
	UE_LOG(LogTemp, Log, TEXT("[AIFlow]   StartRound: Enabling AI movement..."));
	SetAIMovementEnabled(true);

	UE_LOG(LogTemp, Warning, TEXT("[AIFlow] === StartRound COMPLETE === AI: %s | MovementEnabled: %d | InferenceActive: %d"),
		*AIPlayerName, bAIMovementEnabled, bRegisteredWithSharedManager);
}

void AWR_AITankController::EndRound()
{
	// Server-only: Only execute on authority
	if (!HasAuthority())
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[AIFlow] === EndRound === AI: %s | PlayerIndex: %d | Kills: %d | Deaths: %d | Score: %d"),
		*AIPlayerName, PlayerIndex, Kills, Deaths, Score);

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

	UE_LOG(LogTemp, Warning, TEXT("[AIFlow] === RESPAWN START === AI: %s | PlayerIndex: %d | bIsRecovering: %d | bIsStuck: %d"),
		*AIPlayerName, PlayerIndex, bIsRecovering, bIsStuck);

	// Find MapConfiguration actor if not cached
	if (!MapConfigurationActor)
	{
		MapConfigurationActor = FindMapConfigurationActor();
		UE_LOG(LogTemp, Log, TEXT("[AIFlow]   Respawn: MapConfiguration %s"),
			MapConfigurationActor ? TEXT("FOUND") : TEXT("NOT FOUND"));
	}

	// Get respawn transform from MapConfiguration
	if (MapConfigurationActor)
	{
		// Get spawn transform from map configuration (randomized spawn point)
		FTransform RespawnTransform = MapConfigurationActor->MakeTankDeathMatchTransform();

		UE_LOG(LogTemp, Log, TEXT("[AIFlow]   Respawn: SpawnLocation: %s"),
			*RespawnTransform.GetLocation().ToString());

		// Get controlled tank pawn
		APawn* ControlledPawn = GetPawn();
		if (ControlledPawn && ControlledPawn->GetClass()->ImplementsInterface(UWR_ControlsInterface::StaticClass()))
		{
			// Call RespawnTank on the controlled pawn
			IWR_ControlsInterface::Execute_RespawnTank(ControlledPawn, RespawnTransform, true);
			UE_LOG(LogTemp, Warning, TEXT("[AIFlow] === RESPAWN COMPLETE === AI: %s | Tank: %s"),
				*AIPlayerName, *ControlledPawn->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[AIFlow]   RESPAWN FAILED: AI %s has no valid pawn! Pawn: %s"),
				*AIPlayerName, ControlledPawn ? *ControlledPawn->GetName() : TEXT("NULL"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[AIFlow]   RESPAWN FAILED: No MapConfiguration actor found for AI: %s"), *AIPlayerName);
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
	UE_LOG(LogTemp, Log, TEXT("[AIFlow] MakePlayerInfoStruct: AI: %s | PlayerIndex: %d | Team: %d | Kills: %d | Deaths: %d | Score: %d"),
		*AIPlayerName, PlayerIndex, TeamIndex, Kills, Deaths, Score);

	FWR_PlayerInfoStruct PlayerInfo;

	// Create a synthetic UniqueNetId for the AI player
	FString AIUniqueIdString = FString::Printf(TEXT("AI_%s_%d"), *AIPlayerName, PlayerIndex);
	FUniqueNetIdRef AINetIdRef = FUniqueNetIdString::Create(AIUniqueIdString, NAME_None);
	PlayerInfo.PlayerUniqueId = FUniqueNetIdRepl(AINetIdRef);

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
	UE_LOG(LogTemp, Warning, TEXT("[AIFlow] === PC_Take_Damage === AI: %s | KILLED! DamagedIdx: %d | KillerIdx: %d | AssistIdx: %d"),
		*AIPlayerName, DamagedPlayerIndex, KillerPlayerIndex, AssistantPlayerIndex);

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
				UE_LOG(LogTemp, Log, TEXT("[AIFlow]   PC_Take_Damage: Server_Take_Damage called on GameState OK"));
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[AIFlow]   PC_Take_Damage: Server_Take_Damage function NOT FOUND on GameState: %s"),
					*GameState->GetClass()->GetName());
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[AIFlow]   PC_Take_Damage: GameState is NULL!"));
		}
	}
}

void AWR_AITankController::UpdateMovementEnabled_Implementation(bool MovementEnabled)
{
	UE_LOG(LogTemp, Log, TEXT("[AIFlow] UpdateMovementEnabled: AI: %s | Enabled: %d -> %d | bIsRecovering: %d"),
		*AIPlayerName, bAIMovementEnabled, MovementEnabled, bIsRecovering);

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

