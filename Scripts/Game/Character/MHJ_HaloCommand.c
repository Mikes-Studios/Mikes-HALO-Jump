//------------------------------------------------------------------------------------------------
//! Scripted locomotion: belly-flight, then ram-air canopy in the wind.
//! Freefall keeps the capsule upright, plays the prone fall loop, and yaws with A/D.
//! Motion is world-space aero applied in the pawn's current local frame.
//------------------------------------------------------------------------------------------------
class MHJ_HaloCommand : ScriptedCommand
{
	protected ChimeraCharacter m_pCharacter;
	protected CharacterControllerComponent m_pController;
	protected CharacterInputContext m_Input;
	protected CharacterAnimationComponent m_AnimationComponent;
	protected CharacterHeadAimingComponent m_pHeadAim;

	protected MHJ_EHaloPhase m_ePhase;
	protected float m_fOpenAltitude;
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
	protected float m_fNetTurn;
	protected float m_fNetPitch;
	protected float m_fListenTurn;
	protected float m_fListenPitch;
	protected bool m_bInputListening;
	protected float m_fPathDeg;
	protected float m_fPathDegV;
	protected float m_fBank;
	protected float m_fBankV;
	protected float m_fBankApplied;
	protected float m_fPitch;
	protected float m_fPitchV;
	protected float m_fPitchApplied;
	protected float m_fHeadingApplied;
	protected float m_fSwing;
	protected float m_fSwingV;
	protected float m_fSwingPrev;
	protected float m_fOpenT;
	protected float m_fSimT;
	protected bool m_bNeedFinish;
	protected bool m_bHudOwned;
	protected bool m_bSnatchFired;
	protected string m_sFlightMode;
	protected vector m_vWorldVel;
	protected vector m_vWind;
	protected float m_fLandDownSpeed;
	protected float m_fLandHorizSpeed;
	protected float m_fLandHeading;
	protected ref TraceParam m_AglTrace;
	protected IEntity m_pCanopy;
	protected bool m_bCanopyAnimActive;
	protected bool m_bCanopyHandOff;
	protected bool m_bAwaitingSeat;
	protected bool m_bOpenQueued;
	protected float m_fSeatWait;
	protected TAnimGraphCommand m_CmdVehicleGetIn;
	protected TAnimGraphCommand m_CmdVehicleStartControl;
	protected TAnimGraphCommand m_CmdVehicleEndControl;
	protected TAnimGraphCommand m_CmdFall;
	protected TAnimGraphVariable m_VarGetInNoBlending;
	protected TAnimGraphVariable m_VarIsDriver;
	protected TAnimGraphVariable m_VarStance;
	protected float m_fDiagT;
	protected float m_fPrevTurnAbs;

	//------------------------------------------------------------------------------------------------
	void MHJ_HaloCommand(BaseAnimPhysComponent pAnimPhysComponent, ChimeraCharacter pCharacter, CharacterControllerComponent pController, float openAltitude)
	{
		m_pCharacter = pCharacter;
		m_pController = pController;
		m_AnimationComponent = CharacterAnimationComponent.Cast(pAnimPhysComponent);
		if (pController)
			m_Input = pController.GetInputContext();

		m_fOpenAltitude = openAltitude;
		if (m_fOpenAltitude < MHJ_Constants.OPEN_ALT_MIN)
			m_fOpenAltitude = MHJ_Constants.OPEN_ALT_MIN;

		m_CmdVehicleGetIn = -1;
		m_CmdVehicleStartControl = -1;
		m_CmdVehicleEndControl = -1;
		m_CmdFall = -1;
		m_VarGetInNoBlending = -1;
		m_VarIsDriver = -1;
		m_VarStance = -1;
		if (m_AnimationComponent)
		{
			m_CmdVehicleGetIn = m_AnimationComponent.BindCommand("CMD_Vehicle_GetIn");
			m_CmdVehicleStartControl = m_AnimationComponent.BindCommand("CMD_Vehicle_StartControl");
			m_CmdVehicleEndControl = m_AnimationComponent.BindCommand("CMD_Vehicle_EndControl");
			m_CmdFall = m_AnimationComponent.BindCommand("CMD_Locomotion_Fall");
			m_VarGetInNoBlending = m_AnimationComponent.BindVariableBool("GetInNoBlending");
			m_VarIsDriver = m_AnimationComponent.BindVariableBool("IsDriver");
			m_VarStance = m_AnimationComponent.BindVariableInt("Stance");
		}

		ResetFlightState();
	}

	//------------------------------------------------------------------------------------------------
	override void OnActivate()
	{
		if (m_AnimationComponent)
			m_AnimationComponent.PhysicsEnableGravity(false);

		ZeroPhysicsMotion();
		WidenLookLimits();
		ResetFlightState();
		if (m_pController)
		{
			m_pController.SetWeaponRaised(false);
			m_pController.SetWeaponADS(false);
			m_pController.SetForcedFreeLook(true);
		}

		KeepMovementContextAlive();
		BindFreefallInput();
		HoldFreefallPose();

		if (IsLocalCharacter())
		{
			MHJ_JumpHud.Open();
			m_bHudOwned = true;
			SCR_HintManagerComponent.ShowCustomHint("W tracks. S slows the fall. A/D turns. Canopy opens automatically.", "HALO JUMP", 6);
		}
	}

