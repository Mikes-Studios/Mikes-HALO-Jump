//------------------------------------------------------------------------------------------------
//! Starts and holds the HALO scripted command. Keeps FALL from taking over in the air.
//! After a soft landing with leftover ground speed, injects a short walk/run into Move.
//------------------------------------------------------------------------------------------------
modded class SCR_CharacterCommandHandlerComponent
{
	protected ref MHJ_HaloCommand m_MHJ_HaloCommand;
	protected IEntity m_pMHJ_BoardCanopy;
	protected BaseCompartmentSlot m_pMHJ_BoardSlot;
	protected bool m_bMHJ_HoldCanopySeat;
	protected int m_iMHJ_LeaveTries;
	protected bool m_bMHJ_LandSettled;
	protected float m_fMHJ_PendingLandDown;
	protected float m_fMHJ_PendingLandHoriz;
	protected float m_fMHJ_PendingLandHeading;
	protected float m_fMHJ_LandCarryRemain;
	protected float m_fMHJ_LandCarryAnalog;
	protected float m_fMHJ_LandCarryAnalogStart;
	protected float m_fMHJ_LandCarryHeading;
	protected bool m_bMHJ_DriveLookHeading;
	protected bool m_bMHJ_RecoverLookAfterCanopy;
	protected float m_fMHJ_LookRecoverRemain;
	protected bool m_bMHJ_LookLogged;
	protected bool m_bMHJ_SitPoseLogged;
	protected float m_fMHJ_BlockFallRemain;
	protected bool m_bMHJ_SuppressLandFall;
	protected ref TraceParam m_MHJ_LandTrace;
	protected bool m_bMHJ_LandRestoreLogged;

	//------------------------------------------------------------------------------------------------
	void MHJ_StartHaloJump(float openAltitude)
	{
		if (MHJ_IsHaloJumping())
			return;
		if (!m_CharacterAnimComp)
			return;
		if (!m_OwnerEntity)
			return;

		CharacterControllerComponent controller = GetControllerComponent();
		if (!controller)
			controller = m_CharacterControllerComp;
		if (!controller)
			return;

		m_MHJ_HaloCommand = new MHJ_HaloCommand(m_CharacterAnimComp, m_OwnerEntity, controller, openAltitude);
		m_CharacterAnimComp.SetCurrentCommand(m_MHJ_HaloCommand);
		m_MHJ_HaloCommand.KeepMovementContextAlive();
	}

	//------------------------------------------------------------------------------------------------
	bool MHJ_IsHaloJumping()
	{
		if (MHJ_IsBoardingCanopy())
			return true;
		if (MHJ_IsInCanopySeat())
			return true;
		if (m_MHJ_HaloCommand && !m_MHJ_HaloCommand.IsFlagFinished())
			return true;
		if (!m_CharacterAnimComp)
			return false;

		MHJ_HaloCommand halo = MHJ_HaloCommand.Cast(m_CharacterAnimComp.GetCommandScripted());
		if (halo && !halo.IsFlagFinished())
			return true;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	bool MHJ_IsBoardingCanopy()
	{
		if (!m_pMHJ_BoardCanopy)
			return false;
		if (MHJ_IsInCanopySeat())
			return false;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_BeginCanopyBoard(notnull IEntity canopy, BaseCompartmentSlot slot)
	{
		if (m_MHJ_HaloCommand)
			m_MHJ_HaloCommand.ReleaseFreefallPose();

		if (m_CharacterAnimComp)
			m_CharacterAnimComp.PhysicsEnableGravity(false);

		m_pMHJ_BoardCanopy = canopy;
		m_pMHJ_BoardSlot = slot;
		m_bMHJ_HoldCanopySeat = true;
		m_bMHJ_SitPoseLogged = false;
		MHJ_Log.Info("Canopy board requested slot=" + MHJ_Log.Flag(slot != null));
	}

	//------------------------------------------------------------------------------------------------
	//! Gravity off and fall graph cancelled so GetIn can sit. Do not attach a
	//! character sit graph here — that locked the pawn and killed dives.
	void MHJ_KeepCanopySitPose()
	{
		if (m_CharacterAnimComp)
			m_CharacterAnimComp.PhysicsEnableGravity(false);

		if (!GetCommandVehicle())
		{
			if (m_MHJ_HaloCommand)
				m_MHJ_HaloCommand.ReleaseFreefallPose();

			if (m_CharacterAnimComp)
			{
				TAnimGraphCommand cmdFall = m_CharacterAnimComp.BindCommand("CMD_Locomotion_Fall");
				if (cmdFall != -1)
					m_CharacterAnimComp.CallCommand(cmdFall, -1, 0);
			}
		}

		if (GetCommandFall())
		{
			if (m_pMHJ_BoardSlot)
				MHJ_StartCanopyVehicleCommand();
		}

		if (m_bMHJ_SitPoseLogged)
			return;

		m_bMHJ_SitPoseLogged = true;
		string dump = "Canopy sit pose";
		ChimeraCharacter ch = ChimeraCharacter.Cast(m_OwnerEntity);
		if (ch)
		{
			dump = dump + " ypr=" + MHJ_Log.Ypr(ch.GetYawPitchRoll());
			dump = dump + " inVeh=" + MHJ_Log.Flag(ch.IsInVehicle());
		}
		if (m_CharacterAnimComp)
		{
			ref CharacterMovementState st = new CharacterMovementState();
			m_CharacterAnimComp.GetMovementState(st);
			dump = dump + " cmd=" + st.m_CommandTypeId.ToString();
			dump = dump + " stance=" + st.m_iStanceIdx.ToString();
			dump = dump + " linked=" + MHJ_Log.Flag(m_CharacterAnimComp.PhysicsIsLinked());
			dump = dump + " falling=" + MHJ_Log.Flag(m_CharacterAnimComp.PhysicsIsFalling());
		}
		MHJ_Log.Info(dump);
	}

	//------------------------------------------------------------------------------------------------
	protected bool MHJ_ShouldHoldCanopySeat()
	{
		if (!m_bMHJ_HoldCanopySeat)
			return false;
		if (m_pMHJ_BoardCanopy)
			return true;
		if (MHJ_IsInCanopySeat())
			return true;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_EnsureCanopyGetIn()
	{
		if (!m_pMHJ_BoardCanopy)
			return;
		if (!m_pMHJ_BoardSlot)
			return;

		ChimeraCharacter ch = ChimeraCharacter.Cast(m_OwnerEntity);
		if (!ch)
			return;

		CompartmentAccessComponent access = ch.GetCompartmentAccessComponent();
		if (!access)
			return;
		if (access.IsInCompartment())
			return;
		if (access.IsGettingIn())
			return;

		access.GetInVehicle(m_pMHJ_BoardCanopy, m_pMHJ_BoardSlot, true, 0, ECloseDoorAfterActions.INVALID, true);
	}

	//------------------------------------------------------------------------------------------------
	protected bool MHJ_StartCanopyVehicleCommand()
	{
		if (!m_pMHJ_BoardSlot)
		{
			MHJ_Log.Warning("Canopy StartCommand_Vehicle: no slot");
			return false;
		}

		if (GetCommandVehicle())
			return true;

		if (m_CharacterAnimComp)
		{
			m_CharacterAnimComp.PhysicsEnableGravity(false);
			m_CharacterAnimComp.PhysicsSetStance(ECharacterStance.STAND);
			TAnimGraphCommand cmdFall = m_CharacterAnimComp.BindCommand("CMD_Locomotion_Fall");
			if (cmdFall != -1)
				m_CharacterAnimComp.CallCommand(cmdFall, -1, 0);
		}

		CharacterCommandVehicle veh = StartCommand_Vehicle(m_pMHJ_BoardSlot);
		if (!veh)
		{
			MHJ_Log.Warning("Canopy StartCommand_Vehicle returned null");
			return false;
		}

		MHJ_Log.Info("Canopy StartCommand_Vehicle ok");
		return true;
	}

	//------------------------------------------------------------------------------------------------
	bool MHJ_IsInCanopySeat()
	{
		if (!m_OwnerEntity)
			return false;
		if (MHJ_CanopyFlight.OccupantIsInCanopy(m_OwnerEntity))
			return true;
		if (GetCommandVehicle() && m_pMHJ_BoardCanopy)
			return true;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_FinishCanopyLanding(float downSpeed, float horizSpeed, float landHeading)
	{
		m_bMHJ_HoldCanopySeat = false;
		m_pMHJ_BoardCanopy = null;
		m_pMHJ_BoardSlot = null;
		m_iMHJ_LeaveTries = 0;
		m_bMHJ_LandSettled = false;
		m_bMHJ_SuppressLandFall = true;
		m_fMHJ_BlockFallRemain = 5.0;
		m_bMHJ_LandRestoreLogged = false;
		m_bMHJ_DriveLookHeading = true;
		m_fMHJ_PendingLandDown = downSpeed;
		m_fMHJ_PendingLandHoriz = horizSpeed;
		m_fMHJ_PendingLandHeading = landHeading;

		MHJ_DetachFromCanopyParent();
		MHJ_ForceLeaveCanopySeat();
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_ForceLeaveCanopySeat()
	{
		ChimeraCharacter ch = ChimeraCharacter.Cast(m_OwnerEntity);
		CompartmentAccessComponent access;
		if (ch)
			access = ch.GetCompartmentAccessComponent();

		bool stillIn = false;
		if (access && access.IsInCompartment())
			stillIn = true;

		if (stillIn)
			MHJ_AskOwnerGetOut(access, ch);

		stillIn = false;
		if (access && access.IsInCompartment())
			stillIn = true;

		MHJ_Log.Info("Canopy leave try=" + m_iMHJ_LeaveTries.ToString() + " stillIn=" + MHJ_Log.Flag(stillIn) + " veh=" + MHJ_Log.Flag(GetCommandVehicle() != null) + " move=" + MHJ_Log.Flag(GetCommandMove() != null));

		if (stillIn)
		{
			m_iMHJ_LeaveTries = m_iMHJ_LeaveTries + 1;
			if (m_iMHJ_LeaveTries < 40)
				GetGame().GetCallqueue().CallLater(MHJ_ForceLeaveCanopySeat, 50, false);
			return;
		}

		if (access)
		{
			if (!access.IsInCompartment())
			{
				if (access.IsGettingOut())
					access.InterruptVehicleActionQueue(true, true, true);
			}
		}

		MHJ_PlaceOnGround();
		MHJ_ExitFallIntoMove();

		if (m_bMHJ_LandSettled)
			return;

		m_bMHJ_LandSettled = true;
		m_bMHJ_DriveLookHeading = true;
		m_bMHJ_RecoverLookAfterCanopy = true;
		m_fMHJ_LookRecoverRemain = 2.5;
		m_bMHJ_LookLogged = false;
		MHJ_DetachFromCanopyParent();
		MHJ_PlaceOnGround();
		MHJ_RestoreLocomotionAnim();
		MHJ_RestoreCharacterLook();
		GetGame().GetCallqueue().CallLater(MHJ_RestoreLocomotionAnim, 100, false);
		GetGame().GetCallqueue().CallLater(MHJ_RestoreCharacterLook, 100, false);
		GetGame().GetCallqueue().CallLater(MHJ_RestoreCharacterLook, 300, false);
		GetGame().GetCallqueue().CallLater(MHJ_RestoreCharacterLook, 800, false);
		GetGame().GetCallqueue().CallLater(MHJ_RestoreCharacterLook, 1600, false);

		CharacterControllerComponent controller = GetControllerComponent();
		if (!controller)
			controller = m_CharacterControllerComp;
		if (controller)
			controller.ForceStance(ECharacterStance.STAND);

		MHJ_ExitFallIntoMove();
		MHJ_ApplyLandingImpact(m_fMHJ_PendingLandDown);
		MHJ_BeginLandingCarry(m_fMHJ_PendingLandDown, m_fMHJ_PendingLandHoriz, m_fMHJ_PendingLandHeading);
		GetGame().GetCallqueue().CallLater(MHJ_LogLandRestoreCheck, 400, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_RestoreLocomotionAnim()
	{
		ChimeraCharacter ch = ChimeraCharacter.Cast(m_OwnerEntity);
		if (ch)
		{
			CharacterAnimGraphComponent graph = CharacterAnimGraphComponent.Cast(ch.FindComponent(CharacterAnimGraphComponent));
			if (graph)
				graph.RemoveAttachment(MHJ_Constants.CANOPY_VEHICLE_BINDING);
		}

		MHJ_ExitFallIntoMove();
		MHJ_LogLandRestore("anim");
	}

	//------------------------------------------------------------------------------------------------
	//! Vehicle GetOut leaves the locomotion graph in FALL. PhysicsIsFalling can
	//! already be false after a ground snap, so Default never starts a Fall
	//! command to Land(). Own the fall clip, Land() it, then Move.
	protected void MHJ_ExitFallIntoMove()
	{
		MHJ_PlaceOnGround();

		if (GetCommandVehicle())
			StartCommand_Move();

		CharacterCommandFall fallCmd = GetCommandFall();
		if (!fallCmd)
			fallCmd = StartCommand_Fall(0);
		if (fallCmd)
		{
			if (!fallCmd.IsLanding())
				fallCmd.Land(ELandType.LANDTYPE_NONE, 0);
		}

		StartCommand_Move();

		if (m_CharacterAnimComp)
		{
			m_CharacterAnimComp.PhysicsEnableGravity(true);
			m_CharacterAnimComp.PhysicsSetStance(ECharacterStance.STAND);
			TAnimGraphCommand cmdEnd = m_CharacterAnimComp.BindCommand("CMD_Vehicle_EndControl");
			if (cmdEnd != -1)
				m_CharacterAnimComp.CallCommand(cmdEnd, 1, 0);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_TryLandFallCommand()
	{
		CharacterCommandFall fallCmd = GetCommandFall();
		if (!fallCmd)
			return;
		if (fallCmd.IsLanding())
			return;

		fallCmd.Land(ELandType.LANDTYPE_NONE, 0);
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_FillGroundedTransform(notnull IEntity ent, out vector mat[4])
	{
		ent.GetTransform(mat);
		vector ypr = Math3D.MatrixToAngles(mat);
		ypr[1] = 0;
		ypr[2] = 0;
		Math3D.AnglesToMatrix(ypr, mat);

		vector pos = ent.GetOrigin();
		BaseWorld world = ent.GetWorld();
		if (!world)
			world = GetGame().GetWorld();
		if (world)
		{
			if (!m_MHJ_LandTrace)
				m_MHJ_LandTrace = new TraceParam();
			m_MHJ_LandTrace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
			m_MHJ_LandTrace.Exclude = ent;
			pos[1] = SCR_TerrainHelper.GetTerrainY(pos, world, true, m_MHJ_LandTrace);
		}
		mat[3] = pos;
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_PlaceOnGround()
	{
		if (!m_OwnerEntity)
			return;

		vector mat[4];
		MHJ_FillGroundedTransform(m_OwnerEntity, mat);
		m_OwnerEntity.SetTransform(mat);

		Physics phys = m_OwnerEntity.GetPhysics();
		if (phys)
		{
			vector vel = phys.GetVelocity();
			vel[1] = 0;
			phys.SetVelocity(vel);
			phys.SetAngularVelocity(vector.Zero);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_LogLandRestore(string where)
	{
		if (m_bMHJ_LandRestoreLogged)
			return;
		if (!m_CharacterAnimComp)
			return;
		if (m_CharacterAnimComp.PhysicsIsFalling())
			return;
		if (GetCommandFall())
			return;

		m_bMHJ_LandRestoreLogged = true;
		ref CharacterMovementState st = new CharacterMovementState();
		m_CharacterAnimComp.GetMovementState(st);
		MHJ_Log.Info("Canopy land restore " + where + " falling=0 cmd=" + st.m_CommandTypeId.ToString() + " stance=" + st.m_iStanceIdx.ToString() + " veh=" + MHJ_Log.Flag(GetCommandVehicle() != null) + " move=" + MHJ_Log.Flag(GetCommandMove() != null));
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_LogLandRestoreCheck()
	{
		if (!m_CharacterAnimComp)
			return;

		ref CharacterMovementState st = new CharacterMovementState();
		m_CharacterAnimComp.GetMovementState(st);
		CharacterCommandFall fallCmd = GetCommandFall();
		string landed = "0";
		if (fallCmd && fallCmd.PhysicsLanded())
			landed = "1";
		MHJ_Log.Info("Canopy land check falling=" + MHJ_Log.Flag(m_CharacterAnimComp.PhysicsIsFalling()) + " cmd=" + st.m_CommandTypeId.ToString() + " stance=" + st.m_iStanceIdx.ToString() + " fallCmd=" + MHJ_Log.Flag(fallCmd != null) + " landed=" + landed + " veh=" + MHJ_Log.Flag(GetCommandVehicle() != null) + " move=" + MHJ_Log.Flag(GetCommandMove() != null) + " suppress=" + MHJ_Log.Flag(m_bMHJ_SuppressLandFall));
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_DetachFromCanopyParent()
	{
		if (!m_OwnerEntity)
			return;

		IEntity root = m_OwnerEntity.GetRootParent();
		if (!root)
			return;
		if (root == m_OwnerEntity)
			return;
		if (!root.FindComponent(MHJ_CanopyFlight))
			return;

		IEntity parent = m_OwnerEntity.GetParent();
		if (!parent)
			return;

		parent.RemoveChild(m_OwnerEntity, true);
		MHJ_Log.Info("Canopy look: unparented from canopy");
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_RestoreCharacterLook()
	{
		MHJ_DetachFromCanopyParent();

		CharacterControllerComponent controller = GetControllerComponent();
		if (!controller)
			controller = m_CharacterControllerComp;
		if (controller)
		{
			controller.SetForcedFreeLook(false);
			controller.SetFreeLook(false, false, false);

			CharacterInputContext input = controller.GetInputContext();
			if (input)
			{
				input.SetTransferFreeaimAfterFreelookRequest(true);
				input.SetFreelook(false);

				float heading = input.GetHeadingAngle();
				vector aim = input.GetAimingAngles();
				heading = MHJ_WrapHeading(heading + aim[0]);

				input.SetHeadingAngle(heading);
				controller.SetHeadingAngle(heading, true);
				input.SetAimingAngles(vector.Zero);
				input.SetLookAtAngles(vector.Zero);
			}
		}

		InputManager im = GetGame().GetInputManager();
		if (im)
		{
			im.ResetContext("CharacterFreelookMouseContext");
			im.ResetContext("CharacterFreelookGamepadContext");
			im.ActivateContext("CharacterGeneralContext");
			im.ActivateContext("CharacterLookContext");
			im.ActivateContext("CharacterMovementContext");
		}

		if (!m_OwnerEntity)
			return;

		CharacterHeadAimingComponent headAim = CharacterHeadAimingComponent.Cast(m_OwnerEntity.FindComponent(CharacterHeadAimingComponent));
		if (headAim)
		{
			headAim.ResetLimitAnglesOverride();
			headAim.SetAimingRotation(vector.Zero);
			headAim.SetAimingRotationWanted(vector.Zero);
		}

		SCR_CharacterCameraHandlerComponent cam = SCR_CharacterCameraHandlerComponent.Cast(m_OwnerEntity.FindComponent(SCR_CharacterCameraHandlerComponent));
		if (cam && controller)
			cam.SetThirdPerson(controller.IsInThirdPersonView());

		if (!m_bMHJ_LookLogged)
		{
			m_bMHJ_LookLogged = true;
			MHJ_LogLookState("restore");
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_LogLookState(string tag)
	{
		ChimeraCharacter ch = ChimeraCharacter.Cast(m_OwnerEntity);
		if (!ch)
			return;

		string dump = "Canopy look " + tag;
		dump = dump + " inVeh=" + MHJ_Log.Flag(ch.IsInVehicle());

		CompartmentAccessComponent access = ch.GetCompartmentAccessComponent();
		if (access)
			dump = dump + " inSeat=" + MHJ_Log.Flag(access.IsInCompartment()) + " gettingOut=" + MHJ_Log.Flag(access.IsGettingOut());

		CharacterAnimationComponent anim = ch.GetAnimationComponent();
		if (anim)
			dump = dump + " linked=" + MHJ_Log.Flag(anim.PhysicsIsLinked());

		IEntity parent = ch.GetParent();
		dump = dump + " parent=" + MHJ_Log.Flag(parent != null);

		CharacterControllerComponent controller = GetControllerComponent();
		if (!controller)
			controller = m_CharacterControllerComp;
		if (controller)
		{
			dump = dump + " forceFL=" + MHJ_Log.Flag(controller.IsFreeLookForced());
			dump = dump + " free=" + MHJ_Log.Flag(controller.IsFreeLookEnabled());
			CharacterInputContext input = controller.GetInputContext();
			if (input)
				dump = dump + " camFL=" + MHJ_Log.Flag(input.CameraIsFreeLook());
		}

		CharacterHeadAimingComponent headAim = CharacterHeadAimingComponent.Cast(ch.FindComponent(CharacterHeadAimingComponent));
		if (headAim)
		{
			vector lookDeg = headAim.GetLookAngles();
			dump = dump + " lookYaw=" + lookDeg[0].ToString();
		}

		InputManager im = GetGame().GetInputManager();
		if (im)
			dump = dump + " flCtx=" + MHJ_Log.Flag(im.IsContextActive("CharacterFreelookMouseContext"));

		SCR_CharacterCameraHandlerComponent cam = SCR_CharacterCameraHandlerComponent.Cast(ch.FindComponent(SCR_CharacterCameraHandlerComponent));
		if (cam)
		{
			ScriptedCameraItem cur = cam.GetCurrentCamera();
			dump = dump + " vehCam=" + MHJ_Log.Flag(CharacterCamera3rdPersonVehicle.Cast(cur) != null || CharacterCamera1stPersonVehicle.Cast(cur) != null);
		}

		MHJ_Log.Info(dump);
	}

	//------------------------------------------------------------------------------------------------
	protected float MHJ_WrapHeading(float heading)
	{
		float twoPi = Math.PI * 2;
		while (heading >= twoPi)
			heading = heading - twoPi;
		while (heading < 0)
			heading = heading + twoPi;
		return heading;
	}

	//------------------------------------------------------------------------------------------------
	protected bool MHJ_LookNeedsManualHeading()
	{
		ChimeraCharacter ch = ChimeraCharacter.Cast(m_OwnerEntity);
		if (!ch)
			return false;

		if (ch.IsInVehicle())
		{
			CompartmentAccessComponent access = ch.GetCompartmentAccessComponent();
			if (!access)
				return true;
			if (!access.IsInCompartment())
				return true;
		}

		if (GetCommandLadder())
			return false;

		CharacterAnimationComponent anim = ch.GetAnimationComponent();
		if (anim && anim.PhysicsIsLinked())
			return true;

		CharacterControllerComponent controller = GetControllerComponent();
		if (!controller)
			controller = m_CharacterControllerComp;
		if (controller && controller.IsFreeLookForced())
			return true;
		if (controller)
		{
			CharacterInputContext lookInput = controller.GetInputContext();
			if (lookInput && lookInput.CameraIsFreeLook())
			{
				if (!controller.GetFreeLookInput())
					return true;
			}
		}
		if (controller && controller.IsFreeLookEnabled())
		{
			if (!controller.GetFreeLookInput())
				return true;
		}

		InputManager im = GetGame().GetInputManager();
		if (im && im.IsContextActive("CharacterFreelookMouseContext"))
		{
			if (controller && !controller.GetFreeLookInput())
				return true;
		}

		SCR_CharacterCameraHandlerComponent cam = SCR_CharacterCameraHandlerComponent.Cast(m_OwnerEntity.FindComponent(SCR_CharacterCameraHandlerComponent));
		if (cam)
		{
			ScriptedCameraItem cur = cam.GetCurrentCamera();
			if (CharacterCamera3rdPersonVehicle.Cast(cur))
				return true;
			if (CharacterCamera1stPersonVehicle.Cast(cur))
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_ClearForcedFreeLook()
	{
		CharacterControllerComponent controller = GetControllerComponent();
		if (!controller)
			controller = m_CharacterControllerComp;
		if (controller)
		{
			controller.SetForcedFreeLook(false);
			controller.SetFreeLook(false, false, false);
			CharacterInputContext input = controller.GetInputContext();
			if (input)
				input.SetFreelook(false);
		}

		InputManager im = GetGame().GetInputManager();
		if (im)
		{
			im.ResetContext("CharacterFreelookMouseContext");
			im.ResetContext("CharacterFreelookGamepadContext");
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_TickLookHeading(float pDt)
	{
		if (!m_bMHJ_DriveLookHeading)
			return;
		if (MHJ_IsInCanopySeat())
			return;

		MHJ_DetachFromCanopyParent();
		MHJ_ClearForcedFreeLook();

		if (GetCommandVehicle())
			StartCommand_Move();

		CharacterControllerComponent controller = GetControllerComponent();
		if (!controller)
			controller = m_CharacterControllerComp;
		if (!controller)
			return;

		CharacterInputContext input = controller.GetInputContext();
		if (!input)
			return;

		CharacterHeadAimingComponent headAim = CharacterHeadAimingComponent.Cast(m_OwnerEntity.FindComponent(CharacterHeadAimingComponent));
		if (headAim)
			headAim.ResetLimitAnglesOverride();

		float add = 0;
		if (headAim)
		{
			vector lookDeg = headAim.GetLookAngles();
			add = lookDeg[0] * Math.DEG2RAD;
		}

		if (Math.AbsFloat(add) < 0.00001)
		{
			vector aim = input.GetAimingAngles();
			vector look = input.GetLookAtAngles();
			add = aim[0] + look[0];
		}

		if (Math.AbsFloat(add) < 0.00001)
			add = input.GetAimChange()[0] * Math.DEG2RAD;

		if (Math.AbsFloat(add) > 0.00001)
		{
			float heading = MHJ_WrapHeading(input.GetHeadingAngle() + add);
			input.SetHeadingAngle(heading);
			controller.SetHeadingAngle(heading, true);
			AlignNewTurns();
		}

		input.SetAimingAngles(vector.Zero);
		input.SetLookAtAngles(vector.Zero);
		if (headAim)
		{
			vector pitchRad;
			vector lookDeg = headAim.GetLookAngles();
			pitchRad[1] = lookDeg[1] * Math.DEG2RAD;
			headAim.SetAimingRotation(pitchRad);
			headAim.SetAimingRotationWanted(pitchRad);
		}

		if (MHJ_LookNeedsManualHeading())
			return;
		if (Math.AbsFloat(add) > 0.00001)
			return;
		if (m_fMHJ_LookRecoverRemain > 0)
		{
			m_fMHJ_LookRecoverRemain = m_fMHJ_LookRecoverRemain - pDt;
			return;
		}

		m_bMHJ_DriveLookHeading = false;
		m_bMHJ_RecoverLookAfterCanopy = false;
		MHJ_Log.Info("Canopy look: on-foot heading restored");
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_WatchStuckCanopyLook()
	{
		if (!m_bMHJ_RecoverLookAfterCanopy)
			return;
		if (MHJ_IsHaloJumping())
			return;
		if (m_bMHJ_DriveLookHeading)
			return;
		if (!MHJ_HasLeftoverCanopyLook())
			return;

		m_bMHJ_DriveLookHeading = true;
		m_bMHJ_LookLogged = false;
		MHJ_RestoreCharacterLook();
		MHJ_Log.Info("Canopy look: leftover freelook after landing");
	}

	//------------------------------------------------------------------------------------------------
	protected bool MHJ_HasLeftoverCanopyLook()
	{
		ChimeraCharacter ch = ChimeraCharacter.Cast(m_OwnerEntity);
		if (!ch)
			return false;

		CompartmentAccessComponent access = ch.GetCompartmentAccessComponent();
		if (access)
		{
			if (access.IsInCompartment())
				return false;
			if (access.IsGettingIn())
				return false;
		}

		if (ch.IsInVehicle())
			return true;

		CharacterControllerComponent controller = GetControllerComponent();
		if (!controller)
			controller = m_CharacterControllerComp;
		if (controller && controller.IsFreeLookForced())
			return true;
		if (controller)
		{
			CharacterInputContext lookInput = controller.GetInputContext();
			if (lookInput && lookInput.CameraIsFreeLook())
			{
				if (!controller.GetFreeLookInput())
					return true;
			}
		}
		if (controller && controller.IsFreeLookEnabled())
		{
			if (!controller.GetFreeLookInput())
				return true;
		}

		InputManager im = GetGame().GetInputManager();
		if (im && im.IsContextActive("CharacterFreelookMouseContext"))
		{
			if (controller && !controller.GetFreeLookInput())
				return true;
		}

		SCR_CharacterCameraHandlerComponent cam = SCR_CharacterCameraHandlerComponent.Cast(m_OwnerEntity.FindComponent(SCR_CharacterCameraHandlerComponent));
		if (cam)
		{
			ScriptedCameraItem cur = cam.GetCurrentCamera();
			if (CharacterCamera3rdPersonVehicle.Cast(cur))
				return true;
			if (CharacterCamera1stPersonVehicle.Cast(cur))
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_AskOwnerGetOut(CompartmentAccessComponent access, ChimeraCharacter ch)
	{
		if (!access)
			return;
		if (!ch)
			return;

		bool ragdoll = false;
		if (m_iMHJ_LeaveTries >= 10)
			ragdoll = true;

		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(ch);
		PlayerController pc = GetGame().GetPlayerManager().GetPlayerController(playerId);
		SCR_PlayerController scrPc = SCR_PlayerController.Cast(pc);
		if (scrPc)
		{
			scrPc.MHJ_AskLeaveCanopy(ragdoll);
			return;
		}

		vector mat[4];
		ch.GetTransform(mat);
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
	}

	//------------------------------------------------------------------------------------------------
	override bool SubhandlerStatesBegin(CharacterInputContext pInputCtx, float pDt, int pCurrentCommandID)
	{
		if (pCurrentCommandID == ECommandIDs.SCRIPTED)
		{
			if (m_MHJ_HaloCommand && !m_MHJ_HaloCommand.IsFlagFinished())
			{
				MHJ_KeepFreefallControls(pInputCtx);
				return true;
			}
		}

		return super.SubhandlerStatesBegin(pInputCtx, pDt, pCurrentCommandID);
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_ApplyFreefallSteer(float turn, float pitch)
	{
		if (!m_MHJ_HaloCommand)
			return;
		if (m_MHJ_HaloCommand.IsFlagFinished())
			return;

		m_MHJ_HaloCommand.SetSteerInput(turn, pitch);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_KeepFreefallControls(CharacterInputContext pInputCtx)
	{
		if (m_MHJ_HaloCommand)
			m_MHJ_HaloCommand.KeepMovementContextAlive();
	}

	//------------------------------------------------------------------------------------------------
	override bool SubhandlerStatesEnd(CharacterInputContext pInputCtx, float pDt, int pCurrentCommandID)
	{
		bool handled = super.SubhandlerStatesEnd(pInputCtx, pDt, pCurrentCommandID);
		if (m_bMHJ_SuppressLandFall)
		{
			if (GetCommandVehicle())
				StartCommand_Move();
			MHJ_TryLandFallCommand();
			if (!GetCommandVehicle())
			{
				if (GetCommandMove())
					m_bMHJ_SuppressLandFall = false;
				else if (!GetCommandFall())
					m_bMHJ_SuppressLandFall = false;
			}
		}
		if (m_fMHJ_BlockFallRemain > 0)
		{
			m_fMHJ_BlockFallRemain = m_fMHJ_BlockFallRemain - pDt;
			if (m_fMHJ_BlockFallRemain < 0)
				m_fMHJ_BlockFallRemain = 0;
			MHJ_TryLandFallCommand();
		}
		MHJ_TickLandingCarry(pInputCtx, pDt);
		MHJ_WatchStuckCanopyLook();
		MHJ_TickLookHeading(pDt);
		return handled;
	}

	//------------------------------------------------------------------------------------------------
	override bool HandleFalling(CharacterInputContext pInputCtx, float pDt, int pCurrentCommandID)
	{
		if (MHJ_ShouldHoldCanopySeat())
		{
			if (GetCommandFall())
			{
				if (m_pMHJ_BoardSlot)
					MHJ_StartCanopyVehicleCommand();
			}
			return true;
		}
		if (MHJ_IsHaloJumping())
			return true;

		if (m_bMHJ_SuppressLandFall)
		{
			if (GetCommandFall())
			{
				MHJ_TryLandFallCommand();
				StartCommand_Move();
			}
		}
		else if (m_fMHJ_BlockFallRemain > 0)
		{
			if (GetCommandFall())
			{
				MHJ_TryLandFallCommand();
				StartCommand_Move();
			}
		}

		return HandleFallingDefault(pInputCtx, pDt, pCurrentCommandID);
	}

	//------------------------------------------------------------------------------------------------
	override bool HandleWeapons(CharacterInputContext pInputCtx, float pDt, int pCurrentCommandID)
	{
		if (MHJ_IsHaloJumping())
			return true;

		return HandleWeaponsDefault(pInputCtx, pDt, pCurrentCommandID);
	}

	//------------------------------------------------------------------------------------------------
	override bool HandleWeaponADS(CharacterInputContext pInputCtx, float pDt, int pCurrentCommandID)
	{
		if (MHJ_IsHaloJumping())
			return true;

		return HandleWeaponADSDefault(pInputCtx, pDt, pCurrentCommandID);
	}

	//------------------------------------------------------------------------------------------------
	override bool HandleWeaponFire(CharacterInputContext pInputCtx, float pDt, int pCurrentCommandID)
	{
		if (MHJ_IsHaloJumping())
			return true;

		return HandleWeaponFireDefault(pInputCtx, pDt, pCurrentCommandID);
	}

	//------------------------------------------------------------------------------------------------
	override bool HandleDynamicStance(CharacterInputContext pInputCtx, float pDt, int pCurrentCommandID)
	{
		if (m_MHJ_HaloCommand && !m_MHJ_HaloCommand.IsFlagFinished())
			return true;
		if (MHJ_IsBoardingCanopy())
			return true;

		return HandleDynamicStanceDefault(pInputCtx, pDt, pCurrentCommandID);
	}

	//------------------------------------------------------------------------------------------------
	override bool HandleThrowing(CharacterInputContext pInputCtx, float pDt, int pCurrentCommandID)
	{
		if (MHJ_IsHaloJumping())
			return true;

		return HandleThrowingDefault(pInputCtx, pDt, pCurrentCommandID);
	}

	//------------------------------------------------------------------------------------------------
	override bool HandleItemUse(CharacterInputContext pInputCtx, float pDt, int pCurrentCommandID)
	{
		if (MHJ_IsHaloJumping())
			return true;

		return HandleItemUseDefault(pInputCtx, pDt, pCurrentCommandID);
	}

	//------------------------------------------------------------------------------------------------
	override bool HandleLeftHandGadget(CharacterInputContext pInputCtx, float pDt, int pCurrentCommandID)
	{
		if (MHJ_IsHaloJumping())
			return true;

		return HandleLeftHandGadgetDefault(pInputCtx, pDt, pCurrentCommandID);
	}

	//------------------------------------------------------------------------------------------------
	override bool HandleVehicle(CharacterInputContext pInputCtx, float pDt, int pCurrentCommandID)
	{
		if (MHJ_ShouldHoldCanopySeat())
		{
			MHJ_KeepCanopySitPose();
			MHJ_EnsureCanopyGetIn();
			if (GetCommandVehicle())
				return true;
			if (MHJ_IsInCanopySeat())
			{
				MHJ_StartCanopyVehicleCommand();
				return true;
			}
			return HandleVehicleDefault(pInputCtx, pDt, pCurrentCommandID);
		}

		if (m_bMHJ_DriveLookHeading)
		{
			if (GetCommandVehicle())
			{
				StartCommand_Move();
				return true;
			}
		}

		if (m_bMHJ_SuppressLandFall)
		{
			if (GetCommandVehicle())
			{
				StartCommand_Move();
				return true;
			}
		}

		return HandleVehicleDefault(pInputCtx, pDt, pCurrentCommandID);
	}

	//------------------------------------------------------------------------------------------------
	override bool HandleFinishedCommands(bool pCurrentCommandFinished)
	{
		if (pCurrentCommandFinished && m_MHJ_HaloCommand)
		{
			bool seatHandoff = m_MHJ_HaloCommand.IsCanopyHandOff();
			if (!seatHandoff)
				seatHandoff = m_MHJ_HaloCommand.IsAwaitingSeat();
			if (!seatHandoff)
				seatHandoff = MHJ_IsInCanopySeat();

			if (seatHandoff)
			{
				if (m_MHJ_HaloCommand)
					m_MHJ_HaloCommand.ReleaseFreefallPose();
				if (m_CharacterAnimComp)
					m_CharacterAnimComp.PhysicsSetStance(ECharacterStance.STAND);
				m_MHJ_HaloCommand = null;

				MHJ_EnsureCanopyGetIn();
				return true;
			}

			float landDown = m_MHJ_HaloCommand.GetLandDownSpeed();
			float landHoriz = m_MHJ_HaloCommand.GetLandHorizSpeed();
			float landHeading = m_MHJ_HaloCommand.GetLandHeading();
			m_MHJ_HaloCommand = null;
			m_fMHJ_BlockFallRemain = 5.0;
			m_bMHJ_SuppressLandFall = true;
			m_bMHJ_LandRestoreLogged = false;
			MHJ_PlaceOnGround();
			MHJ_ExitFallIntoMove();
			MHJ_ApplyLandingImpact(landDown);
			MHJ_BeginLandingCarry(landDown, landHoriz, landHeading);
			return true;
		}

		if (pCurrentCommandFinished)
		{
			if (MHJ_ShouldHoldCanopySeat())
			{
				if (m_pMHJ_BoardSlot)
					MHJ_StartCanopyVehicleCommand();
				return true;
			}

			if (!MHJ_IsInCanopySeat())
			{
				if (!GetCommandMove())
					StartCommand_Move();
			}
		}

		if (MHJ_ShouldHoldCanopySeat())
			return true;

		return HandleFinishedCommandsDefault(pCurrentCommandFinished);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_BeginLandingCarry(float downSpeed, float horizSpeed, float landHeading)
	{
		MHJ_ClearLandingCarry();

		if (downSpeed >= MHJ_Constants.LAND_HARD_SINK)
			return;
		if (horizSpeed < MHJ_Constants.LAND_CARRY_MIN)
			return;

		float analog = MHJ_MapLandCarryAnalog(horizSpeed);
		if (analog <= 0)
			return;

		CharacterControllerComponent controller = GetControllerComponent();
		if (!controller)
			controller = m_CharacterControllerComp;
		if (!controller)
			return;

		m_fMHJ_LandCarryHeading = landHeading;
		m_fMHJ_LandCarryAnalogStart = analog;
		m_fMHJ_LandCarryAnalog = analog;
		m_fMHJ_LandCarryRemain = MHJ_Constants.LAND_CARRY_TIME;

		controller.SetHeadingAngle(landHeading, false);
		MHJ_ApplyLandCarryMovement(controller, analog);
	}

	//------------------------------------------------------------------------------------------------
	protected float MHJ_MapLandCarryAnalog(float horizSpeed)
	{
		if (horizSpeed < MHJ_Constants.LAND_CARRY_MIN)
			return 0;

		float t = 1;
		if (horizSpeed < MHJ_Constants.LAND_CARRY_FULL)
		{
			float span = MHJ_Constants.LAND_CARRY_FULL - MHJ_Constants.LAND_CARRY_MIN;
			if (span <= 0)
				return MHJ_Constants.LAND_CARRY_ANALOG_MAX;
			t = (horizSpeed - MHJ_Constants.LAND_CARRY_MIN) / span;
		}

		float analogSpan = MHJ_Constants.LAND_CARRY_ANALOG_MAX - MHJ_Constants.LAND_CARRY_ANALOG_MIN;
		return MHJ_Constants.LAND_CARRY_ANALOG_MIN + (analogSpan * t);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_ApplyLandCarryMovement(notnull CharacterControllerComponent controller, float analog)
	{
		controller.SetMovement(analog, vector.Forward);

		CharacterInputContext inputCtx = controller.GetInputContext();
		if (inputCtx)
			inputCtx.SetMovement(analog, vector.Forward);
	}

	//------------------------------------------------------------------------------------------------
	//! Raw WASD/stick only. GetMovement() includes our own SetMovement inject and would
	//! cancel the carry on the next tick.
	protected bool MHJ_HasPlayerMoveBreak()
	{
		InputManager im = GetGame().GetInputManager();
		if (!im)
			return false;

		float fwd = im.GetActionValue("CharacterForward");
		float right = im.GetActionValue("CharacterRight");
		if (fwd < 0)
			fwd = -fwd;
		if (right < 0)
			right = -right;

		if (fwd > MHJ_Constants.LAND_CARRY_INPUT_BREAK)
			return true;
		if (right > MHJ_Constants.LAND_CARRY_INPUT_BREAK)
			return true;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_TickLandingCarry(CharacterInputContext pInputCtx, float pDt)
	{
		if (m_fMHJ_LandCarryRemain <= 0)
			return;

		if (!GetCommandMove())
		{
			MHJ_ClearLandingCarry();
			return;
		}

		CharacterControllerComponent controller = GetControllerComponent();
		if (!controller)
			controller = m_CharacterControllerComp;
		if (!controller)
		{
			MHJ_ClearLandingCarry();
			return;
		}

		if (controller.IsDead())
		{
			MHJ_ClearLandingCarry();
			return;
		}

		if (MHJ_HasPlayerMoveBreak())
		{
			MHJ_ClearLandingCarry();
			return;
		}

		m_fMHJ_LandCarryRemain = m_fMHJ_LandCarryRemain - pDt;
		if (m_fMHJ_LandCarryRemain <= 0)
		{
			MHJ_ClearLandingCarry();
			return;
		}

		float frac = m_fMHJ_LandCarryRemain / MHJ_Constants.LAND_CARRY_TIME;
		if (frac < 0)
			frac = 0;
		if (frac > 1)
			frac = 1;

		m_fMHJ_LandCarryAnalog = m_fMHJ_LandCarryAnalogStart * frac;
		if (m_fMHJ_LandCarryAnalog < MHJ_Constants.LAND_CARRY_ANALOG_MIN * 0.45)
		{
			MHJ_ClearLandingCarry();
			return;
		}

		MHJ_ApplyLandCarryMovement(controller, m_fMHJ_LandCarryAnalog);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_ClearLandingCarry()
	{
		m_fMHJ_LandCarryRemain = 0;
		m_fMHJ_LandCarryAnalog = 0;
		m_fMHJ_LandCarryAnalogStart = 0;
		m_fMHJ_LandCarryHeading = 0;
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_ApplyLandingImpact(float downSpeed)
	{
		if (!Replication.IsServer())
			return;
		if (downSpeed < MHJ_Constants.LAND_HARD_SINK)
			return;
		if (!m_OwnerEntity)
			return;

		SCR_CharacterDamageManagerComponent dmg = SCR_CharacterDamageManagerComponent.Cast(m_OwnerEntity.FindComponent(SCR_CharacterDamageManagerComponent));
		if (!dmg)
			return;

		float damage = 18;
		if (downSpeed >= MHJ_Constants.LAND_FATAL_SINK)
			damage = 200;
		else if (downSpeed >= 12)
			damage = 45;

		dmg.HandleAnimatedFallDamage(damage);
	}
}
