//------------------------------------------------------------------------------------------------
//! Ram-air flight after the jumper sits in the canopy cargo seat.
//! The standing graph cannot pitch with the wing; GetInVehicle links the pawn so
//! the sit pose inherits this entity's world attitude.
//------------------------------------------------------------------------------------------------
[ComponentEditorProps(category: "MHJ", description: "HALO canopy flight after cargo-seat GetIn.")]
class MHJ_CanopyFlightClass : ScriptComponentClass
{
}

class MHJ_CanopyFlight : ScriptComponent
{
	protected RplComponent m_Rpl;
	protected ChimeraCharacter m_pJumper;
	protected CharacterInputContext m_Input;
	protected CharacterHeadAimingComponent m_pHeadAim;
	protected BaseCompartmentSlot m_Slot;

	protected float m_fHeading;
	protected float m_fHeadingV;
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
	protected bool m_bActive;
	protected bool m_bHudOwned;
	protected bool m_bSnatchFired;
	protected bool m_bLanding;
	protected bool m_bLookWidened;
	protected string m_sFlightMode;
	protected vector m_vWorldVel;
	protected vector m_vWind;
	protected ref TraceParam m_AglTrace;
	protected int m_iBoardTries;
	protected bool m_bBoardQueued;
	protected int m_iDeleteTries;
	protected float m_fDiagT;

	//------------------------------------------------------------------------------------------------
	static bool OccupantIsInCanopy(IEntity character)
	{
		if (!character)
			return false;

		ChimeraCharacter pawn = ChimeraCharacter.Cast(character);
		if (!pawn)
			return false;
		if (!pawn.IsInVehicle())
			return false;

		CompartmentAccessComponent access = pawn.GetCompartmentAccessComponent();
		if (!access)
			return false;

		BaseCompartmentSlot slot = access.GetCompartment();
		if (!slot)
			return false;

		IEntity veh = slot.GetOwner();
		if (!veh)
			return false;

		MHJ_CanopyFlight flight = MHJ_CanopyFlight.Cast(veh.FindComponent(MHJ_CanopyFlight));
		if (!flight)
			return false;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (SCR_Global.IsEditMode())
			return;

		SetEventMask(owner, EntityEvent.FRAME);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		RestoreLookLimits();
		if (m_bHudOwned)
		{
			MHJ_JumpHud.Close();
			m_bHudOwned = false;
		}
	}