	//------------------------------------------------------------------------------------------------
	override void OnDeactivate()
	{
		UnbindFreefallInput();
		GetGame().GetCallqueue().Remove(OpenCanopy);
		GetGame().GetCallqueue().Remove(FallbackCanopyVisual);

		if (m_bCanopyHandOff)
		{
			ReleaseFreefallPose();
			m_pCanopy = null;
			m_bHudOwned = false;
			m_pHeadAim = null;
			return;
		}

		if (m_bAwaitingSeat)
		{
			ReleaseFreefallPose();
			m_bCanopyHandOff = true;
			m_pCanopy = null;
			m_bHudOwned = false;
			m_pHeadAim = null;
			return;
		}

		if (MHJ_CanopyFlight.OccupantIsInCanopy(m_pCharacter))
		{
			ReleaseFreefallPose();
			m_bCanopyHandOff = true;
			m_pCanopy = null;
			m_bHudOwned = false;
			m_pHeadAim = null;
			return;
		}

		ReleaseFreefallPose();
		if (m_AnimationComponent)
			m_AnimationComponent.PhysicsEnableGravity(true);

		RestoreLookLimits();
		ZeroPhysicsMotion();
		StopCanopyAnim();
		DestroyCanopyVisual();

		if (m_bHudOwned)
		{
			MHJ_JumpHud.Close();
			m_bHudOwned = false;
		}
	}

	//------------------------------------------------------------------------------------------------
	override void PreAnimUpdate(float pDt)
	{
		if (m_bNeedFinish)
		{
			StopCanopyAnim();
			return;
		}

		if (m_bCanopyHandOff)
			return;
		if (m_bAwaitingSeat)
			return;
		if (m_ePhase == MHJ_EHaloPhase.FREEFALL)
		{
			if (m_bOpenQueued)
				ReleaseFreefallPose();
			else
				HoldFreefallPose();
			return;
		}
		if (m_ePhase == MHJ_EHaloPhase.CANOPY)
			TickCanopyAnim();
	}

