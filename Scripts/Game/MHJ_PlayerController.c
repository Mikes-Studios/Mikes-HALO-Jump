//------------------------------------------------------------------------------------------------
//! Coordinates HALO deployment and the owner-owned canopy setup. Freefall input
//! uses this client-owned node; canopy input goes directly through the owned canopy.
//------------------------------------------------------------------------------------------------
modded class SCR_PlayerController
{
	protected RplId m_MHJ_CanopyId = RplId.Invalid();
	protected IEntity m_MHJ_ServerCanopy;
	protected int m_iMHJ_SetupTries;
	protected int m_iMHJ_ExitTries;
	protected float m_fMHJ_ExitDown;
	protected float m_fMHJ_ExitHorizontal;
	protected float m_fMHJ_ExitHeading;
	protected vector m_vMHJ_PendingJumpPosition;
	protected float m_fMHJ_PendingOpenAltitude;
	protected int m_iMHJ_BeginTries;

	//------------------------------------------------------------------------------------------------
	override void OnControlledEntityChanged(IEntity from, IEntity to)
	{
		if (from && from != to)
		{
			SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(from.FindComponent(SCR_CharacterCommandHandlerComponent));
			if (handler)
				handler.MHJ_OnControlledEntityLeaving();
		}

		super.OnControlledEntityChanged(from, to);
		if (from && from != to)
			MHJ_CancelCanopyCoordination(true);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDestroyed(notnull Instigator killer)
	{
		MHJ_CancelCanopyCoordination(true);
		super.OnDestroyed(killer);
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_AskHaloJump(vector dropXZ, float jumpAltitude, float openAltitude)
	{
		if (!MHJ_ValidateJump(dropXZ, jumpAltitude, openAltitude))
			return;

		if (Replication.IsServer())
		{
			MHJ_PerformHaloJump(dropXZ, jumpAltitude, openAltitude);
			return;
		}

		Rpc(RpcAsk_MHJ_HaloJump, dropXZ, jumpAltitude, openAltitude);
		MHJ_LocalStartHalo(dropXZ, jumpAltitude, openAltitude);
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
		if (handler)
			handler.MHJ_ApplyFreefallSteer(turn, pitch);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_MHJ_HaloJump(vector dropXZ, float jumpAltitude, float openAltitude)
	{
		MHJ_PerformHaloJump(dropXZ, jumpAltitude, openAltitude);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_PerformHaloJump(vector dropXZ, float jumpAltitude, float openAltitude)
	{
		if (!MHJ_ValidateJump(dropXZ, jumpAltitude, openAltitude))
			return;

		int playerId = GetPlayerId();
		IEntity controlled = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(controlled);
		if (!character)
			return;
		if (character.IsInVehicle())
			return;

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(character.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (handler && handler.MHJ_IsHaloJumping())
			return;

		BaseWorld world = GetGame().GetWorld();
		float terrainY = 0;
		if (world)
			terrainY = world.GetSurfaceY(dropXZ[0], dropXZ[2]);

		vector jumpPosition;
		jumpPosition[0] = dropXZ[0];
		jumpPosition[1] = terrainY + jumpAltitude;
		jumpPosition[2] = dropXZ[2];

		if (!SCR_Global.TeleportPlayer(playerId, jumpPosition, SCR_EPlayerTeleportedReason.DEFAULT))
		{
			MHJ_Log.Error("HALO teleport failed");
			return;
		}

		if (handler)
			handler.MHJ_StartHaloJump(openAltitude);

		Rpc(RpcDo_MHJ_BeginHalo, jumpPosition, openAltitude);
		MHJ_Log.Info("HALO start player " + playerId.ToString() + " at " + jumpPosition.ToString());
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_MHJ_BeginHalo(vector jumpPosition, float openAltitude)
	{
		if (Replication.IsServer())
			return;

		m_vMHJ_PendingJumpPosition = jumpPosition;
		m_fMHJ_PendingOpenAltitude = openAltitude;
		m_iMHJ_BeginTries = 0;
		MHJ_TryBeginHalo();
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_LocalStartHalo(vector dropXZ, float jumpAltitude, float openAltitude)
	{
		BaseWorld world = GetGame().GetWorld();
		float terrainY = 0;
		if (world)
			terrainY = world.GetSurfaceY(dropXZ[0], dropXZ[2]);

		m_vMHJ_PendingJumpPosition[0] = dropXZ[0];
		m_vMHJ_PendingJumpPosition[1] = terrainY + jumpAltitude;
		m_vMHJ_PendingJumpPosition[2] = dropXZ[2];
		m_fMHJ_PendingOpenAltitude = openAltitude;
		m_iMHJ_BeginTries = 0;
		MHJ_TryBeginHalo();
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_TryBeginHalo()
	{
		SCR_Global.TeleportPlayer(GetPlayerId(), m_vMHJ_PendingJumpPosition, SCR_EPlayerTeleportedReason.DEFAULT);

		IEntity controlled = GetControlledEntity();
		if (!controlled)
		{
			MHJ_RetryBeginHalo();
			return;
		}

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(controlled.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (!handler)
		{
			MHJ_RetryBeginHalo();
			return;
		}

		handler.MHJ_StartHaloJump(m_fMHJ_PendingOpenAltitude);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_RetryBeginHalo()
	{
		m_iMHJ_BeginTries = m_iMHJ_BeginTries + 1;
		if (m_iMHJ_BeginTries >= 40)
			return;

		GetGame().GetCallqueue().CallLater(MHJ_TryBeginHalo, 50, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only session creation. Ownership is assigned before flight setup.
	bool MHJ_StartCanopySession(notnull ChimeraCharacter jumper, vector worldVelocity, vector wind, float heading, float pitch, float bank, float simTime, float openAltitude)
	{
		if (!Replication.IsServer())
			return false;
		if (m_MHJ_ServerCanopy)
			return false;
		if (GetControlledEntity() != jumper)
			return false;

		Resource resource = Resource.Load(MHJ_Constants.CANOPY_PREFAB);
		if (!resource || !resource.IsValid())
			return false;

		ref EntitySpawnParams spawnParams = new EntitySpawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		vector ypr;
		ypr[0] = heading * Math.RAD2DEG;
		ypr[1] = 0;
		ypr[2] = 0;
		Math3D.AnglesToMatrix(ypr, spawnParams.Transform);
		spawnParams.Transform[3] = jumper.GetOrigin();

		IEntity canopy = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), spawnParams);
		if (!canopy)
			return false;

		RplComponent replication = RplComponent.Cast(canopy.FindComponent(RplComponent));
		MHJ_CanopyFlight flight = MHJ_CanopyFlight.Cast(canopy.FindComponent(MHJ_CanopyFlight));
		if (!replication || !flight)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(canopy);
			return false;
		}

		RplIdentity identity = GetRplIdentity();
		if (!identity.IsValid())
		{
			SCR_EntityHelper.DeleteEntityAndChildren(canopy);
			return false;
		}

		replication.GiveExt(identity, true);
		replication.EnableStreamingConNode(identity, true);
		if (!flight.BeginFlight(jumper, worldVelocity, wind, heading, pitch, bank, simTime, openAltitude))
		{
			replication.EnableStreamingConNode(identity, false);
			RplComponent.DeleteRplEntity(canopy, false);
			return false;
		}

		RplId canopyId = replication.Id();
		if (!canopyId.IsValid())
		{
			flight.MHJ_ServerAbortSession();
			return false;
		}

		m_MHJ_ServerCanopy = canopy;
		m_MHJ_CanopyId = canopyId;

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(jumper.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (handler)
		{
			handler.MHJ_BeginCanopySession(openAltitude);
			handler.MHJ_OnFreefallBoarded();
		}

		IEntity localCharacter = SCR_PlayerController.GetLocalControlledEntity();
		if (localCharacter == jumper)
			MHJ_ReceiveCanopySetup(canopyId, openAltitude);
		else
			Rpc(RpcDo_MHJ_CanopySetup, canopyId, openAltitude);

		MHJ_Log.Info("Canopy spawned, owned, and queued for owner setup");
		return true;
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_MHJ_CanopySetup(RplId canopyId, float openAltitude)
	{
		MHJ_ReceiveCanopySetup(canopyId, openAltitude);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_ReceiveCanopySetup(RplId canopyId, float openAltitude)
	{
		MHJ_ClearOwnerSetupCalls();
		m_MHJ_CanopyId = canopyId;
		m_fMHJ_PendingOpenAltitude = openAltitude;
		m_iMHJ_SetupTries = 0;
		MHJ_TryOwnerCanopySetup();
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_TryOwnerCanopySetup()
	{
		if (!m_MHJ_CanopyId.IsValid())
			return;

		ChimeraCharacter jumper = ChimeraCharacter.Cast(GetControlledEntity());
		if (!jumper)
		{
			MHJ_RetryOwnerCanopySetup();
			return;
		}

		CharacterControllerComponent controller = jumper.GetCharacterController();
		if (!controller || controller.IsDead())
		{
			MHJ_FailOwnerCanopySetup();
			return;
		}

		IEntity canopy = MHJ_ResolveCanopy(m_MHJ_CanopyId);
		if (!canopy)
		{
			MHJ_RetryOwnerCanopySetup();
			return;
		}

		RplComponent replication = RplComponent.Cast(canopy.FindComponent(RplComponent));
		MHJ_CanopyFlight flight = MHJ_CanopyFlight.Cast(canopy.FindComponent(MHJ_CanopyFlight));
		if (!replication || !flight)
		{
			MHJ_RetryOwnerCanopySetup();
			return;
		}

		BaseCompartmentSlot slot = flight.GetCanopySlot();
		if (!slot)
		{
			MHJ_RetryOwnerCanopySetup();
			return;
		}

		CompartmentAccessComponent access = jumper.GetCompartmentAccessComponent();
		if (!access)
		{
			MHJ_FailOwnerCanopySetup();
			return;
		}
		if (access.IsInCompartment())
		{
			if (MHJ_CanopyFlight.OccupantIsInCanopy(jumper))
			{
				MHJ_OnOwnerBoarded(flight);
				return;
			}

			MHJ_RetryOwnerCanopySetup();
			return;
		}
		if (access.IsGettingIn())
		{
			GetGame().GetCallqueue().CallLater(MHJ_MonitorOwnerBoard, 50, false);
			return;
		}
		if (access.IsGettingOut())
		{
			MHJ_RetryOwnerCanopySetup();
			return;
		}

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(jumper.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (!handler)
		{
			MHJ_FailOwnerCanopySetup();
			return;
		}

		handler.MHJ_BeginCanopySession(m_fMHJ_PendingOpenAltitude);
		flight.MHJ_BeginOwnerSession(jumper, m_fMHJ_PendingOpenAltitude);
		handler.MHJ_PrepareCanopyBoard();
		GetGame().GetCallqueue().CallLater(MHJ_TryOwnerGetIn, 50, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_TryOwnerGetIn()
	{
		if (!m_MHJ_CanopyId.IsValid())
			return;

		ChimeraCharacter jumper = ChimeraCharacter.Cast(GetControlledEntity());
		if (!jumper)
		{
			MHJ_RetryOwnerCanopySetup();
			return;
		}

		IEntity canopy = MHJ_ResolveCanopy(m_MHJ_CanopyId);
		MHJ_CanopyFlight flight;
		if (canopy)
			flight = MHJ_CanopyFlight.Cast(canopy.FindComponent(MHJ_CanopyFlight));
		if (!canopy || !flight)
		{
			MHJ_RetryOwnerCanopySetup();
			return;
		}

		if (MHJ_CanopyFlight.OccupantIsInCanopy(jumper))
		{
			MHJ_OnOwnerBoarded(flight);
			return;
		}

		BaseCompartmentSlot slot = flight.GetCanopySlot();
		CompartmentAccessComponent access = jumper.GetCompartmentAccessComponent();
		if (!slot || !access)
		{
			MHJ_FailOwnerCanopySetup();
			return;
		}
		if (access.IsGettingIn())
		{
			GetGame().GetCallqueue().CallLater(MHJ_MonitorOwnerBoard, 50, false);
			return;
		}
		if (access.IsInCompartment() || access.IsGettingOut())
		{
			MHJ_RetryOwnerCanopySetup();
			return;
		}

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(jumper.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (!handler || !handler.MHJ_IsNativeCanopyBoardReady())
		{
			MHJ_RetryOwnerCanopyGetIn();
			return;
		}

		if (!access.GetInVehicle(canopy, slot, true, 0, ECloseDoorAfterActions.INVALID, true))
		{
			MHJ_Log.Warning("Canopy GetIn refused; retrying");
			MHJ_RetryOwnerCanopyGetIn();
			return;
		}

		MHJ_Log.Info("Canopy GetIn accepted");
		GetGame().GetCallqueue().CallLater(MHJ_MonitorOwnerBoard, 50, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_RetryOwnerCanopyGetIn()
	{
		m_iMHJ_SetupTries = m_iMHJ_SetupTries + 1;
		if (m_iMHJ_SetupTries >= 120)
		{
			MHJ_FailOwnerCanopySetup();
			return;
		}

		GetGame().GetCallqueue().CallLater(MHJ_TryOwnerGetIn, 50, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_OnOwnerBoarded(notnull MHJ_CanopyFlight flight)
	{
		flight.MHJ_OwnerConfirmBoarded();

		IEntity controlled = GetControlledEntity();
		if (controlled)
		{
			SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(controlled.FindComponent(SCR_CharacterCommandHandlerComponent));
			if (handler)
				handler.MHJ_OnFreefallBoarded();
		}

		MHJ_Log.Info("Canopy native GetIn completed");
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_RetryOwnerCanopySetup()
	{
		m_iMHJ_SetupTries = m_iMHJ_SetupTries + 1;
		if (m_iMHJ_SetupTries >= 120)
		{
			MHJ_FailOwnerCanopySetup();
			return;
		}

		GetGame().GetCallqueue().CallLater(MHJ_TryOwnerCanopySetup, 50, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_MonitorOwnerBoard()
	{
		ChimeraCharacter jumper = ChimeraCharacter.Cast(GetControlledEntity());
		if (!jumper)
		{
			MHJ_FailOwnerCanopySetup();
			return;
		}

		if (MHJ_CanopyFlight.OccupantIsInCanopy(jumper))
		{
			IEntity canopy = MHJ_ResolveCanopy(m_MHJ_CanopyId);
			if (canopy)
			{
				MHJ_CanopyFlight flight = MHJ_CanopyFlight.Cast(canopy.FindComponent(MHJ_CanopyFlight));
				if (flight)
					MHJ_OnOwnerBoarded(flight);
			}
			return;
		}

		CharacterControllerComponent controller = jumper.GetCharacterController();
		if (controller && controller.IsDead())
		{
			MHJ_FailOwnerCanopySetup();
			return;
		}

		m_iMHJ_SetupTries = m_iMHJ_SetupTries + 1;
		if (m_iMHJ_SetupTries >= 120)
		{
			MHJ_FailOwnerCanopySetup();
			return;
		}

		GetGame().GetCallqueue().CallLater(MHJ_MonitorOwnerBoard, 50, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_FailOwnerCanopySetup()
	{
		IEntity canopy = MHJ_ResolveCanopy(m_MHJ_CanopyId);
		if (canopy)
		{
			MHJ_CanopyFlight flight = MHJ_CanopyFlight.Cast(canopy.FindComponent(MHJ_CanopyFlight));
			if (flight)
				flight.MHJ_OwnerAbortSession();
		}

		if (Replication.IsServer())
			MHJ_ServerAbortTrackedCanopy();
		else
			Rpc(RpcAsk_MHJ_AbortCanopySession);

		IEntity controlled = GetControlledEntity();
		SCR_CharacterCommandHandlerComponent handler;
		if (controlled)
			handler = SCR_CharacterCommandHandlerComponent.Cast(controlled.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (handler)
			handler.MHJ_CanopyBoardFailed();

		MHJ_ClearOwnerSetupCalls();
		m_MHJ_CanopyId = RplId.Invalid();
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_MHJ_AbortCanopySession()
	{
		MHJ_ServerAbortTrackedCanopy();
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_ServerAbortTrackedCanopy()
	{
		if (!Replication.IsServer())
			return;
		if (!m_MHJ_ServerCanopy)
			return;

		MHJ_CanopyFlight flight = MHJ_CanopyFlight.Cast(m_MHJ_ServerCanopy.FindComponent(MHJ_CanopyFlight));
		if (flight)
			flight.MHJ_ServerAbortSession();
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_HandleCanopySessionAborted()
	{
		IEntity controlled = GetControlledEntity();
		ChimeraCharacter jumper = ChimeraCharacter.Cast(controlled);
		CompartmentAccessComponent access;
		if (jumper)
			access = jumper.GetCompartmentAccessComponent();

		if (access && (access.IsInCompartment() || access.IsGettingIn() || access.IsGettingOut()))
		{
			MHJ_BeginOwnerCanopyExit(0, 0, 0);
			return;
		}

		SCR_CharacterCommandHandlerComponent handler;
		if (controlled)
			handler = SCR_CharacterCommandHandlerComponent.Cast(controlled.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (handler)
			handler.MHJ_CanopyBoardFailed();

		MHJ_ClearOwnerSetupCalls();
		m_MHJ_CanopyId = RplId.Invalid();
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_BeginOwnerCanopyExit(float downSpeed, float horizontalSpeed, float heading)
	{
		MHJ_ClearOwnerSetupCalls();
		m_fMHJ_ExitDown = downSpeed;
		m_fMHJ_ExitHorizontal = horizontalSpeed;
		m_fMHJ_ExitHeading = heading;
		m_iMHJ_ExitTries = 0;
		MHJ_WaitOwnerCanopyExit();
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_WaitOwnerCanopyExit()
	{
		IEntity controlled = GetControlledEntity();
		ChimeraCharacter jumper = ChimeraCharacter.Cast(controlled);
		if (!jumper)
			return;

		CompartmentAccessComponent access = jumper.GetCompartmentAccessComponent();
		bool busy = false;
		if (access)
		{
			if (access.IsInCompartment() || access.IsGettingIn() || access.IsGettingOut())
				busy = true;
		}

		if (busy)
		{
			m_iMHJ_ExitTries = m_iMHJ_ExitTries + 1;
			if (m_iMHJ_ExitTries < 120)
				GetGame().GetCallqueue().CallLater(MHJ_WaitOwnerCanopyExit, 50, false);
			return;
		}

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(jumper.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (handler)
			handler.MHJ_OnCanopyExited(m_fMHJ_ExitDown, m_fMHJ_ExitHorizontal, m_fMHJ_ExitHeading);

		m_MHJ_CanopyId = RplId.Invalid();
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_ServerCanopyDeleted(IEntity canopy)
	{
		if (!Replication.IsServer())
			return;
		if (m_MHJ_ServerCanopy != canopy)
			return;

		m_MHJ_ServerCanopy = null;
		m_MHJ_CanopyId = RplId.Invalid();
	}

	//------------------------------------------------------------------------------------------------
	protected IEntity MHJ_ResolveCanopy(RplId canopyId)
	{
		if (!canopyId.IsValid())
			return null;

		Managed instance = Replication.FindItem(canopyId);
		if (!instance)
			return null;

		RplComponent replication = RplComponent.Cast(instance);
		if (replication)
			return replication.GetEntity();
		return IEntity.Cast(instance);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_CancelCanopyCoordination(bool notifyAuthority)
	{
		MHJ_ClearOwnerSetupCalls();
		GetGame().GetCallqueue().Remove(MHJ_WaitOwnerCanopyExit);

		if (notifyAuthority)
		{
			IEntity canopy = MHJ_ResolveCanopy(m_MHJ_CanopyId);
			if (canopy)
			{
				MHJ_CanopyFlight flight = MHJ_CanopyFlight.Cast(canopy.FindComponent(MHJ_CanopyFlight));
				if (flight)
				{
					if (Replication.IsServer())
						flight.MHJ_ServerAbortSession();
					else
						flight.MHJ_OwnerAbortSession();
				}
			}
		}

		m_MHJ_CanopyId = RplId.Invalid();
		m_MHJ_ServerCanopy = null;
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_ClearOwnerSetupCalls()
	{
		GetGame().GetCallqueue().Remove(MHJ_TryOwnerCanopySetup);
		GetGame().GetCallqueue().Remove(MHJ_TryOwnerGetIn);
		GetGame().GetCallqueue().Remove(MHJ_MonitorOwnerBoard);
	}

	//------------------------------------------------------------------------------------------------
	protected bool MHJ_ValidateJump(vector dropXZ, float jumpAltitude, float openAltitude)
	{
		if (jumpAltitude < MHJ_Constants.JUMP_ALT_MIN || jumpAltitude > MHJ_Constants.JUMP_ALT_MAX)
			return false;
		if (openAltitude < MHJ_Constants.OPEN_ALT_MIN || openAltitude > MHJ_Constants.OPEN_ALT_MAX)
			return false;
		if (jumpAltitude < openAltitude + MHJ_Constants.OPEN_MARGIN)
			return false;

		vector checkPosition;
		checkPosition[0] = dropXZ[0];
		checkPosition[2] = dropXZ[2];
		BaseWorld world = GetGame().GetWorld();
		if (world)
			checkPosition[1] = world.GetSurfaceY(dropXZ[0], dropXZ[2]) + jumpAltitude;
		else
			checkPosition[1] = jumpAltitude;

		return SCR_Global.IsPositionWithinTerrainBounds(checkPosition);
	}
}
