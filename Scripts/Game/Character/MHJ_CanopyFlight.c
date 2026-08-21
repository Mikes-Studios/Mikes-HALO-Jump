//------------------------------------------------------------------------------------------------
//! Owner-owned jump craft. Freefall and canopy share this cargo entity.
//! Occupancy gates steer. Authority ticks in EOnSimulate. Gravity stays off
//! on the craft; drive with SetVelocity plus ForceNodeMovement.
//! Dedicated owner replicas predict the same aero, then rewind to each
//! authority snapshot and replay unacked input. Proxies extrapolate.
//------------------------------------------------------------------------------------------------
[ComponentEditorProps(category: "MHJ", description: "Server-authoritative owner-owned HALO jump craft.")]
class MHJ_CanopyFlightClass : ScriptComponentClass
{
}

class MHJ_CanopyFlight : ScriptComponent
{
	protected RplComponent m_Rpl;
	protected IEntity m_Owner;
	protected ChimeraCharacter m_pJumper;
	protected BaseCompartmentSlot m_Slot;
	protected BaseCompartmentSlot m_FreefallSlot;
	protected BaseCompartmentSlot m_SitSlot;
	protected CharacterInputContext m_Input;
	protected int m_iExpectedPlayerId;
	protected MHJ_EHaloPhase m_ePhase;

	protected float m_fHeading;
	protected float m_fAirspeed;
	protected float m_fVelY;
	protected float m_fTurnInput;
	protected float m_fTurnFilt;
	protected float m_fTurnFiltV;
	protected float m_fPitchInput;
	protected float m_fPitchInputFilt;
	protected float m_fPitchInputFiltV;
	protected float m_fPathDeg;
	protected float m_fPathDegV;
	protected float m_fBank;
	protected float m_fBankV;
	protected float m_fPitch;
	protected float m_fPitchV;
	protected float m_fOpenT;
	protected float m_fSimT;
	protected float m_fOpenAltitude;
	protected float m_fNetTurn;
	protected float m_fNetPitch;
	protected float m_fListenTurn;
	protected float m_fListenPitch;
	protected float m_fInputAge;
	protected float m_fStateSendTime;
	protected float m_fInputSendTime;
	protected float m_fTouchdownCooldown;
	protected float m_fBoardWait;
	protected float m_fLandDown;
	protected float m_fLandHorizontal;
	protected float m_fLandHeading;
	protected bool m_bServerSession;
	protected bool m_bOwnerSession;
	protected bool m_bBoarded;
	protected bool m_bLanding;
	protected bool m_bExitAsked;
	protected bool m_bApplyLandingResult;
	protected bool m_bSnatchFired;
	protected bool m_bInputListening;
	protected bool m_bCanopyVisual;
	protected bool m_bCanopyHintShown;
	protected int m_iExitWaitTries;
	protected string m_sFlightMode;
	protected vector m_vWorldVel;
	protected vector m_vWind;
	protected float m_fDiagTime;
	protected bool m_bReplicaLive;
	protected vector m_vSnapOrigin;
	protected vector m_vSnapVel;
	protected vector m_vSnapYpr;
	protected vector m_vPredOrigin;
	protected float m_fSnapAge;
	protected float m_fReplicaDiag;
	protected float m_fPktDt;
	protected float m_fSincePacket;
	protected float m_fOriginStick;
	protected int m_iInputSeq;
	protected int m_iAckSeq;
	protected int m_iReplayFrames;
	protected bool m_bPendingSnap;
	protected vector m_vVisualError;
	protected ref MHJ_FlightState m_PendingSnap;
	protected ref array<ref MHJ_InputSample> m_aInputRing;

