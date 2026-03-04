#pragma once

#include "CoreMinimal.h"
#include "AILearningAgentsController.h"
#include "WR_ControlsInterface.h"
#include "WR_PlayerInfoStruct.h"
#include "WR_AITankController.generated.h"

// Forward declarations
class AWR_MapConfiguration;

/**
 * WR_AITankController - Game-specific AI tank controller
 * Inherits from AAILearningAgentsController for ML-based movement
 * Used for AI opponents in multiplayer lobbies and game matches
 * Implements IWR_ControlsInterface for tank control callbacks (RespawnTankAfterDestroy, etc.)
 */
UCLASS()
class TANKAICONTROLLER_API AWR_AITankController : public AAILearningAgentsController, public IWR_ControlsInterface
{
	GENERATED_BODY()

public:
	AWR_AITankController();

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

public:
	// ========== AI PLAYER INFO ==========

	/** Display name for this AI player (shown in lobby/scoreboard) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Player", meta = (ExposeOnSpawn = "true"))
	FString AIPlayerName = TEXT("Bot");

	/** Team index for this AI player */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Player", meta = (ExposeOnSpawn = "true"))
	int32 TeamIndex = 0;

	/** Unique player index assigned by GameMode */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Player")
	int32 PlayerIndex = -1;

	/** Set player index (called by GameMode) */
	UFUNCTION(BlueprintCallable, Category = "AI|Player")
	void SetPlayerIndex(int32 InPlayerIndex)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AIFlow] SetPlayerIndex: AI: %s | OldIndex: %d -> NewIndex: %d"), *AIPlayerName, PlayerIndex, InPlayerIndex);
		PlayerIndex = InPlayerIndex;
	}

	/** Get AI player name */
	UFUNCTION(BlueprintPure, Category = "AI|Player")
	FString GetAIPlayerName() const { return AIPlayerName; }

	/** Get team index */
	UFUNCTION(BlueprintPure, Category = "AI|Player")
	int32 GetTeamIndex() const { return TeamIndex; }

	// ========== GAME STATE ==========

	/** Is game started flag (local only - GameState is authoritative) */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "AI|GameState")
	bool bIsGameStart = false;

	/** Is game ended flag (local only - GameState is authoritative) */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "AI|GameState")
	bool bIsGameEnd = false;

	// ========== AI PLAYER STATS ==========
	// NOTE: These are local copies for MakePlayerInfoStruct().
	// Authoritative stats are in GameState->LoggedPlayers array.

	/** Number of kills (local copy) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Stats")
	int32 Kills = 0;

	/** Number of deaths (local copy) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Stats")
	int32 Deaths = 0;

	/** Number of assists (local copy) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Stats")
	int32 Assistances = 0;

	/** Player score (local copy) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Stats")
	int32 Score = 0;

	/** Add a kill to stats */
	UFUNCTION(BlueprintCallable, Category = "AI|Stats")
	void AddKill() { Kills++; Score += 100; }

	/** Add a death to stats */
	UFUNCTION(BlueprintCallable, Category = "AI|Stats")
	void AddDeath() { Deaths++; }

	/** Add an assist to stats */
	UFUNCTION(BlueprintCallable, Category = "AI|Stats")
	void AddAssist() { Assistances++; Score += 50; }

	/** Reset all stats */
	UFUNCTION(BlueprintCallable, Category = "AI|Stats")
	void ResetStats() { Kills = 0; Deaths = 0; Assistances = 0; Score = 0; }

	// ========== PLAYER INFO STRUCT ==========

	/** Generate WR_PlayerInfoStruct for this AI player (for GameState LoggedPlayers array) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "AI|Player")
	FWR_PlayerInfoStruct MakePlayerInfoStruct() const;

	/** Get team as EWR_TeamTypeEnum */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "AI|Player")
	EWR_TeamTypeEnum GetTeamType() const;

	// ========== GAME EVENTS ==========

	/** Prepare for round start - disables movement, sets bIsGameStart to false. Server-only. */
	UFUNCTION(BlueprintCallable, Category = "AI|GameEvents")
	void PrepareStartRound();

	/** Start the round - enables movement, sets bIsGameStart to true, bIsGameEnd to false. Server-only. */
	UFUNCTION(BlueprintCallable, Category = "AI|GameEvents")
	void StartRound();

	/** End the round - disables AI movement, sets bIsGameEnd to true, bIsGameStart to false. Server-only. */
	UFUNCTION(BlueprintCallable, Category = "AI|GameEvents")
	void EndRound();

	// ========== TANK SETUP (DataAssets) ==========
	// NOTE: Tank spawning and possession is handled by GameMode.
	// These indices are used by GameMode when creating the tank.

	/** Index of tank to use from TanksDataAsset (0 = first tank) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|TankSetup")
	int32 SelectedTankIndex = 0;

	/** Index of turret to use from TurretsDataAsset (0 = first turret) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|TankSetup")
	int32 SelectedTurretIndex = 0;

	/** Index of skin texture to use from SkinTexturesDataAsset (0 = first skin) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|TankSetup")
	int32 SelectedSkinIndex = 0;

	// ========== IWR_ControlsInterface ==========

	/** Called when tank is destroyed and needs to respawn. Server-only. */
	virtual void RespawnTankAfterDestroy_Implementation() override;

	/** Called when damage is dealt - forwards to GameState for stat tracking.
	 *  DamagedPlayerIndex = player who died
	 *  KillerPlayerIndex = player who got the kill
	 *  AssistantPlayerIndex = player who assisted (-1 if none) */
	virtual void PC_Take_Damage_Implementation(int32 DamagedPlayerIndex, int32 KillerPlayerIndex, int32 AssistantPlayerIndex) override;

	// ========== IWR_ControlsInterface - Game State ==========
	virtual bool IsGameStarted_Implementation() const override { return bIsGameStart; }
	virtual bool IsGameEnded_Implementation() const override { return bIsGameEnd; }
	virtual int32 GetPlayerIndex_Implementation() const override { return PlayerIndex; }

	/** Called to enable/disable AI movement (e.g., when tank is destroyed) */
	virtual void UpdateMovementEnabled_Implementation(bool MovementEnabled) override;

	// ========== COLLECTABLES ==========

	/** Apply full health to controlled tank (called when picking up health collectable) */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "AI|Collectables")
	void ApplyFullHealth();
	virtual void ApplyFullHealth_Implementation();

	/** Apply full armor to controlled tank (called when picking up armor collectable) */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "AI|Collectables")
	void ApplyFullArmor();
	virtual void ApplyFullArmor_Implementation();

	/** Apply full battery to controlled tank (called when picking up battery collectable) */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "AI|Collectables")
	void ApplyFullBattery();
	virtual void ApplyFullBattery_Implementation();

	// ========== REPLICATION ==========

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	// ========== MAP CONFIGURATION ==========

	/** Cached reference to MapConfiguration actor (used for respawn location) */
	UPROPERTY()
	TObjectPtr<AWR_MapConfiguration> MapConfigurationActor;

	/** Find MapConfiguration actor in world */
	AWR_MapConfiguration* FindMapConfigurationActor();
};
