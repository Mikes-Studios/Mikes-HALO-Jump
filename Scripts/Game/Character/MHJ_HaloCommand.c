//------------------------------------------------------------------------------------------------
//! Freefall aero, HUD, and canopy-open trigger. Locomotion stays on native Fall;
//! this object never becomes a ScriptedCommand and never calls SetCurrentCommand.
//------------------------------------------------------------------------------------------------
class MHJ_HaloCommand : Managed
{
	protected ChimeraCharacter m_pCharacter;
	protected CharacterControllerComponent m_pController;
	protected CharacterInputContext m_Input;
	protected CharacterAnimationComponent m_AnimationComponent;
	protected CharacterHeadAimingComponent m_pHeadAim;

	protected MHJ_EHaloPhase m_ePhase;
	protected float m_fOpenAltitude;
	protected float m_fHeading;
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
	protected float m_fBank;
	protected float m_fBankV;
	protected float m_fPitch;
	protected float m_fPitchV;
	protected float m_fSimT;
	protected bool m_bStarted;
	protected bool m_bFinished;
	protected bool m_bAeroPaused;
	protected bool m_bHudOwned;
	protected bool m_bGroundedPending;
	protected bool m_bOpenQueued;
	protected bool m_bOpenBlocked;
	protected bool m_bAllowCanopy;
	protected bool m_bInputListening;
	protected string m_sFlightMode;
	protected vector m_vWorldVel;
	protected vector m_vWind;
	protected float m_fLandDownSpeed;
	protected float m_fLandHorizSpeed;
	protected float m_fLandHeading;
	protected ref TraceParam m_AglTrace;

	//------------------------------------------------------------------------------------------------
	void MHJ_HaloCommand(ChimeraCharacter character, CharacterControllerComponent controller, CharacterAnimationComponent animation, float openAltitude, bool allowCanopy)
	{
		m_pCharacter = character;
		m_pController = controller;
		m_AnimationComponent = animation;
		if (controller)
			m_Input = controller.GetInputContext();

		m_fOpenAltitude = openAltitude;
		if (m_fOpenAltitude < MHJ_Constants.OPEN_ALT_MIN)
			m_fOpenAltitude = MHJ_Constants.OPEN_ALT_MIN;
		m_bAllowCanopy = allowCanopy;

		ResetFlightState();
	}

	//------------------------------------------------------------------------------------------------
	void Start()
	{
		if (m_bStarted)
			return;

		m_bStarted = true;
		m_bFinished = false;
		m_bAeroPaused = false;
		WidenLookLimits();
		ResetFlightState();
		SetGravityEnabled(false);
		m_vWorldVel[1] = -8;
		m_fVelY = -8;
		if (m_pController)
		{
			m_pController.SetWeaponRaised(false);
			m_pController.SetWeaponADS(false);
		}

		BindFreefallInput();
		MHJ_Log.Info("HALO native Fall started");

		if (IsLocalCharacter())
		{
			MHJ_JumpHud.Open();
			m_bHudOwned = true;
			SCR_HintManagerComponent.ShowCustomHint("W tracks. S slows the fall. A/D turns. Canopy opens automatically.", "HALO JUMP", 6);
		}
	}

	//------------------------------------------------------------------------------------------------
	void Stop()
	{
		if (!m_bStarted)
			return;

		UnbindFreefallInput();
		GetGame().GetCallqueue().Remove(OpenCanopy);
		SetGravityEnabled(true);
		ApplyFlightMotion(0);
		RestoreLookLimits();

		if (m_bHudOwned)
			MHJ_JumpHud.Close();

		m_bHudOwned = false;
		m_bAeroPaused = true;
		m_bFinished = true;
		m_bStarted = false;
	}

	//------------------------------------------------------------------------------------------------
	//! Entity replacement/death can bypass the normal board/land path.
	void Abort()
	{
		UnbindFreefallInput();
		GetGame().GetCallqueue().Remove(OpenCanopy);
		SetGravityEnabled(true);
		RestoreLookLimits();
		if (m_bHudOwned)
			MHJ_JumpHud.Close();

		m_bHudOwned = false;
		m_bAeroPaused = true;
		m_bFinished = true;
		m_bStarted = false;
	}