	//------------------------------------------------------------------------------------------------
	//! Idle stand is the scripted-command default. Keep the prone fall loop alive so
	//! the mesh is belly-to-earth instead of an upright T-pose.
	protected void HoldFreefallPose()
	{
		KeepMovementContextAlive();

		if (m_VarStance != -1)
			PreAnim_SetInt(m_VarStance, ECharacterStance.PRONE);
		if (m_CmdFall != -1)
			PreAnim_CallCommand(m_CmdFall, 1, 0);

		if (m_pController)
		{
			m_pController.SetWeaponRaised(false);
			m_pController.SetWeaponADS(false);
			m_pController.SetForcedFreeLook(true);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Drop the prone fall loop. Do not ForceStance STAND here — that leaves a
	//! standing fall in mid-air before GetIn can sit the jumper.
	void ReleaseFreefallPose()
	{
		if (m_VarStance != -1)
		{
			PreAnim_SetInt(m_VarStance, ECharacterStance.STAND);
			if (m_AnimationComponent)
				m_AnimationComponent.SetVariableInt(m_VarStance, ECharacterStance.STAND);
		}
		if (m_CmdFall != -1)
		{
			PreAnim_CallCommand(m_CmdFall, -1, 0);
			if (m_AnimationComponent)
				m_AnimationComponent.CallCommand(m_CmdFall, -1, 0);
		}
		if (m_AnimationComponent)
			m_AnimationComponent.PhysicsSetStance(ECharacterStance.STAND);
		if (m_Input)
			m_Input.SetLean(0);
	}

	//------------------------------------------------------------------------------------------------
	//! Sitting graph is local to the entity, so idle / weapon / foot-IK cannot stand the mesh back up.
	protected void StartCanopyAnim()
	{
		if (m_bCanopyAnimActive)
			return;

		ReleaseFreefallPose();

		if (m_VarGetInNoBlending != -1)
			PreAnim_SetBool(m_VarGetInNoBlending, true);
		if (m_VarIsDriver != -1)
			PreAnim_SetBool(m_VarIsDriver, true);

		PreAnim_SetAttachment(MHJ_Constants.CANOPY_VEHICLE_BINDING, MHJ_Constants.CANOPY_VEHICLE_AGR, MHJ_Constants.CANOPY_VEHICLE_ASI, "MasterControl");

		if (m_CmdVehicleGetIn != -1)
			PreAnim_CallCommand(m_CmdVehicleGetIn, 1, 0);
		if (m_CmdVehicleStartControl != -1)
			PreAnim_CallCommand(m_CmdVehicleStartControl, 1, 0);

		m_bCanopyAnimActive = true;
		MHJ_Log.Info("Canopy sit graph MasterControl");
		SuppressStandingPose();
	}

	//------------------------------------------------------------------------------------------------
	protected void TickCanopyAnim()
	{
		if (!m_bCanopyAnimActive)
			StartCanopyAnim();

		if (m_VarIsDriver != -1)
			PreAnim_SetBool(m_VarIsDriver, true);
		if (m_CmdVehicleStartControl != -1)
			PreAnim_CallCommand(m_CmdVehicleStartControl, 1, 0);
	}

	//------------------------------------------------------------------------------------------------
	protected void SuppressStandingPose()
	{
		if (!m_pController)
			return;

		m_pController.SetForcedFreeLook(true);
		m_pController.SetWeaponRaised(false);
		m_pController.SetWeaponADS(false);
		m_pController.ForceStance(ECharacterStance.STAND);
		m_pController.SetMovement(0, vector.Forward);
	}

	//------------------------------------------------------------------------------------------------
	protected void StopCanopyAnim()
	{
		if (!m_bCanopyAnimActive)
			return;

		if (m_CmdVehicleEndControl != -1)
			PreAnim_CallCommand(m_CmdVehicleEndControl, 1, 0);
		PreAnim_SetAttachment(MHJ_Constants.CANOPY_VEHICLE_BINDING, string.Empty, string.Empty, string.Empty);
		if (m_VarGetInNoBlending != -1)
			PreAnim_SetBool(m_VarGetInNoBlending, false);

		m_bCanopyAnimActive = false;
		if (m_pController)
			m_pController.SetForcedFreeLook(false);
	}

	//------------------------------------------------------------------------------------------------
	override void PrePhysUpdate(float pDt)
	{
		if (m_bNeedFinish)
			return;
		if (!m_pCharacter)
		{
			m_bNeedFinish = true;
			return;
		}

		if (m_bCanopyHandOff)
			return;

		if (m_bAwaitingSeat)
		{
			TickSeatWait(pDt);
			return;
		}

		m_fSimT = m_fSimT + pDt;
		KeepMovementContextAlive();
		RefreshWind();
		ReadInput();

		float agl = GetAgl();
		if (m_ePhase == MHJ_EHaloPhase.FREEFALL)
		{
			if (agl <= m_fOpenAltitude)
				QueueOpenCanopy();
			else if (agl <= MHJ_Constants.LAND_AGL)
				QueueOpenCanopy();
		}

		if (m_bAwaitingSeat)
			return;
		if (m_bCanopyHandOff)
			return;

		if (m_ePhase == MHJ_EHaloPhase.CANOPY)
		{
			if (agl <= MHJ_Constants.LAND_AGL)
			{
				m_fLandDownSpeed = -m_fVelY;
				if (m_fLandDownSpeed < 0)
					m_fLandDownSpeed = 0;

				vector landHoriz = m_vWorldVel;
				landHoriz[1] = 0;
				m_fLandHorizSpeed = landHoriz.Length();
				m_fLandHeading = m_fHeading;

				m_ePhase = MHJ_EHaloPhase.LANDED;
				m_sFlightMode = "LANDING";
				m_bNeedFinish = true;
				SetFlagFinished(true);
				return;
			}
		}

		if (m_ePhase == MHJ_EHaloPhase.FREEFALL)
			ApplyFreefall(pDt);
		else
			ApplyCanopy(pDt, agl);

		ApplyHeadingYawDelta();
		ApplyFlightTranslation(pDt);
		PushHud(agl);
	}

	//------------------------------------------------------------------------------------------------
	override bool PostPhysUpdate(float pDt)
	{
		if (m_bNeedFinish)
			return false;

		ApplyBodyAttitude();
		LogFlightDiag(pDt);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	MHJ_EHaloPhase GetPhase()
	{
		return m_ePhase;
	}

	//------------------------------------------------------------------------------------------------
	float GetLandDownSpeed()
	{
		return m_fLandDownSpeed;
	}

	//------------------------------------------------------------------------------------------------
	float GetLandHorizSpeed()
	{
		return m_fLandHorizSpeed;
	}

	//------------------------------------------------------------------------------------------------
	float GetLandHeading()
	{
		return m_fLandHeading;
	}

	//------------------------------------------------------------------------------------------------
	protected void ResetFlightState()
	{
		m_ePhase = MHJ_EHaloPhase.FREEFALL;
		m_fAirspeed = 0;
		m_fVelY = 0;
		m_fOpenT = 0;
		m_fSimT = 0;
		m_bNeedFinish = false;
		m_bHudOwned = false;
		m_bSnatchFired = false;
		m_bCanopyAnimActive = false;
		m_bCanopyHandOff = false;
		m_bAwaitingSeat = false;
		m_bOpenQueued = false;
		m_fSeatWait = 0;
		m_sFlightMode = "EXIT";
		m_fHeading = ReadBodyHeading();
		m_fHeadingApplied = m_fHeading;
		m_fHeadingV = 0;
		m_fBank = 0;
		m_fBankV = 0;
		m_fBankApplied = 0;
		m_fPitch = 0;
		m_fPitchV = 0;
		m_fPitchApplied = 0;
		m_fSwing = 0;
		m_fSwingV = 0;
		m_fSwingPrev = 0;
		m_fTurnFilt = 0;
		m_fTurnFiltV = 0;
		m_fPitchInputFilt = 0;
		m_fPitchInputFiltV = 0;
		m_fNetTurn = 0;
		m_fNetPitch = 0;
		m_fListenTurn = 0;
		m_fListenPitch = 0;
		m_fPathDeg = MHJ_Constants.CANOPY_PATH_CRUISE;
		m_fPathDegV = 0;
		m_vWorldVel = vector.Zero;
		m_vWind = vector.Zero;
		m_fLandDownSpeed = 0;
		m_fLandHorizSpeed = 0;
		m_fLandHeading = 0;
		if (!m_AglTrace)
			m_AglTrace = new TraceParam();
	}

	//------------------------------------------------------------------------------------------------
	//! SpawnEntityPrefab cannot run from PrePhys (physics thread). Queue to the main thread.
	protected void QueueOpenCanopy()
	{
		if (m_bOpenQueued)
			return;
		if (m_ePhase != MHJ_EHaloPhase.FREEFALL)
			return;

		m_bOpenQueued = true;
		GetGame().GetCallqueue().CallLater(OpenCanopy, 0, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void OpenCanopy()
	{
		if (m_bNeedFinish)
			return;
		if (!m_pCharacter)
			return;
		if (m_ePhase == MHJ_EHaloPhase.CANOPY)
			return;
		if (m_ePhase == MHJ_EHaloPhase.LANDED)
			return;

		m_ePhase = MHJ_EHaloPhase.CANOPY;
		m_fOpenT = 0;
		m_bSnatchFired = false;
		m_fHeadingV = 0;
		m_sFlightMode = "OPENING";

		ReleaseFreefallPose();

		SyncHeadingToBody();
		SnapAirToHeading();
		m_fHeadingApplied = m_fHeading;
		m_fPitchV = 0;
		m_fBankV = 0;
		m_fTurnFilt = 0;
		m_fTurnFiltV = 0;
		m_fPitchInputFilt = 0;
		m_fPitchInputFiltV = 0;
		m_fPathDegV = 0;
		m_fSwing = 0;
		m_fSwingV = 0;
		m_fSwingPrev = 0;
		InitPathFromAir();
		m_fPitch = MHJ_Constants.CANOPY_PITCH_CRUISE;
		m_fBank = 0;
		MHJ_Log.Info("Canopy open path=" + MHJ_Log.Deg(m_fPathDeg) + " pitch=" + MHJ_Log.Deg(m_fPitch) + " bank=" + MHJ_Log.Deg(m_fBank) + " hdg=" + MHJ_Log.Deg(m_fHeading * Math.RAD2DEG));

		m_bOpenQueued = false;
		if (TryStartSeatHandoff())
		{
			if (IsLocalCharacter())
				SCR_HintManagerComponent.ShowCustomHint("W dives. S flares — dive first to swoop. Land into the wind.", "CANOPY", 7);
			return;
		}

		SpawnCanopyVisual();
		if (IsLocalCharacter())
			SCR_HintManagerComponent.ShowCustomHint("W dives. S flares — dive first to swoop. Land into the wind.", "CANOPY", 7);
	}

	//------------------------------------------------------------------------------------------------
	bool IsCanopyHandOff()
	{
		return m_bCanopyHandOff;
	}

	//------------------------------------------------------------------------------------------------
	bool IsAwaitingSeat()
	{
		return m_bAwaitingSeat;
	}

	//------------------------------------------------------------------------------------------------
	protected bool TryStartSeatHandoff()
	{
		if (!m_pCharacter)
			return false;

		if (!Replication.IsServer())
		{
			MHJ_Log.Info("Canopy seat handoff: client finish command");
			FinishSeatHandoff();
			return true;
		}

		IEntity canopy = SpawnCanopyWorld();
		if (!canopy)
		{
			MHJ_Log.Warning("Canopy seat handoff: spawn failed");
			return false;
		}

		MHJ_CanopyFlight flight = MHJ_CanopyFlight.Cast(canopy.FindComponent(MHJ_CanopyFlight));
		if (!flight)
		{
			MHJ_Log.Warning("Canopy seat handoff: missing MHJ_CanopyFlight");
			SCR_EntityHelper.DeleteEntityAndChildren(canopy);
			return false;
		}

		if (!flight.BeginFlight(m_pCharacter, m_vWorldVel, m_vWind, m_fHeading, m_fPitch, m_fBank, m_fPathDeg, m_fSimT, m_bHudOwned, m_fOpenAltitude))
		{
			MHJ_Log.Warning("Canopy seat handoff: BeginFlight failed");
			SCR_EntityHelper.DeleteEntityAndChildren(canopy);
			return false;
		}

		MHJ_Log.Info("Canopy seat handoff: finishing HALO command");
		m_pCanopy = canopy;
		FinishSeatHandoff();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void TickSeatWait(float pDt)
	{
		m_fSeatWait = m_fSeatWait + pDt;
		if (MHJ_CanopyFlight.OccupantIsInCanopy(m_pCharacter))
		{
			FinishSeatHandoff();
			return;
		}

		if (m_fSeatWait < 1.5)
			return;

		if (!Replication.IsServer())
		{
			m_bAwaitingSeat = false;
			return;
		}

		MHJ_Log.Warning("Canopy seat GetIn timed out; falling back to parented visual");
		m_bAwaitingSeat = false;
		GetGame().GetCallqueue().CallLater(FallbackCanopyVisual, 0, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void FallbackCanopyVisual()
	{
		if (m_bNeedFinish)
			return;
		if (m_bCanopyHandOff)
			return;

		AbortPendingCanopy();
		SpawnCanopyVisual();
	}

	//------------------------------------------------------------------------------------------------
	protected void FinishSeatHandoff()
	{
		m_bCanopyHandOff = true;
		m_bAwaitingSeat = false;
		m_bHudOwned = false;
		m_pCanopy = null;
		m_bNeedFinish = true;
		SetFlagFinished(true);
	}

	//------------------------------------------------------------------------------------------------
	protected void AbortPendingCanopy()
	{
		if (!m_pCanopy)
			return;

		IEntity canopy = m_pCanopy;
		m_pCanopy = null;
		if (Replication.IsServer())
			SCR_EntityHelper.DeleteEntityAndChildren(canopy);
	}

	//------------------------------------------------------------------------------------------------
	protected IEntity SpawnCanopyWorld()
	{
		if (!m_pCharacter)
			return null;

		Resource res = Resource.Load(MHJ_Constants.CANOPY_PREFAB);
		if (!res)
			return null;
		if (!res.IsValid())
			return null;

		ref EntitySpawnParams sp = new EntitySpawnParams();
		sp.TransformMode = ETransformMode.WORLD;

		vector ypr;
		ypr[0] = m_fHeading * Math.RAD2DEG;
		ypr[1] = 0;
		ypr[2] = 0;
		Math3D.AnglesToMatrix(ypr, sp.Transform);
		sp.Transform[3] = m_pCharacter.GetOrigin();

		IEntity canopy = GetGame().SpawnEntityPrefab(res, m_pCharacter.GetWorld(), sp);
		if (!canopy)
			MHJ_Log.Warning("Canopy prefab failed to spawn");
		return canopy;
	}

	//------------------------------------------------------------------------------------------------
	protected void SpawnCanopyVisual()
	{
		if (m_pCanopy)
			return;
		if (!m_pCharacter)
			return;
		if (!Replication.IsServer())
			return;

		Resource res = Resource.Load(MHJ_Constants.CANOPY_PREFAB);
		if (!res)
			return;
		if (!res.IsValid())
			return;

		EntitySpawnParams sp = new EntitySpawnParams();
		sp.TransformMode = ETransformMode.LOCAL;
		sp.Parent = m_pCharacter;
		vector localPos;
		localPos[0] = 0;
		localPos[1] = MHJ_Constants.CANOPY_MESH_OFFSET_Y;
		localPos[2] = 0;
		sp.Transform[3] = localPos;

		m_pCanopy = GetGame().SpawnEntityPrefab(res, m_pCharacter.GetWorld(), sp);
		if (!m_pCanopy)
			MHJ_Log.Warning("Canopy prefab failed to spawn");
	}

	//------------------------------------------------------------------------------------------------
	protected void DestroyCanopyVisual()
	{
		if (!m_pCanopy)
			return;

		IEntity canopy = m_pCanopy;
		m_pCanopy = null;
		if (Replication.IsServer())
			SCR_EntityHelper.DeleteEntityAndChildren(canopy);
	}

	//------------------------------------------------------------------------------------------------
	protected void ReadInput()
	{
		if (!IsLocalCharacter())
		{
			m_fTurnInput = m_fNetTurn;
			m_fPitchInput = m_fNetPitch;
			return;
		}

		m_fTurnInput = m_fListenTurn;
		m_fPitchInput = m_fListenPitch;

		InputManager im = GetGame().GetInputManager();
		if (im)
		{
			FillInputIfIdle(im.GetActionValue("CharacterRight"), im.GetActionValue("CharacterForward"));
		}

		if (m_Input)
		{
			float moveSpeed;
			vector locDir;
			m_Input.GetMovement(moveSpeed, locDir);
			FillInputIfIdle(locDir[0], locDir[2]);
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

		PushSteerToServer();
	}

	//------------------------------------------------------------------------------------------------
	protected void FillInputIfIdle(float turn, float pitch)
	{
		if (m_fTurnInput > -0.12 && m_fTurnInput < 0.12)
			m_fTurnInput = turn;
		if (m_fPitchInput > -0.12 && m_fPitchInput < 0.12)
			m_fPitchInput = pitch;
	}

	//------------------------------------------------------------------------------------------------
	protected void PushSteerToServer()
	{
		if (Replication.IsServer())
			return;

		PlayerController pc = GetGame().GetPlayerController();
		SCR_PlayerController scrPc = SCR_PlayerController.Cast(pc);
		if (!scrPc)
			return;

		scrPc.MHJ_AskFreefallSteer(m_fTurnInput, m_fPitchInput);
	}

	//------------------------------------------------------------------------------------------------
	void SetSteerInput(float turn, float pitch)
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
	}

	//------------------------------------------------------------------------------------------------
	void KeepMovementContextAlive()
	{
		if (!IsLocalCharacter())
			return;

		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;

		im.ActivateContext("CharacterMovementContext", MHJ_Constants.FREEFALL_INPUT_HOLD_MS);
	}

	//------------------------------------------------------------------------------------------------
	protected void BindFreefallInput()
	{
		if (m_bInputListening)
			return;
		if (!IsLocalCharacter())
			return;

		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;

		im.AddActionListener("CharacterForward", EActionTrigger.VALUE, OnFreefallForward);
		im.AddActionListener("CharacterRight", EActionTrigger.VALUE, OnFreefallRight);
		m_bInputListening = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void UnbindFreefallInput()
	{
		if (!m_bInputListening)
			return;

		InputManager im = GetGame().GetInputManager();
		if (im)
		{
			im.RemoveActionListener("CharacterForward", EActionTrigger.VALUE, OnFreefallForward);
			im.RemoveActionListener("CharacterRight", EActionTrigger.VALUE, OnFreefallRight);
		}

		m_bInputListening = false;
		m_fListenTurn = 0;
		m_fListenPitch = 0;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnFreefallForward(float value, EActionTrigger reason)
	{
		m_fListenPitch = value;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnFreefallRight(float value, EActionTrigger reason)
	{
		m_fListenTurn = value;
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyFreefall(float pDt)
	{
		float auth = ExitAuthority();
		float turnCmd = m_fTurnInput * auth;
		float pitchCmd = m_fPitchInput * auth;

		m_fTurnFilt = Math.SmoothCD(m_fTurnFilt, turnCmd, m_fTurnFiltV, MHJ_Constants.STEER_FILTER_TIME, MHJ_Constants.STEER_FILTER_MAX, pDt);
		m_fPitchInputFilt = Math.SmoothCD(m_fPitchInputFilt, pitchCmd, m_fPitchInputFiltV, 0.14, 80, pDt);

		UpdateFlightHeading(m_fTurnFilt * MHJ_Constants.FREEFALL_TURN_RATE, pDt);
		SyncHeadingToBody();

		float wantBank = m_fTurnFilt * MHJ_Constants.FREEFALL_BANK_MAX;
		float exit = 1 - auth;
		wantBank = wantBank + Math.Sin(m_fSimT * 7.3) * 12 * exit;
		m_fBank = Math.SmoothCD(m_fBank, wantBank, m_fBankV, 0.22, 90, pDt);

		float wantPitch = MHJ_Constants.FREEFALL_PITCH_ARCH;
		if (m_fPitchInputFilt > 0.05)
			wantPitch = MHJ_Constants.FREEFALL_PITCH_TRACK * m_fPitchInputFilt;
		else if (m_fPitchInputFilt < -0.05)
			wantPitch = MHJ_Constants.FREEFALL_PITCH_SLOW * (-m_fPitchInputFilt);
		wantPitch = wantPitch * auth;
		wantPitch = wantPitch + Math.Sin(m_fSimT * 5.1 + 1.2) * 10 * exit;
		wantPitch = ClampDivePitch(wantPitch);
		m_fPitch = Math.SmoothCD(m_fPitch, wantPitch, m_fPitchV, 0.28, 80, pDt);
		ClampAppliedDivePitch();

		if (auth < 0.95)
			m_sFlightMode = "EXIT";
		else if (m_fPitchInputFilt > 0.2)
			m_sFlightMode = "TRACKING";
		else if (m_fPitchInputFilt < -0.2)
			m_sFlightMode = "SLOW FALL";
		else
			m_sFlightMode = "FREEFALL";

		if (m_Input)
			m_Input.SetLean(m_fTurnFilt);

		IntegrateAero(pDt, false);
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

		UpdateFlightHeading(m_fTurnFilt * MHJ_Constants.CANOPY_TURN_RATE * infl01, pDt);

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
				if (IsLocalCharacter())
					SCR_CameraShakeManagerComponent.AddCameraShake(0.85, 1.35, 0.04, 0.28, 0.55);
			}
		}

		IntegrateAero(pDt, true);
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
	protected void IntegrateAero(float pDt, bool canopy)
	{
		int steps = 1;
		if (pDt > 0.019)
			steps = 2;
		if (pDt > 0.033)
			steps = 3;

		float h = pDt / steps;
		int i;
		for (i = 0; i < steps; i++)
		{
			if (canopy)
				IntegrateCanopyStep(h);
			else
				IntegrateFreefallStep(h);
		}

		float spd = m_vWorldVel.Length();
		float maxSpd = MHJ_Constants.FREEFALL_MAX_SPEED;
		if (canopy)
			maxSpd = MHJ_Constants.CANOPY_MAX_TAS;
		if (spd > maxSpd)
			m_vWorldVel = m_vWorldVel * (maxSpd / spd);

		SyncSpeedFromWorld();
	}

	//------------------------------------------------------------------------------------------------
	protected void IntegrateFreefallStep(float dt)
	{
		float msl = 0;
		if (m_pCharacter)
			msl = m_pCharacter.GetOrigin()[1];

		float density = MHJ_FlightAero.DensityRatio(msl);
		m_vWorldVel[1] = m_vWorldVel[1] - MHJ_Constants.GRAVITY * dt;

		vector airVel = m_vWorldVel - m_vWind;
		float tas = airVel.Length();
		if (tas < 0.4)
			return;

		vector airDir = airVel;
		airDir.Normalize();

		float track = MHJ_Constants.FREEFALL_TRACK_BASE;
		float slow = 0;
		if (m_fPitchInputFilt > 0)
			track = MHJ_Constants.FREEFALL_TRACK_BASE + (1 - MHJ_Constants.FREEFALL_TRACK_BASE) * m_fPitchInputFilt;
		if (m_fPitchInputFilt < 0)
		{
			slow = -m_fPitchInputFilt;
			track = MHJ_Constants.FREEFALL_TRACK_BASE * (1 - slow);
		}

		float vTerm = MHJ_Constants.FREEFALL_TERMINAL;
		vTerm = vTerm + track * (MHJ_Constants.FREEFALL_TRACK_TERMINAL - MHJ_Constants.FREEFALL_TERMINAL);
		vTerm = vTerm + slow * (MHJ_Constants.FREEFALL_SLOW_TERMINAL - MHJ_Constants.FREEFALL_TERMINAL);
		if (vTerm < 20)
			vTerm = 20;

		float dragMag = MHJ_Constants.GRAVITY * density * tas * tas / (vTerm * vTerm);
		m_vWorldVel = m_vWorldVel - airDir * (dragMag * dt);

		AlignFreefallToHeading(dt);

		vector nose = HeadingForward();
		float liftIn = track - slow * 0.28;
		m_vWorldVel = m_vWorldVel + nose * (liftIn * MHJ_Constants.FREEFALL_TRACK_LIFT * dragMag * dt);

		vector right = HeadingRight();
		vector slideDir = right - airDir * vector.Dot(right, airDir);
		float slideLen = slideDir.Length();
		if (slideLen > 0.05)
		{
			slideDir.Normalize();
			m_vWorldVel = m_vWorldVel + slideDir * (-m_fTurnFilt * MHJ_Constants.FREEFALL_SLIDE * dragMag * dt);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void IntegrateCanopyStep(float dt)
	{
		float msl = 0;
		if (m_pCharacter)
			msl = m_pCharacter.GetOrigin()[1];

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

		ClampCanopyAirDive();
	}

	//------------------------------------------------------------------------------------------------
	//! Point the air vector at the SmoothCD path. TAS fill follows how far that
	//! path has tucked, so W cannot dump you onto the dive line in one frame.
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
		if (k > 0.55)
			k = 0.55;

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
	//! How far the smoothed path has tucked from cruise toward the dive line.
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
	//! Opening starts from the current fall, not from cruise, so SmoothCD does not yank the nose up.
	protected void InitPathFromAir()
	{
		vector air = m_vWorldVel - m_vWind;
		float tas = air.Length();
		if (tas < 0.35)
			return;

		vector horiz = air;
		horiz[1] = 0;
		float hs = horiz.Length();
		m_fPathDeg = Math.Atan2(air[1], hs) * Math.RAD2DEG;
		m_fPathDegV = 0;
	}

	//------------------------------------------------------------------------------------------------
	//! 90° straight down gimbal-flips look and lift. Keep a sliver of forward path.
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
	//! Horizontal airspeed follows the canopy nose so A/D changes the flight path, not just facing.
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
	//! A/D yaws the body. Horizontal air must follow that heading or the canopy
	//! opens flying a line the mesh is not facing.
	protected void AlignFreefallToHeading(float dt)
	{
		vector air = m_vWorldVel - m_vWind;
		vector horiz = air;
		horiz[1] = 0;
		float hs = horiz.Length();
		vector nose = HeadingForward();

		float turnAbs = m_fTurnFilt;
		if (turnAbs < 0)
			turnAbs = -turnAbs;

		float align = MHJ_Constants.FREEFALL_TURN_ALIGN * dt;
		align = align + turnAbs * MHJ_Constants.FREEFALL_TURN_ALIGN * dt;
		if (align > 1)
			align = 1;

		vector newHoriz = vector.Zero;
		if (hs < 0.35)
		{
			float kick = 2.5 * ExitAuthority();
			newHoriz[0] = nose[0] * kick;
			newHoriz[2] = nose[2] * kick;
		}
		else
		{
			vector curDir = horiz;
			curDir.Normalize();
			vector blended = curDir + (nose - curDir) * align;
			float bLen = blended.Length();
			if (bLen < 0.001)
				return;
			blended.Normalize();
			newHoriz[0] = blended[0] * hs;
			newHoriz[2] = blended[2] * hs;
		}

		m_vWorldVel[0] = m_vWind[0] + newHoriz[0];
		m_vWorldVel[2] = m_vWind[2] + newHoriz[2];
	}

	//------------------------------------------------------------------------------------------------
	protected void SnapAirToHeading()
	{
		vector air = m_vWorldVel - m_vWind;
		vector horiz = air;
		horiz[1] = 0;
		float hs = horiz.Length();
		vector nose = HeadingForward();

		vector newAir = nose * hs;
		newAir[1] = air[1];
		m_vWorldVel = newAir + m_vWind;
		SyncSpeedFromWorld();
	}

	//------------------------------------------------------------------------------------------------
	protected void SyncSpeedFromWorld()
	{
		m_fVelY = m_vWorldVel[1];
		vector air = m_vWorldVel - m_vWind;
		m_fAirspeed = air.Length();
	}

	//------------------------------------------------------------------------------------------------
	protected float ExitAuthority()
	{
		float auth = m_fSimT / MHJ_Constants.FREEFALL_EXIT_TIME;
		if (auth > 1)
			auth = 1;
		if (auth < 0)
			auth = 0;
		return auth;
	}

	//------------------------------------------------------------------------------------------------
	//! Body yaw is A/D in freefall, plus a little toggle assist under canopy.
	protected void UpdateFlightHeading(float yawRate, float pDt)
	{
		m_fHeading = WrapHeading(m_fHeading + yawRate * pDt);
	}

	//------------------------------------------------------------------------------------------------
	//! Look ±160 is why A/D at the stop yaws the camera the wrong way. Open it for the jump.
	protected void WidenLookLimits()
	{
		m_pHeadAim = null;
		if (!m_pCharacter)
			return;

		m_pHeadAim = CharacterHeadAimingComponent.Cast(m_pCharacter.FindComponent(CharacterHeadAimingComponent));
		if (!m_pHeadAim)
			return;

		m_pHeadAim.SetLimitAnglesOverride(-89, 89, -360, 360);
	}

	//------------------------------------------------------------------------------------------------
	protected void RestoreLookLimits()
	{
		if (m_pHeadAim)
			m_pHeadAim.ResetLimitAnglesOverride();
		m_pHeadAim = null;
	}

	//------------------------------------------------------------------------------------------------
	//! Look heading tracks the flight heading. true keeps the mouse offset when we turn.
	protected void SyncHeadingToBody()
	{
		if (!m_pCharacter)
			return;
		if (!m_pController)
			return;

		m_pController.SetHeadingAngle(m_fHeading, true);
	}

	//------------------------------------------------------------------------------------------------
	protected void LogFlightDiag(float pDt)
	{
		m_fDiagT = m_fDiagT + pDt;
		float turnAbs = m_fTurnInput;
		if (turnAbs < 0)
			turnAbs = -turnAbs;

		bool turnEdge = false;
		if (turnAbs >= 0.2 && m_fPrevTurnAbs < 0.2)
			turnEdge = true;
		m_fPrevTurnAbs = turnAbs;

		if (!turnEdge)
		{
			if (m_fDiagT < 0.5)
				return;
		}
		m_fDiagT = 0;

		string phase = "FF";
		if (m_ePhase == MHJ_EHaloPhase.CANOPY)
			phase = "CAN";
		else if (m_ePhase == MHJ_EHaloPhase.LANDED)
			phase = "LAND";

		string dump = phase + " diag mode=" + m_sFlightMode;
		dump = dump + " local=" + MHJ_Log.Flag(IsLocalCharacter());
		dump = dump + " srv=" + MHJ_Log.Flag(Replication.IsServer());
		dump = dump + " turn=" + MHJ_Log.Deg(m_fTurnInput);
		dump = dump + " listen=" + MHJ_Log.Deg(m_fListenTurn);
		dump = dump + " filt=" + MHJ_Log.Deg(m_fTurnFilt);
		dump = dump + " pitchIn=" + MHJ_Log.Deg(m_fPitchInput);
		dump = dump + " hdg=" + MHJ_Log.Deg(m_fHeading * Math.RAD2DEG);
		dump = dump + " want=" + MHJ_Log.Deg(m_fHeading * Math.RAD2DEG) + "," + MHJ_Log.Deg(GetVisualPitchDeg()) + "," + MHJ_Log.Deg(GetVisualBankDeg());
		dump = dump + " path=" + MHJ_Log.Deg(m_fPathDeg);
		dump = dump + " handoff=" + MHJ_Log.Flag(m_bCanopyHandOff);
		dump = dump + " waitSeat=" + MHJ_Log.Flag(m_bAwaitingSeat);

		if (m_pCharacter)
		{
			dump = dump + " ent=" + MHJ_Log.Ypr(m_pCharacter.GetYawPitchRoll());
			if (m_AnimationComponent)
			{
				dump = dump + " linked=" + MHJ_Log.Flag(m_AnimationComponent.PhysicsIsLinked());
				dump = dump + " falling=" + MHJ_Log.Flag(m_AnimationComponent.PhysicsIsFalling());
				ref CharacterMovementState st = new CharacterMovementState();
				m_AnimationComponent.GetMovementState(st);
				dump = dump + " cmd=" + st.m_CommandTypeId.ToString();
				dump = dump + " stance=" + st.m_iStanceIdx.ToString();
			}
		}

		if (m_pController)
		{
			dump = dump + " forceFL=" + MHJ_Log.Flag(m_pController.IsFreeLookForced());
			CharacterInputContext input = m_pController.GetInputContext();
			if (input)
				dump = dump + " inHdg=" + MHJ_Log.Deg(input.GetHeadingAngle() * Math.RAD2DEG);
		}

		InputManager im = GetGame().GetInputManager();
		if (im)
			dump = dump + " ADaxis=" + MHJ_Log.Deg(im.GetActionValue("CharacterRight"));

		MHJ_Log.Info(dump);
	}

	//------------------------------------------------------------------------------------------------
	//! Yaw the capsule toward flight heading before physics. PostPhys_LockRotation
	//! kept the spawn yaw; this is a per-step delta so A/D can actually turn the mesh.
	protected void ApplyHeadingYawDelta()
	{
		if (!m_pCharacter)
			return;

		vector currentYpr = m_pCharacter.GetYawPitchRoll();
		float currentYaw = WrapHeading(currentYpr[0] * Math.DEG2RAD);
		float delta = m_fHeading - currentYaw;
		float twoPi = Math.PI * 2;
		while (delta > Math.PI)
			delta = delta - twoPi;
		while (delta < -Math.PI)
			delta = delta + twoPi;

		vector dYpr;
		dYpr[0] = delta * Math.RAD2DEG;
		dYpr[1] = 0;
		dYpr[2] = 0;
		vector mat[3];
		Math3D.AnglesToMatrix(dYpr, mat);
		float q[4];
		Math3D.MatrixToQuat(mat, q);
		PrePhys_SetRotation(q);
	}

	//------------------------------------------------------------------------------------------------
	//! Freefall: yaw/bank/tuck after physics. Do not lock — that froze spawn yaw.
	//! Translation stays in heading space so bank cannot eat the fall.
	protected void ApplyBodyAttitude()
	{
		SyncHeadingToBody();

		vector ypr;
		ypr[0] = m_fHeading * Math.RAD2DEG;
		ypr[1] = GetVisualPitchDeg();
		ypr[2] = GetVisualBankDeg();

		vector mat[3];
		Math3D.AnglesToMatrix(ypr, mat);
		float q[4];
		Math3D.MatrixToQuat(mat, q);
		PostPhys_SetRotation(q);

		if (m_pCharacter)
			m_pCharacter.SetYawPitchRoll(ypr);

		if (m_ePhase == MHJ_EHaloPhase.CANOPY)
			AttachCanopyToHarness(ypr, mat);

		m_fHeadingApplied = m_fHeading;
		m_fPitchApplied = m_fPitch;
		m_fBankApplied = m_fBank;
	}

	//------------------------------------------------------------------------------------------------
	//! World-rotate the chute around the character origin. Parent + SetYawPitchRoll
	//! spun it around the mesh pivot, so the wing flew away from the jumper.
	protected void AttachCanopyToHarness(vector ypr, vector rot[3])
	{
		if (!m_pCanopy)
			return;
		if (!m_pCharacter)
			return;

		vector localOff;
		localOff[0] = 0;
		localOff[1] = MHJ_Constants.CANOPY_MESH_OFFSET_Y;
		localOff[2] = 0;

		vector worldMat[4];
		worldMat[0] = rot[0];
		worldMat[1] = rot[1];
		worldMat[2] = rot[2];
		worldMat[3] = m_pCharacter.GetOrigin() + localOff.Multiply3(rot);
		m_pCanopy.SetWorldTransform(worldMat);
	}

	//------------------------------------------------------------------------------------------------
	//! Enfusion YPR: negative pitch is head down, positive bank is right-wing down.
	//! Prone fall is already belly-to-earth; m_fPitch is only the extra tuck / arch.
	protected float GetVisualPitchDeg()
	{
		return ClampDivePitch(m_fPitch);
	}

	//------------------------------------------------------------------------------------------------
	protected float GetVisualBankDeg()
	{
		return m_fBank;
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
	//! World velocity in yaw space. Visual bank/pitch is PostPhys-only so it cannot
	//! turn the fall into a sideways slide.
	protected void ApplyFlightTranslation(float pDt)
	{
		vector worldDelta = vector.Zero;
		worldDelta[0] = m_vWorldVel[0] * pDt;
		worldDelta[1] = m_vWorldVel[1] * pDt;
		worldDelta[2] = m_vWorldVel[2] * pDt;

		vector ypr = vector.Zero;
		ypr[0] = m_fHeading * Math.RAD2DEG;
		vector yawMat[4];
		Math3D.AnglesToMatrix(ypr, yawMat);
		vector trans = worldDelta.InvMultiply3(yawMat);

		if (m_ePhase == MHJ_EHaloPhase.CANOPY)
		{
			float bankRad = m_fBank * Math.DEG2RAD;
			float wantSwing = Math.Sin(bankRad) * MHJ_Constants.CANOPY_HANG_LENGTH;
			m_fSwing = Math.SmoothCD(m_fSwing, wantSwing, m_fSwingV, MHJ_Constants.CANOPY_HANG_INERTIA, 40, pDt);
			trans[0] = trans[0] + (m_fSwing - m_fSwingPrev);
			m_fSwingPrev = m_fSwing;
		}

		PrePhys_SetTranslation(trans);
	}

	//------------------------------------------------------------------------------------------------
	protected void RefreshWind()
	{
		float msl = 0;
		if (m_pCharacter)
			msl = m_pCharacter.GetOrigin()[1];
		m_vWind = MHJ_FlightAero.WindWorld(msl, m_fSimT);
	}

	//------------------------------------------------------------------------------------------------
	protected float ReadBodyHeading()
	{
		if (m_pCharacter)
		{
			vector ypr = m_pCharacter.GetYawPitchRoll();
			return WrapHeading(ypr[0] * Math.DEG2RAD);
		}
		if (m_Input)
			return m_Input.GetHeadingAngle();
		return 0;
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
	protected void ZeroPhysicsMotion()
	{
		if (!m_pCharacter)
			return;
		Physics phys = m_pCharacter.GetPhysics();
		if (!phys)
			return;
		phys.SetVelocity(vector.Zero);
		phys.SetAngularVelocity(vector.Zero);
	}

	//------------------------------------------------------------------------------------------------
	protected float GetAgl()
	{
		if (!m_pCharacter)
			return 0;

		vector pos = m_pCharacter.GetOrigin();
		BaseWorld world = m_pCharacter.GetWorld();
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
		m_AglTrace.Exclude = m_pCharacter;
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
		MHJ_JumpHud.SetState(m_ePhase, agl, m_fOpenAltitude, m_fAirspeed, m_fVelY, m_sFlightMode, wSpeed, windRel);
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsLocalCharacter()
	{
		IEntity localChar = SCR_PlayerController.GetLocalControlledEntity();
		if (!localChar)
			return false;
		return localChar == m_pCharacter;
	}
}
