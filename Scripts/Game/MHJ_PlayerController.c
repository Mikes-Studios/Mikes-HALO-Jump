//------------------------------------------------------------------------------------------------
//! Client-to-server HALO request. Player controller is always owned by the client.
//------------------------------------------------------------------------------------------------
modded class SCR_PlayerController
{
	//------------------------------------------------------------------------------------------------
	void MHJ_AskHaloJump(vector dropXZ, float jumpAlt, float openAlt)
	{
		if (Replication.IsServer())
		{
			MHJ_PerformHaloJump(dropXZ, jumpAlt, openAlt);
			return;
		}

		Rpc(RpcAsk_MHJ_HaloJump, dropXZ, jumpAlt, openAlt);
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_AskFreefallSteer(float turn, float pitch)
	{
		Rpc(RpcAsk_MHJ_FreefallSteer, turn, pitch);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Unreliable, RplRcver.Server)]
	protected void RpcAsk_MHJ_FreefallSteer(float turn, float pitch)
	{
		IEntity controlled = GetControlledEntity();
		if (!controlled)
			return;

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(controlled.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (!handler)
			return;

		handler.MHJ_ApplyFreefallSteer(turn, pitch);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_MHJ_HaloJump(vector dropXZ, float jumpAlt, float openAlt)
	{
		MHJ_PerformHaloJump(dropXZ, jumpAlt, openAlt);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_PerformHaloJump(vector dropXZ, float jumpAlt, float openAlt)
	{
		if (!MHJ_ValidateJump(dropXZ, jumpAlt, openAlt))
			return;

		int playerId = GetPlayerId();
		IEntity controlled = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		if (!controlled)
		{
			MHJ_Log.Warning("HALO rejected: no controlled entity");
			return;
		}

		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(controlled);
		if (!character)
			return;

		if (character.IsInVehicle())
		{
			MHJ_Log.Warning("HALO rejected: in vehicle");
			return;
		}

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(character.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (handler && handler.MHJ_IsHaloJumping())
		{
			MHJ_Log.Warning("HALO rejected: already jumping");
			return;
		}

		float terrainY = 0;
		BaseWorld world = GetGame().GetWorld();
		if (world)
			terrainY = world.GetSurfaceY(dropXZ[0], dropXZ[2]);

		vector jumpPos;
		jumpPos[0] = dropXZ[0];
		jumpPos[1] = terrainY + jumpAlt;
		jumpPos[2] = dropXZ[2];

		if (!SCR_Global.TeleportPlayer(playerId, jumpPos, SCR_EPlayerTeleportedReason.DEFAULT))
		{
			MHJ_Log.Error("HALO teleport failed");
			return;
		}

		if (handler)
			handler.MHJ_StartHaloJump(openAlt);

		Rpc(RpcDo_MHJ_BeginHalo, jumpPos, openAlt);
		MHJ_Log.Info("HALO start player " + playerId.ToString() + " at " + jumpPos.ToString());
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_MHJ_BeginHalo(vector jumpPos, float openAlt)
	{
		if (Replication.IsServer())
			return;

		int playerId = GetPlayerId();
		SCR_Global.TeleportPlayer(playerId, jumpPos, SCR_EPlayerTeleportedReason.DEFAULT);

		IEntity controlled = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		if (!controlled)
			return;

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(controlled.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (handler)
			handler.MHJ_StartHaloJump(openAlt);
	}

	//------------------------------------------------------------------------------------------------
	protected bool MHJ_ValidateJump(vector dropXZ, float jumpAlt, float openAlt)
	{
		if (jumpAlt < MHJ_Constants.JUMP_ALT_MIN)
			return false;
		if (jumpAlt > MHJ_Constants.JUMP_ALT_MAX)
			return false;
		if (openAlt < MHJ_Constants.OPEN_ALT_MIN)
			return false;
		if (openAlt > MHJ_Constants.OPEN_ALT_MAX)
			return false;
		if (jumpAlt < openAlt + MHJ_Constants.OPEN_MARGIN)
			return false;

		vector checkPos;
		checkPos[0] = dropXZ[0];
		checkPos[2] = dropXZ[2];
		BaseWorld world = GetGame().GetWorld();
		if (world)
			checkPos[1] = world.GetSurfaceY(dropXZ[0], dropXZ[2]) + jumpAlt;
		else
			checkPos[1] = jumpAlt;
		if (!SCR_Global.IsPositionWithinTerrainBounds(checkPos))
		{
			MHJ_Log.Warning("HALO rejected: drop outside terrain");
			return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_AskLeaveCanopy(bool ragdoll)
	{
		IEntity localChar = SCR_PlayerController.GetLocalControlledEntity();
		IEntity controlled = GetControlledEntity();
		if (localChar && controlled)
		{
			if (localChar == controlled)
			{
				MHJ_LeaveCanopyOnOwner(ragdoll);
				return;
			}
		}

		Rpc(RpcDo_MHJ_LeaveCanopy, ragdoll);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_MHJ_LeaveCanopy(bool ragdoll)
	{
		MHJ_LeaveCanopyOnOwner(ragdoll);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_LeaveCanopyOnOwner(bool ragdoll)
	{
		IEntity controlled = GetControlledEntity();
		if (!controlled)
			return;

		ChimeraCharacter ch = ChimeraCharacter.Cast(controlled);
		if (!ch)
			return;

		CompartmentAccessComponent access = ch.GetCompartmentAccessComponent();
		if (!access)
			return;
		if (!access.IsInCompartment())
			return;

		vector mat[4];
		ch.GetTransform(mat);
		vector ypr = Math3D.MatrixToAngles(mat);
		ypr[1] = 0;
		ypr[2] = 0;
		Math3D.AnglesToMatrix(ypr, mat);

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(ch.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (handler)
			handler.MHJ_FillGroundedTransform(ch, mat);
		else
			mat[3] = ch.GetOrigin();

		access.GetOutVehicle_NoDoor(mat, ragdoll, true, true);
		if (!access.IsInCompartment())
		{
			if (access.IsGettingOut())
				access.InterruptVehicleActionQueue(true, true, true);
		}
		MHJ_Log.Info("Canopy owner GetOut ragdoll=" + MHJ_Log.Flag(ragdoll) + " stillIn=" + MHJ_Log.Flag(access.IsInCompartment()) + " gettingOut=" + MHJ_Log.Flag(access.IsGettingOut()));
	}
}