	//------------------------------------------------------------------------------------------------
	//! Pause aero and restore gravity so GetInVehicle runs from native Fall.
	void PauseForCanopyBoard()
	{
		if (!IsActive())
			return;
		if (m_bAeroPaused)
			return;

		m_bAeroPaused = true;
		m_ePhase = MHJ_EHaloPhase.CANOPY;
		UnbindFreefallInput();
		SetGravityEnabled(true);
		ExportPhysicsVelocity();
		RestoreLookLimits();
		m_bHudOwned = false;
	}

	//------------------------------------------------------------------------------------------------
	void ResumeAero()
	{
		if (!IsActive())
			return;
		if (!m_bAeroPaused)
			return;

		m_bAeroPaused = false;
		m_bOpenQueued = false;
		m_bOpenBlocked = true;
		m_ePhase = MHJ_EHaloPhase.FREEFALL;
		m_sFlightMode = "FREEFALL";
		SetGravityEnabled(false);
		WidenLookLimits();
		BindFreefallInput();
		if (IsLocalCharacter() && !MHJ_JumpHud.IsOpen())
		{
			MHJ_JumpHud.Open();
			m_bHudOwned = true;
		}

		MHJ_Log.Warning("Canopy handoff cancelled; continuing native freefall");
	}

	//------------------------------------------------------------------------------------------------
	void Tick(float pDt)
	{
		if (!IsActive())
			return;
		if (!m_pCharacter)
		{
			Abort();
			return;
		}

		if (m_bAeroPaused)
			return;

		m_fSimT = m_fSimT + pDt;
		RefreshWind();
		ReadInput();

		float agl = GetAgl();
		if (m_bAllowCanopy && !m_bOpenBlocked)
		{
			if (agl <= m_fOpenAltitude)
				QueueOpenCanopy();
			else if (agl <= MHJ_Constants.LAND_AGL)
				QueueOpenCanopy();
		}

		if (agl <= MHJ_Constants.LAND_AGL)
		{
			m_fLandDownSpeed = -m_fVelY;
			if (m_fLandDownSpeed < 0)
				m_fLandDownSpeed = 0;

			vector landHorizontal = m_vWorldVel;
			landHorizontal[1] = 0;
			m_fLandHorizSpeed = landHorizontal.Length();
			m_fLandHeading = m_fHeading;
			m_ePhase = MHJ_EHaloPhase.LANDED;
			m_sFlightMode = "LANDING";
			m_bGroundedPending = true;
			SetGravityEnabled(true);
			ApplyFlightMotion(0);
			UnbindFreefallInput();
			m_bAeroPaused = true;
			m_bFinished = true;
			m_bStarted = false;
			if (m_bHudOwned)
				MHJ_JumpHud.Close();
			m_bHudOwned = false;
			RestoreLookLimits();
			return;
		}

		ApplyFreefall(pDt);
		ApplyFlightMotion(pDt);
		ApplyBodyAttitude();
		PushHud(agl);
	}

	//------------------------------------------------------------------------------------------------
	bool IsActive()
	{
		return m_bStarted && !m_bFinished;
	}

	//------------------------------------------------------------------------------------------------
	bool IsAeroActive()
	{
		if (!IsActive())
			return false;
		return !m_bAeroPaused;
	}

	//------------------------------------------------------------------------------------------------
	bool IsFinished()
	{
		return m_bFinished;
	}

	//------------------------------------------------------------------------------------------------
	bool ConsumeGrounded()
	{
		if (!m_bGroundedPending)
			return false;
		m_bGroundedPending = false;
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
		m_fSimT = 0;
		m_bHudOwned = false;
		m_bGroundedPending = false;
		m_bAeroPaused = false;
		m_bOpenQueued = false;
		m_bOpenBlocked = false;
		m_sFlightMode = "EXIT";
		m_fHeading = ReadBodyHeading();
		m_fBank = 0;
		m_fBankV = 0;
		m_fPitch = 0;
		m_fPitchV = 0;
		m_fTurnFilt = 0;
		m_fTurnFiltV = 0;
		m_fPitchInputFilt = 0;
		m_fPitchInputFiltV = 0;
		m_fNetTurn = 0;
		m_fNetPitch = 0;
		m_fListenTurn = 0;
		m_fListenPitch = 0;
		m_vWorldVel = vector.Zero;
		m_vWind = vector.Zero;
		m_fLandDownSpeed = 0;
		m_fLandHorizSpeed = 0;
		m_fLandHeading = 0;
		if (!m_AglTrace)
			m_AglTrace = new TraceParam();
	}