	//------------------------------------------------------------------------------------------------
	static bool OccupantIsInCanopy(IEntity character)
	{
		ChimeraCharacter jumper = ChimeraCharacter.Cast(character);
		if (!jumper)
			return false;

		CompartmentAccessComponent access = jumper.GetCompartmentAccessComponent();
		if (!access || !access.IsInCompartment())
			return false;

		BaseCompartmentSlot slot = access.GetCompartment();
		if (!slot)
			return false;

		IEntity vehicle = slot.GetOwner();
		if (!vehicle)
			return false;
		return MHJ_CanopyFlight.Cast(vehicle.FindComponent(MHJ_CanopyFlight)) != null;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (SCR_Global.IsEditMode())
			return;

		m_Rpl = RplComponent.Cast(owner.FindComponent(RplComponent));
		m_Owner = owner;
		m_ePhase = MHJ_EHaloPhase.FREEFALL;
		CacheSlots();
		ApplyCraftVisual(false);
		Physics physics = owner.GetPhysics();
		if (physics)
		{
			physics.EnableGravity(false);
			physics.SetAngularVelocity(vector.Zero);
		}
		SetEventMask(owner, EntityEvent.FRAME | EntityEvent.SIMULATE | EntityEvent.POSTSIMULATE | EntityEvent.CONTACT);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(MHJ_DeleteAfterExit);
		DisableOwnerControls();
		if (m_bOwnerSession)
		{
			MHJ_JumpHud.Close();
			SCR_PlayerController playerController = SCR_PlayerController.Cast(GetGame().GetPlayerController());
			if (playerController)
				playerController.MHJ_HandleCanopySessionAborted();
		}
		if (m_bServerSession && m_pJumper)
		{
			SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(m_pJumper.FindComponent(SCR_CharacterCommandHandlerComponent));
			if (handler)
				handler.MHJ_CanopyBoardFailed();
		}
		m_bOwnerSession = false;
		m_bServerSession = false;
		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	//! Called on the server after GiveExt. Ownership never changes RplRole.Authority.
	bool BeginFlight(notnull ChimeraCharacter jumper, vector worldVelocity, vector wind, float heading, float pitch, float bank, float simTime, float openAltitude)
	{
		if (!IsAuthority())
			return false;

		m_pJumper = jumper;
		m_iExpectedPlayerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(jumper);
		if (m_iExpectedPlayerId <= 0)
			return false;

		m_vWorldVel = worldVelocity;
		if (m_vWorldVel.Length() < 0.5)
			m_vWorldVel[1] = -MHJ_Constants.FREEFALL_START_SINK;
		float inheritSpeed = m_vWorldVel.Length();
		if (inheritSpeed > MHJ_Constants.FREEFALL_TERMINAL)
			m_vWorldVel = m_vWorldVel * (MHJ_Constants.FREEFALL_TERMINAL / inheritSpeed);
		m_vWind = wind;
		m_fHeading = heading;
		m_fPitch = pitch;
		m_fBank = bank;
		m_fSimT = simTime;
		m_fOpenAltitude = openAltitude;
		m_ePhase = MHJ_EHaloPhase.FREEFALL;
		m_bCanopyVisual = false;
		m_bCanopyHintShown = false;
		m_fOpenT = 0;
		ApplyCraftVisual(false);
		m_fPathDeg = MHJ_Constants.CANOPY_PATH_CRUISE;
		m_fPathDegV = 0;
		m_fTurnInput = 0;
		m_fPitchInput = 0;
		m_fNetTurn = 0;
		m_fNetPitch = 0;
		m_fListenTurn = 0;
		m_fListenPitch = 0;
		m_fInputAge = 0;
		m_fBoardWait = 0;
		m_fTurnFilt = 0;
		m_fTurnFiltV = 0;
		m_fPitchInputFilt = 0;
		m_fPitchInputFiltV = 0;
		m_fBankV = 0;
		m_fPitchV = 0;
		m_bBoarded = false;
		m_bLanding = false;
		m_bExitAsked = false;
		m_bApplyLandingResult = false;
		m_iExitWaitTries = 0;
		m_bSnatchFired = false;
		m_bInputListening = false;
		m_sFlightMode = "EXIT";
		m_bServerSession = true;
		m_iAckSeq = 0;

		WakePhysics();
		SyncSpeedFromWorld();
		MHJ_Log.Info("Jump craft authority initialized");
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Owner-local replica setup. Prediction starts after the first pose RPC.
	void MHJ_BeginOwnerSession(notnull ChimeraCharacter jumper, float openAltitude)
	{
		m_pJumper = jumper;
		m_fOpenAltitude = openAltitude;
		CharacterControllerComponent controller = jumper.GetCharacterController();
		if (controller)
			m_Input = controller.GetInputContext();

		m_bOwnerSession = true;
		m_bLanding = false;
		m_fTouchdownCooldown = 0;
		m_iInputSeq = 0;
		m_iAckSeq = 0;
		m_iReplayFrames = 0;
		m_bPendingSnap = false;
		m_vVisualError = vector.Zero;
		if (!m_PendingSnap)
			m_PendingSnap = new MHJ_FlightState();
		if (!m_aInputRing)
			m_aInputRing = new array<ref MHJ_InputSample>();
		m_aInputRing.Clear();
		IEntity craft = m_Owner;
		if (!craft)
			craft = GetOwner();
		if (craft)
			m_vPredOrigin = craft.GetOrigin();
		MHJ_Log.Info("Replica owner session y=" + m_vPredOrigin[1].ToString());
		if (!MHJ_JumpHud.IsOpen())
			MHJ_JumpHud.Open();
		ShowPhaseHint();
	}

	//------------------------------------------------------------------------------------------------
	MHJ_EHaloPhase GetPhase()
	{
		return m_ePhase;
	}

	//------------------------------------------------------------------------------------------------
	BaseCompartmentSlot GetFreefallSlot()
	{
		CacheSlots();
		return m_FreefallSlot;
	}

	//------------------------------------------------------------------------------------------------
	BaseCompartmentSlot GetCanopySlot()
	{
		CacheSlots();
		return m_SitSlot;
	}

	//------------------------------------------------------------------------------------------------
	BaseCompartmentSlot GetBoardSlot()
	{
		CacheSlots();
		if (m_ePhase == MHJ_EHaloPhase.CANOPY)
			return m_SitSlot;
		return m_FreefallSlot;
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_OwnerConfirmBoarded()
	{
		if (!IsOwnedHere())
			return;
		if (IsAuthority())
			RpcAsk_MHJ_OwnerBoarded();
		else
			Rpc(RpcAsk_MHJ_OwnerBoarded);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_MHJ_OwnerBoarded()
	{
		if (!IsAuthority())
			return;

		m_bBoarded = true;
		WakePhysics();
		if (m_pJumper)
		{
			SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(m_pJumper.FindComponent(SCR_CharacterCommandHandlerComponent));
			if (handler)
				handler.MHJ_OnFreefallBoarded();
		}
		MHJ_Log.Info("Canopy owner reported boarded");
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_OwnerAbortSession()
	{
		if (!IsOwnedHere())
			return;
		if (IsAuthority())
			MHJ_ServerAbortSession();
		else
			Rpc(RpcAsk_MHJ_AbortSession);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_MHJ_AbortSession()
	{
		MHJ_ServerAbortSession();
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_ServerAbortSession()
	{
		if (!IsAuthority())
			return;
		if (m_bLanding)
			return;

		m_bLanding = true;
		m_bApplyLandingResult = false;
		m_bServerSession = false;
		m_iExitWaitTries = 0;
		MHJ_Log.Warning("Canopy session aborted boarded=" + MHJ_Log.Flag(m_bBoarded) + " agl=" + GetAgl().ToString());
		FreezeCraftForExit();
		NotifyJumperExitStarted();
		if (IsOwnedHere())
			RpcDo_MHJ_SessionAborted();
		else
			Rpc(RpcDo_MHJ_SessionAborted);
		AskOwnerToExitOnce();
		GetGame().GetCallqueue().CallLater(MHJ_DeleteAfterExit, MHJ_Constants.CANOPY_DELETE_DELAY_MS, false);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_MHJ_SessionAborted()
	{
		m_bOwnerSession = false;
		DisableOwnerControls();
		MHJ_JumpHud.Close();
		SCR_PlayerController playerController = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (playerController)
			playerController.MHJ_HandleCanopySessionAborted();
	}

	//------------------------------------------------------------------------------------------------
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		if (SCR_Global.IsEditMode())
			return;

		if (IsOwnedHere() && m_bOwnerSession && !m_bLanding)
		{
			if (IsLocalOccupant())
				TickOwner(timeSlice);
			else
				DisableOwnerControls();
		}
	}

	//------------------------------------------------------------------------------------------------
	override void EOnSimulate(IEntity owner, float timeSlice)
	{
		if (SCR_Global.IsEditMode())
			return;

		LockCanopySpin(owner);
		if (m_bLanding)
			return;

		if (IsAuthority())
		{
			if (m_bServerSession)
				TickAuthorityFlight(owner, timeSlice);
			return;
		}

		TickReplicaMotion(owner, timeSlice);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnPostSimulate(IEntity owner, float timeSlice)
	{
		if (SCR_Global.IsEditMode())
			return;

		LockCanopySpin(owner);
		if (m_bLanding)
			return;

		if (IsAuthority())
		{
			if (m_bServerSession)
				ApplyAuthorityOrientation();
			return;
		}

		if (!m_bReplicaLive)
			return;

		if (!IsOwnedHere() || !m_bOwnerSession)
		{
			ApplyReplicaProxyPose(owner);
			return;
		}

		owner.SetOrigin(m_vPredOrigin + m_vVisualError);
		m_fOriginStick = (owner.GetOrigin() - m_vPredOrigin).Length();
		ApplyReplicaVelocity();
		ApplyAuthorityOrientation();
	}

	//------------------------------------------------------------------------------------------------
	override void EOnContact(IEntity owner, IEntity other, Contact contact)
	{
		if (SCR_Global.IsEditMode())
			return;
		if (m_bLanding)
			return;
		if (other)
		{
			if (other == m_pJumper)
				return;
			if (other.GetParent() == owner)
				return;
		}

		if (IsAuthority() && m_bServerSession && m_bBoarded)
		{
			FinishLanding();
			return;
		}

		if (IsOwnedHere() && m_bOwnerSession)
			SendOwnerTouchdown();
	}

	//------------------------------------------------------------------------------------------------
	protected void TickAuthorityFlight(notnull IEntity owner, float timeSlice)
	{
		if (!ValidateSessionCharacter())
		{
			MHJ_ServerAbortSession();
			return;
		}

		if (!m_bBoarded)
		{
			CacheSlots();
			if (OccupantIsJumper())
			{
				m_bBoarded = true;
				WakePhysics();
			}
			else
			{
				m_fBoardWait = m_fBoardWait + timeSlice;
				if (m_fBoardWait >= MHJ_Constants.CANOPY_BOARD_WAIT)
				{
					MHJ_Log.Warning("Craft board wait expired occupant=" + MHJ_Log.Flag(OccupantIsJumper()) + " agl=" + GetAgl().ToString());
					MHJ_ServerAbortSession();
					return;
				}
			}
		}

		CharacterControllerComponent controller = m_pJumper.GetCharacterController();
		if (controller && controller.IsDead())
		{
			FinishLanding();
			return;
		}

		if (m_bBoarded)
		{
			m_fInputAge = m_fInputAge + timeSlice;
			if (m_fInputAge > MHJ_Constants.INPUT_STALE_SEC)
			{
				m_fNetTurn = 0;
				m_fNetPitch = 0;
			}
			m_fTurnInput = m_fNetTurn;
			m_fPitchInput = m_fNetPitch;
		}
		else
		{
			m_fTurnInput = 0;
			m_fPitchInput = 0;
		}

		m_fSimT = m_fSimT + timeSlice;
		m_vWind = MHJ_FlightAero.WindWorld(owner.GetOrigin()[1], m_fSimT);

		if (IsBelowTerrain())
		{
			FinishLanding();
			return;
		}

		float agl = GetAgl();
		if (m_ePhase == MHJ_EHaloPhase.FREEFALL && m_bBoarded)
		{
			if (agl <= m_fOpenAltitude)
				OpenCanopyInPlace();
		}

		if (agl <= MHJ_Constants.LAND_AGL && m_ePhase == MHJ_EHaloPhase.CANOPY)
		{
			FinishLanding();
			return;
		}

		if (m_ePhase == MHJ_EHaloPhase.FREEFALL)
		{
			ApplyFreefall(timeSlice);
			IntegrateFreefall(timeSlice);
		}
		else
		{
			ApplyCanopy(timeSlice, agl);
			IntegrateAero(timeSlice);
		}
		ApplyAuthorityMotion();
		SendFlightState(timeSlice);
		LogCanopyTick(timeSlice, agl);
	}

	//------------------------------------------------------------------------------------------------
	protected void TickOwner(float timeSlice)
	{
		EnableOwnerControls();
		if (IsAuthority())
		{
			ReadInput();
			m_fInputSendTime = m_fInputSendTime + timeSlice;
			if (m_fInputSendTime >= MHJ_Constants.FLIGHT_STATE_DT)
			{
				m_fInputSendTime = 0;
				ApplyOwnedInput(m_fTurnInput, m_fPitchInput);
			}
		}

		if (m_fTouchdownCooldown > 0)
			m_fTouchdownCooldown = m_fTouchdownCooldown - timeSlice;

		float agl = GetAgl();
		if (agl <= MHJ_Constants.LAND_AGL || IsBelowTerrain())
			SendOwnerTouchdown();

		PushHud(agl);
	}

	//------------------------------------------------------------------------------------------------
	//! Owner view can hit terrain a frame before authority AGL. Ask the server
	//! to land; it rejects if the 3D gap is still beyond the sanity cap.
	protected void SendOwnerTouchdown()
	{
		if (m_bLanding)
			return;
		if (!IsOwnedHere())
			return;
		if (m_fTouchdownCooldown > 0)
			return;

		m_fTouchdownCooldown = 0.2;
		vector origin = m_vPredOrigin;
		if (origin.LengthSq() < 0.01)
		{
			IEntity craft = GetOwner();
			if (craft)
				origin = craft.GetOrigin();
		}

		if (IsAuthority())
			RpcAsk_MHJ_OwnerTouchdown(origin);
		else
			Rpc(RpcAsk_MHJ_OwnerTouchdown, origin);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_MHJ_OwnerTouchdown(vector ownerOrigin)
	{
		if (!IsAuthority() || m_bLanding)
			return;
		if (!m_bBoarded)
			return;
		if (!ValidateExpectedOccupant())
			return;

		IEntity craft = GetOwner();
		if (craft)
		{
			float gap = (ownerOrigin - craft.GetOrigin()).Length();
			if (gap > MHJ_Constants.CANOPY_OWNER_TOUCHDOWN_AGL)
			{
				MHJ_Log.Warning("Owner touchdown ignored gap=" + gap.ToString());
				return;
			}
		}

		PlaceCraftAtOwnerGround(ownerOrigin);
		MHJ_Log.Land("Owner touchdown agl=" + GetAgl().ToString());
		FinishLanding();
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Unreliable, RplRcver.Server)]
	protected void RpcAsk_MHJ_Steer(float turn, float pitch, int seq)
	{
		if (!IsAuthority())
			return;
		if (!ValidateExpectedOccupant())
			return;
		if (seq < m_iAckSeq)
			return;

		ApplyOwnedInput(turn, pitch);
		m_iAckSeq = seq;
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyOwnedInput(float turn, float pitch)
	{
		m_fNetTurn = turn;
		m_fNetPitch = pitch;
		if (m_fNetTurn > 1)
			m_fNetTurn = 1;
		if (m_fNetTurn < -1)
			m_fNetTurn = -1;
		if (m_fNetPitch > 1)
			m_fNetPitch = 1;
		if (m_fNetPitch < -1)
			m_fNetPitch = -1;
		m_fInputAge = 0;
	}

	//------------------------------------------------------------------------------------------------
	protected void ReadInput()
	{
		m_fTurnInput = m_fListenTurn;
		m_fPitchInput = m_fListenPitch;

		InputManager inputManager = GetGame().GetInputManager();
		if (inputManager)
		{
			float characterTurn = inputManager.GetActionValue("CharacterRight");
			float characterPitch = inputManager.GetActionValue("CharacterForward");
			if (m_fTurnInput > -0.12 && m_fTurnInput < 0.12)
				m_fTurnInput = characterTurn;
			if (m_fPitchInput > -0.12 && m_fPitchInput < 0.12)
				m_fPitchInput = characterPitch;

			float vehicleSteer = inputManager.GetActionValue("VehicleSteer");
			float vehicleThrottle = inputManager.GetActionValue("VehicleThrottle");
			if (m_fTurnInput > -0.12 && m_fTurnInput < 0.12)
				m_fTurnInput = vehicleSteer;
			if (m_fPitchInput > -0.12 && m_fPitchInput < 0.12)
				m_fPitchInput = vehicleThrottle;
		}

		if (m_Input)
		{
			float moveSpeed;
			vector localDirection;
			m_Input.GetMovement(moveSpeed, localDirection);
			if (m_fTurnInput > -0.12 && m_fTurnInput < 0.12)
				m_fTurnInput = localDirection[0];
			if (m_fPitchInput > -0.12 && m_fPitchInput < 0.12)
				m_fPitchInput = localDirection[2];
		}

		if (m_fTurnInput > 1)
			m_fTurnInput = 1;
		if (m_fTurnInput < -1)
			m_fTurnInput = -1;
		if (m_fPitchInput > 1)
			m_fPitchInput = 1;
		if (m_fPitchInput < -1)
			m_fPitchInput = -1;
		if (m_fTurnInput > -0.12 && m_fTurnInput < 0.12)
			m_fTurnInput = 0;
		if (m_fPitchInput > -0.12 && m_fPitchInput < 0.12)
			m_fPitchInput = 0;
	}

	//------------------------------------------------------------------------------------------------
	protected void EnableOwnerControls()
	{
		InputManager inputManager = GetGame().GetInputManager();
		if (!inputManager)
			return;

		if (!inputManager.IsContextActive("CharacterMovementContext"))
			inputManager.ActivateContext("CharacterMovementContext");
		if (m_bInputListening)
			return;

		inputManager.AddActionListener("CharacterForward", EActionTrigger.VALUE, OnOwnerForward);
		inputManager.AddActionListener("CharacterRight", EActionTrigger.VALUE, OnOwnerRight);
		inputManager.AddActionListener("VehicleThrottle", EActionTrigger.VALUE, OnOwnerForward);
		inputManager.AddActionListener("VehicleSteer", EActionTrigger.VALUE, OnOwnerRight);
		m_bInputListening = true;
		MHJ_Log.Info("Canopy owner controls enabled");
	}

	//------------------------------------------------------------------------------------------------
	protected void DisableOwnerControls()
	{
		if (!m_bInputListening)
			return;

		InputManager inputManager = GetGame().GetInputManager();
		if (inputManager)
		{
			inputManager.RemoveActionListener("CharacterForward", EActionTrigger.VALUE, OnOwnerForward);
			inputManager.RemoveActionListener("CharacterRight", EActionTrigger.VALUE, OnOwnerRight);
			inputManager.RemoveActionListener("VehicleThrottle", EActionTrigger.VALUE, OnOwnerForward);
			inputManager.RemoveActionListener("VehicleSteer", EActionTrigger.VALUE, OnOwnerRight);
		}

		m_bInputListening = false;
		m_fListenTurn = 0;
		m_fListenPitch = 0;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnOwnerForward(float value, EActionTrigger reason)
	{
		m_fListenPitch = value;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnOwnerRight(float value, EActionTrigger reason)
	{
		m_fListenTurn = value;
	}

	//------------------------------------------------------------------------------------------------
	protected void FinishLanding()
	{
		if (!IsAuthority() || m_bLanding)
			return;

		m_bLanding = true;
		m_bServerSession = false;
		m_bApplyLandingResult = true;
		m_iExitWaitTries = 0;

		m_fLandDown = -m_fVelY;
		if (m_fLandDown < 0)
			m_fLandDown = 0;
		vector horizontal = m_vWorldVel;
		horizontal[1] = 0;
		m_fLandHorizontal = horizontal.Length();
		m_fLandHeading = m_fHeading;

		MHJ_Log.Land("agl=" + GetAgl().ToString() + " down=" + m_fLandDown.ToString() + " hs=" + m_fLandHorizontal.ToString() + " boarded=" + MHJ_Log.Flag(m_bBoarded));
		StickCraftToTerrain();
		FreezeCraftForExit();
		NotifyJumperExitStarted();
		if (IsOwnedHere())
			RpcDo_MHJ_Landing(m_fLandDown, m_fLandHorizontal, m_fLandHeading);
		else
			Rpc(RpcDo_MHJ_Landing, m_fLandDown, m_fLandHorizontal, m_fLandHeading);
		AskOwnerToExitOnce();
		GetGame().GetCallqueue().CallLater(MHJ_DeleteAfterExit, MHJ_Constants.CANOPY_DELETE_DELAY_MS, false);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_MHJ_Landing(float downSpeed, float horizontalSpeed, float heading)
	{
		m_bLanding = true;
		m_bOwnerSession = false;
		DisableOwnerControls();
		MHJ_JumpHud.Close();

		SCR_PlayerController playerController = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (playerController)
			playerController.MHJ_BeginOwnerCanopyExit(downSpeed, horizontalSpeed, heading);
	}

	//------------------------------------------------------------------------------------------------
	protected void AskOwnerToExitOnce()
	{
		if (m_bExitAsked)
			return;
		if (!m_pJumper)
			return;
		SCR_CompartmentAccessComponent access = SCR_CompartmentAccessComponent.Cast(m_pJumper.GetCompartmentAccessComponent());
		if (!access || !access.IsInCompartment())
			return;

		m_bExitAsked = true;
		access.AskOwnerToGetOutFromVehicle(EGetOutType.TELEPORT, 0, ECloseDoorAfterActions.LEAVE_OPEN, true, true);
		MHJ_Log.Land("AskOwner GetOut TELEPORT on-spot");
	}

	//------------------------------------------------------------------------------------------------
	//! Working MK4 never sleeps the RigidBody until the occupant is out. Inactive
	//! cargo physics can leave PhysicsIsLinked / PerceivableComponent.IsInCompartment.
	protected void FreezeCraftForExit()
	{
		IEntity owner = m_Owner;
		if (!owner)
			owner = GetOwner();
		if (!owner)
			return;

		Physics physics = owner.GetPhysics();
		if (!physics)
			return;

		physics.EnableGravity(false);
		physics.SetVelocity(vector.Zero);
		physics.SetAngularVelocity(vector.Zero);
		physics.SetActive(ActiveState.ACTIVE);
	}

	//------------------------------------------------------------------------------------------------
	protected void NotifyJumperExitStarted()
	{
		if (!m_pJumper)
			return;

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(m_pJumper.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (handler)
			handler.MHJ_BeginCanopyExit();
	}

	//------------------------------------------------------------------------------------------------
	protected bool JumperStillExiting()
	{
		if (!m_pJumper)
			return false;
		if (OccupantIsJumper())
			return true;

		CompartmentAccessComponent access = m_pJumper.GetCompartmentAccessComponent();
		if (access)
		{
			if (access.IsInCompartment() || access.IsGettingIn() || access.IsGettingOut())
				return true;
		}

		CharacterAnimationComponent anim = CharacterAnimationComponent.Cast(m_pJumper.FindComponent(CharacterAnimationComponent));
		if (anim && anim.PhysicsIsLinked())
			return true;

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_DeleteAfterExit()
	{
		if (!IsAuthority())
			return;

		AskOwnerToExitOnce();

		if (JumperStillExiting())
		{
			m_iExitWaitTries = m_iExitWaitTries + 1;
			if (m_iExitWaitTries < MHJ_Constants.CANOPY_EXIT_MAX_TRIES)
			{
				GetGame().GetCallqueue().CallLater(MHJ_DeleteAfterExit, MHJ_Constants.CANOPY_EXIT_POLL_MS, false);
				return;
			}

			MHJ_Log.Warning("Deleting craft before GetOut finished");
		}

		SleepPhysics();

		if (m_pJumper)
		{
			SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(m_pJumper.FindComponent(SCR_CharacterCommandHandlerComponent));
			if (handler)
			{
				if (m_bApplyLandingResult || m_bBoarded)
					handler.MHJ_OnCanopyExited(m_fLandDown, m_fLandHorizontal, m_fLandHeading);
				else
					handler.MHJ_CanopyBoardFailed();
			}
		}

		int playerId = m_iExpectedPlayerId;
		PlayerController controller = GetGame().GetPlayerManager().GetPlayerController(playerId);
		SCR_PlayerController playerController = SCR_PlayerController.Cast(controller);
		if (playerController)
			playerController.MHJ_ServerCanopyDeleted(GetOwner());

		ClearOwnerStreaming();
		IEntity owner = GetOwner();
		if (owner)
			RplComponent.DeleteRplEntity(owner, false);
	}

	//------------------------------------------------------------------------------------------------
	protected bool ValidateSessionCharacter()
	{
		if (!m_pJumper)
			return false;
		IEntity controlled = GetGame().GetPlayerManager().GetPlayerControlledEntity(m_iExpectedPlayerId);
		return controlled == m_pJumper;
	}

	//------------------------------------------------------------------------------------------------
	protected bool ValidateExpectedOccupant()
	{
		return OccupantIsJumper();
	}

	//------------------------------------------------------------------------------------------------
	protected bool OccupantIsJumper()
	{
		if (!ValidateSessionCharacter())
			return false;

		CacheSlots();
		if (m_FreefallSlot && m_FreefallSlot.GetOccupant() == m_pJumper)
			return true;
		if (m_SitSlot && m_SitSlot.GetOccupant() == m_pJumper)
			return true;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsLocalOccupant()
	{
		if (!m_pJumper)
			return false;
		return SCR_PlayerController.GetLocalControlledEntity() == m_pJumper && OccupantIsInCanopy(m_pJumper);
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsAuthority()
	{
		if (!m_Rpl)
			return false;
		return m_Rpl.Role() == RplRole.Authority;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsOwnedHere()
	{
		if (!m_Rpl)
			return false;
		return m_Rpl.IsOwner();
	}

	//------------------------------------------------------------------------------------------------
	protected void CacheSlots()
	{
		if (m_FreefallSlot && m_SitSlot)
			return;

		IEntity owner = GetOwner();
		if (!owner)
			return;
		BaseCompartmentManagerComponent manager = BaseCompartmentManagerComponent.Cast(owner.FindComponent(BaseCompartmentManagerComponent));
		if (!manager)
			return;

		ref array<BaseCompartmentSlot> slots = new array<BaseCompartmentSlot>();
		manager.GetCompartments(slots);
		int count = slots.Count();
		int i;
		for (i = 0; i < count; i++)
		{
			BaseCompartmentSlot slot = slots[i];
			if (!slot)
				continue;
			if (slot.GetType() != ECompartmentType.CARGO)
				continue;

			string name = slot.GetCompartmentName();
			string uniqueName = slot.GetCompartmentUniqueName();
			if (name == MHJ_Constants.SLOT_FREEFALL || uniqueName == MHJ_Constants.SLOT_FREEFALL)
				m_FreefallSlot = slot;
			else if (name == MHJ_Constants.SLOT_CANOPY || uniqueName == MHJ_Constants.SLOT_CANOPY)
				m_SitSlot = slot;
		}

		if (!m_SitSlot)
		{
			for (i = 0; i < count; i++)
			{
				BaseCompartmentSlot slot = slots[i];
				if (slot && slot.GetType() == ECompartmentType.CARGO && slot != m_FreefallSlot)
				{
					m_SitSlot = slot;
					break;
				}
			}
		}

		if (!m_FreefallSlot)
			m_FreefallSlot = m_SitSlot;
		if (m_ePhase == MHJ_EHaloPhase.CANOPY)
			m_Slot = m_SitSlot;
		else
			m_Slot = m_FreefallSlot;
	}

	//------------------------------------------------------------------------------------------------
	//! The prefab starts with transparent materials but keeps model geometry so
	//! RigidBody exists. At open, remap the same VObject to its visible materials.
	protected void ApplyCraftVisual(bool showCanopy)
	{
		IEntity owner = m_Owner;
		if (!owner)
			owner = GetOwner();
		if (!owner)
			return;

		if (showCanopy && !m_bCanopyVisual)
		{
			VObject mesh = owner.GetVObject();
			if (mesh)
				owner.SetObject(mesh, MHJ_Constants.CANOPY_MESH_REMAP);
		}

		m_bCanopyVisual = showCanopy;
	}

	//------------------------------------------------------------------------------------------------
	protected void ShowPhaseHint()
	{
		if (m_ePhase == MHJ_EHaloPhase.CANOPY)
		{
			if (m_bCanopyHintShown)
				return;
			m_bCanopyHintShown = true;
			SCR_HintManagerComponent.ShowCustomHint("W dives. S flares — dive first to swoop. Land into the wind.", "CANOPY", 7);
			return;
		}

		SCR_HintManagerComponent.ShowCustomHint("W tracks. S slows the fall. A/D turns. Canopy opens automatically.", "HALO JUMP", 6);
	}

	//------------------------------------------------------------------------------------------------
	protected void OpenCanopyInPlace()
	{
		if (m_ePhase == MHJ_EHaloPhase.CANOPY)
			return;

		m_ePhase = MHJ_EHaloPhase.CANOPY;
		m_fOpenT = 0;
		m_bSnatchFired = false;
		m_sFlightMode = "OPENING";
		m_Slot = m_SitSlot;
		ApplyCraftVisual(true);
		if (IsOwnedHere())
		{
			RpcDo_MHJ_CanopyOpened();
			RpcDo_MHJ_SwitchToSit();
		}
		else
		{
			Rpc(RpcDo_MHJ_CanopyOpened);
			Rpc(RpcDo_MHJ_SwitchToSit);
		}
		MHJ_Log.Info("Canopy opened in place agl=" + GetAgl().ToString());
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_MHJ_CanopyOpened()
	{
		m_ePhase = MHJ_EHaloPhase.CANOPY;
		m_fOpenT = 0;
		ApplyCraftVisual(true);
		if (m_bOwnerSession)
			ShowPhaseHint();
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_MHJ_SwitchToSit()
	{
		if (!IsOwnedHere())
			return;

		SCR_PlayerController playerController = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (playerController)
			playerController.MHJ_OwnerSwitchToSitSlot();
	}

	//------------------------------------------------------------------------------------------------
	protected void WakePhysics()
	{
		IEntity owner = m_Owner;
		if (!owner)
			owner = GetOwner();
		if (!owner)
			return;

		Physics physics = owner.GetPhysics();
		if (!physics)
			return;

		physics.EnableGravity(false);
		physics.SetActive(ActiveState.ACTIVE);
		physics.SetAngularVelocity(vector.Zero);
		physics.SetVelocity(m_vWorldVel);
	}

	//------------------------------------------------------------------------------------------------
	//! Gravity plus the canopy COM offset will tumble the cargo. Script heading
	//! owns orientation, so kill spin on every physics step.
	protected void LockCanopySpin(IEntity owner)
	{
		if (!owner)
			owner = GetOwner();
		if (!owner)
			return;

		Physics physics = owner.GetPhysics();
		if (!physics)
			return;

		physics.EnableGravity(false);
		physics.SetAngularVelocity(vector.Zero);
	}

	//------------------------------------------------------------------------------------------------
	protected void SleepPhysics()
	{
		IEntity owner = m_Owner;
		if (!owner)
			owner = GetOwner();
		if (!owner)
			return;

		Physics physics = owner.GetPhysics();
		if (!physics)
			return;

		physics.EnableGravity(false);
		physics.SetVelocity(vector.Zero);
		physics.SetAngularVelocity(vector.Zero);
		physics.SetActive(ActiveState.INACTIVE);
	}

	//------------------------------------------------------------------------------------------------
	protected void ClearOwnerStreaming()
	{
		if (!m_Rpl)
			return;

		PlayerController controller = GetGame().GetPlayerManager().GetPlayerController(m_iExpectedPlayerId);
		if (!controller)
			return;

		RplIdentity identity = controller.GetRplIdentity();
		if (!identity.IsValid())
			return;

		m_Rpl.EnableStreamingConNode(identity, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyAuthorityMotion()
	{
		if (!IsAuthority())
			return;

		IEntity owner = m_Owner;
		if (!owner)
			owner = GetOwner();
		if (!owner)
			return;

		Physics physics = owner.GetPhysics();
		if (!physics)
			return;

		vector previousOrigin = owner.GetOrigin();
		ApplyAuthorityOrientation();

		physics.EnableGravity(false);
		physics.SetAngularVelocity(vector.Zero);
		physics.SetVelocity(m_vWorldVel);
		physics.SetActive(ActiveState.ACTIVE);
		if (m_Rpl)
			m_Rpl.ForceNodeMovement(previousOrigin);
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyAuthorityOrientation()
	{
		IEntity owner = m_Owner;
		if (!owner)
			owner = GetOwner();
		if (!owner)
			return;

		vector ypr;
		ypr[0] = m_fHeading * Math.RAD2DEG;
		ypr[1] = ClampDivePitch(m_fPitch);
		ypr[2] = m_fBank;
		owner.SetYawPitchRoll(ypr);
	}

	//------------------------------------------------------------------------------------------------
	protected void LogCanopyTick(float timeSlice, float agl)
	{
		m_fDiagTime = m_fDiagTime + timeSlice;
		if (m_fDiagTime < 2)
			return;
		m_fDiagTime = 0;

		IEntity owner = GetOwner();
		float y = 0;
		if (owner)
			y = owner.GetOrigin()[1];

		MHJ_Log.Info("Craft tick phase=" + m_ePhase.ToString() + " boarded=" + MHJ_Log.Flag(m_bBoarded) + " agl=" + agl.ToString() + " tas=" + m_fAirspeed.ToString() + " vy=" + m_fVelY.ToString() + " y=" + y.ToString());
	}

	//------------------------------------------------------------------------------------------------
	protected void SendFlightState(float timeSlice)
	{
		m_fStateSendTime = m_fStateSendTime + timeSlice;
		if (m_fStateSendTime < MHJ_Constants.FLIGHT_STATE_DT)
			return;
		m_fStateSendTime = 0;

		IEntity owner = m_Owner;
		if (!owner)
			owner = GetOwner();
		if (!owner)
			return;

		vector ypr;
		ypr[0] = m_fHeading * Math.RAD2DEG;
		ypr[1] = ClampDivePitch(m_fPitch);
		ypr[2] = m_fBank;
		vector auxA;
		auxA[0] = m_fSimT;
		auxA[1] = m_iAckSeq;
		auxA[2] = m_fOpenT;
		vector auxB;
		auxB[0] = m_fPathDeg;
		auxB[1] = m_fHeading;
		auxB[2] = m_fTurnFilt;
		vector auxC;
		auxC[0] = m_fTurnFiltV;
		auxC[1] = m_fPitchInputFilt;
		auxC[2] = m_fPitchInputFiltV;
		vector auxD;
		auxD[0] = m_fPathDegV;
		auxD[1] = m_fBankV;
		auxD[2] = m_fPitchV;
		Rpc(RpcDo_MHJ_FlightState, m_vWorldVel, owner.GetOrigin(), ypr, auxA, auxB, auxC, auxD, ModeToId(m_sFlightMode) + (m_iAckSeq * 16));
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Unreliable, RplRcver.Broadcast)]
	protected void RpcDo_MHJ_FlightState(vector worldVelocity, vector origin, vector ypr, vector auxA, vector auxB, vector auxC, vector auxD, int packed)
	{
		if (IsAuthority())
			return;

		int modeId = packed % 16;
		int ackSeq = packed / 16;
		m_sFlightMode = IdToMode(modeId);
		if (modeId >= 6)
		{
			if (m_ePhase != MHJ_EHaloPhase.CANOPY)
				m_ePhase = MHJ_EHaloPhase.FREEFALL;
		}
		else
		{
			m_ePhase = MHJ_EHaloPhase.CANOPY;
		}
		ApplyCraftVisual(m_ePhase == MHJ_EHaloPhase.CANOPY);
		StoreReplicaSnapshot(origin, ypr, worldVelocity, auxA, auxB, auxC, auxD, ackSeq);

		if (m_bOwnerSession && m_ePhase == MHJ_EHaloPhase.CANOPY && !m_bSnatchFired)
		{
			if (MHJ_FlightAero.CanopyInflation(m_fOpenT) > 0.45)
			{
				m_bSnatchFired = true;
				SCR_CameraShakeManagerComponent.AddCameraShake(0.85, 1.35, 0.04, 0.28, 0.55);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected int ModeToId(string mode)
	{
		if (mode == "STALL")
			return 1;
		if (mode == "FLARE")
			return 2;
		if (mode == "BRAKES")
			return 3;
		if (mode == "DIVE")
			return 4;
		if (mode == "GLIDE")
			return 5;
		if (mode == "FREEFALL")
			return 6;
		if (mode == "TRACKING")
			return 7;
		if (mode == "SLOW FALL")
			return 8;
		if (mode == "EXIT")
			return 9;
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	protected string IdToMode(int modeId)
	{
		if (modeId == 1)
			return "STALL";
		if (modeId == 2)
			return "FLARE";
		if (modeId == 3)
			return "BRAKES";
		if (modeId == 4)
			return "DIVE";
		if (modeId == 5)
			return "GLIDE";
		if (modeId == 6)
			return "FREEFALL";
		if (modeId == 7)
			return "TRACKING";
		if (modeId == 8)
			return "SLOW FALL";
		if (modeId == 9)
			return "EXIT";
		return "OPENING";
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyCanopy(float pDt, float agl)
	{
		m_fOpenT = m_fOpenT + pDt;
		float inflation = MHJ_FlightAero.CanopyInflation(m_fOpenT);

		float pitchTarget = m_fPitchInput;
		if (agl < MHJ_Constants.FLARE_AGL && m_fPitchInput < 0.12)
		{
			float span = MHJ_Constants.FLARE_AGL - MHJ_Constants.FLARE_END_AGL;
			if (span < 0.5)
				span = 0.5;
			float autoFlare = 1 - (agl - MHJ_Constants.FLARE_END_AGL) / span;
			if (autoFlare < 0)
				autoFlare = 0;
			if (autoFlare > 1)
				autoFlare = 1;
			float wantedPitch = -autoFlare;
			if (wantedPitch < pitchTarget)
				pitchTarget = wantedPitch;
		}

		m_fTurnFilt = Math.SmoothCD(m_fTurnFilt, m_fTurnInput, m_fTurnFiltV, MHJ_Constants.STEER_FILTER_TIME, MHJ_Constants.STEER_FILTER_MAX, pDt);

		float pitchInTime = MHJ_Constants.CANOPY_PITCH_IN_TIME;
		float pitchInMax = MHJ_Constants.CANOPY_PITCH_IN_MAX;
		if (pitchTarget > m_fPitchInputFilt)
		{
			pitchInTime = MHJ_Constants.CANOPY_DIVE_IN_TIME;
			pitchInMax = MHJ_Constants.CANOPY_DIVE_IN_MAX;
		}
		m_fPitchInputFilt = Math.SmoothCD(m_fPitchInputFilt, pitchTarget, m_fPitchInputFiltV, pitchInTime, pitchInMax, pDt);

		float dive = 0;
		float brake = 0;
		if (m_fPitchInputFilt > 0)
			dive = m_fPitchInputFilt;
		if (m_fPitchInputFilt < 0)
			brake = -m_fPitchInputFilt;

		float wantedPath = MHJ_FlightAero.CanopyDemandPathDeg(dive, brake);
		m_fPathDeg = Math.SmoothCD(m_fPathDeg, wantedPath, m_fPathDegV, MHJ_Constants.CANOPY_PATH_INERTIA, MHJ_Constants.CANOPY_PATH_IN_MAX, pDt);

		float inflation01 = inflation;
		if (inflation01 > 1)
			inflation01 = 1;
		if (inflation01 < 0)
			inflation01 = 0;

		m_fHeading = WrapHeading(m_fHeading + m_fTurnFilt * MHJ_Constants.CANOPY_TURN_RATE * inflation01 * pDt);

		float wantedBank = m_fTurnFilt * MHJ_Constants.CANOPY_BANK_MAX * inflation01;
		wantedBank = wantedBank + Math.PerlinNoise(m_fSimT * 0.55, 2.2) * 3.5 * inflation01;
		m_fBank = Math.SmoothCD(m_fBank, wantedBank, m_fBankV, MHJ_Constants.CANOPY_BANK_INERTIA, 80, pDt);

		float wantedPitch = MHJ_Constants.CANOPY_PITCH_CRUISE;
		wantedPitch = wantedPitch + dive * (MHJ_Constants.CANOPY_PITCH_DIVE - MHJ_Constants.CANOPY_PITCH_CRUISE);
		wantedPitch = wantedPitch + brake * (MHJ_Constants.CANOPY_PITCH_FLARE - MHJ_Constants.CANOPY_PITCH_CRUISE);
		if (m_fOpenT < MHJ_Constants.CANOPY_OPEN_TIME)
		{
			float snatch = inflation;
			if (snatch > 1)
				snatch = 1;
			wantedPitch = wantedPitch + snatch * MHJ_Constants.CANOPY_PITCH_SNATCH;
		}
		m_fPitch = Math.SmoothCD(m_fPitch, ClampDivePitch(wantedPitch), m_fPitchV, MHJ_Constants.CANOPY_PITCH_INERTIA, 120, pDt);

		SteerCanopyTowardHeading(pDt, inflation01);

		if (inflation01 < 0.92)
			m_sFlightMode = "OPENING";
		else if (m_fAirspeed < MHJ_Constants.CANOPY_STALL_SPEED + 0.6)
			m_sFlightMode = "STALL";
		else if (brake > 0.62)
			m_sFlightMode = "FLARE";
		else if (brake > 0.2)
			m_sFlightMode = "BRAKES";
		else if (dive > 0.2)
			m_sFlightMode = "DIVE";
		else
			m_sFlightMode = "GLIDE";
	}

	//------------------------------------------------------------------------------------------------
	protected void IntegrateAero(float pDt)
	{
		int steps = 1;
		if (pDt > 0.019)
			steps = 2;
		if (pDt > 0.033)
			steps = 3;

		float stepTime = pDt / steps;
		int i;
		for (i = 0; i < steps; i++)
			IntegrateCanopyStep(stepTime);

		float speed = m_vWorldVel.Length();
		if (speed > MHJ_Constants.CANOPY_MAX_TAS)
			m_vWorldVel = m_vWorldVel * (MHJ_Constants.CANOPY_MAX_TAS / speed);
		ClampCanopySpeed(pDt);
		SyncSpeedFromWorld();
	}

	//------------------------------------------------------------------------------------------------
	protected void IntegrateCanopyStep(float pDt)
	{
		float msl = GetSimAltitude();

		float density = MHJ_FlightAero.DensityRatio(msl);
		float inflation = MHJ_FlightAero.CanopyInflation(m_fOpenT);
		float inflation01 = inflation;
		if (inflation01 > 1)
			inflation01 = 1;
		if (inflation01 < 0)
			inflation01 = 0;

		m_vWorldVel[1] = m_vWorldVel[1] - MHJ_Constants.GRAVITY * pDt;

		float dive = 0;
		float brake = 0;
		if (m_fPitchInputFilt > 0)
			dive = m_fPitchInputFilt;
		if (m_fPitchInputFilt < 0)
			brake = -m_fPitchInputFilt;

		AlignCanopyPath(pDt, inflation01, dive, brake);

		vector airVelocity = m_vWorldVel - m_vWind;
		float trueAirspeed = airVelocity.Length();
		if (trueAirspeed < 0.35)
			return;

		vector airDirection = airVelocity;
		airDirection.Normalize();

		float bodyDrag = MHJ_Constants.GRAVITY * density * trueAirspeed * trueAirspeed / (MHJ_Constants.FREEFALL_TERMINAL * MHJ_Constants.FREEFALL_TERMINAL);
		m_vWorldVel = m_vWorldVel - airDirection * (bodyDrag * (1 - inflation01) * pDt);
		if (inflation < 0.04)
			return;

		float liftCoefficient;
		float dragCoefficient;
		MHJ_FlightAero.CanopyCoeff(dive, brake, trueAirspeed, PathDiveAmount(), liftCoefficient, dragCoefficient);
		liftCoefficient = liftCoefficient * inflation;
		dragCoefficient = dragCoefficient * inflation;

		float area = MHJ_Constants.BODY_AREA + inflation01 * inflation01 * (MHJ_Constants.CANOPY_AREA - MHJ_Constants.BODY_AREA);
		float dynamicPressure = 0.5 * MHJ_Constants.AIR_DENSITY_SL * density * trueAirspeed * trueAirspeed;
		float liftForce = dynamicPressure * liftCoefficient * area;
		float dragForce = dynamicPressure * dragCoefficient * area;
		float inverseMass = 1 / MHJ_Constants.MASS;

		vector right = HeadingRight();
		vector liftDirection = SCR_Math3D.Cross(airDirection, right);
		if (liftDirection.Length() < 0.05)
		{
			liftDirection = Vector(0, 1, 0) - airDirection * vector.Dot(Vector(0, 1, 0), airDirection);
			if (liftDirection.Length() < 0.05)
				liftDirection = HeadingForward();
			else
				liftDirection.Normalize();
		}
		else
		{
			liftDirection.Normalize();
			if (vector.Dot(liftDirection, Vector(0, 1, 0)) < 0)
				liftDirection = liftDirection * -1;
		}

		liftDirection = RotateAroundAxis(liftDirection, airDirection, m_fBank * Math.DEG2RAD);
		m_vWorldVel = m_vWorldVel + liftDirection * (liftForce * inverseMass * pDt);
		m_vWorldVel = m_vWorldVel - airDirection * (dragForce * inverseMass * pDt);
		if (m_vWorldVel[1] > MHJ_Constants.CANOPY_MAX_CLIMB)
			m_vWorldVel[1] = MHJ_Constants.CANOPY_MAX_CLIMB;

		SnapAirToPath();
	}

	//------------------------------------------------------------------------------------------------
	protected void AlignCanopyPath(float pDt, float inflation01, float dive, float brake)
	{
		if (inflation01 < 0.04)
			return;

		vector airVelocity = m_vWorldVel - m_vWind;
		float trueAirspeed = airVelocity.Length();
		if (trueAirspeed < 0.35)
			return;

		vector airDirection = airVelocity;
		airDirection.Normalize();
		vector nose = CanopyPathNose(m_fPathDeg);
		float blend = MHJ_FlightAero.CanopyPathAlign(dive, brake) * inflation01 * pDt;
		if (blend > 1)
			blend = 1;

		vector mixed = airDirection + (nose - airDirection) * blend;
		if (mixed.Length() < 0.001)
			return;
		mixed.Normalize();
		mixed = ClampPathDirection(mixed);

		float pathDive = PathDiveAmount();
		if (m_fOpenT >= MHJ_Constants.CANOPY_OPEN_TIME)
			trueAirspeed = trueAirspeed + MHJ_Constants.CANOPY_DIVE_ACCEL * pathDive * inflation01 * pDt;
		trueAirspeed = MHJ_FlightAero.CanopyCoastBleed(trueAirspeed, dive, brake, pathDive, pDt);
		m_vWorldVel = mixed * trueAirspeed + m_vWind;
	}

	//------------------------------------------------------------------------------------------------
	protected void SteerCanopyTowardHeading(float pDt, float inflation01)
	{
		if (inflation01 < 0.2)
			return;

		vector air = m_vWorldVel - m_vWind;
		vector horizontal = air;
		horizontal[1] = 0;
		float horizontalSpeed = horizontal.Length();
		vector nose = HeadingForward();
		float blend = MHJ_Constants.CANOPY_HEADING_LERP * pDt;
		if (blend > 1)
			blend = 1;
		blend = blend * inflation01;

		vector newHorizontal = vector.Zero;
		if (horizontalSpeed < 0.5)
		{
			float kick = MHJ_Constants.CANOPY_CRUISE_TAS * 0.4 * inflation01;
			newHorizontal[0] = nose[0] * kick;
			newHorizontal[2] = nose[2] * kick;
		}
		else
		{
			vector currentDirection = horizontal;
			currentDirection.Normalize();
			vector blended = currentDirection + (nose - currentDirection) * blend;
			if (blended.Length() < 0.001)
				return;
			blended.Normalize();
			newHorizontal[0] = blended[0] * horizontalSpeed;
			newHorizontal[2] = blended[2] * horizontalSpeed;
		}

		m_vWorldVel[0] = m_vWind[0] + newHorizontal[0];
		m_vWorldVel[2] = m_vWind[2] + newHorizontal[2];
	}

	//------------------------------------------------------------------------------------------------
	protected vector CanopyPathNose(float pathDeg)
	{
		if (pathDeg < -MHJ_Constants.DIVE_ANGLE_MAX)
			pathDeg = -MHJ_Constants.DIVE_ANGLE_MAX;

		float pathRadians = pathDeg * Math.DEG2RAD;
		float cosine = Math.Cos(pathRadians);
		vector nose = vector.Zero;
		nose[0] = Math.Sin(m_fHeading) * cosine;
		nose[1] = Math.Sin(pathRadians);
		nose[2] = Math.Cos(m_fHeading) * cosine;
		return nose;
	}

	//------------------------------------------------------------------------------------------------
	protected float PathDiveAmount()
	{
		if (m_fPathDeg >= MHJ_Constants.CANOPY_PATH_CRUISE)
			return 0;

		float span = MHJ_Constants.CANOPY_PATH_CRUISE - MHJ_Constants.CANOPY_PATH_DIVE;
		if (span < 1)
			span = 1;
		float amount = (MHJ_Constants.CANOPY_PATH_CRUISE - m_fPathDeg) / span;
		if (amount > 1)
			amount = 1;
		if (amount < 0)
			amount = 0;
		return amount;
	}

	//------------------------------------------------------------------------------------------------
	protected vector ClampPathDirection(vector direction)
	{
		vector horizontal = direction;
		horizontal[1] = 0;
		float pathDeg = Math.Atan2(direction[1], horizontal.Length()) * Math.RAD2DEG;
		if (pathDeg >= -MHJ_Constants.DIVE_ANGLE_MAX)
			return direction;
		return CanopyPathNose(-MHJ_Constants.DIVE_ANGLE_MAX);
	}

	//------------------------------------------------------------------------------------------------
	protected void SnapAirToPath()
	{
		vector air = m_vWorldVel - m_vWind;
		float trueAirspeed = air.Length();
		if (trueAirspeed < 0.35)
			return;

		vector nose = ClampPathDirection(CanopyPathNose(m_fPathDeg));
		if (nose.Length() < 0.001)
			return;
		nose.Normalize();
		m_vWorldVel = nose * trueAirspeed + m_vWind;
	}

	//------------------------------------------------------------------------------------------------
	protected float InitialPathFromVelocity(vector airVelocity)
	{
		if (airVelocity.Length() < 0.35)
			return MHJ_Constants.CANOPY_PATH_CRUISE;
		vector horizontal = airVelocity;
		horizontal[1] = 0;
		float pathDeg = Math.Atan2(airVelocity[1], horizontal.Length()) * Math.RAD2DEG;
		if (pathDeg < -MHJ_Constants.DIVE_ANGLE_MAX)
			pathDeg = -MHJ_Constants.DIVE_ANGLE_MAX;
		return pathDeg;
	}

	//------------------------------------------------------------------------------------------------
	protected float ClampDivePitch(float pitch)
	{
		if (pitch > MHJ_Constants.DIVE_ANGLE_MAX)
			return MHJ_Constants.DIVE_ANGLE_MAX;
		if (pitch < -MHJ_Constants.DIVE_ANGLE_MAX)
			return -MHJ_Constants.DIVE_ANGLE_MAX;
		return pitch;
	}

	//------------------------------------------------------------------------------------------------
	protected vector RotateAroundAxis(vector value, vector axis, float angle)
	{
		if (axis.Length() < 0.001)
			return value;
		if (angle > -0.0002 && angle < 0.0002)
			return value;

		float quaternion[4];
		SCR_Math3D.QuatAngleAxis(angle, axis, quaternion);
		return SCR_Math3D.QuatMultiply(quaternion, value);
	}

	//------------------------------------------------------------------------------------------------
	protected vector HeadingForward()
	{
		vector direction = vector.Zero;
		direction[0] = Math.Sin(m_fHeading);
		direction[2] = Math.Cos(m_fHeading);
		return direction;
	}

	//------------------------------------------------------------------------------------------------
	protected vector HeadingRight()
	{
		vector direction = vector.Zero;
		direction[0] = Math.Cos(m_fHeading);
		direction[2] = -Math.Sin(m_fHeading);
		return direction;
	}

	//------------------------------------------------------------------------------------------------
	protected float WrapHeading(float heading)
	{
		float twoPi = Math.PI * 2;
		while (heading >= twoPi)
			heading = heading - twoPi;
		while (heading < 0)
			heading = heading + twoPi;
		return heading;
	}

	//------------------------------------------------------------------------------------------------
	protected void ClampCanopySpeed(float pDt)
	{
		float dive = 0;
		if (m_fPitchInputFilt > 0)
			dive = m_fPitchInputFilt;

		vector air = m_vWorldVel - m_vWind;
		float tas = air.Length();
		float maxTas = MHJ_Constants.CANOPY_MAX_TAS;
		if (m_fOpenT < MHJ_Constants.CANOPY_OPEN_TIME)
		{
			float u = m_fOpenT / MHJ_Constants.CANOPY_OPEN_TIME;
			if (u < 0)
				u = 0;
			if (u > 1)
				u = 1;
			maxTas = MHJ_Constants.FREEFALL_TERMINAL + (MHJ_Constants.CANOPY_CRUISE_TAS - MHJ_Constants.FREEFALL_TERMINAL) * u;
		}

		if (tas > maxTas && tas > 0.001)
		{
			air = air * (maxTas / tas);
			m_vWorldVel = air + m_vWind;
		}

		float cap = MHJ_Constants.CANOPY_MAX_SINK;
		float decel = MHJ_Constants.CANOPY_SINK_DECEL;
		if (m_fOpenT < MHJ_Constants.CANOPY_OPEN_TIME)
		{
			float u = m_fOpenT / MHJ_Constants.CANOPY_OPEN_TIME;
			if (u < 0)
				u = 0;
			if (u > 1)
				u = 1;
			cap = MHJ_Constants.FREEFALL_TERMINAL + (MHJ_Constants.CANOPY_MAX_SINK - MHJ_Constants.FREEFALL_TERMINAL) * u;
			decel = MHJ_Constants.CANOPY_OPEN_SINK_DECEL;
		}
		else if (dive > 0.2)
		{
			return;
		}

		if (pDt <= 0)
			return;
		if (m_vWorldVel[1] >= -cap)
			return;

		float nextY = m_vWorldVel[1] + (decel * pDt);
		if (nextY > -cap)
			nextY = -cap;
		m_vWorldVel[1] = nextY;
	}

	//------------------------------------------------------------------------------------------------
	//! Snap authority onto the owner's reported ground so GetOut is where they
	//! saw terrain if a few metres of residual remain.
	protected void PlaceCraftAtOwnerGround(vector ownerOrigin)
	{
		IEntity owner = m_Owner;
		if (!owner)
			owner = GetOwner();
		if (!owner)
			return;

		BaseWorld world = owner.GetWorld();
		if (!world)
			world = GetGame().GetWorld();
		if (!world)
			return;

		vector previousOrigin = owner.GetOrigin();
		float terrainY = SCR_TerrainHelper.GetTerrainY(ownerOrigin, world, true);
		ownerOrigin[1] = terrainY + MHJ_Constants.LAND_AGL;
		owner.SetOrigin(ownerOrigin);
		if (m_Rpl)
			m_Rpl.ForceNodeMovement(previousOrigin);
	}

	//------------------------------------------------------------------------------------------------
	//! Drop the authority craft onto terrain before GetOut so an early owner
	//! touchdown does not eject from 10 m AGL.
	protected void StickCraftToTerrain()
	{
		IEntity owner = m_Owner;
		if (!owner)
			owner = GetOwner();
		if (!owner)
			return;

		vector position = owner.GetOrigin();
		BaseWorld world = owner.GetWorld();
		if (!world)
			world = GetGame().GetWorld();
		if (!world)
			return;

		float terrainY = SCR_TerrainHelper.GetTerrainY(position, world, true);
		float wantY = terrainY + MHJ_Constants.LAND_AGL;
		if (position[1] <= wantY)
			return;

		vector previousOrigin = position;
		position[1] = wantY;
		owner.SetOrigin(position);
		if (m_Rpl)
			m_Rpl.ForceNodeMovement(previousOrigin);
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsBelowTerrain()
	{
		vector position;
		if (m_bReplicaLive && IsOwnedHere() && m_bOwnerSession)
		{
			position = m_vPredOrigin;
		}
		else
		{
			IEntity owner = GetOwner();
			if (!owner)
				return false;
			position = owner.GetOrigin();
		}

		IEntity craft = GetOwner();
		BaseWorld world;
		if (craft)
			world = craft.GetWorld();
		if (!world)
			world = GetGame().GetWorld();
		if (!world)
			return false;

		float terrainY = SCR_TerrainHelper.GetTerrainY(position, world, true);
		return position[1] < terrainY;
	}

	//------------------------------------------------------------------------------------------------
	//! Cargo has no NwkMovementComponent. Native rpl will not fly the replica.
	//! Owner restores each authority snapshot and replays unacked input.
	//! Proxies extrapolate origin + vel * age.
	protected void StoreReplicaSnapshot(vector origin, vector ypr, vector worldVelocity, vector auxA, vector auxB, vector auxC, vector auxD, int ackSeq)
	{
		IEntity owner = m_Owner;
		if (!owner)
			owner = GetOwner();
		if (!owner)
			return;

		vector previousOrigin = owner.GetOrigin();
		bool firstPose = !m_bReplicaLive;
		m_vSnapOrigin = origin;
		m_vSnapVel = worldVelocity;
		m_vSnapYpr = ypr;
		m_fSnapAge = 0;
		m_fPktDt = m_fSincePacket;
		m_fSincePacket = 0;

		ref MHJ_FlightState state = new MHJ_FlightState();
		state.FromPacket(origin, worldVelocity, ypr, auxA, auxB, auxC, auxD, ackSeq);

		if (firstPose)
		{
			m_bReplicaLive = true;
			RestoreFlightState(state);
			m_vWind = MHJ_FlightAero.WindWorld(m_vPredOrigin[1], m_fSimT);
			owner.SetOrigin(origin);
			owner.SetYawPitchRoll(ypr);
			ApplyReplicaVelocity();
			SyncSpeedFromWorld();
			MHJ_Log.Info("Replica pose started");
			return;
		}

		if (IsOwnedHere() && m_bOwnerSession)
		{
			if (!m_PendingSnap)
				m_PendingSnap = new MHJ_FlightState();
			m_PendingSnap.FromPacket(origin, worldVelocity, ypr, auxA, auxB, auxC, auxD, ackSeq);
			m_bPendingSnap = true;
			return;
		}

		float speedSq = worldVelocity.LengthSq();
		if (speedSq > 0.01)
		{
			m_fSnapAge = vector.Dot(previousOrigin - origin, worldVelocity) / speedSq;
			if (m_fSnapAge < 0)
				m_fSnapAge = 0;
			if (m_fSnapAge > MHJ_Constants.REPLICA_SNAP_AGE_MAX)
				m_fSnapAge = MHJ_Constants.REPLICA_SNAP_AGE_MAX;
		}

		m_vWorldVel = worldVelocity;
		m_fHeading = state.m_fHeading;
		m_fPitch = ypr[1];
		m_fBank = ypr[2];
		m_fPathDeg = state.m_fPathDeg;
		m_fOpenT = state.m_fOpenT;
		m_fSimT = state.m_fSimT;
		SyncSpeedFromWorld();
	}

	//------------------------------------------------------------------------------------------------
	protected void RestoreFlightState(notnull MHJ_FlightState state)
	{
		m_vPredOrigin = state.m_vOrigin;
		m_vWorldVel = state.m_vWorldVel;
		m_fHeading = state.m_fHeading;
		m_fPitch = state.m_fPitch;
		m_fBank = state.m_fBank;
		m_fBankV = state.m_fBankV;
		m_fPitchV = state.m_fPitchV;
		m_fPathDeg = state.m_fPathDeg;
		m_fPathDegV = state.m_fPathDegV;
		m_fOpenT = state.m_fOpenT;
		m_fSimT = state.m_fSimT;
		m_fTurnFilt = state.m_fTurnFilt;
		m_fTurnFiltV = state.m_fTurnFiltV;
		m_fPitchInputFilt = state.m_fPitchInputFilt;
		m_fPitchInputFiltV = state.m_fPitchInputFiltV;
		m_iAckSeq = state.m_iAckSeq;
	}

	//------------------------------------------------------------------------------------------------
	protected void ReconcileOwnerFromSnapshot(notnull MHJ_FlightState state)
	{
		vector before = m_vPredOrigin;
		RestoreFlightState(state);
		DropAckedInput(state.m_iAckSeq);
		ReplayUnackedInput();
		m_vWind = MHJ_FlightAero.WindWorld(m_vPredOrigin[1], m_fSimT);
		SyncSpeedFromWorld();
		m_vVisualError = m_vVisualError + (before - m_vPredOrigin);
	}

	//------------------------------------------------------------------------------------------------
	protected void DropAckedInput(int ackSeq)
	{
		if (!m_aInputRing)
			return;

		while (m_aInputRing.Count() > 0)
		{
			MHJ_InputSample sample = m_aInputRing.Get(0);
			if (!sample)
			{
				m_aInputRing.RemoveOrdered(0);
				continue;
			}
			if (sample.m_iSeq > ackSeq)
				return;

			m_aInputRing.RemoveOrdered(0);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void ReplayUnackedInput()
	{
		m_iReplayFrames = 0;
		if (!m_aInputRing)
			return;

		int count = m_aInputRing.Count();
		m_iReplayFrames = count;
		int i;
		for (i = 0; i < count; i++)
		{
			MHJ_InputSample sample = m_aInputRing.Get(i);
			if (!sample)
				continue;

			m_fTurnInput = sample.m_fTurn;
			m_fPitchInput = sample.m_fPitch;
			IntegrateOwnerAero(sample.m_fDt);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void PushOwnerInput(int seq, float turn, float pitch, float dt)
	{
		if (!m_aInputRing)
			m_aInputRing = new array<ref MHJ_InputSample>();

		ref MHJ_InputSample sample = new MHJ_InputSample();
		sample.m_iSeq = seq;
		sample.m_fTurn = turn;
		sample.m_fPitch = pitch;
		sample.m_fDt = dt;
		m_aInputRing.Insert(sample);

		while (m_aInputRing.Count() > MHJ_Constants.INPUT_RING_MAX)
			m_aInputRing.RemoveOrdered(0);
	}

	//------------------------------------------------------------------------------------------------
	protected void DecayVisualError(float timeSlice)
	{
		float window = MHJ_Constants.REPLICA_VISUAL_ERROR_TIME;
		if (window < 0.05)
			window = 0.05;
		float k = timeSlice / window;
		if (k > 1)
			k = 1;
		m_vVisualError = m_vVisualError * (1 - k);
	}

	//------------------------------------------------------------------------------------------------
	protected void IntegrateOwnerAero(float timeSlice)
	{
		m_fSimT = m_fSimT + timeSlice;
		m_vWind = MHJ_FlightAero.WindWorld(m_vPredOrigin[1], m_fSimT);

		float agl = GetAgl();
		if (m_ePhase == MHJ_EHaloPhase.FREEFALL)
		{
			ApplyFreefall(timeSlice);
			IntegrateFreefall(timeSlice);
		}
		else
		{
			ApplyCanopy(timeSlice, agl);
			IntegrateAero(timeSlice);
		}

		m_vPredOrigin = m_vPredOrigin + m_vWorldVel * timeSlice;
	}

	//------------------------------------------------------------------------------------------------
	protected float GetSimAltitude()
	{
		if (m_bReplicaLive && IsOwnedHere() && m_bOwnerSession)
			return m_vPredOrigin[1];

		IEntity owner = GetOwner();
		if (!owner)
			return 0;
		return owner.GetOrigin()[1];
	}

	//------------------------------------------------------------------------------------------------
	protected void TickReplicaMotion(notnull IEntity owner, float timeSlice)
	{
		if (!m_bReplicaLive)
			return;

		m_fSnapAge = m_fSnapAge + timeSlice;
		m_fSincePacket = m_fSincePacket + timeSlice;
		if (IsOwnedHere() && m_bOwnerSession)
			TickReplicaOwnerPredict(owner, timeSlice);

		ApplyReplicaVelocity();
		LogReplicaTick(owner, timeSlice);
	}

	//------------------------------------------------------------------------------------------------
	protected void TickReplicaOwnerPredict(notnull IEntity owner, float timeSlice)
	{
		if (m_bPendingSnap && m_PendingSnap)
		{
			ReconcileOwnerFromSnapshot(m_PendingSnap);
			m_bPendingSnap = false;
		}

		m_fInputSendTime = m_fInputSendTime + timeSlice;
		if (m_fInputSendTime >= MHJ_Constants.FLIGHT_STATE_DT)
		{
			m_fInputSendTime = 0;
			ReadInput();
			m_iInputSeq = m_iInputSeq + 1;
			Rpc(RpcAsk_MHJ_Steer, m_fTurnInput, m_fPitchInput, m_iInputSeq);
		}

		PushOwnerInput(m_iInputSeq, m_fTurnInput, m_fPitchInput, timeSlice);
		DecayVisualError(timeSlice);
		IntegrateOwnerAero(timeSlice);
		ApplyAuthorityOrientation();
	}

	//------------------------------------------------------------------------------------------------
	//! Apply after physics so velocity integration cannot add a one-frame sawtooth.
	//! Packet reconnect keeps snapAge on the visual so a new origin is not a yank.
	protected void ApplyReplicaProxyPose(notnull IEntity owner)
	{
		vector predicted = m_vSnapOrigin + m_vSnapVel * m_fSnapAge;
		owner.SetOrigin(predicted);
		owner.SetYawPitchRoll(m_vSnapYpr);
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyReplicaVelocity()
	{
		IEntity owner = m_Owner;
		if (!owner)
			owner = GetOwner();
		if (!owner)
			return;

		Physics physics = owner.GetPhysics();
		if (!physics)
			return;

		physics.EnableGravity(false);
		physics.SetActive(ActiveState.ACTIVE);
		physics.SetAngularVelocity(vector.Zero);
		physics.SetVelocity(m_vWorldVel);
	}

	//------------------------------------------------------------------------------------------------
	protected void LogReplicaTick(notnull IEntity owner, float timeSlice)
	{
		m_fReplicaDiag = m_fReplicaDiag + timeSlice;
		if (m_fReplicaDiag < 0.5)
			return;
		m_fReplicaDiag = 0;

		vector visual = owner.GetOrigin();
		vector pred = visual;
		if (IsOwnedHere() && m_bOwnerSession)
			pred = m_vPredOrigin;

		vector snap = m_vSnapOrigin + m_vSnapVel * m_fSnapAge;
		vector predDelta = pred - snap;
		vector visPred = visual - pred;
		float visPredM = visPred.Length();
		float predSnapM = predDelta.Length();
		float visSnapM = (visual - snap).Length();

		float along = 0;
		float lat = predSnapM;
		float speed = m_vSnapVel.Length();
		if (speed > 0.1)
		{
			along = vector.Dot(predDelta, m_vSnapVel) / speed;
			vector alongVec = m_vSnapVel * (along / speed);
			lat = (predDelta - alongVec).Length();
		}

		float physVy = 0;
		Physics physics = owner.GetPhysics();
		if (physics)
			physVy = physics.GetVelocity()[1];

		float visErr = m_vVisualError.Length();
		bool ownerSession = IsOwnedHere() && m_bOwnerSession;
		string message = "Replica diag owner=" + MHJ_Log.Flag(ownerSession);
		message = message + " phase=" + m_ePhase.ToString();
		message = message + " pktDt=" + m_fPktDt.ToString();
		message = message + " visPred=" + visPredM.ToString();
		message = message + " predSnap=" + predSnapM.ToString();
		message = message + " visSnap=" + visSnapM.ToString();
		message = message + " along=" + along.ToString();
		message = message + " lat=" + lat.ToString();
		message = message + " tas=" + m_fAirspeed.ToString();
		message = message + " vy=" + m_fVelY.ToString();
		message = message + " pvy=" + physVy.ToString();
		message = message + " stick=" + m_fOriginStick.ToString();
		message = message + " ackAge=" + m_iReplayFrames.ToString();
		message = message + " visErr=" + visErr.ToString();
		message = message + " y=" + visual[1].ToString();
		MHJ_Log.Info(message);
	}

	//------------------------------------------------------------------------------------------------
	protected void SyncSpeedFromWorld()
	{
		m_fVelY = m_vWorldVel[1];
		m_fAirspeed = (m_vWorldVel - m_vWind).Length();
	}

	//------------------------------------------------------------------------------------------------
	protected float GetAgl()
	{
		vector position;
		if (m_bReplicaLive && IsOwnedHere() && m_bOwnerSession)
		{
			position = m_vPredOrigin;
		}
		else
		{
			IEntity owner = GetOwner();
			if (!owner)
				return 0;
			position = owner.GetOrigin();
		}

		BaseWorld world = GetGame().GetWorld();
		IEntity craft = GetOwner();
		if (craft)
		{
			BaseWorld craftWorld = craft.GetWorld();
			if (craftWorld)
				world = craftWorld;
		}
		if (!world)
			return position[1];

		float terrainAgl = SCR_TerrainHelper.GetHeightAboveTerrain(position, world, true);
		if (terrainAgl < 0)
			terrainAgl = 0;
		return terrainAgl;
	}

	//------------------------------------------------------------------------------------------------
	protected void PushHud(float agl)
	{
		if (!m_bOwnerSession)
			return;

		vector forward = HeadingForward();
		vector horizontalWind = m_vWind;
		horizontalWind[1] = 0;
		float windSpeed = horizontalWind.Length();
		float headingDot = 0;
		if (windSpeed > 0.05)
			headingDot = vector.Dot(forward, horizontalWind) / windSpeed;

		string windRelative = MHJ_FlightAero.WindRelativeLabel(windSpeed, headingDot);
		MHJ_JumpHud.SetState(m_ePhase, agl, m_fOpenAltitude, m_fAirspeed, m_fVelY, m_sFlightMode, windSpeed, windRelative);
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyFreefall(float pDt)
	{
		float authority = ExitAuthority();
		float turnCommand = m_fTurnInput * authority;
		float pitchCommand = m_fPitchInput * authority;

		m_fTurnFilt = Math.SmoothCD(m_fTurnFilt, turnCommand, m_fTurnFiltV, MHJ_Constants.STEER_FILTER_TIME, MHJ_Constants.STEER_FILTER_MAX, pDt);
		m_fPitchInputFilt = Math.SmoothCD(m_fPitchInputFilt, pitchCommand, m_fPitchInputFiltV, 0.14, 80, pDt);

		m_fHeading = WrapHeading(m_fHeading + m_fTurnFilt * MHJ_Constants.FREEFALL_TURN_RATE * pDt);

		float wantBank = m_fTurnFilt * MHJ_Constants.FREEFALL_BANK_MAX;
		float exitAmount = 1 - authority;
		wantBank = wantBank + Math.Sin(m_fSimT * 7.3) * 12 * exitAmount;
		m_fBank = Math.SmoothCD(m_fBank, wantBank, m_fBankV, 0.22, 90, pDt);

		float wantPitch = MHJ_Constants.FREEFALL_PITCH_ARCH;
		if (m_fPitchInputFilt > 0.05)
			wantPitch = MHJ_Constants.FREEFALL_PITCH_TRACK * m_fPitchInputFilt;
		else if (m_fPitchInputFilt < -0.05)
			wantPitch = MHJ_Constants.FREEFALL_PITCH_SLOW * (-m_fPitchInputFilt);
		wantPitch = wantPitch * authority;
		wantPitch = wantPitch + Math.Sin(m_fSimT * 5.1 + 1.2) * 10 * exitAmount;
		m_fPitch = Math.SmoothCD(m_fPitch, ClampDivePitch(wantPitch), m_fPitchV, 0.28, 80, pDt);

		if (authority < 0.95)
			m_sFlightMode = "EXIT";
		else if (m_fPitchInputFilt > 0.2)
			m_sFlightMode = "TRACKING";
		else if (m_fPitchInputFilt < -0.2)
			m_sFlightMode = "SLOW FALL";
		else
			m_sFlightMode = "FREEFALL";
	}

	//------------------------------------------------------------------------------------------------
	protected void IntegrateFreefall(float pDt)
	{
		int steps = 1;
		if (pDt > 0.019)
			steps = 2;
		if (pDt > 0.033)
			steps = 3;

		float stepTime = pDt / steps;
		int i;
		for (i = 0; i < steps; i++)
			IntegrateFreefallStep(stepTime);

		float speed = m_vWorldVel.Length();
		if (speed > MHJ_Constants.FREEFALL_MAX_SPEED)
			m_vWorldVel = m_vWorldVel * (MHJ_Constants.FREEFALL_MAX_SPEED / speed);

		SyncSpeedFromWorld();
	}

	//------------------------------------------------------------------------------------------------
	protected void IntegrateFreefallStep(float pDt)
	{
		float msl = GetSimAltitude();

		float density = MHJ_FlightAero.DensityRatio(msl);
		m_vWorldVel[1] = m_vWorldVel[1] - MHJ_Constants.GRAVITY * pDt;

		vector airVelocity = m_vWorldVel - m_vWind;
		float trueAirspeed = airVelocity.Length();
		if (trueAirspeed < 0.4)
			return;

		vector airDirection = airVelocity;
		airDirection.Normalize();

		float track = MHJ_Constants.FREEFALL_TRACK_BASE;
		float slow = 0;
		if (m_fPitchInputFilt > 0)
			track = MHJ_Constants.FREEFALL_TRACK_BASE + (1 - MHJ_Constants.FREEFALL_TRACK_BASE) * m_fPitchInputFilt;
		if (m_fPitchInputFilt < 0)
		{
			slow = -m_fPitchInputFilt;
			track = MHJ_Constants.FREEFALL_TRACK_BASE * (1 - slow);
		}

		float terminalSpeed = MHJ_Constants.FREEFALL_TERMINAL;
		terminalSpeed = terminalSpeed + track * (MHJ_Constants.FREEFALL_TRACK_TERMINAL - MHJ_Constants.FREEFALL_TERMINAL);
		terminalSpeed = terminalSpeed + slow * (MHJ_Constants.FREEFALL_SLOW_TERMINAL - MHJ_Constants.FREEFALL_TERMINAL);
		if (terminalSpeed < 20)
			terminalSpeed = 20;

		float dragMagnitude = MHJ_Constants.GRAVITY * density * trueAirspeed * trueAirspeed / (terminalSpeed * terminalSpeed);
		m_vWorldVel = m_vWorldVel - airDirection * (dragMagnitude * pDt);

		AlignFreefallToHeading(pDt);

		vector nose = HeadingForward();
		float liftInput = track - slow * 0.28;
		m_vWorldVel = m_vWorldVel + nose * (liftInput * MHJ_Constants.FREEFALL_TRACK_LIFT * dragMagnitude * pDt);

		vector right = HeadingRight();
		vector slideDirection = right - airDirection * vector.Dot(right, airDirection);
		if (slideDirection.Length() > 0.05)
		{
			slideDirection.Normalize();
			m_vWorldVel = m_vWorldVel + slideDirection * (-m_fTurnFilt * MHJ_Constants.FREEFALL_SLIDE * dragMagnitude * pDt);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void AlignFreefallToHeading(float pDt)
	{
		vector air = m_vWorldVel - m_vWind;
		vector horizontal = air;
		horizontal[1] = 0;
		float horizontalSpeed = horizontal.Length();
		vector nose = HeadingForward();

		float turnAmount = m_fTurnFilt;
		if (turnAmount < 0)
			turnAmount = -turnAmount;

		float align = MHJ_Constants.FREEFALL_TURN_ALIGN * pDt;
		align = align + turnAmount * MHJ_Constants.FREEFALL_TURN_ALIGN * pDt;
		if (align > 1)
			align = 1;

		vector newHorizontal = vector.Zero;
		if (horizontalSpeed < 0.35)
		{
			float kick = 2.5 * ExitAuthority();
			newHorizontal[0] = nose[0] * kick;
			newHorizontal[2] = nose[2] * kick;
		}
		else
		{
			vector currentDirection = horizontal;
			currentDirection.Normalize();
			vector blended = currentDirection + (nose - currentDirection) * align;
			if (blended.Length() < 0.001)
				return;
			blended.Normalize();
			newHorizontal[0] = blended[0] * horizontalSpeed;
			newHorizontal[2] = blended[2] * horizontalSpeed;
		}

		m_vWorldVel[0] = m_vWind[0] + newHorizontal[0];
		m_vWorldVel[2] = m_vWind[2] + newHorizontal[2];
	}

	//------------------------------------------------------------------------------------------------
	protected float ExitAuthority()
	{
		float authority = m_fSimT / MHJ_Constants.FREEFALL_EXIT_TIME;
		if (authority > 1)
			authority = 1;
		if (authority < 0)
			authority = 0;
		return authority;
	}
}
