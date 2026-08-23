//------------------------------------------------------------------------------------------------
//! HALO drop planner. Hosted on the workspace so Workbench Play does not need
//! chimeraMenus.conf (MenuManager never loads addon presets there).
//! Left options rail + right-pane picker. Native MapWidget is owned by MHJ_MapHost
//! and framed over the picker slot so click-to-drop covers the full right pane.
//! Named drop sites come from MHJ_DropSiteCatalog (I&A fills it). Empty catalog hides the list.
//! Root z-order is above HUD ALWAYS_TOP (100) so Role Selection / Role Switcher
//! stay behind this overlay.
//------------------------------------------------------------------------------------------------
class MHJ_HaloJumpMenu
{
	protected static const int MENU_Z = 110;

	protected static ref MHJ_HaloJumpMenu s_Instance;

	protected Widget m_wRoot;
	protected ref MUI_MenuHost m_Host;
	protected ref MHJ_MapHost m_Map;
	protected ref MHJ_MapPicker m_Picker;
	protected ref MUI_Label m_CoordLabel;
	protected ref MUI_NumericField m_JumpAlt;
	protected ref MUI_NumericField m_OpenAlt;
	protected ref MUI_Label m_Status;
	protected ref MUI_Label m_SitesLabel;
	protected ref MUI_ScrollView m_SitesList;
	protected ref array<ref MHJ_DropSite> m_aSites;
	protected ref array<ref MHJ_DropSiteBind> m_aSiteBinds;
	protected ref array<ref MUI_Button> m_aSiteButtons;
	protected int m_iSelectedSite;
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
	static void NotifyPlannerMapReady()
	{
		if (!s_Instance)
			return;
		s_Instance.OnPlannerMapReady();
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

		m_wRoot = workspace.CreateWidget(WidgetType.FrameWidgetTypeID, WidgetFlags.VISIBLE, Color.FromInt(Color.WHITE), MENU_Z, workspace);
		if (!m_wRoot)
			return false;

		m_wRoot.SetZOrder(MENU_Z);
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
			if (m_Picker && m_Picker.HasDrop())
				m_Map.FocusWorld(m_Picker.GetDropX(), m_Picker.GetDropZ());
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
		m_SitesLabel = null;
		m_SitesList = null;
		if (m_aSiteBinds)
			m_aSiteBinds.Clear();
		if (m_aSiteButtons)
			m_aSiteButtons.Clear();
		if (m_aSites)
			m_aSites.Clear();
		m_iSelectedSite = -1;
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
		if (m_wRoot)
			m_wRoot.SetZOrder(MENU_Z);
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

		ref MUI_Row split = runtime.CreateRow("split");
		split.SetFillWidth();
		split.SetFillHeight();
		split.SetGap(16);
		split.SetPadding(20);
		split.SetIntro(0.06, 0.55, 46);

		ref MUI_Card card = runtime.CreateCard("card");
		card.SetWidth(440);
		card.SetFillHeight();
		card.SetPadding(28);
		card.SetPaddingTRBL(22, 28, 24, 28);
		card.SetGap(12);
		card.SetIntro(0.16, 0.4, 18);

		ref MUI_LiveHeader liveHeader = runtime.CreateLiveHeader("HALO JUMP", "liveHeader");
		liveHeader.SetKicker("MIKE'S HALO  //  DROP PLANNER");
		liveHeader.SetIntro(0.22, 0.4, 18);

		ref MUI_Label subtitle = runtime.CreateLabel("Pick a drop site or click the map. Drag pans, wheel zooms. Controller: D-pad the list, A selects, stick pans, triggers zoom.", "subtitle");
		subtitle.SetFontSize(MUI_Theme.FONT_SMALL);
		subtitle.SetMuted(true);

		ref MUI_Hairline lineA = runtime.CreateHairline("lineA");

		m_aSites = new array<ref MHJ_DropSite>();
		m_aSiteBinds = new array<ref MHJ_DropSiteBind>();
		m_aSiteButtons = new array<ref MUI_Button>();
		m_iSelectedSite = -1;
		MHJ_DropSiteCatalog.Collect(m_aSites);

		if (m_aSites.Count() > 0)
		{
			m_SitesLabel = runtime.CreateLabel("DROP SITES", "sitesLabel");
			m_SitesLabel.SetFontSize(MUI_Theme.FONT_SMALL);
			m_SitesLabel.SetMuted(true);

			m_SitesList = runtime.CreateScrollView("sitesList");
			m_SitesList.SetGap(6);
			int siteCount = m_aSites.Count();
			float listH = siteCount * 50;
			if (listH > 360)
				listH = 360;
			if (listH < 80)
				listH = 80;
			m_SitesList.SetViewportHeight(listH);
			BuildSiteButtons(runtime);
		}

		m_Picker = new MHJ_MapPicker();
		runtime.Adopt(m_Picker);
		m_Picker.SetName("picker");
		m_Picker.InitWorld();
		m_Picker.GetOnChanged().Insert(OnPickerChanged);
		m_Picker.SetFillWidth();
		m_Picker.SetFillHeight();
		m_Picker.SetGrow(1);

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
		if (m_SitesLabel)
			card.AddChild(m_SitesLabel);
		if (m_SitesList)
			card.AddChild(m_SitesList);
		card.AddChild(m_CoordLabel);
		card.AddChild(m_JumpAlt);
		card.AddChild(m_OpenAlt);
		card.AddChild(m_Status);
		ref MUI_Spacer railGrow = runtime.CreateSpacer(0, "railGrow");
		railGrow.SetFillHeight();
		railGrow.SetGrow(1);
		card.AddChild(railGrow);
		card.AddChild(lineB);
		card.AddChild(buttons);

		split.AddChild(card);
		split.AddChild(m_Picker);

		overlay.AddChild(fx);
		overlay.AddChild(split);
		runtime.SetRoot(overlay);

		if (m_aSites && m_aSites.Count() > 0)
			SelectDropSite(0);
		else
			RefreshLabels();
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildSiteButtons(notnull MUI_Runtime runtime)
	{
		if (!m_SitesList)
			return;
		if (!m_aSites)
			return;

		int count = m_aSites.Count();
		int i;
		for (i = 0; i < count; i++)
		{
			MHJ_DropSite site = m_aSites[i];
			if (!site)
				continue;

			string label = site.m_sName;
			if (label.IsEmpty())
				label = "Drop " + (i + 1).ToString();

			ref MUI_Button btn = runtime.CreateButton(label, "site_" + i.ToString());
			btn.SetFillWidth();
			btn.SetGrow(0);
			m_SitesList.AddChild(btn);
			m_aSiteButtons.Insert(btn);

			ref MHJ_DropSiteBind bind = new MHJ_DropSiteBind();
			bind.Init(this, i);
			btn.GetOnClicked().Insert(bind.OnClicked);
			m_aSiteBinds.Insert(bind);
		}
	}

	//------------------------------------------------------------------------------------------------
	void SelectDropSite(int index)
	{
		if (!m_aSites)
			return;
		if (index < 0)
			return;
		if (index >= m_aSites.Count())
			return;

		MHJ_DropSite site = m_aSites[index];
		if (!site)
			return;

		m_iSelectedSite = index;
		if (m_Picker)
			m_Picker.SetDrop(site.m_fX, site.m_fZ);
		if (m_Map)
			m_Map.FocusWorld(site.m_fX, site.m_fZ);

		int btnCount = 0;
		if (m_aSiteButtons)
			btnCount = m_aSiteButtons.Count();
		int b;
		for (b = 0; b < btnCount; b++)
		{
			MUI_Button btn = m_aSiteButtons[b];
			if (!btn)
				continue;
			if (b == index)
				btn.MakeAccent();
			else
				btn.MakeDefault();
		}

		RefreshLabels();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnPlannerMapReady()
	{
		if (!m_aSites)
			return;
		if (!m_SitesList)
			return;
		if (!m_Host)
			return;

		MUI_Runtime runtime = m_Host.GetRuntime();
		if (!runtime)
			return;

		ref array<ref MHJ_DropSite> incoming = new array<ref MHJ_DropSite>();
		MHJ_DropSiteCatalog.Collect(incoming);
		int incomingCount = incoming.Count();
		int i;
		for (i = 0; i < incomingCount; i++)
		{
			MHJ_DropSite site = incoming[i];
			if (!site)
				continue;
			if (HasSite(site.m_sName, site.m_fX, site.m_fZ))
				continue;
			if (m_aSites.Count() >= 16)
				break;

			m_aSites.Insert(site);
			AppendSiteButton(runtime, site, m_aSites.Count() - 1);
		}

		int btnCount = 0;
		if (m_aSiteButtons)
			btnCount = m_aSiteButtons.Count();
		int b;
		for (b = 0; b < btnCount; b++)
		{
			MUI_Button btn = m_aSiteButtons[b];
			if (!btn)
				continue;
			if (b == m_iSelectedSite)
				btn.MakeAccent();
			else
				btn.MakeDefault();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool HasSite(string name, float x, float z)
	{
		if (!m_aSites)
			return false;

		float sameSq = 45 * 45;
		int count = m_aSites.Count();
		int i;
		for (i = 0; i < count; i++)
		{
			MHJ_DropSite existing = m_aSites[i];
			if (!existing)
				continue;
			float dx = existing.m_fX - x;
			float dz = existing.m_fZ - z;
			if (dx * dx + dz * dz <= sameSq)
				return true;
			if (existing.m_sName == name && dx * dx + dz * dz <= 180 * 180)
				return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void AppendSiteButton(notnull MUI_Runtime runtime, notnull MHJ_DropSite site, int index)
	{
		if (!m_SitesList)
			return;
		if (!m_aSiteButtons)
			return;
		if (!m_aSiteBinds)
			return;

		string label = site.m_sName;
		if (label.IsEmpty())
			label = "Drop " + (index + 1).ToString();

		ref MUI_Button btn = runtime.CreateButton(label, "site_" + index.ToString());
		btn.SetFillWidth();
		btn.SetGrow(0);
		m_SitesList.AddChild(btn);
		m_aSiteButtons.Insert(btn);

		ref MHJ_DropSiteBind bind = new MHJ_DropSiteBind();
		bind.Init(this, index);
		btn.GetOnClicked().Insert(bind.OnClicked);
		m_aSiteBinds.Insert(bind);
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
