//------------------------------------------------------------------------------------------------
//! Starts native Fall after a HALO teleport so GetInVehicle can board the jump
//! craft. Native compartment code owns entry, vehicle commands, exit, and
//! restoration to infantry controls. This handler never SetCurrentCommand.
//------------------------------------------------------------------------------------------------
modded class SCR_CharacterCommandHandlerComponent
{
	protected bool m_bMHJ_CanopySession;
	protected bool m_bMHJ_ExitStarted;
	protected float m_fMHJ_CanopyOpenAltitude;
	protected float m_fMHJ_LandCarryRemain;
	protected float m_fMHJ_LandCarryAnalog;
	protected float m_fMHJ_LandCarryAnalogStart;
	protected bool m_bMHJ_IgnoreFallDamage;
	protected bool m_bMHJ_ProtectHaloFall;

	//------------------------------------------------------------------------------------------------
	void MHJ_StartHaloJump(float openAltitude)
	{
		if (!m_CharacterAnimComp)
			return;
		if (!m_OwnerEntity)
			return;

		m_fMHJ_CanopyOpenAltitude = openAltitude;
		MHJ_EnsureFallCommand();
		MHJ_Log.Info("HALO native Fall started openAlt=" + openAltitude.ToString());
	}

	//------------------------------------------------------------------------------------------------
	bool MHJ_IsHaloJumping()
	{
		if (m_bMHJ_CanopySession)
			return true;
		if (MHJ_CanopyFlight.OccupantIsInCanopy(m_OwnerEntity))
			return true;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_OnControlledEntityLeaving()
	{
		MHJ_ClearLandingCarry();
		GetGame().GetCallqueue().Remove(MHJ_EndFallDamageIgnore);
		MHJ_EndFallDamageIgnore();
		m_bMHJ_ProtectHaloFall = false;
		m_bMHJ_CanopySession = false;
		m_bMHJ_ExitStarted = false;
	}

	//------------------------------------------------------------------------------------------------
	void MHJ_BeginCanopySession(float openAltitude)
	{
		m_bMHJ_CanopySession = true;
		m_bMHJ_ExitStarted = false;
		m_fMHJ_CanopyOpenAltitude = openAltitude;
	}

	//------------------------------------------------------------------------------------------------
	//! Fall-damage ignore for scripted AI drops. Does not start a player canopy session
	//! or inject native Fall.
	void MHJ_BeginAiDropIgnore()
	{
		MHJ_BeginFallDamageIgnore();
	}

	//------------------------------------------------------------------------------------------------
	//! GetInVehicle runs from native Fall. Do not Land the fall command first.
	void MHJ_PrepareCanopyBoard()
	{
		m_bMHJ_CanopySession = true;
		m_bMHJ_ExitStarted = false;
		MHJ_EnsureFallCommand();
	}

	//------------------------------------------------------------------------------------------------
	//! Stop injecting Fall. Native GetOut owns infantry restoration from here,
	//! matching ArmaReforgerParachutes.
	void MHJ_BeginCanopyExit()
	{
		m_bMHJ_ExitStarted = true;
		MHJ_BeginFallDamageIgnore();
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_EnsureFallCommand()
	{
		if (GetCommandFall())
			return;
		if (GetCommandVehicle())
			return;

		float verticalVelocity = -MHJ_Constants.FREEFALL_START_SINK;
		Physics physics;
		if (m_OwnerEntity)
			physics = m_OwnerEntity.GetPhysics();
		if (physics)
			verticalVelocity = physics.GetVelocity()[1];
		StartCommand_Fall(verticalVelocity);
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
	}

	//------------------------------------------------------------------------------------------------
	//! Deployment failed before GetIn. Stay on native Fall.
	void MHJ_CanopyBoardFailed()
	{
		m_bMHJ_CanopySession = false;

		if (MHJ_IsCompartmentBusy())
		{
			m_bMHJ_ExitStarted = true;
			return;
		}

		m_bMHJ_ExitStarted = false;
		MHJ_EnsureFallCommand();
		MHJ_Log.Warning("Craft handoff failed; remaining in native Fall");
	}

	//------------------------------------------------------------------------------------------------
	//! Called only after native exit reports no compartment and no GetOut in progress.
	//! Do not StartCommand_Move or write freelook/contexts here. Native
	//! GetOutVehicle_NoDoor owns infantry restoration, matching the working chute.
	void MHJ_OnCanopyExited(float downSpeed, float horizSpeed, float landHeading)
	{
		if (!m_bMHJ_CanopySession)
			return;

		m_bMHJ_ExitStarted = true;
		if (MHJ_IsCompartmentBusy())
		{
			MHJ_Log.Warning("OnCanopyExited while still seated; waiting for native GetOut");
			return;
		}

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
	protected void MHJ_LogInfantryInputState()
	{
		if (SCR_PlayerController.GetLocalControlledEntity() != m_OwnerEntity)
			return;

		string state = "input-recovery";
		state = state + " craft=" + MHJ_Log.Flag(MHJ_CanopyFlight.OccupantIsInCanopy(m_OwnerEntity));
		state = state + " veh=" + MHJ_Log.Flag(GetCommandVehicle() != null);
		state = state + " move=" + MHJ_Log.Flag(GetCommandMove() != null);
		state = state + " fall=" + MHJ_Log.Flag(GetCommandFall() != null);

		ChimeraCharacter character = ChimeraCharacter.Cast(m_OwnerEntity);
		CompartmentAccessComponent access;
		if (character)
			access = character.GetCompartmentAccessComponent();
		if (access)
		{
			state = state + " inComp=" + MHJ_Log.Flag(access.IsInCompartment());
			state = state + " gettingOut=" + MHJ_Log.Flag(access.IsGettingOut());
		}
		if (character)
			state = state + " inVeh=" + MHJ_Log.Flag(character.IsInVehicle());
		if (m_CharacterAnimComp)
			state = state + " linked=" + MHJ_Log.Flag(m_CharacterAnimComp.PhysicsIsLinked());

		PerceivableComponent perceivable;
		if (m_OwnerEntity)
			perceivable = PerceivableComponent.Cast(m_OwnerEntity.FindComponent(PerceivableComponent));
		if (perceivable)
		{
			state = state + " percInComp=" + MHJ_Log.Flag(perceivable.IsInCompartment());
			state = state + " disarmed=" + MHJ_Log.Flag(perceivable.IsDisarmed());
			state = state + " vis=" + perceivable.GetVisualRecognitionFactor().ToString();
		}

		if (m_OwnerEntity)
		{
			Physics physics = m_OwnerEntity.GetPhysics();
			if (physics)
				state = state + " layer=" + physics.GetInteractionLayer().ToString();
		}

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
			state = state + " life=" + controller.GetLifeState().ToString();
			state = state + " uncon=" + MHJ_Log.Flag(controller.IsUnconscious());

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
		if (MHJ_AiDropDirector.IsActiveJumper(m_OwnerEntity))
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
	override bool HandleFalling(CharacterInputContext pInputCtx, float pDt, int pCurrentCommandID)
	{
		if (!m_bMHJ_ExitStarted && m_bMHJ_CanopySession && !MHJ_CanopyFlight.OccupantIsInCanopy(m_OwnerEntity) && !MHJ_IsCompartmentBusy())
			MHJ_EnsureFallCommand();

		return HandleFallingDefault(pInputCtx, pDt, pCurrentCommandID);
	}

	//------------------------------------------------------------------------------------------------
	protected bool MHJ_IsCompartmentBusy()
	{
		if (GetCommandVehicle())
			return true;
		if (m_CharacterAnimComp && m_CharacterAnimComp.PhysicsIsLinked())
			return true;

		ChimeraCharacter character = ChimeraCharacter.Cast(m_OwnerEntity);
		CompartmentAccessComponent access;
		if (character)
			access = character.GetCompartmentAccessComponent();
		if (!access)
			return false;
		if (access.IsInCompartment() || access.IsGettingIn() || access.IsGettingOut())
			return true;
		return false;
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
