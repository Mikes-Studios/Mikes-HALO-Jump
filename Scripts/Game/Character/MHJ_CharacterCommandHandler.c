//------------------------------------------------------------------------------------------------
//! Starts native Fall after a HALO teleport and ticks MHJ freefall aero beside it.
//! Native compartment code owns canopy entry, vehicle commands, exit, and
//! restoration to infantry controls. This handler never SetCurrentCommand.
//------------------------------------------------------------------------------------------------
modded class SCR_CharacterCommandHandlerComponent
{
	protected ref MHJ_HaloCommand m_MHJ_HaloCommand;
	protected bool m_bMHJ_CanopySession;
	protected float m_fMHJ_CanopyOpenAltitude;
	protected float m_fMHJ_LandCarryRemain;
	protected float m_fMHJ_LandCarryAnalog;
	protected float m_fMHJ_LandCarryAnalogStart;
	protected bool m_bMHJ_IgnoreFallDamage;
	protected bool m_bMHJ_ProtectHaloFall;

	//------------------------------------------------------------------------------------------------
	void MHJ_StartHaloJump(float openAltitude)
	{
		if (MHJ_IsHaloJumping())
			return;
		if (!m_CharacterAnimComp)
			return;
		if (!m_OwnerEntity)
			return;

		ChimeraCharacter character = ChimeraCharacter.Cast(m_OwnerEntity);
		if (!character)
			return;

		CharacterControllerComponent controller = GetControllerComponent();
		if (!controller)
			controller = m_CharacterControllerComp;
		if (!controller)
			return;

		if (!GetCommandFall())
		{
			float verticalVelocity = 0;
			Physics physics = m_OwnerEntity.GetPhysics();
			if (physics)
				verticalVelocity = physics.GetVelocity()[1];
			StartCommand_Fall(verticalVelocity);
		}

		m_MHJ_HaloCommand = new MHJ_HaloCommand(character, controller, m_CharacterAnimComp, openAltitude, true);
		m_MHJ_HaloCommand.Start();
		MHJ_Log.Info("HALO jump started openAlt=" + openAltitude.ToString());
	}

	//------------------------------------------------------------------------------------------------
	bool MHJ_IsHaloJumping()
	{
		if (m_bMHJ_CanopySession)
			return true;
		return MHJ_IsFreefallActive();
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_OnControlledEntityLeaving()
	{
		MHJ_ClearLandingCarry();
		GetGame().GetCallqueue().Remove(MHJ_EndFallDamageIgnore);
		MHJ_EndFallDamageIgnore();
		m_bMHJ_ProtectHaloFall = false;
		m_bMHJ_CanopySession = false;
		if (!m_MHJ_HaloCommand)
			return;

		m_MHJ_HaloCommand.Abort();
		m_MHJ_HaloCommand = null;
	}

	//------------------------------------------------------------------------------------------------
	protected bool MHJ_IsFreefallActive()
	{
		if (!m_MHJ_HaloCommand)
			return false;
		return m_MHJ_HaloCommand.IsActive();
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_BeginCanopySession(float openAltitude)
	{
		m_bMHJ_CanopySession = true;
		m_fMHJ_CanopyOpenAltitude = openAltitude;
	}

	//------------------------------------------------------------------------------------------------
	//! Stop HALO aero. GetInVehicle runs from native Fall. Do not leave a
	//! paused helper that can ResumeAero after a successful GetIn or land.
	void MHJ_PrepareCanopyBoard()
	{
		m_bMHJ_CanopySession = true;
		MHJ_OnFreefallBoarded();

		MHJ_ConsumeHaloFallCommand();

		if (!GetCommandFall() && !GetCommandVehicle())
		{
			float verticalVelocity = 0;
			Physics physics;
			if (m_OwnerEntity)
				physics = m_OwnerEntity.GetPhysics();
			if (physics)
				verticalVelocity = physics.GetVelocity()[1];
			StartCommand_Fall(verticalVelocity);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! GetIn must begin from vanilla Fall/Move, matching the working parachute.
	bool MHJ_IsNativeCanopyBoardReady()
	{
		if (GetCommandFall())
			return true;
		if (GetCommandMove())
			return true;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_OnFreefallBoarded()
	{
		if (!m_MHJ_HaloCommand)
			return;

		m_MHJ_HaloCommand.Stop();
		m_MHJ_HaloCommand = null;
	}

	//------------------------------------------------------------------------------------------------
	//! Deployment failed before GetIn. Resume HALO aero only if the helper is
	//! still alive (spawn/session start failed). After Stop, stay on native Fall.
	void MHJ_CanopyBoardFailed()
	{
		m_bMHJ_CanopySession = false;

		if (m_MHJ_HaloCommand && m_MHJ_HaloCommand.IsActive())
		{
			m_MHJ_HaloCommand.ResumeAero();
			return;
		}

		ChimeraCharacter character = ChimeraCharacter.Cast(m_OwnerEntity);
		CompartmentAccessComponent access;
		if (character)
			access = character.GetCompartmentAccessComponent();
		if (access && (access.IsInCompartment() || access.IsGettingIn() || access.IsGettingOut()))
			return;

		if (GetCommandVehicle())
		{
			float verticalVelocity = 0;
			Physics physics;
			if (m_OwnerEntity)
				physics = m_OwnerEntity.GetPhysics();
			if (physics)
				verticalVelocity = physics.GetVelocity()[1];
			StartCommand_Fall(verticalVelocity);
		}

		MHJ_Log.Warning("Canopy handoff failed; remaining in native freefall");
	}

	//------------------------------------------------------------------------------------------------
	//! Called only after native exit reports no compartment and no GetOut in progress.
	//! Do not StartCommand_Move or write freelook/contexts here. Native
	//! GetOutVehicle_NoDoor owns infantry restoration, matching the working chute.
	void MHJ_OnCanopyExited(float downSpeed, float horizSpeed, float landHeading)
	{
		if (!m_bMHJ_CanopySession)
			return;

		m_bMHJ_CanopySession = false;
		MHJ_BeginFallDamageIgnore();
		MHJ_ClearPawnFallVelocity();
		MHJ_ConsumeHaloFallCommand();
		MHJ_ApplyLandingImpact(downSpeed);

		IEntity localCharacter = SCR_PlayerController.GetLocalControlledEntity();
		if (localCharacter != m_OwnerEntity)
			return;

		GetGame().GetCallqueue().CallLater(MHJ_LogInfantryInputState, 250, false);
		GetGame().GetCallqueue().CallLater(MHJ_LogInfantryInputState, 1000, false);
		MHJ_BeginLandingCarry(downSpeed, horizSpeed, landHeading);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_OnFreefallGrounded()
	{
		if (!m_MHJ_HaloCommand)
			return;

		float landDown = m_MHJ_HaloCommand.GetLandDownSpeed();
		float landHoriz = m_MHJ_HaloCommand.GetLandHorizSpeed();
		float landHeading = m_MHJ_HaloCommand.GetLandHeading();
		m_MHJ_HaloCommand = null;

		MHJ_BeginFallDamageIgnore();
		MHJ_ClearPawnFallVelocity();
		MHJ_ConsumeHaloFallCommand();
		MHJ_ApplyLandingImpact(landDown);
		MHJ_BeginLandingCarry(landDown, landHoriz, landHeading);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_LogInfantryInputState()
	{
		if (SCR_PlayerController.GetLocalControlledEntity() != m_OwnerEntity)
			return;

		string state = "input-recovery";
		state = state + " freefall=" + MHJ_Log.Flag(MHJ_IsFreefallActive());
		state = state + " veh=" + MHJ_Log.Flag(GetCommandVehicle() != null);
		state = state + " move=" + MHJ_Log.Flag(GetCommandMove() != null);
		state = state + " fall=" + MHJ_Log.Flag(GetCommandFall() != null);

		InputManager inputManager = GetGame().GetInputManager();
		if (inputManager)
		{
			state = state + " genCtx=" + MHJ_Log.Flag(inputManager.IsContextActive("CharacterGeneralContext"));
			state = state + " wpnCtx=" + MHJ_Log.Flag(inputManager.IsContextActive("CharacterWeaponContext"));
			state = state + " moveCtx=" + MHJ_Log.Flag(inputManager.IsContextActive("CharacterMovementContext"));
			state = state + " flCtx=" + MHJ_Log.Flag(inputManager.IsContextActive("CharacterFreelookMouseContext"));
			state = state + " seatCtx=" + MHJ_Log.Flag(inputManager.IsContextActive("CharacterCompartmentContext"));
		}

		CharacterControllerComponent controller = GetControllerComponent();
		if (!controller)
			controller = m_CharacterControllerComp;
		if (controller)
		{
			state = state + " disWpn=" + MHJ_Log.Flag(controller.GetDisableWeaponControls());
			state = state + " disView=" + MHJ_Log.Flag(controller.GetDisableViewControls());
			state = state + " forceFL=" + MHJ_Log.Flag(controller.IsFreeLookForced());
			state = state + " free=" + MHJ_Log.Flag(controller.IsFreeLookEnabled());

			CharacterInputContext inputContext = controller.GetInputContext();
			if (inputContext)
			{
				state = state + " camFL=" + MHJ_Log.Flag(inputContext.CameraIsFreeLook());
				state = state + " xferAim=" + MHJ_Log.Flag(inputContext.ShouldTransferFreeaimAfterFreelook());
			}
		}

		MHJ_Log.Land(state);
	}

	//------------------------------------------------------------------------------------------------
	bool MHJ_ShouldIgnoreFallDamage()
	{
		if (MHJ_IsHaloJumping())
			return true;
		if (m_bMHJ_IgnoreFallDamage)
			return true;
		if (m_bMHJ_ProtectHaloFall && GetCommandFall())
			return true;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_BeginFallDamageIgnore()
	{
		m_bMHJ_ProtectHaloFall = true;
		m_bMHJ_IgnoreFallDamage = true;
		GetGame().GetCallqueue().Remove(MHJ_EndFallDamageIgnore);
		GetGame().GetCallqueue().CallLater(MHJ_EndFallDamageIgnore, MHJ_Constants.FALL_DAMAGE_IGNORE_MS, false);
		MHJ_Log.Info("Fall-damage ignore armed for " + MHJ_Constants.FALL_DAMAGE_IGNORE_MS.ToString() + " ms");
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_EndFallDamageIgnore()
	{
		m_bMHJ_IgnoreFallDamage = false;
		if (GetCommandFall())
			return;

		m_bMHJ_ProtectHaloFall = false;
		MHJ_Log.Info("Fall-damage ignore ended");
	}

	//------------------------------------------------------------------------------------------------
	//! Native Fall stores jump-altitude height until Land(). GetOut then applies
	//! that as delayed LANDING damage. Consume it as a safe land.
	protected void MHJ_ConsumeHaloFallCommand()
	{
		CharacterCommandFall fallCmd = GetCommandFall();
		if (!fallCmd)
			return;

		fallCmd.Land(ELandType.LANDTYPE_NONE, 0);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_ClearPawnFallVelocity()
	{
		if (!m_OwnerEntity)
			return;

		Physics physics = m_OwnerEntity.GetPhysics();
		if (!physics)
			return;

		physics.SetVelocity(vector.Zero);
		physics.SetAngularVelocity(vector.Zero);
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_ApplyFreefallSteer(float turn, float pitch)
	{
		if (!m_MHJ_HaloCommand)
			return;
		if (!m_MHJ_HaloCommand.IsAeroActive())
			return;

		m_MHJ_HaloCommand.SetSteerInput(turn, pitch);
	}

	//------------------------------------------------------------------------------------------------
	override bool HandleFalling(CharacterInputContext pInputCtx, float pDt, int pCurrentCommandID)
	{
		if (m_MHJ_HaloCommand && m_MHJ_HaloCommand.IsAeroActive())
		{
			if (!GetCommandFall())
				StartCommand_Fall(m_MHJ_HaloCommand.GetVerticalVelocity());

			m_MHJ_HaloCommand.Tick(pDt);
			if (m_MHJ_HaloCommand.ConsumeGrounded())
				MHJ_OnFreefallGrounded();

			// HandleFallingDefault Lands when PhysicsLanded. Gravity-off plus
			// zeroed velocity made that true at jump altitude and dropped the
			// pawn back to Move, which ignores SetVelocity.
			return true;
		}

		if (m_MHJ_HaloCommand && m_MHJ_HaloCommand.IsActive())
		{
			m_MHJ_HaloCommand.Tick(pDt);
			if (m_MHJ_HaloCommand.ConsumeGrounded())
				MHJ_OnFreefallGrounded();
		}

		return HandleFallingDefault(pInputCtx, pDt, pCurrentCommandID);
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

		m_fMHJ_LandCarryAnalogStart = analog;
		m_fMHJ_LandCarryAnalog = analog;
		m_fMHJ_LandCarryRemain = MHJ_Constants.LAND_CARRY_TIME;

		controller.SetHeadingAngle(landHeading, false);
		MHJ_ApplyLandCarryMovement(controller, analog);
		GetGame().GetCallqueue().CallLater(MHJ_TickLandingCarry, 16, false);
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
	protected bool MHJ_HasPlayerMoveBreak()
	{
		InputManager inputManager = GetGame().GetInputManager();
		if (!inputManager)
			return false;

		float forward = inputManager.GetActionValue("CharacterForward");
		float right = inputManager.GetActionValue("CharacterRight");
		if (forward < 0)
			forward = -forward;
		if (right < 0)
			right = -right;

		if (forward > MHJ_Constants.LAND_CARRY_INPUT_BREAK)
			return true;
		if (right > MHJ_Constants.LAND_CARRY_INPUT_BREAK)
			return true;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_TickLandingCarry()
	{
		if (m_fMHJ_LandCarryRemain <= 0)
			return;

		CharacterControllerComponent controller = GetControllerComponent();
		if (!controller)
			controller = m_CharacterControllerComp;
		if (!controller)
		{
			MHJ_ClearLandingCarry();
			return;
		}

		if (controller.IsDead() || MHJ_HasPlayerMoveBreak())
		{
			MHJ_ClearLandingCarry();
			return;
		}

		CompartmentAccessComponent access;
		ChimeraCharacter character = ChimeraCharacter.Cast(m_OwnerEntity);
		if (character)
			access = character.GetCompartmentAccessComponent();
		if (access)
		{
			if (access.IsInCompartment() || access.IsGettingIn() || access.IsGettingOut())
			{
				MHJ_ClearLandingCarry();
				return;
			}
		}

		m_fMHJ_LandCarryRemain = m_fMHJ_LandCarryRemain - 0.016;
		if (m_fMHJ_LandCarryRemain <= 0)
		{
			MHJ_ClearLandingCarry();
			return;
		}

		float fraction = m_fMHJ_LandCarryRemain / MHJ_Constants.LAND_CARRY_TIME;
		if (fraction < 0)
			fraction = 0;
		if (fraction > 1)
			fraction = 1;

		m_fMHJ_LandCarryAnalog = m_fMHJ_LandCarryAnalogStart * fraction;
		if (m_fMHJ_LandCarryAnalog < MHJ_Constants.LAND_CARRY_ANALOG_MIN * 0.45)
		{
			MHJ_ClearLandingCarry();
			return;
		}

		MHJ_ApplyLandCarryMovement(controller, m_fMHJ_LandCarryAnalog);
		GetGame().GetCallqueue().CallLater(MHJ_TickLandingCarry, 16, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_ClearLandingCarry()
	{
		GetGame().GetCallqueue().Remove(MHJ_TickLandingCarry);
		m_fMHJ_LandCarryRemain = 0;
		m_fMHJ_LandCarryAnalog = 0;
		m_fMHJ_LandCarryAnalogStart = 0;
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

		SCR_CharacterDamageManagerComponent damageManager = SCR_CharacterDamageManagerComponent.Cast(m_OwnerEntity.FindComponent(SCR_CharacterDamageManagerComponent));
		if (!damageManager)
			return;

		float damage = 18;
		if (downSpeed >= MHJ_Constants.LAND_FATAL_SINK)
			damage = 200;
		else if (downSpeed >= 12)
			damage = 45;

		damageManager.HandleAnimatedFallDamage(damage);
	}
}
