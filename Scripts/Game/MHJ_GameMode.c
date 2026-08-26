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
	void MHJ_RelayAiDropAdd(RplId jumperId, vector ypr)
	{
		Rpc(RpcDo_MHJ_AiDropAdd, jumperId, ypr);
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_RelayAiDropRemove(RplId jumperId)
	{
		Rpc(RpcDo_MHJ_AiDropRemove, jumperId);
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_RelayAiDropPose(RplId jumperId, vector ypr)
	{
		Rpc(RpcDo_MHJ_AiDropPose, jumperId, ypr);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_MHJ_AiDropAdd(RplId jumperId, vector ypr)
	{
		MHJ_AiDropDirector.ClientAddRemote(jumperId, ypr);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_MHJ_AiDropRemove(RplId jumperId)
	{
		MHJ_AiDropDirector.ClientRemoveRemote(jumperId);
	}

	//------------------------------------------------------------------------------------------------
	//! Visual-only. Lost packets are covered by SmoothCD on the last pose.
	[RplRpc(RplChannel.Unreliable, RplRcver.Broadcast)]
	protected void RpcDo_MHJ_AiDropPose(RplId jumperId, vector ypr)
	{
		MHJ_AiDropDirector.ClientApplyRelay(jumperId, ypr);
	}
}
