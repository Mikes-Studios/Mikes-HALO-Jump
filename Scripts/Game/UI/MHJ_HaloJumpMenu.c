//------------------------------------------------------------------------------------------------
//! HALO drop planner. Hosted on the workspace so Workbench Play does not need
//! chimeraMenus.conf (MenuManager never loads addon presets there).
//! Native MapWidget is owned by MHJ_MapHost (planner session) and framed over the picker slot.
//------------------------------------------------------------------------------------------------
class MHJ_HaloJumpMenu
{
	protected static ref MHJ_HaloJumpMenu s_Instance;

	protected Widget m_wRoot;
	protected ref MUI_MenuHost m_Host;
	protected ref MHJ_MapHost m_Map;
	protected ref MHJ_MapPicker m_Picker;
	protected ref MUI_Label m_CoordLabel;
	protected ref MUI_NumericField m_JumpAlt;
	protected ref MUI_NumericField m_OpenAlt;
	protected ref MUI_Label m_Status;
	protected bool m_bSyncing;
	protected bool m_bClosing;

	//------------------------------------------------------------------------------------------------
	void ~MHJ_HaloJumpMenu()
	{
		CloseInternal();
		if (s_Instance == this)
			s_Instance = null;
	}

	//------------------------------------------------------------------------------------------------
	static void Open()
	{
		if (s_Instance)
			return;

		ref MHJ_HaloJumpMenu menu = new MHJ_HaloJumpMenu();
		if (!menu.OpenInternal())
			return;
		s_Instance = menu;
	}

	//------------------------------------------------------------------------------------------------
	static void Close()
	{
		if (!s_Instance)
			return;
		s_Instance.CloseInternal();
		s_Instance = null;
	}