	//------------------------------------------------------------------------------------------------
	bool BeginFlight(notnull ChimeraCharacter jumper, vector worldVel, vector wind, float heading, float pitch, float bank, float pathDeg, float simT, bool hudOwned, float openAltitude)
	{
		IEntity owner = GetOwner();
		if (!owner)
			return false;

		m_Rpl = RplComponent.Cast(owner.FindComponent(RplComponent));
		m_pJumper = jumper;
		m_vWorldVel = worldVel;
		m_vWind = wind;
		m_fHeading = heading;
		m_fPitch = pitch;
		m_fBank = bank;
		m_fPathDeg = pathDeg;
		m_fSimT = simT;
		m_fOpenAltitude = openAltitude;
		m_fOpenT = 0;
		m_bSnatchFired = false;
		m_bLanding = false;
		m_sFlightMode = "OPENING";
		m_fTurnFilt = 0;
		m_fTurnFiltV = 0;
		m_fPitchInputFilt = 0;
		m_fPitchInputFiltV = 0;
		m_fPathDegV = 0;
		m_fHeadingV = 0;
		m_fBankV = 0;
		m_fPitchV = 0;
		m_fTurnInput = 0;
		m_fPitchInput = 0;
		m_fNetTurn = 0;
		m_fNetPitch = 0;
		if (!m_AglTrace)
			m_AglTrace = new TraceParam();

		SyncSpeedFromWorld();
		CacheSlot();

		CharacterControllerComponent ctrl = jumper.GetCharacterController();
		if (ctrl)
		{
			m_Input = ctrl.GetInputContext();
			ctrl.SetWeaponRaised(false);
			ctrl.SetWeaponADS(false);
			ctrl.ForceStance(ECharacterStance.STAND);
			ctrl.SetHeadingAngle(heading, true);
			ctrl.SetForcedFreeLook(true);
		}

		if (IsLocalOccupant())
		{
			m_bHudOwned = hudOwned;
			WidenLookLimits();
		}
		else if (hudOwned)
		{
			m_bHudOwned = false;
		}

		ApplyYawOnly();
		m_bActive = true;
		MHJ_Log.Info("Canopy BeginFlight at " + owner.GetOrigin().ToString() + " vel=" + m_vWorldVel.ToString() + " pitch=" + MHJ_Log.Deg(m_fPitch) + " bank=" + MHJ_Log.Deg(m_fBank) + " path=" + MHJ_Log.Deg(m_fPathDeg) + " hdg=" + MHJ_Log.Deg(m_fHeading * Math.RAD2DEG));

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(jumper.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (handler)
			handler.MHJ_BeginCanopyBoard(owner, m_Slot);
		else
			MHJ_Log.Warning("Canopy BeginFlight: no command handler");

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		if (SCR_Global.IsEditMode())
			return;
		if (!m_bActive)
			return;
		if (m_bLanding)
			return;

		if (!m_pJumper)
			AdoptOccupant();

		if (IsOwner())
		{
			if (IsLocalOccupant())
			{
				InputManager im = GetGame().GetInputManager();
				if (im)
					im.ActivateContext("CharacterMovementContext");
				KeepCanopyFreeLook();
			}
			ReadInput();
			if (!IsAuthority())
				Rpc(RpcAsk_MHJ_Steer, m_fTurnInput, m_fPitchInput);
		}
		else if (IsAuthority())
		{
			m_fTurnInput = m_fNetTurn;
			m_fPitchInput = m_fNetPitch;
		}

		if (IsLocalOccupant())
			KeepCanopyFreeLook();

		if (!IsAuthority())
		{
			if (m_bHudOwned)
				PushHud(GetAgl());
			return;
		}

		if (m_pJumper && HasOccupant())
		{
			CharacterControllerComponent ctrl = m_pJumper.GetCharacterController();
			if (ctrl && ctrl.IsDead())
			{
				FinishLanding();
				return;
			}
		}

		m_fSimT = m_fSimT + timeSlice;
		m_vWind = MHJ_FlightAero.WindWorld(owner.GetOrigin()[1], m_fSimT);

		float agl = GetAgl();
		if (agl <= MHJ_Constants.LAND_AGL)
		{
			FinishLanding();
			return;
		}

		if (HasOccupant())
		{
			SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(m_pJumper.FindComponent(SCR_CharacterCommandHandlerComponent));
			if (handler)
				handler.MHJ_KeepCanopySitPose();

			ApplyCanopy(timeSlice, agl);
			IntegrateAero(timeSlice);
			ApplyWorldAttitudeWithDt(timeSlice);
			LogCanopyDiag(timeSlice);
		}
		else
			HoldAtJumper();

		PushHud(agl);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Unreliable, RplRcver.Server)]
	protected void RpcAsk_MHJ_Steer(float turn, float pitch)
	{
		if (!IsAuthority())
			return;

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
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_MHJ_Board()
	{
		RequestBoard();
	}

	//------------------------------------------------------------------------------------------------
	protected void RequestBoard()
	{
		m_iBoardTries = 0;
		m_bBoardQueued = false;
		MHJ_Log.Info("Canopy RequestBoard delay=150 server=" + MHJ_Log.Flag(Replication.IsServer()) + " local=" + MHJ_Log.Flag(IsLocalOccupant()));
		GetGame().GetCallqueue().CallLater(TryBoard, 150, false);
		if (m_Rpl && !IsOwner())
			Rpc(RpcDo_MHJ_Board);
	}

	//------------------------------------------------------------------------------------------------
	protected void TryBoard()
	{
		if (!GetOwner())
		{
			MHJ_Log.Warning("Canopy TryBoard: owner gone");
			return;
		}
		if (m_bLanding)
		{
			MHJ_Log.Warning("Canopy TryBoard: already landing");
			return;
		}

		string why;
		if (TryGetIn(why))
		{
			MHJ_Log.Info("Canopy GetIn ok tries=" + m_iBoardTries.ToString());
			return;
		}

		MHJ_Log.Warning("Canopy GetIn try=" + m_iBoardTries.ToString() + " why=" + why + " " + BuildBoardDump());

		m_iBoardTries = m_iBoardTries + 1;
		if (m_iBoardTries >= 20)
		{
			MHJ_Log.Warning("Canopy GetIn failed after 20 tries last=" + why);
			return;
		}

		GetGame().GetCallqueue().CallLater(TryBoard, 50, false);
	}

	//------------------------------------------------------------------------------------------------
	protected bool TryGetIn(out string why)
	{
		why = "unknown";
		if (!m_pJumper)
		{
			why = "no-jumper";
			return false;
		}
		if (m_pJumper.IsInVehicle())
		{
			if (OccupantIsInCanopy(m_pJumper))
			{
				why = "already-in-canopy";
				return true;
			}

			why = "in-other-vehicle";
			return false;
		}

		CompartmentAccessComponent access = m_pJumper.GetCompartmentAccessComponent();
		if (!access)
		{
			why = "no-access";
			return false;
		}

		CacheSlot();
		if (!m_Slot)
		{
			why = "no-slot";
			return false;
		}

		IEntity owner = GetOwner();
		if (!owner)
		{
			why = "no-owner";
			return false;
		}

		m_Slot.SetCharacterHasCollision(false);

		if (m_bBoardQueued)
		{
			if (OccupantIsInCanopy(m_pJumper) || m_pJumper.IsInVehicle())
			{
				why = "queued-now-seated";
				return true;
			}

			if (access.IsGettingIn())
			{
				why = "waiting-getin";
				return false;
			}

			if (m_iBoardTries < 8)
			{
				why = "waiting-getin";
				return false;
			}

			access.InterruptVehicleActionQueue(true, true, true);
			m_bBoardQueued = false;
			MHJ_Log.Warning("Canopy GetIn queue stalled; retry");
		}

		bool can = access.CanGetInVehicle(owner);
		bool accessible = access.IsTargetVehicleAccessible(owner);
		bool ok = access.GetInVehicle(owner, m_Slot, true, 0, ECloseDoorAfterActions.INVALID, true);
		if (!ok)
		{
			why = "GetInVehicle-false";
			if (!can)
				why = why + " CanGetIn=0";
			if (!accessible)
				why = why + " Accessible=0";
			return false;
		}

		m_bBoardQueued = true;
		if (OccupantIsInCanopy(m_pJumper) || m_pJumper.IsInVehicle())
		{
			why = "ok-seated";
			return true;
		}

		why = "waiting-getin";
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected string BuildBoardDump()
	{
		IEntity owner = GetOwner();
		string dump = "srv=" + MHJ_Log.Flag(Replication.IsServer());
		dump = dump + " local=" + MHJ_Log.Flag(IsLocalOccupant());

		if (owner)
		{
			dump = dump + " canopy=" + owner.GetOrigin().ToString();
			ControllersManagerComponent ctrls = ControllersManagerComponent.Cast(owner.FindComponent(ControllersManagerComponent));
			dump = dump + " ctrls=" + MHJ_Log.Flag(ctrls != null);
			BaseCompartmentManagerComponent mgr = BaseCompartmentManagerComponent.Cast(owner.FindComponent(BaseCompartmentManagerComponent));
			dump = dump + " mgr=" + MHJ_Log.Flag(mgr != null);
			VehicleAnimationComponent vanim = VehicleAnimationComponent.Cast(owner.FindComponent(VehicleAnimationComponent));
			dump = dump + " vanim=" + MHJ_Log.Flag(vanim != null);
			if (m_Rpl)
				dump = dump + " rplOwner=" + MHJ_Log.Flag(m_Rpl.IsOwner());
			else
				dump = dump + " rpl=0";
		}

		if (m_Slot)
		{
			dump = dump + " slot=" + m_Slot.GetCompartmentName();
			int slotType = m_Slot.GetType();
			dump = dump + " type=" + slotType.ToString();
			dump = dump + " occ=" + MHJ_Log.Flag(m_Slot.IsOccupied());
			dump = dump + " lock=" + MHJ_Log.Flag(m_Slot.IsGetInLocked());
		}
		else
			dump = dump + " slot=0";

		if (!m_pJumper)
			return dump + " jumper=0";

		dump = dump + " jumper=" + m_pJumper.GetOrigin().ToString();
		dump = dump + " inVeh=" + MHJ_Log.Flag(m_pJumper.IsInVehicle());

		CompartmentAccessComponent access = m_pJumper.GetCompartmentAccessComponent();
		if (access)
		{
			dump = dump + " gettingIn=" + MHJ_Log.Flag(access.IsGettingIn());
			if (owner)
			{
				dump = dump + " can=" + MHJ_Log.Flag(access.CanGetInVehicle(owner));
				dump = dump + " acc=" + MHJ_Log.Flag(access.IsTargetVehicleAccessible(owner));
			}
		}

		CharacterControllerComponent ctrl = m_pJumper.GetCharacterController();
		if (ctrl)
		{
			dump = dump + " fall=" + MHJ_Log.Flag(ctrl.IsFalling());
			dump = dump + " dead=" + MHJ_Log.Flag(ctrl.IsDead());
			dump = dump + " uncon=" + MHJ_Log.Flag(ctrl.IsUnconscious());
		}

		CharacterAnimationComponent anim = m_pJumper.GetAnimationComponent();
		if (anim)
		{
			MHJ_HaloCommand halo = MHJ_HaloCommand.Cast(anim.GetCommandScripted());
			dump = dump + " haloCmd=" + MHJ_Log.Flag(halo != null);
			if (halo)
				dump = dump + " haloFin=" + MHJ_Log.Flag(halo.IsFlagFinished());
		}

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(m_pJumper.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (handler)
			dump = dump + " jumping=" + MHJ_Log.Flag(handler.MHJ_IsHaloJumping());

		return dump;
	}

	//------------------------------------------------------------------------------------------------
	protected void FinishLanding()
	{
		if (m_bLanding)
			return;

		m_bLanding = true;
		m_bActive = false;

		float landDown = -m_fVelY;
		if (landDown < 0)
			landDown = 0;

		vector landHoriz = m_vWorldVel;
		landHoriz[1] = 0;
		float landHorizSpeed = landHoriz.Length();
		float landHeading = m_fHeading;

		MHJ_Log.Info("Canopy land agl=" + GetAgl().ToString() + " down=" + landDown.ToString() + " hs=" + landHorizSpeed.ToString());

		ChimeraCharacter jumper = m_pJumper;
		GetOutOccupant();
		RestoreLookLimits();

		if (m_bHudOwned)
		{
			MHJ_JumpHud.Close();
			m_bHudOwned = false;
		}

		if (jumper)
		{
			SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(jumper.FindComponent(SCR_CharacterCommandHandlerComponent));
			if (handler)
				handler.MHJ_FinishCanopyLanding(landDown, landHorizSpeed, landHeading);
		}

		m_iDeleteTries = 0;
		if (Replication.IsServer())
			GetGame().GetCallqueue().CallLater(TryDeleteAfterLeave, 50, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void TryDeleteAfterLeave()
	{
		if (!Replication.IsServer())
			return;

		bool stillIn = false;
		if (m_pJumper)
		{
			CompartmentAccessComponent access = m_pJumper.GetCompartmentAccessComponent();
			if (access && access.IsInCompartment())
				stillIn = true;
		}

		if (stillIn)
		{
			GetOutOccupant();
			m_iDeleteTries = m_iDeleteTries + 1;
			if (m_iDeleteTries < 40)
			{
				GetGame().GetCallqueue().CallLater(TryDeleteAfterLeave, 50, false);
				return;
			}

			MHJ_Log.Warning("Canopy still occupied; not deleting to avoid A-pose");
			return;
		}

		bool stillLinked = false;
		if (m_pJumper)
		{
			IEntity parent = m_pJumper.GetParent();
			if (parent)
			{
				IEntity root = parent.GetRootParent();
				IEntity owner = GetOwner();
				if (parent == owner)
					parent.RemoveChild(m_pJumper, true);
				else if (root == owner)
					parent.RemoveChild(m_pJumper, true);
			}

			CharacterAnimationComponent anim = m_pJumper.GetAnimationComponent();
			if (anim && anim.PhysicsIsLinked())
				stillLinked = true;
		}

		if (stillLinked)
		{
			m_iDeleteTries = m_iDeleteTries + 1;
			if (m_iDeleteTries < 40)
			{
				GetGame().GetCallqueue().CallLater(TryDeleteAfterLeave, 50, false);
				return;
			}

			MHJ_Log.Warning("Canopy still physics-linked; deleting anyway");
		}

		SleepPhysics();
		DeleteSelf();
	}

	//------------------------------------------------------------------------------------------------
	protected void DeleteSelf()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;
		if (!Replication.IsServer())
			return;

		SCR_EntityHelper.DeleteEntityAndChildren(owner);
	}

	//------------------------------------------------------------------------------------------------
	protected void GetOutOccupant()
	{
		if (!m_pJumper)
			return;

		CompartmentAccessComponent access = m_pJumper.GetCompartmentAccessComponent();
		if (!access)
			return;
		if (!access.IsInCompartment())
			return;

		bool canOut = access.CanGetOutVehicle();
		bool canDoor = access.CanGetOutVehicleViaDoor(0);
		MHJ_Log.Info("Canopy GetOut can=" + MHJ_Log.Flag(canOut) + " door0=" + MHJ_Log.Flag(canDoor) + " gettingOut=" + MHJ_Log.Flag(access.IsGettingOut()));

		bool ragdoll = false;
		if (m_iDeleteTries >= 10)
			ragdoll = true;

		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(m_pJumper);
		PlayerController pc = GetGame().GetPlayerManager().GetPlayerController(playerId);
		SCR_PlayerController scrPc = SCR_PlayerController.Cast(pc);
		if (scrPc)
		{
			scrPc.MHJ_AskLeaveCanopy(ragdoll);
			return;
		}

		vector mat[4];
		m_pJumper.GetTransform(mat);
		SCR_CharacterCommandHandlerComponent landHandler = SCR_CharacterCommandHandlerComponent.Cast(m_pJumper.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (landHandler)
			landHandler.MHJ_FillGroundedTransform(m_pJumper, mat);
		else
			mat[3] = m_pJumper.GetOrigin();
		access.GetOutVehicle_NoDoor(mat, ragdoll, true, true);
	}

	//------------------------------------------------------------------------------------------------
	protected void GiveOwnership(notnull ChimeraCharacter jumper)
	{
		if (!m_Rpl)
			m_Rpl = RplComponent.Cast(GetOwner().FindComponent(RplComponent));
		if (!m_Rpl)
			return;

		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(jumper);
		if (playerId <= 0)
			return;

		PlayerController pc = GetGame().GetPlayerManager().GetPlayerController(playerId);
		if (!pc)
			return;

		m_Rpl.GiveExt(pc.GetRplIdentity(), true);
	}

	//------------------------------------------------------------------------------------------------
	protected void SleepPhysics()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;

		Physics phys = owner.GetPhysics();
		if (!phys)
			return;

		phys.SetVelocity(vector.Zero);
		phys.SetAngularVelocity(vector.Zero);
		phys.SetActive(ActiveState.INACTIVE);
	}

	//------------------------------------------------------------------------------------------------
	protected void CacheSlot()
	{
		if (m_Slot)
			return;

		IEntity owner = GetOwner();
		if (!owner)
			return;

		BaseCompartmentManagerComponent mgr = BaseCompartmentManagerComponent.Cast(owner.FindComponent(BaseCompartmentManagerComponent));
		if (!mgr)
			return;

		ref array<BaseCompartmentSlot> slots = new array<BaseCompartmentSlot>();
		mgr.GetCompartments(slots);

		int i;
		for (i = 0; i < slots.Count(); i++)
		{
			BaseCompartmentSlot slot = slots[i];
			if (!slot)
				continue;
			if (slot.GetType() != ECompartmentType.CARGO)
				continue;

			m_Slot = slot;
			return;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void AdoptOccupant()
	{
		CacheSlot();
		if (!m_Slot)
			return;

		IEntity occ = m_Slot.GetOccupant();
		if (!occ)
			return;

		ChimeraCharacter pawn = ChimeraCharacter.Cast(occ);
		if (!pawn)
			return;

		m_pJumper = pawn;
		CharacterControllerComponent ctrl = pawn.GetCharacterController();
		if (ctrl)
			m_Input = ctrl.GetInputContext();

		if (IsLocalOccupant() && !m_bHudOwned)
		{
			if (!MHJ_JumpHud.IsOpen())
				MHJ_JumpHud.Open();
			m_bHudOwned = true;
			WidenLookLimits();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool HasOccupant()
	{
		CacheSlot();
		if (m_Slot && m_Slot.IsOccupied())
			return true;
		if (!m_pJumper)
			return false;
		if (OccupantIsInCanopy(m_pJumper))
			return true;

		CharacterCommandHandlerComponent handler = m_pJumper.GetCommandHandler();
		if (handler && handler.GetCommandVehicle())
			return true;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsAuthority()
	{
		return Replication.IsServer();
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsOwner()
	{
		if (IsLocalOccupant())
			return true;
		return Replication.IsServer();
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsLocalOccupant()
	{
		if (!m_pJumper)
			return false;

		IEntity localChar = SCR_PlayerController.GetLocalControlledEntity();
		if (!localChar)
			return false;
		return localChar == m_pJumper;
	}

	//------------------------------------------------------------------------------------------------
	protected void ReadInput()
	{
		m_fTurnInput = 0;
		m_fPitchInput = 0;

		InputManager im = GetGame().GetInputManager();
		if (im)
		{
			m_fTurnInput = im.GetActionValue("CharacterRight");
			m_fPitchInput = im.GetActionValue("CharacterForward");

			float steer = im.GetActionValue("VehicleSteer");
			float throttle = im.GetActionValue("VehicleThrottle");
			if (m_fTurnInput > -0.12 && m_fTurnInput < 0.12)
				m_fTurnInput = steer;
			if (m_fPitchInput > -0.12 && m_fPitchInput < 0.12)
				m_fPitchInput = throttle;
		}

		if (m_Input)
		{
			float moveSpeed;
			vector locDir;
			m_Input.GetMovement(moveSpeed, locDir);
			if (m_fTurnInput > -0.12 && m_fTurnInput < 0.12)
				m_fTurnInput = locDir[0];
			if (m_fPitchInput > -0.12 && m_fPitchInput < 0.12)
				m_fPitchInput = locDir[2];
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
	protected void ApplyCanopy(float pDt, float agl)
	{
		m_fOpenT = m_fOpenT + pDt;
		float infl = MHJ_FlightAero.CanopyInflation(m_fOpenT);

		float pitchTarget = m_fPitchInput;
		if (agl < MHJ_Constants.FLARE_AGL)
		{
			if (m_fPitchInput < 0.12)
			{
				float span = MHJ_Constants.FLARE_AGL - MHJ_Constants.FLARE_END_AGL;
				if (span < 0.5)
					span = 0.5;
				float autoFlare = 1 - (agl - MHJ_Constants.FLARE_END_AGL) / span;
				if (autoFlare < 0)
					autoFlare = 0;
				if (autoFlare > 1)
					autoFlare = 1;
				float want = -autoFlare;
				if (want < pitchTarget)
					pitchTarget = want;
			}
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

		float wantPath = MHJ_FlightAero.CanopyDemandPathDeg(dive, brake);
		m_fPathDeg = Math.SmoothCD(m_fPathDeg, wantPath, m_fPathDegV, MHJ_Constants.CANOPY_PATH_INERTIA, MHJ_Constants.CANOPY_PATH_IN_MAX, pDt);

		float infl01 = infl;
		if (infl01 > 1)
			infl01 = 1;
		if (infl01 < 0)
			infl01 = 0;

		m_fHeading = WrapHeading(m_fHeading + m_fTurnFilt * MHJ_Constants.CANOPY_TURN_RATE * infl01 * pDt);

		float wantBank = m_fTurnFilt * MHJ_Constants.CANOPY_BANK_MAX * infl01;
		wantBank = wantBank + Math.PerlinNoise(m_fSimT * 0.55, 2.2) * 3.5 * infl01;
		m_fBank = Math.SmoothCD(m_fBank, wantBank, m_fBankV, MHJ_Constants.CANOPY_BANK_INERTIA, 80, pDt);

		float wantPitch = MHJ_Constants.CANOPY_PITCH_CRUISE;
		wantPitch = wantPitch + dive * (MHJ_Constants.CANOPY_PITCH_DIVE - MHJ_Constants.CANOPY_PITCH_CRUISE);
		wantPitch = wantPitch + brake * (MHJ_Constants.CANOPY_PITCH_FLARE - MHJ_Constants.CANOPY_PITCH_CRUISE);
		if (m_fOpenT < MHJ_Constants.CANOPY_OPEN_TIME)
		{
			float snatch = infl;
			if (snatch > 1)
				snatch = 1;
			wantPitch = wantPitch + snatch * MHJ_Constants.CANOPY_PITCH_SNATCH;
		}

		wantPitch = ClampDivePitch(wantPitch);
		m_fPitch = Math.SmoothCD(m_fPitch, wantPitch, m_fPitchV, MHJ_Constants.CANOPY_PITCH_INERTIA, 120, pDt);
		ClampAppliedDivePitch();

		if (!m_bSnatchFired)
		{
			if (infl > 0.45)
			{
				m_bSnatchFired = true;
				if (IsLocalOccupant())
					SCR_CameraShakeManagerComponent.AddCameraShake(0.85, 1.35, 0.04, 0.28, 0.55);
			}
		}

		SteerCanopyTowardHeading(pDt, infl01);

		float tas = m_fAirspeed;
		if (infl01 < 0.92)
			m_sFlightMode = "OPENING";
		else if (tas < MHJ_Constants.CANOPY_STALL_SPEED + 0.6)
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

		float h = pDt / steps;
		int i;
		for (i = 0; i < steps; i++)
			IntegrateCanopyStep(h);

		float spd = m_vWorldVel.Length();
		if (spd > MHJ_Constants.CANOPY_MAX_TAS)
			m_vWorldVel = m_vWorldVel * (MHJ_Constants.CANOPY_MAX_TAS / spd);

		SyncSpeedFromWorld();
	}

	//------------------------------------------------------------------------------------------------
	protected void IntegrateCanopyStep(float dt)
	{
		IEntity owner = GetOwner();
		float msl = 0;
		if (owner)
			msl = owner.GetOrigin()[1];

		float density = MHJ_FlightAero.DensityRatio(msl);
		float infl = MHJ_FlightAero.CanopyInflation(m_fOpenT);
		float infl01 = infl;
		if (infl01 > 1)
			infl01 = 1;
		if (infl01 < 0)
			infl01 = 0;

		m_vWorldVel[1] = m_vWorldVel[1] - MHJ_Constants.GRAVITY * dt;

		float dive = 0;
		float brake = 0;
		if (m_fPitchInputFilt > 0)
			dive = m_fPitchInputFilt;
		if (m_fPitchInputFilt < 0)
			brake = -m_fPitchInputFilt;

		AlignCanopyPath(dt, infl01, dive, brake);

		vector airVel = m_vWorldVel - m_vWind;
		float tas = airVel.Length();
		if (tas < 0.35)
			return;

		vector airDir = airVel;
		airDir.Normalize();

		float bodyDrag = MHJ_Constants.GRAVITY * density * tas * tas / (MHJ_Constants.FREEFALL_TERMINAL * MHJ_Constants.FREEFALL_TERMINAL);
		float bodyBlend = 1 - infl01;
		m_vWorldVel = m_vWorldVel - airDir * (bodyDrag * bodyBlend * dt);

		if (infl < 0.04)
			return;

		float cl;
		float cd;
		MHJ_FlightAero.CanopyCoeff(dive, brake, tas, PathDiveAmount(), cl, cd);
		cl = cl * infl;
		cd = cd * infl;

		float area = MHJ_Constants.BODY_AREA + infl01 * infl01 * (MHJ_Constants.CANOPY_AREA - MHJ_Constants.BODY_AREA);
		float q = 0.5 * MHJ_Constants.AIR_DENSITY_SL * density * tas * tas;
		float liftF = q * cl * area;
		float dragF = q * cd * area;
		float invMass = 1 / MHJ_Constants.MASS;

		vector right = HeadingRight();
		vector liftDir = SCR_Math3D.Cross(airDir, right);
		float liftLen = liftDir.Length();
		if (liftLen < 0.05)
		{
			liftDir = Vector(0, 1, 0) - airDir * vector.Dot(Vector(0, 1, 0), airDir);
			liftLen = liftDir.Length();
			if (liftLen < 0.05)
				liftDir = HeadingForward();
			else
				liftDir.Normalize();
		}
		else
		{
			liftDir.Normalize();
			if (vector.Dot(liftDir, Vector(0, 1, 0)) < 0)
				liftDir = liftDir * -1;
		}

		liftDir = RotateAroundAxis(liftDir, airDir, m_fBank * Math.DEG2RAD);

		m_vWorldVel = m_vWorldVel + liftDir * (liftF * invMass * dt);
		m_vWorldVel = m_vWorldVel - airDir * (dragF * invMass * dt);

		if (m_vWorldVel[1] > MHJ_Constants.CANOPY_MAX_CLIMB)
			m_vWorldVel[1] = MHJ_Constants.CANOPY_MAX_CLIMB;

		SnapAirToPath();
	}

	//------------------------------------------------------------------------------------------------
	protected void AlignCanopyPath(float dt, float infl01, float dive, float brake)
	{
		if (infl01 < 0.04)
			return;

		vector airVel = m_vWorldVel - m_vWind;
		float tas = airVel.Length();
		if (tas < 0.35)
			return;

		vector airDir = airVel;
		airDir.Normalize();

		vector nose = CanopyPathNose(m_fPathDeg);
		float align = MHJ_FlightAero.CanopyPathAlign(dive, brake);
		float k = align * infl01 * dt;
		if (k > 1)
			k = 1;

		vector mixed = airDir + (nose - airDir) * k;
		float mLen = mixed.Length();
		if (mLen < 0.001)
			return;

		mixed.Normalize();
		mixed = ClampPathDir(mixed);
		float pathDive = PathDiveAmount();
		tas = tas + MHJ_Constants.CANOPY_DIVE_ACCEL * pathDive * infl01 * dt;
		tas = MHJ_FlightAero.CanopyCoastBleed(tas, dive, brake, pathDive, dt);
		m_vWorldVel = mixed * tas + m_vWind;
	}

	//------------------------------------------------------------------------------------------------
	protected void SteerCanopyTowardHeading(float pDt, float infl01)
	{
		if (infl01 < 0.2)
			return;

		vector air = m_vWorldVel - m_vWind;
		vector horiz = air;
		horiz[1] = 0;
		float hs = horiz.Length();

		vector nose = HeadingForward();
		float blend = MHJ_Constants.CANOPY_HEADING_LERP * pDt;
		if (blend > 1)
			blend = 1;
		blend = blend * infl01;

		vector newHoriz = vector.Zero;
		if (hs < 0.5)
		{
			float kick = MHJ_Constants.CANOPY_CRUISE_TAS * 0.4 * infl01;
			newHoriz[0] = nose[0] * kick;
			newHoriz[2] = nose[2] * kick;
		}
		else
		{
			vector curDir = horiz;
			curDir.Normalize();
			vector blended = curDir + (nose - curDir) * blend;
			float bLen = blended.Length();
			if (bLen < 0.001)
				return;
			blended.Normalize();
			newHoriz[0] = blended[0] * hs;
			newHoriz[2] = blended[2] * hs;
		}

		m_vWorldVel[0] = m_vWind[0] + newHoriz[0];
		m_vWorldVel[2] = m_vWind[2] + newHoriz[2];
		SyncSpeedFromWorld();
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyYawOnly()
	{
		ApplyYawWithDt(0);
	}

	//------------------------------------------------------------------------------------------------
	protected void HoldAtJumper()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;
		if (!m_pJumper)
			return;

		vector ypr;
		ypr[0] = m_fHeading * Math.RAD2DEG;
		ypr[1] = 0;
		ypr[2] = 0;

		vector mat[4];
		Math3D.AnglesToMatrix(ypr, mat);
		mat[3] = m_pJumper.GetOrigin();
		owner.SetWorldTransform(mat);
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyYawWithDt(float pDt)
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;

		vector ypr;
		ypr[0] = m_fHeading * Math.RAD2DEG;
		ypr[1] = 0;
		ypr[2] = 0;

		vector mat[4];
		Math3D.AnglesToMatrix(ypr, mat);

		vector origin = owner.GetOrigin();
		origin[0] = origin[0] + m_vWorldVel[0] * pDt;
		origin[1] = origin[1] + m_vWorldVel[1] * pDt;
		origin[2] = origin[2] + m_vWorldVel[2] * pDt;
		mat[3] = origin;
		owner.SetWorldTransform(mat);
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyAttitudeOnly()
	{
		ApplyWorldAttitudeWithDt(0);
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyWorldAttitudeWithDt(float pDt)
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;

		vector ypr;
		ypr[0] = m_fHeading * Math.RAD2DEG;
		ypr[1] = ClampDivePitch(m_fPitch);
		ypr[2] = m_fBank;

		vector mat[4];
		Math3D.AnglesToMatrix(ypr, mat);

		vector origin = owner.GetOrigin();
		origin[0] = origin[0] + m_vWorldVel[0] * pDt;
		origin[1] = origin[1] + m_vWorldVel[1] * pDt;
		origin[2] = origin[2] + m_vWorldVel[2] * pDt;
		mat[3] = origin;
		owner.SetWorldTransform(mat);
	}

	//------------------------------------------------------------------------------------------------
	protected void LogCanopyDiag(float pDt)
	{
		m_fDiagT = m_fDiagT + pDt;
		if (m_fDiagT < 0.5)
			return;
		m_fDiagT = 0;

		IEntity owner = GetOwner();
		string dump = "Canopy diag mode=" + m_sFlightMode;
		dump = dump + " local=" + MHJ_Log.Flag(IsLocalOccupant());
		dump = dump + " srv=" + MHJ_Log.Flag(Replication.IsServer());
		dump = dump + " auth=" + MHJ_Log.Flag(IsAuthority());
		dump = dump + " hdg=" + MHJ_Log.Deg(m_fHeading * Math.RAD2DEG);
		dump = dump + " pitch=" + MHJ_Log.Deg(m_fPitch);
		dump = dump + " bank=" + MHJ_Log.Deg(m_fBank);
		dump = dump + " path=" + MHJ_Log.Deg(m_fPathDeg);
		dump = dump + " turn=" + MHJ_Log.Deg(m_fTurnInput);
		dump = dump + " pitchIn=" + MHJ_Log.Deg(m_fPitchInput);

		if (owner)
			dump = dump + " veh=" + MHJ_Log.Ypr(owner.GetYawPitchRoll());

		if (m_pJumper)
		{
			dump = dump + " jumper=" + MHJ_Log.Ypr(m_pJumper.GetYawPitchRoll());
			dump = dump + " inVeh=" + MHJ_Log.Flag(m_pJumper.IsInVehicle());

			CompartmentAccessComponent access = m_pJumper.GetCompartmentAccessComponent();
			if (access)
			{
				dump = dump + " inSeat=" + MHJ_Log.Flag(access.IsInCompartment());
				dump = dump + " gettingIn=" + MHJ_Log.Flag(access.IsGettingIn());
			}

			CharacterAnimationComponent anim = m_pJumper.GetAnimationComponent();
			if (anim)
			{
				dump = dump + " linked=" + MHJ_Log.Flag(anim.PhysicsIsLinked());
				dump = dump + " falling=" + MHJ_Log.Flag(anim.PhysicsIsFalling());
				ref CharacterMovementState st = new CharacterMovementState();
				anim.GetMovementState(st);
				dump = dump + " cmd=" + st.m_CommandTypeId.ToString();
				dump = dump + " stance=" + st.m_iStanceIdx.ToString();
			}

			IEntity parent = m_pJumper.GetParent();
			dump = dump + " parent=" + MHJ_Log.Flag(parent != null);

			CharacterControllerComponent ctrl = m_pJumper.GetCharacterController();
			if (ctrl)
			{
				dump = dump + " forceFL=" + MHJ_Log.Flag(ctrl.IsFreeLookForced());
				CharacterInputContext input = ctrl.GetInputContext();
				if (input)
					dump = dump + " ctrlHdg=" + MHJ_Log.Deg(input.GetHeadingAngle() * Math.RAD2DEG);
			}

			SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(m_pJumper.FindComponent(SCR_CharacterCommandHandlerComponent));
			if (handler)
			{
				dump = dump + " vehCmd=" + MHJ_Log.Flag(handler.GetCommandVehicle() != null);
				dump = dump + " moveCmd=" + MHJ_Log.Flag(handler.GetCommandMove() != null);
				dump = dump + " inCanopy=" + MHJ_Log.Flag(handler.MHJ_IsInCanopySeat());
			}
		}

		MHJ_Log.Info(dump);
	}

	//------------------------------------------------------------------------------------------------
	protected vector CanopyPathNose(float pathDeg)
	{
		if (pathDeg < -MHJ_Constants.DIVE_ANGLE_MAX)
			pathDeg = -MHJ_Constants.DIVE_ANGLE_MAX;

		float pr = pathDeg * Math.DEG2RAD;
		float c = Math.Cos(pr);
		float s = Math.Sin(pr);
		vector n = vector.Zero;
		n[0] = Math.Sin(m_fHeading) * c;
		n[1] = s;
		n[2] = Math.Cos(m_fHeading) * c;
		return n;
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
	protected vector ClampPathDir(vector dir)
	{
		vector horiz = dir;
		horiz[1] = 0;
		float hs = horiz.Length();
		float pathDeg = Math.Atan2(dir[1], hs) * Math.RAD2DEG;
		if (pathDeg >= -MHJ_Constants.DIVE_ANGLE_MAX)
			return dir;

		return CanopyPathNose(-MHJ_Constants.DIVE_ANGLE_MAX);
	}

	//------------------------------------------------------------------------------------------------
	protected void ClampCanopyAirDive()
	{
		vector air = m_vWorldVel - m_vWind;
		float tas = air.Length();
		if (tas < 0.35)
			return;

		vector clamped = ClampPathDir(air);
		float cLen = clamped.Length();
		if (cLen < 0.001)
			return;

		clamped.Normalize();
		m_vWorldVel = clamped * tas + m_vWind;
	}

	//------------------------------------------------------------------------------------------------
	//! Keep TAS from aero, but fly along the commanded path so a 85° visual dive is an 85° fall.
	protected void SnapAirToPath()
	{
		vector air = m_vWorldVel - m_vWind;
		float tas = air.Length();
		if (tas < 0.35)
			return;

		vector nose = CanopyPathNose(m_fPathDeg);
		nose = ClampPathDir(nose);
		float nLen = nose.Length();
		if (nLen < 0.001)
			return;

		nose.Normalize();
		m_vWorldVel = nose * tas + m_vWind;
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
	protected void ClampAppliedDivePitch()
	{
		if (m_fPitch > MHJ_Constants.DIVE_ANGLE_MAX)
		{
			m_fPitch = MHJ_Constants.DIVE_ANGLE_MAX;
			if (m_fPitchV > 0)
				m_fPitchV = 0;
		}
		else if (m_fPitch < -MHJ_Constants.DIVE_ANGLE_MAX)
		{
			m_fPitch = -MHJ_Constants.DIVE_ANGLE_MAX;
			if (m_fPitchV < 0)
				m_fPitchV = 0;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void SyncSpeedFromWorld()
	{
		m_fVelY = m_vWorldVel[1];
		vector air = m_vWorldVel - m_vWind;
		m_fAirspeed = air.Length();
	}

	//------------------------------------------------------------------------------------------------
	protected vector RotateAroundAxis(vector v, vector axis, float angle)
	{
		float axisLen = axis.Length();
		if (axisLen < 0.001)
			return v;
		if (angle > -0.0002 && angle < 0.0002)
			return v;

		float q[4];
		SCR_Math3D.QuatAngleAxis(angle, axis, q);
		return SCR_Math3D.QuatMultiply(q, v);
	}

	//------------------------------------------------------------------------------------------------
	protected vector HeadingForward()
	{
		vector v = vector.Zero;
		v[0] = Math.Sin(m_fHeading);
		v[2] = Math.Cos(m_fHeading);
		return v;
	}

	//------------------------------------------------------------------------------------------------
	protected vector HeadingRight()
	{
		vector v = vector.Zero;
		v[0] = Math.Cos(m_fHeading);
		v[2] = -Math.Sin(m_fHeading);
		return v;
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
	protected float GetAgl()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return 0;

		vector pos = owner.GetOrigin();
		if (m_pJumper)
			pos = m_pJumper.GetOrigin();

		BaseWorld world = owner.GetWorld();
		if (!world)
			world = GetGame().GetWorld();
		if (!world)
			return pos[1];

		float terrainAgl = SCR_TerrainHelper.GetHeightAboveTerrain(pos, world, true);
		if (terrainAgl > 90)
			return terrainAgl;

		if (!m_AglTrace)
			m_AglTrace = new TraceParam();
		m_AglTrace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		m_AglTrace.Exclude = owner;
		float surfaceY = SCR_TerrainHelper.GetTerrainY(pos, world, true, m_AglTrace);
		return pos[1] - surfaceY;
	}

	//------------------------------------------------------------------------------------------------
	protected void PushHud(float agl)
	{
		if (!m_bHudOwned)
			return;

		vector fwd = HeadingForward();
		vector windH = m_vWind;
		windH[1] = 0;
		float wSpeed = windH.Length();
		float headingDot = 0;
		if (wSpeed > 0.05)
			headingDot = vector.Dot(fwd, windH) / wSpeed;

		string windRel = MHJ_FlightAero.WindRelativeLabel(wSpeed, headingDot);
		MHJ_JumpHud.SetState(MHJ_EHaloPhase.CANOPY, agl, m_fOpenAltitude, m_fAirspeed, m_fVelY, m_sFlightMode, wSpeed, windRel);
	}

	//------------------------------------------------------------------------------------------------
	protected void WidenLookLimits()
	{
		if (m_bLookWidened)
			return;
		if (!m_pJumper)
			return;

		m_pHeadAim = CharacterHeadAimingComponent.Cast(m_pJumper.FindComponent(CharacterHeadAimingComponent));
		if (!m_pHeadAim)
			return;

		m_pHeadAim.SetLimitAnglesOverride(-89, 89, -360, 360);
		m_bLookWidened = true;
		KeepCanopyFreeLook();
	}

	//------------------------------------------------------------------------------------------------
	protected void KeepCanopyFreeLook()
	{
		if (!m_pJumper)
			return;

		CharacterControllerComponent ctrl = m_pJumper.GetCharacterController();
		if (ctrl)
			ctrl.SetForcedFreeLook(true);
	}

	//------------------------------------------------------------------------------------------------
	protected void RestoreLookLimits()
	{
		if (m_pJumper)
		{
			CharacterControllerComponent ctrl = m_pJumper.GetCharacterController();
			if (ctrl)
			{
				ctrl.SetForcedFreeLook(false);
				ctrl.SetFreeLook(false, false, false);
			}

			if (!m_pHeadAim)
				m_pHeadAim = CharacterHeadAimingComponent.Cast(m_pJumper.FindComponent(CharacterHeadAimingComponent));
		}

		if (m_pHeadAim)
			m_pHeadAim.ResetLimitAnglesOverride();
		m_pHeadAim = null;
		m_bLookWidened = false;
	}
}
