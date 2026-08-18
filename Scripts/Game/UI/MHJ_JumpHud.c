//------------------------------------------------------------------------------------------------
//! Pass-through altitude HUD while a local HALO jump is in progress.
//------------------------------------------------------------------------------------------------
class MHJ_JumpHud
{
	protected static ref MHJ_JumpHud s_Instance;

	protected Widget m_wRoot;
	protected ref MUI_HudHost m_Host;
	protected ref MUI_Label m_PhaseLabel;
	protected ref MUI_Label m_AltLabel;
	protected ref MUI_Label m_WindLabel;
	protected ref MUI_Progress m_AltBar;
	protected MHJ_EHaloPhase m_ePhase;
	protected float m_fAgl;
	protected float m_fOpenAlt;
	protected float m_fAirspeed;
	protected float m_fVelY;
	protected float m_fWindSpeed;
	protected string m_sMode;
	protected string m_sWindRel;
	protected bool m_bClosing;

	//------------------------------------------------------------------------------------------------
	void ~MHJ_JumpHud()
	{
		CloseInternal();
		if (s_Instance == this)
			s_Instance = null;
	}

	//------------------------------------------------------------------------------------------------
	static void Open()
	{
		if (s_Instance)
		{
			s_Instance.CloseInternal();
			s_Instance = null;
		}

		ref MHJ_JumpHud hud = new MHJ_JumpHud();
		if (!hud.OpenInternal())
			return;
		s_Instance = hud;
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
	static void SetState(MHJ_EHaloPhase phase, float agl, float openAlt, float airspeed, float velY, string mode, float windSpeed, string windRel)
	{
		if (!s_Instance)
			return;
		s_Instance.m_ePhase = phase;
		s_Instance.m_fAgl = agl;
		s_Instance.m_fOpenAlt = openAlt;
		s_Instance.m_fAirspeed = airspeed;
		s_Instance.m_fVelY = velY;
		s_Instance.m_sMode = mode;
		s_Instance.m_fWindSpeed = windSpeed;
		s_Instance.m_sWindRel = windRel;
		s_Instance.Refresh();
	}

	//------------------------------------------------------------------------------------------------
	protected bool OpenInternal()
	{
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return false;

		m_wRoot = workspace.CreateWidget(WidgetType.FrameWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR, Color.FromInt(Color.WHITE), 40, workspace);
		if (!m_wRoot)
			return false;

		FrameSlot.SetAnchorMin(m_wRoot, 0, 0);
		FrameSlot.SetAnchorMax(m_wRoot, 1, 1);
		FrameSlot.SetOffsets(m_wRoot, 0, 0, 0, 0);
		m_wRoot.SetFlags(WidgetFlags.IGNORE_CURSOR);

		m_Host = new MUI_HudHost();
		if (!m_Host.Open(m_wRoot, "MHJ", false))
		{
			m_Host = null;
			m_wRoot.RemoveFromHierarchy();
			m_wRoot = null;
			MHJ_Log.Error("Jump HUD mount failed");
			return false;
		}

		BuildUI(m_Host.GetRuntime());
		GetGame().GetCallqueue().CallLater(this.TickQueued, 1, true);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void CloseInternal()
	{
		if (m_bClosing)
			return;
		m_bClosing = true;

		GetGame().GetCallqueue().Remove(this.TickQueued);
		m_PhaseLabel = null;
		m_AltLabel = null;
		m_WindLabel = null;
		m_AltBar = null;
		m_sMode = "";
		m_sWindRel = "";
		if (m_Host)
		{
			m_Host.Close();
			m_Host = null;
		}
		if (m_wRoot)
		{
			m_wRoot.RemoveFromHierarchy();
			m_wRoot = null;
		}
		m_bClosing = false;
	}

	//------------------------------------------------------------------------------------------------
	protected void TickQueued()
	{
		if (m_Host)
			m_Host.Tick(System.GetFrameTimeS());
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildUI(notnull MUI_Runtime runtime)
	{
		ref MUI_Panel overlay = runtime.CreatePanel("overlay");
		overlay.MakePassThroughOverlay();

		ref MUI_Surface card = runtime.CreateSurface("hudCard");
		card.SetWidth(340);
		card.SetPadding(14);
		card.SetGap(6);
		card.SetAlign(0.5, 0.08);
		card.SetRadius(10);
		card.GetStyle().m_bBlockHit = false;

		m_PhaseLabel = runtime.CreateLabel("EXIT", "phase");
		m_PhaseLabel.SetBold(true);
		m_PhaseLabel.SetColor(runtime.GetTheme().Cyan);
		m_PhaseLabel.GetStyle().m_bBlockHit = false;

		m_AltLabel = runtime.CreateLabel("AGL  --- m", "agl");
		m_AltLabel.SetFontSize(runtime.GetTheme().FONT_SMALL);
		m_AltLabel.SetMuted(true);
		m_AltLabel.GetStyle().m_bBlockHit = false;

		m_WindLabel = runtime.CreateLabel("WIND  ---", "wind");
		m_WindLabel.SetFontSize(runtime.GetTheme().FONT_SMALL);
		m_WindLabel.SetMuted(true);
		m_WindLabel.GetStyle().m_bBlockHit = false;

		m_AltBar = runtime.CreateProgress("aglBar");
		m_AltBar.SetValue(1);
		m_AltBar.GetStyle().m_bBlockHit = false;

		card.AddChild(m_PhaseLabel);
		card.AddChild(m_AltLabel);
		card.AddChild(m_WindLabel);
		card.AddChild(m_AltBar);
		overlay.AddChild(card);
		runtime.SetRoot(overlay);
	}

	//------------------------------------------------------------------------------------------------
	protected void Refresh()
	{
		string mode = m_sMode;
		if (mode == "")
			mode = "FREEFALL";
		if (m_PhaseLabel)
		{
			m_PhaseLabel.SetText(mode);
			ApplyPhaseColor(mode);
		}

		int aglM = m_fAgl;
		int vs = m_fVelY;
		int kmh = m_fAirspeed * 3.6;
		if (m_AltLabel)
			m_AltLabel.SetText("AGL  " + aglM.ToString() + " m    VS  " + vs.ToString() + " m/s    TAS  " + kmh.ToString() + " km/h");

		if (m_WindLabel)
		{
			string rel = m_sWindRel;
			if (rel == "")
				rel = "CALM";
			int w = Math.Round(m_fWindSpeed);
			m_WindLabel.SetText("WIND  " + w.ToString() + " m/s    " + rel);
		}

		if (m_AltBar)
		{
			float span = m_fOpenAlt * 4;
			if (span < 400)
				span = 400;
			float t = m_fAgl / span;
			if (t < 0)
				t = 0;
			if (t > 1)
				t = 1;
			m_AltBar.SetValue(t);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyPhaseColor(string mode)
	{
		if (!m_PhaseLabel)
			return;
		if (!m_Host)
			return;
		MUI_Runtime runtime = m_Host.GetRuntime();
		if (!runtime)
			return;
		MUI_ThemeData theme = runtime.GetTheme();
		if (!theme)
			return;

		Color c = theme.Cyan;
		if (mode == "STALL")
			c = theme.Danger;
		else if (mode == "FLARE")
			c = theme.Accent;
		else if (mode == "DIVE")
			c = theme.Accent;
		else if (mode == "TRACKING")
			c = theme.Live;
		else if (mode == "GLIDE")
			c = theme.Live;
		else if (mode == "OPENING")
			c = theme.Accent;
		m_PhaseLabel.SetColor(c);
	}
}
