//------------------------------------------------------------------------------------------------
//! Close the HALO planner and jump HUD when the game instance is torn down so
//! Workbench Reload Game cannot leak MapWidget / PLAIN map-session natives.
//!
//! Consumer: loaded with the addon. Do not instantiate.
//!
//! Extend: keep the Close() calls; always call super.
//------------------------------------------------------------------------------------------------
modded class SCR_BaseGameMode
{
	//------------------------------------------------------------------------------------------------
	override void OnGameEnd()
	{
		MHJ_HaloJumpMenu.Close();
		MHJ_JumpHud.Close();
		super.OnGameEnd();
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_RelayAiDropAdd(RplId jumperId)
	{
		Rpc(RpcDo_MHJ_AiDropAdd, jumperId);
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_RelayAiDropRemove(RplId jumperId)
	{
		Rpc(RpcDo_MHJ_AiDropRemove, jumperId);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_MHJ_AiDropAdd(RplId jumperId)
	{
		MHJ_AiDropDirector.ClientAddRemote(jumperId);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_MHJ_AiDropRemove(RplId jumperId)
	{
		MHJ_AiDropDirector.ClientRemoveRemote(jumperId);
	}
}