	//------------------------------------------------------------------------------------------------
	static bool IsOpen()
	{
		if (!s_Instance)
			return false;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected bool OpenInternal()
	{
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
		{
			MHJ_Log.Error("No workspace for HALO menu");
			return false;
		}

		m_wRoot = workspace.CreateWidget(WidgetType.FrameWidgetTypeID, WidgetFlags.VISIBLE, Color.FromInt(Color.WHITE), 80, workspace);
		if (!m_wRoot)
			return false;

		FrameSlot.SetAnchorMin(m_wRoot, 0, 0);
		FrameSlot.SetAnchorMax(m_wRoot, 1, 1);
		FrameSlot.SetOffsets(m_wRoot, 0, 0, 0, 0);

		m_Host = new MUI_MenuHost();
		if (!m_Host.Open(m_wRoot, "MHJ_HaloJumpMenu", false))
		{
			m_Host = null;
			m_wRoot.RemoveFromHierarchy();
			m_wRoot = null;
			MHJ_Log.Error("HALO menu MUI mount failed");
			return false;
		}

		MUI_Runtime runtime = m_Host.GetRuntime();
		if (!runtime)
		{
			CloseInternal();
			return false;
		}

		BuildUI(runtime);

		m_Map = new MHJ_MapHost();
		if (m_Map.Open(m_wRoot, runtime))
		{
			if (m_Picker)
				m_Picker.SetMapHost(m_Map);
		}
		else
		{
			MHJ_Log.Warning("HALO planner map host failed; using grid fallback");
			m_Map = null;
		}

		runtime.GetOnBack().Insert(OnCancelClicked);
		WidgetManager.SetCursor(0);
		GetGame().GetCallqueue().CallLater(this.TickQueued, 1, true);
		MHJ_Log.Info("HALO planner opened");
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void CloseInternal()
	{
		if (m_bClosing)
			return;
		m_bClosing = true;

		GetGame().GetCallqueue().Remove(this.TickQueued);

		if (m_Picker)
			m_Picker.SetMapHost(null);

		if (m_Map)
		{
			m_Map.Close();
			m_Map = null;
		}

		m_Picker = null;
		m_CoordLabel = null;
		m_JumpAlt = null;
		m_OpenAlt = null;
		m_Status = null;
		if (m_Host)
		{
			MUI_Runtime runtime = m_Host.GetRuntime();
			if (runtime)
				runtime.GetOnBack().Remove(OnCancelClicked);
			m_Host.Close();
			m_Host = null;
		}
		if (m_wRoot)
		{
			m_wRoot.RemoveFromHierarchy();
			m_wRoot = null;
		}
		WidgetManager.SetCursor(0);
		m_bClosing = false;
	}

	//------------------------------------------------------------------------------------------------
	protected void TickQueued()
	{
		if (m_Host)
			m_Host.Tick(System.GetFrameTimeS());
		if (m_Map)
			m_Map.Tick(m_Picker);
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildUI(notnull MUI_Runtime runtime)
	{
		ref MUI_Panel overlay = runtime.CreatePanel("overlay");
		overlay.MakeOverlay();
		overlay.GetStyle().m_Fill = Color.FromInt(0);

		ref MUI_FxBackdrop fx = runtime.CreateFxBackdrop("fx");

		ref MUI_Card card = runtime.CreateCard("card");
		card.SetWidth(680);
		card.SetPadding(28);
		card.SetPaddingTRBL(22, 28, 24, 28);
		card.SetGap(12);
		card.SetAlign(0.5, 0.5);
		card.SetIntro(0.06, 0.55, 46);

		ref MUI_LiveHeader liveHeader = runtime.CreateLiveHeader("HALO JUMP", "liveHeader");
		liveHeader.SetKicker("MIKE'S HALO  //  DROP PLANNER");
		liveHeader.SetIntro(0.16, 0.4, 18);

		ref MUI_Label subtitle = runtime.CreateLabel("Click to set drop. Drag pans, wheel zooms. On controller: stick pans, triggers zoom, A sets drop at the centre reticle. D-pad down leaves the map.", "subtitle");
		subtitle.SetFontSize(MUI_Theme.FONT_SMALL);
		subtitle.SetMuted(true);

		ref MUI_Hairline lineA = runtime.CreateHairline("lineA");

		m_Picker = new MHJ_MapPicker();
		runtime.Adopt(m_Picker);
		m_Picker.SetName("picker");
		m_Picker.InitWorld();
		m_Picker.GetOnChanged().Insert(OnPickerChanged);

		m_CoordLabel = runtime.CreateLabel("", "coords");
		m_CoordLabel.SetFontSize(MUI_Theme.FONT_SMALL);
		m_CoordLabel.SetMuted(true);

		m_JumpAlt = runtime.CreateNumericField("Jump altitude (m AGL)", "jumpAlt");
		m_JumpAlt.SetRange(MHJ_Constants.JUMP_ALT_MIN, MHJ_Constants.JUMP_ALT_MAX);
		m_JumpAlt.SetStep(50);
		m_JumpAlt.SetDecimals(0);
		m_JumpAlt.SetValue(MHJ_Constants.JUMP_ALT_DEFAULT);
		m_JumpAlt.GetOnChanged().Insert(OnAltitudeChanged);

		m_OpenAlt = runtime.CreateNumericField("Canopy open altitude (m AGL)", "openAlt");
		m_OpenAlt.SetRange(MHJ_Constants.OPEN_ALT_MIN, MHJ_Constants.OPEN_ALT_MAX);
		m_OpenAlt.SetStep(25);
		m_OpenAlt.SetDecimals(0);
		m_OpenAlt.SetValue(MHJ_Constants.OPEN_ALT_DEFAULT);
		m_OpenAlt.GetOnChanged().Insert(OnAltitudeChanged);

		m_Status = runtime.CreateLabel("", "status");
		m_Status.SetFontSize(MUI_Theme.FONT_SMALL);
		m_Status.SetMuted(true);

		ref MUI_Hairline lineB = runtime.CreateHairline("lineB");

		ref MUI_Row buttons = runtime.CreateRow("buttons");
		buttons.SetGap(12);

		ref MUI_Button cancelBtn = runtime.CreateButton("Cancel", "cancel");
		cancelBtn.GetOnClicked().Insert(OnCancelClicked);

		ref MUI_Button jumpBtn = runtime.CreateButton("Jump", "jump");
		jumpBtn.MakeAccent();
		jumpBtn.GetOnClicked().Insert(OnJumpClicked);

		buttons.AddChild(cancelBtn);
		buttons.AddChild(jumpBtn);

		card.AddChild(liveHeader);
		card.AddChild(subtitle);
		card.AddChild(lineA);
		card.AddChild(m_Picker);
		card.AddChild(m_CoordLabel);
		card.AddChild(m_JumpAlt);
		card.AddChild(m_OpenAlt);
		card.AddChild(m_Status);
		card.AddChild(lineB);
		card.AddChild(buttons);

		overlay.AddChild(fx);
		overlay.AddChild(card);
		runtime.SetRoot(overlay);

		RefreshLabels();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnPickerChanged()
	{
		if (m_bSyncing)
			return;
		RefreshLabels();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnAltitudeChanged()
	{
		RefreshLabels();
	}

	//------------------------------------------------------------------------------------------------
	protected MUI_ThemeData GetTheme()
	{
		if (!m_Host)
			return null;
		MUI_Runtime runtime = m_Host.GetRuntime();
		if (!runtime)
			return null;
		return runtime.GetTheme();
	}

	//------------------------------------------------------------------------------------------------
	protected void RefreshLabels()
	{
		if (!m_CoordLabel)
			return;

		if (m_Picker && m_Picker.HasDrop())
			m_CoordLabel.SetText("Drop  " + m_Picker.GetDropX().ToString() + "  /  " + m_Picker.GetDropZ().ToString());
		else
			m_CoordLabel.SetText("No drop selected");

		if (!m_Status)
			return;
		if (!m_JumpAlt)
			return;
		if (!m_OpenAlt)
			return;

		MUI_ThemeData theme = GetTheme();
		float jumpAlt = m_JumpAlt.GetValue();
		float openAlt = m_OpenAlt.GetValue();
		if (jumpAlt < openAlt + MHJ_Constants.OPEN_MARGIN)
		{
			m_Status.SetMuted(false);
			if (theme)
				m_Status.SetColor(theme.Danger);
			m_Status.SetText("Jump altitude must be at least " + MHJ_Constants.OPEN_MARGIN.ToString() + " m above canopy open.");
			return;
		}

		m_Status.SetMuted(true);
		if (theme)
			m_Status.SetColor(theme.TextMuted);

		string line = "Freefall from " + jumpAlt.ToString() + " m. Canopy at " + openAlt.ToString() + " m AGL.";
		float msl = jumpAlt;
		if (m_Picker && m_Picker.HasDrop())
		{
			BaseWorld world = GetGame().GetWorld();
			if (world)
				msl = world.GetSurfaceY(m_Picker.GetDropX(), m_Picker.GetDropZ()) + jumpAlt;
		}
		vector wind = MHJ_FlightAero.WindWorld(msl, 0);
		if (m_Picker)
			m_Picker.SetWind(wind);

		line = line + " " + MHJ_FlightAero.FormatWindStatus(wind);
		float wSpeed = wind.Length();
		if (wSpeed >= 0.8)
		{
			float fallM = jumpAlt - openAlt;
			float drift = wSpeed * MHJ_FlightAero.EstimateFreefallSeconds(fallM);
			int driftM = Math.Round(drift);
			line = line + " ~" + driftM.ToString() + " m freefall drift.";
		}
		m_Status.SetText(line);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnCancelClicked()
	{
		Close();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnJumpClicked()
	{
		if (!m_Picker || !m_Picker.HasDrop())
		{
			if (m_Status)
			{
				m_Status.SetMuted(false);
				MUI_ThemeData theme = GetTheme();
				if (theme)
					m_Status.SetColor(theme.Danger);
				m_Status.SetText("Select a drop on the map.");
			}
			return;
		}

		float jumpAlt = m_JumpAlt.GetValue();
		float openAlt = m_OpenAlt.GetValue();
		if (jumpAlt < openAlt + MHJ_Constants.OPEN_MARGIN)
		{
			RefreshLabels();
			return;
		}

		vector dropXZ;
		dropXZ[0] = m_Picker.GetDropX();
		dropXZ[1] = 0;
		dropXZ[2] = m_Picker.GetDropZ();

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
		{
			MHJ_Log.Error("No player controller for HALO request");
			return;
		}

		pc.MHJ_AskHaloJump(dropXZ, jumpAlt, openAlt);
		Close();
	}
}