	//------------------------------------------------------------------------------------------------
	//! Entity spawning must run on the main thread, not from the fall tick.
	protected void QueueOpenCanopy()
	{
		if (m_bOpenQueued)
			return;
		if (m_bOpenBlocked)
			return;

		m_bOpenQueued = true;
		GetGame().GetCallqueue().CallLater(OpenCanopy, 0, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void OpenCanopy()
	{
		m_bOpenQueued = false;
		if (!IsAeroActive())
			return;
		if (!m_pCharacter)
			return;

		PauseForCanopyBoard();
		m_bOpenBlocked = true;

		if (!Replication.IsServer())
		{
			MHJ_Log.Info("Canopy handoff: owner waiting for setup");
			return;
		}

		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(m_pCharacter);
		PlayerController playerController = GetGame().GetPlayerManager().GetPlayerController(playerId);
		SCR_PlayerController scrPlayerController = SCR_PlayerController.Cast(playerController);
		if (!scrPlayerController)
		{
			ResumeAero();
			return;
		}

		if (!scrPlayerController.MHJ_StartCanopySession(m_pCharacter, m_vWorldVel, m_vWind, m_fHeading, m_fPitch, m_fBank, m_fSimT, m_fOpenAltitude))
			ResumeAero();
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

		InputManager inputManager = GetGame().GetInputManager();
		if (inputManager)
			FillInputIfIdle(inputManager.GetActionValue("CharacterRight"), inputManager.GetActionValue("CharacterForward"));

		if (m_Input)
		{
			float moveSpeed;
			vector localDirection;
			m_Input.GetMovement(moveSpeed, localDirection);
			FillInputIfIdle(localDirection[0], localDirection[2]);
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

		PlayerController playerController = GetGame().GetPlayerController();
		SCR_PlayerController scrPlayerController = SCR_PlayerController.Cast(playerController);
		if (!scrPlayerController)
			return;

		scrPlayerController.MHJ_AskFreefallSteer(m_fTurnInput, m_fPitchInput);
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
	protected void BindFreefallInput()
	{
		if (m_bInputListening)
			return;
		if (!IsLocalCharacter())
			return;

		InputManager inputManager = GetGame().GetInputManager();
		if (!inputManager)
			return;

		inputManager.AddActionListener("CharacterForward", EActionTrigger.VALUE, OnFreefallForward);
		inputManager.AddActionListener("CharacterRight", EActionTrigger.VALUE, OnFreefallRight);
		m_bInputListening = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void UnbindFreefallInput()
	{
		if (!m_bInputListening)
			return;

		InputManager inputManager = GetGame().GetInputManager();
		if (inputManager)
		{
			inputManager.RemoveActionListener("CharacterForward", EActionTrigger.VALUE, OnFreefallForward);
			inputManager.RemoveActionListener("CharacterRight", EActionTrigger.VALUE, OnFreefallRight);
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
		float authority = ExitAuthority();
		float turnCommand = m_fTurnInput * authority;
		float pitchCommand = m_fPitchInput * authority;

		m_fTurnFilt = Math.SmoothCD(m_fTurnFilt, turnCommand, m_fTurnFiltV, MHJ_Constants.STEER_FILTER_TIME, MHJ_Constants.STEER_FILTER_MAX, pDt);
		m_fPitchInputFilt = Math.SmoothCD(m_fPitchInputFilt, pitchCommand, m_fPitchInputFiltV, 0.14, 80, pDt);

		m_fHeading = WrapHeading(m_fHeading + m_fTurnFilt * MHJ_Constants.FREEFALL_TURN_RATE * pDt);
		SyncHeadingToBody();

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

		if (m_Input)
			m_Input.SetLean(m_fTurnFilt);

		IntegrateFreefall(pDt);
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
		float msl = 0;
		if (m_pCharacter)
			msl = m_pCharacter.GetOrigin()[1];

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

	//------------------------------------------------------------------------------------------------
	protected void SyncSpeedFromWorld()
	{
		m_fVelY = m_vWorldVel[1];
		vector air = m_vWorldVel - m_vWind;
		m_fAirspeed = air.Length();
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
	protected void ApplyBodyAttitude()
	{
		SyncHeadingToBody();
	}

	//------------------------------------------------------------------------------------------------
	float GetVerticalVelocity()
	{
		return m_fVelY;
	}

	//------------------------------------------------------------------------------------------------
	vector GetWorldVelocity()
	{
		return m_vWorldVel;
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyFlightMotion(float pDt)
	{
		if (!m_pCharacter)
			return;
		if (pDt <= 0)
			return;

		vector origin = m_pCharacter.GetOrigin();
		m_pCharacter.SetOrigin(origin + (m_vWorldVel * pDt));

		Physics physics = m_pCharacter.GetPhysics();
		if (physics)
			physics.SetAngularVelocity(vector.Zero);
	}

	//------------------------------------------------------------------------------------------------
	void ExportPhysicsVelocity()
	{
		if (!m_pCharacter)
			return;

		Physics physics = m_pCharacter.GetPhysics();
		if (!physics)
			return;

		physics.SetVelocity(m_vWorldVel);
		physics.SetAngularVelocity(vector.Zero);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetGravityEnabled(bool enabled)
	{
		if (m_AnimationComponent)
			m_AnimationComponent.PhysicsEnableGravity(enabled);

		if (!m_pCharacter)
			return;

		Physics physics = m_pCharacter.GetPhysics();
		if (physics)
			physics.EnableGravity(enabled);
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
	protected float GetAgl()
	{
		if (!m_pCharacter)
			return 0;

		vector position = m_pCharacter.GetOrigin();
		BaseWorld world = m_pCharacter.GetWorld();
		if (!world)
			world = GetGame().GetWorld();
		if (!world)
			return position[1];

		float terrainAgl = SCR_TerrainHelper.GetHeightAboveTerrain(position, world, true);
		if (terrainAgl > 90)
			return terrainAgl;

		if (!m_AglTrace)
			m_AglTrace = new TraceParam();
		m_AglTrace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		m_AglTrace.Exclude = m_pCharacter;
		float surfaceY = SCR_TerrainHelper.GetTerrainY(position, world, true, m_AglTrace);
		return position[1] - surfaceY;
	}

	//------------------------------------------------------------------------------------------------
	protected void PushHud(float agl)
	{
		if (!m_bHudOwned)
			return;

		vector forward = HeadingForward();
		vector windHorizontal = m_vWind;
		windHorizontal[1] = 0;
		float windSpeed = windHorizontal.Length();
		float headingDot = 0;
		if (windSpeed > 0.05)
			headingDot = vector.Dot(forward, windHorizontal) / windSpeed;

		string windRelative = MHJ_FlightAero.WindRelativeLabel(windSpeed, headingDot);
		MHJ_JumpHud.SetState(m_ePhase, agl, m_fOpenAltitude, m_fAirspeed, m_fVelY, m_sFlightMode, windSpeed, windRelative);
	}

	//------------------------------------------------------------------------------------------------
	protected void WidenLookLimits()
	{
		m_pHeadAim = null;
		if (!m_pCharacter)
			return;

		m_pHeadAim = CharacterHeadAimingComponent.Cast(m_pCharacter.FindComponent(CharacterHeadAimingComponent));
		if (m_pHeadAim)
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
	protected void SyncHeadingToBody()
	{
		if (!m_pController)
			return;
		m_pController.SetHeadingAngle(m_fHeading, true);
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsLocalCharacter()
	{
		IEntity localCharacter = SCR_PlayerController.GetLocalControlledEntity();
		if (!localCharacter)
			return false;
		return localCharacter == m_pCharacter;
	}
}
