//------------------------------------------------------------------------------------------------
//! Native MapWidget + SCR_MapEntity planner session for the HALO drop picker.
//! MapWidget is spawned from UI/Layouts/MHJ_MapHost.layout (vanilla MapWidgetClass),
//! as a sibling frame on the menu root. The frame is named MapFrame and hosts
//! UIIconsContainer so paper-map pins and tasks can parent onto it. OpenMap waits
//! until the card intro has settled, then waits until the slot has not moved for
//! a beat, then OpenMap once. Clicks stay in map-widget space; overlay widgets
//! are IGNORE_CURSOR.
//!
//! Consumer: owned by MHJ_HaloJumpMenu only.
//------------------------------------------------------------------------------------------------
class MHJ_MapHost
{
	protected static const int MAP_FRAME_Z = 60;
	protected static const float INSET = 3;
	protected static const float MIN_OPEN_SIZE = 32;
	protected static const float PAN_NAV_PX = 48;
	protected static const float ZOOM_STEP = 1.18;
	protected static const float FOCUS_VIEW_M = 700;
	protected static const int STABLE_MS = 50;
	protected static const float SLOT_MOVE_PX = 8;
	protected static const string MAP_CONTEXT = "MapContext";
	protected static const string ICONS_CONTAINER_NAME = "UIIconsContainer";

	protected Widget m_wMenuRoot;
	protected Widget m_wMapFrame;
	protected Widget m_wMapWidget;
	protected MUI_Runtime m_Runtime;
	protected ref MapItem m_PlayerPip;
	protected ref MapItem m_DropPip;
	protected bool m_bOwnsMap;
	protected bool m_bClosingOwnMap;
	protected bool m_bWantOpen;
	protected bool m_bZoomReady;
	protected bool m_bLive;
	protected float m_fZoomGate;
	protected float m_fLastFx;
	protected float m_fLastFy;
	protected float m_fLastFw;
	protected float m_fLastFh;

	protected bool m_bClosing;
	protected int m_iStableSince;
	protected bool m_bHasFocusTarget;
	protected float m_fFocusX;
	protected float m_fFocusZ;

	//------------------------------------------------------------------------------------------------
	void ~MHJ_MapHost()
	{
		Close();
	}

	//------------------------------------------------------------------------------------------------
	bool Open(notnull Widget menuRoot, notnull MUI_Runtime runtime)
	{
		Close();

		m_wMenuRoot = menuRoot;
		m_Runtime = runtime;
		m_bLive = false;
		m_bZoomReady = false;

		SCR_MapEntity mapEnt = SCR_MapEntity.GetMapInstance();
		if (!mapEnt)
		{
			MHJ_Log.Warning("No SCR_MapEntity; drop picker uses grid fallback");
			return false;
		}

		SCR_MapEntity.GetOnMapOpen().Insert(OnMapOpen);
		SCR_MapEntity.GetOnMapOpenComplete().Insert(OnMapOpenComplete);
		SCR_MapEntity.GetOnMapClose().Insert(OnMapClose);

		m_bWantOpen = true;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	void Close()
	{
		if (m_bClosing)
			return;
		m_bClosing = true;

		SCR_MapEntity.GetOnMapOpen().Remove(OnMapOpen);
		SCR_MapEntity.GetOnMapOpenComplete().Remove(OnMapOpenComplete);
		SCR_MapEntity.GetOnMapClose().Remove(OnMapClose);

		RecyclePips();
		CloseOwnMap();
		ReleaseMapContext();

		if (m_wMapWidget)
		{
			m_wMapWidget.RemoveFromHierarchy();
			m_wMapWidget = null;
		}

		if (m_wMapFrame)
		{
			m_wMapFrame.RemoveFromHierarchy();
			m_wMapFrame = null;
		}

		m_wMenuRoot = null;
		m_Runtime = null;
		m_bLive = false;
		m_bWantOpen = false;
		m_bZoomReady = false;
		m_bOwnsMap = false;
		m_fZoomGate = 0;
		m_fLastFx = -1;
		m_fLastFy = -1;
		m_fLastFw = -1;
		m_fLastFh = -1;
		m_iStableSince = 0;
		m_bHasFocusTarget = false;
		m_fFocusX = 0;
		m_fFocusZ = 0;
		m_bClosing = false;
	}

	//------------------------------------------------------------------------------------------------
	protected void AbortOpen()
	{
		m_bWantOpen = false;
		m_bOwnsMap = false;
		SCR_MapEntity.GetOnMapOpen().Remove(OnMapOpen);
		SCR_MapEntity.GetOnMapOpenComplete().Remove(OnMapOpenComplete);
		SCR_MapEntity.GetOnMapClose().Remove(OnMapClose);
		ReleaseMapContext();
	}

	//------------------------------------------------------------------------------------------------
	bool IsLive()
	{
		return m_bLive;
	}

	//------------------------------------------------------------------------------------------------
	//! Convert DPI-scaled screen mouse to world XZ. Gamepad uses the map view centre.
	bool ScreenToWorldDrop(out float worldX, out float worldZ)
	{
		worldX = 0;
		worldZ = 0;

		if (!m_bLive)
			return false;

		SCR_MapEntity mapEnt = SCR_MapEntity.GetMapInstance();
		if (!mapEnt || !mapEnt.IsOpen())
			return false;

		CanvasWidget canvas = ResolveMapCanvas();
		if (!canvas)
			return false;

		if (!IsMouseAim())
		{
			mapEnt.GetMapCenterWorldPosition(worldX, worldZ);
			return true;
		}

		int mx;
		int my;
		WidgetManager.GetMousePos(mx, my);

		float offX;
		float offY;
		canvas.GetScreenPos(offX, offY);

		int localX = Math.Round(mx - offX);
		int localY = Math.Round(my - offY);
		float wx;
		float wy;
		mapEnt.ScreenToWorld(localX, localY, wx, wy);
		worldX = wx;
		worldZ = wy;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	bool IsMouseAim()
	{
		InputManager im = GetGame().GetInputManager();
		if (!im)
			return true;
		return im.IsUsingMouseAndKeyboard();
	}

	//------------------------------------------------------------------------------------------------
	void PanByNav(int dirX, int dirY)
	{
		SCR_MapEntity mapEnt = LiveMap();
		if (!mapEnt)
			return;

		if (dirX != 0)
			mapEnt.Pan(EMapPanMode.HORIZONTAL, -dirX * PAN_NAV_PX);
		if (dirY != 0)
			mapEnt.Pan(EMapPanMode.VERTICAL, dirY * PAN_NAV_PX);
	}

	//------------------------------------------------------------------------------------------------
	void PanDrag()
	{
		SCR_MapEntity mapEnt = LiveMap();
		if (!mapEnt)
			return;

		SyncCursorFromMouse();
		mapEnt.Pan(EMapPanMode.DRAG);
	}

	//------------------------------------------------------------------------------------------------
	void EndPan()
	{
		SCR_MapCursorInfo.startPos = {0, 0};
	}

	//------------------------------------------------------------------------------------------------
	void ZoomByStep(int dir)
	{
		SCR_MapEntity mapEnt = LiveMap();
		if (!mapEnt)
			return;

		CanvasWidget canvas = ResolveMapCanvas();
		if (!canvas)
			return;
		if (canvas.PixelPerUnit() < 0.0001)
			return;

		float ppu = mapEnt.GetTargetZoomPPU();
		float next = ppu;
		if (dir > 0)
			next = ppu * ZOOM_STEP;
		else
			next = ppu / ZOOM_STEP;

		bool toCenter = true;
		if (IsMouseAim())
		{
			SyncCursorFromMouse();
			toCenter = false;
		}

		mapEnt.ZoomSmooth(next, 0.15, toCenter);
	}

	//------------------------------------------------------------------------------------------------
	//! Pan and zoom the live planner to a world XZ. Safe before the session is live;
	//! the pending target applies after EnsureFit. Zoom shows about FOCUS_VIEW_M
	//! of the short map edge so nearby terrain stays readable.
	void FocusWorld(float worldX, float worldZ)
	{
		m_bHasFocusTarget = true;
		m_fFocusX = worldX;
		m_fFocusZ = worldZ;
		TryApplyFocus();
	}

	//------------------------------------------------------------------------------------------------
	void Tick(MHJ_MapPicker picker)
	{
		if (!picker)
			return;
		if (!m_Runtime)
			return;

		if (!IsPickerSettled(picker))
		{
			if (m_wMapFrame)
				m_wMapFrame.SetVisible(false);
			m_iStableSince = 0;
			return;
		}

		if (!m_wMapFrame)
		{
			if (!SpawnMapFrame())
			{
				MHJ_Log.Error("Failed to create HALO map frame; using grid fallback");
				AbortOpen();
				return;
			}
		}

		if (!m_bLive)
		{
			bool slotMoved = SyncFrame(picker);
			if (slotMoved)
			{
				m_iStableSince = 0;
			}
			else if (m_iStableSince == 0)
			{
				m_iStableSince = System.GetTickCount();
			}
		}

		if (m_bWantOpen && !m_bOwnsMap)
		{
			if (m_iStableSince > 0)
			{
				if (System.GetTickCount() - m_iStableSince >= STABLE_MS)
					TryOpenMap();
			}
		}

		if (m_bLive && IsMouseAim())
			SyncCursorFromMouse();

		ActivateMapContext(picker);
		TickGamepadLook(picker);

		if (!m_bLive)
			return;

		SCR_MapEntity mapEnt = SCR_MapEntity.GetMapInstance();
		if (!mapEnt || !mapEnt.IsOpen())
			return;

		if (!ResolveMapCanvas())
			return;

		EnsureFit(mapEnt);
		TryApplyFocus();
		EnsurePips(mapEnt);
		UpdatePips(picker);
		UpdatePrompts(picker);
	}

	//------------------------------------------------------------------------------------------------
	protected void TryApplyFocus()
	{
		if (!m_bHasFocusTarget)
			return;
		if (!m_bLive)
			return;
		if (!m_bZoomReady)
			return;

		SCR_MapEntity mapEnt = LiveMap();
		if (!mapEnt)
			return;

		float target = 1;
		CanvasWidget canvas = ResolveMapCanvas();
		if (canvas)
		{
			float screenW;
			float screenH;
			canvas.GetScreenSize(screenW, screenH);
			float shortSide = screenH;
			if (screenW < screenH)
				shortSide = screenW;
			if (shortSide > 8)
				target = shortSide / FOCUS_VIEW_M;
		}
		float maxZ = mapEnt.GetMaxZoom();
		float minZ = mapEnt.GetMinZoom();
		if (target > maxZ)
			target = maxZ;
		if (target < minZ)
			target = minZ;
		if (target < 0.0001)
			return;

		mapEnt.ZoomPanSmooth(target, m_fFocusX, m_fFocusZ, 0.28);
		m_bHasFocusTarget = false;
	}

	//------------------------------------------------------------------------------------------------
	protected void TryOpenMap()
	{
		if (!m_bWantOpen)
			return;
		if (m_bOwnsMap)
			return;
		if (m_fLastFw < MIN_OPEN_SIZE)
			return;
		if (m_fLastFh < MIN_OPEN_SIZE)
			return;
		if (!m_wMenuRoot)
			return;

		CanvasWidget canvas = FindRootMapCanvas();
		if (!canvas)
		{
			MHJ_Log.Error("FindAnyWidget missed MapWidget; using grid fallback");
			AbortOpen();
			return;
		}

		SCR_MapEntity mapEnt = SCR_MapEntity.GetMapInstance();
		if (!mapEnt)
		{
			MHJ_Log.Warning("No SCR_MapEntity; drop picker uses grid fallback");
			m_bWantOpen = false;
			return;
		}

		if (mapEnt.IsOpen())
		{
			MapConfiguration openCfg = mapEnt.GetMapConfig();
			if (!IsPlannerConfig(openCfg))
			{
				MHJ_Log.Warning("Another map is open; drop picker uses grid fallback");
				AbortOpen();
				return;
			}

			mapEnt.CloseMap();
		}

		MapConfiguration cfg = mapEnt.SetupMapConfig(EMapEntityMode.MHJ_PLANNER, MHJ_Resources.OWN_PLAIN_MAP_CONF, m_wMenuRoot);
		if (!cfg)
			cfg = mapEnt.SetupMapConfig(EMapEntityMode.PLAIN, MHJ_Resources.PLAIN_MAP_CONF, m_wMenuRoot);
		if (!cfg)
		{
			MHJ_Log.Error("SetupMapConfig failed for HALO planner map");
			AbortOpen();
			return;
		}

		mapEnt.SetMapWidget(canvas);
		MHJ_Log.Info("OpenMap planner");
		mapEnt.OpenMap(cfg);
		mapEnt.SetMapWidget(canvas);
		if (!mapEnt.IsOpen())
		{
			MHJ_Log.Error("OpenMap aborted without a canvas; using grid fallback");
			AbortOpen();
			return;
		}

		if (!mapEnt.GetMapWidget())
		{
			MHJ_Log.Error("OpenMap dropped MapWidget; using grid fallback");
			mapEnt.CloseMap();
			AbortOpen();
			return;
		}

		m_bOwnsMap = true;
	}

	//------------------------------------------------------------------------------------------------
	protected SCR_MapEntity LiveMap()
	{
		if (!m_bLive)
			return null;

		SCR_MapEntity mapEnt = SCR_MapEntity.GetMapInstance();
		if (!mapEnt || !mapEnt.IsOpen())
			return null;
		return mapEnt;
	}

	//------------------------------------------------------------------------------------------------
	protected void ActivateMapContext(MHJ_MapPicker picker)
	{
		if (!m_bLive)
			return;

		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;
		if (m_Runtime && m_Runtime.IsEditing())
			return;

		if (!IsMouseAim())
		{
			if (!picker || !picker.IsFocused())
				return;
		}

		im.ActivateContext(MAP_CONTEXT);
	}

	//------------------------------------------------------------------------------------------------
	protected void SyncCursorFromMouse()
	{
		CanvasWidget canvas = ResolveMapCanvas();
		if (!canvas)
			return;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return;

		int mx;
		int my;
		WidgetManager.GetMousePos(mx, my);

		float offX;
		float offY;
		canvas.GetScreenPos(offX, offY);

		SCR_MapCursorInfo.x = workspace.DPIUnscale(mx - offX);
		SCR_MapCursorInfo.y = workspace.DPIUnscale(my - offY);
	}

	//------------------------------------------------------------------------------------------------
	protected void TickGamepadLook(MHJ_MapPicker picker)
	{
		if (!picker)
			return;
		if (!picker.IsFocused())
			return;
		if (IsMouseAim())
			return;

		SCR_MapEntity mapEnt = LiveMap();
		if (!mapEnt)
			return;

		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;

		float dt = System.GetFrameTimeS();
		if (dt < 0.001)
			dt = 0.001;

		float stickPx = 900 * dt;
		float hx = im.GetActionValue("MapPanHGamepad");
		float hy = im.GetActionValue("MapPanVGamepad");
		if (Math.AbsFloat(hx) < 0.2)
			hx = im.GetActionValue("MapGamepadCursorX");
		if (Math.AbsFloat(hy) < 0.2)
			hy = im.GetActionValue("MapGamepadCursorY");
		if (Math.AbsFloat(hx) > 0.2)
			mapEnt.Pan(EMapPanMode.HORIZONTAL, -hx * stickPx);
		if (Math.AbsFloat(hy) > 0.2)
			mapEnt.Pan(EMapPanMode.VERTICAL, -hy * stickPx);

		m_fZoomGate = m_fZoomGate - dt;
		float zoomIn = im.GetActionValue("MapZoomIn");
		float zoomOut = im.GetActionValue("MapZoomOut");
		if (m_fZoomGate <= 0)
		{
			if (zoomIn > 0.4)
			{
				ZoomByStep(1);
				m_fZoomGate = 0.09;
			}
			else if (zoomOut > 0.4)
			{
				ZoomByStep(-1);
				m_fZoomGate = 0.09;
			}
		}

		CenterCursorOnMap();
	}

	//------------------------------------------------------------------------------------------------
	protected void CenterCursorOnMap()
	{
		CanvasWidget canvas = ResolveMapCanvas();
		if (!canvas)
			return;

		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;

		MouseDeviceHandler mouse = im.GetMouseDeviceHandler();
		if (!mouse)
			return;

		float sx;
		float sy;
		float px;
		float py;
		canvas.GetScreenSize(sx, sy);
		canvas.GetScreenPos(px, py);
		mouse.SetCursorPosition(px + sx * 0.5, py + sy * 0.5);
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdatePrompts(MHJ_MapPicker picker)
	{
		if (!m_Runtime)
			return;
		if (!picker)
			return;

		if (picker.IsFocused())
		{
			m_Runtime.SetPromptText("<action name='MenuSelect' scale='1.35'/>  Set drop", "<action name='MenuBack' scale='1.35'/>  Back");
			return;
		}

		m_Runtime.SetPromptText("<action name='MenuSelect' scale='1.35'/>  Select", "<action name='MenuBack' scale='1.35'/>  Back");
	}

	//------------------------------------------------------------------------------------------------
	protected bool SpawnMapFrame()
	{
		if (!m_wMenuRoot)
			return false;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return false;

		m_wMapFrame = workspace.CreateWidgets(MHJ_Resources.MAP_HOST_LAYOUT, m_wMenuRoot);
		if (!m_wMapFrame)
		{
			MHJ_Log.Error("CreateWidgets failed for MHJ_MapHost.layout");
			return false;
		}

		m_wMapFrame.SetName(SCR_MapConstants.MAP_FRAME_NAME);
		m_wMapFrame.SetZOrder(MAP_FRAME_Z);
		m_wMapFrame.SetFlags(WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR);
		m_wMapFrame.SetColor(Color.FromSRGBA(18, 42, 48, 255));
		m_wMapFrame.SetVisible(false);
		FrameSlot.SetAnchorMin(m_wMapFrame, 0, 0);
		FrameSlot.SetAnchorMax(m_wMapFrame, 0, 0);
		FrameSlot.SetPos(m_wMapFrame, 0, 0);
		FrameSlot.SetSize(m_wMapFrame, 8, 8);

		m_wMapWidget = m_wMapFrame.FindAnyWidget(SCR_MapConstants.MAP_WIDGET_NAME);
		if (!m_wMapWidget)
		{
			MHJ_Log.Error("Layout has no MapWidget");
			m_wMapFrame.RemoveFromHierarchy();
			m_wMapFrame = null;
			return false;
		}

		m_wMapWidget.SetFlags(WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR);

		Widget icons = m_wMapFrame.FindAnyWidget(ICONS_CONTAINER_NAME);
		if (icons)
			icons.SetFlags(WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR);

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsPickerSettled(notnull MHJ_MapPicker picker)
	{
		MUI_Node node = picker;
		while (node)
		{
			if (node.GetIntro() < 0.999)
				return false;
			node = node.GetParent();
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected bool SyncFrame(notnull MHJ_MapPicker picker)
	{
		if (!m_wMapFrame || !m_Runtime)
			return false;

		float x;
		float y;
		m_Runtime.GetHostLocalPos(picker, x, y);
		float w = picker.GetWorldRect().m_fW;
		float h = picker.GetWorldRect().m_fH;
		if (w < 8 || h < 8)
		{
			m_wMapFrame.SetVisible(false);
			return false;
		}

		float inset = INSET;
		if (w < inset * 2 + 8)
			inset = 0;
		if (h < inset * 2 + 8)
			inset = 0;

		float fx = x + inset;
		float fy = y + inset;
		float fw = w - inset * 2;
		float fh = h - inset * 2;
		bool moved = SlotMoved(fx, fy, fw, fh);
		if (!moved)
			return false;

		m_fLastFx = fx;
		m_fLastFy = fy;
		m_fLastFw = fw;
		m_fLastFh = fh;

		m_wMapFrame.SetVisible(true);
		FrameSlot.SetAnchorMin(m_wMapFrame, 0, 0);
		FrameSlot.SetAnchorMax(m_wMapFrame, 0, 0);
		FrameSlot.SetPos(m_wMapFrame, fx, fy);
		FrameSlot.SetSize(m_wMapFrame, fw, fh);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected bool SlotMoved(float fx, float fy, float fw, float fh)
	{
		if (Math.AbsFloat(fw - m_fLastFw) >= SLOT_MOVE_PX)
			return true;
		if (Math.AbsFloat(fh - m_fLastFh) >= SLOT_MOVE_PX)
			return true;
		if (Math.AbsFloat(fx - m_fLastFx) >= SLOT_MOVE_PX)
			return true;
		if (Math.AbsFloat(fy - m_fLastFy) >= SLOT_MOVE_PX)
			return true;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsPlannerConfig(MapConfiguration config)
	{
		if (!config)
			return false;
		if (config.MapEntityMode == EMapEntityMode.MHJ_PLANNER)
			return true;
		if (config.MapEntityMode == EMapEntityMode.PLAIN)
			return true;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected CanvasWidget FindRootMapCanvas()
	{
		if (!m_wMenuRoot)
			return null;

		return CanvasWidget.Cast(m_wMenuRoot.FindAnyWidget(SCR_MapConstants.MAP_WIDGET_NAME));
	}

	//------------------------------------------------------------------------------------------------
	protected CanvasWidget ResolveMapCanvas()
	{
		CanvasWidget foundCanvas = FindRootMapCanvas();
		if (foundCanvas)
			m_wMapWidget = foundCanvas;

		return CanvasWidget.Cast(m_wMapWidget);
	}

	//------------------------------------------------------------------------------------------------
	protected void CloseOwnMap()
	{
		SCR_MapEntity mapEnt = SCR_MapEntity.GetMapInstance();
		if (!mapEnt)
		{
			m_bOwnsMap = false;
			m_bLive = false;
			return;
		}

		bool ours = m_bOwnsMap;
		if (!ours && mapEnt.IsOpen())
		{
			CanvasWidget oursCanvas = CanvasWidget.Cast(m_wMapWidget);
			if (oursCanvas && mapEnt.GetMapWidget() == oursCanvas)
				ours = true;
		}

		if (ours && mapEnt.IsOpen())
		{
			m_bClosingOwnMap = true;
			mapEnt.CloseMap();
			mapEnt.SetMapWidget(null);
			m_bClosingOwnMap = false;
		}

		m_bOwnsMap = false;
		m_bLive = false;
		m_bZoomReady = false;
		ReleaseMapContext();
	}

	//------------------------------------------------------------------------------------------------
	protected void ReleaseMapContext()
	{
		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;

		im.ResetContext(MAP_CONTEXT);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnMapOpen(MapConfiguration config)
	{
		if (!config)
			return;

		if (!IsPlannerConfig(config))
		{
			if (m_bOwnsMap || m_bWantOpen)
			{
				m_bOwnsMap = false;
				m_bLive = false;
				m_bWantOpen = false;
				MHJ_HaloJumpMenu.Close();
			}
			return;
		}

		if (!m_bWantOpen && !m_bOwnsMap)
			return;

		if (!ResolveMapCanvas())
		{
			MHJ_Log.Error("Planner map opened without a canvas");
			m_bWantOpen = false;
			m_bOwnsMap = false;
			m_bLive = false;
			return;
		}

		m_bOwnsMap = true;
		m_bWantOpen = false;
		m_bLive = true;
		m_bZoomReady = false;
		ApplyFitAndPips(SCR_MapEntity.GetMapInstance());
	}

	//------------------------------------------------------------------------------------------------
	protected void OnMapOpenComplete(MapConfiguration config)
	{
		if (!config)
			return;
		if (!IsPlannerConfig(config))
			return;
		if (!m_bOwnsMap)
			return;

		if (m_wMenuRoot)
		{
			Widget icons = m_wMenuRoot.FindAnyWidget(ICONS_CONTAINER_NAME);
			if (!icons)
				MHJ_Log.Warning("Planner UIIconsContainer missing; paper-map tasks will not show");
		}

		ApplyFitAndPips(SCR_MapEntity.GetMapInstance());
		MHJ_HaloJumpMenu.NotifyPlannerMapReady();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnMapClose(MapConfiguration config)
	{
		bool wasOurs = false;
		if (m_bOwnsMap)
			wasOurs = true;
		if (m_bLive)
			wasOurs = true;

		m_bOwnsMap = false;
		m_bLive = false;
		m_bZoomReady = false;
		RecyclePips();

		if (m_bClosingOwnMap)
			return;
		if (m_bWantOpen)
			return;
		if (!wasOurs)
			return;

		MHJ_HaloJumpMenu.Close();
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyFitAndPips(SCR_MapEntity mapEnt)
	{
		if (!mapEnt)
			return;
		if (!mapEnt.IsOpen())
			return;
		if (!ResolveMapCanvas())
			return;

		BindMapWidget(mapEnt);
		EnsureFit(mapEnt);
		EnsurePips(mapEnt);
	}

	//------------------------------------------------------------------------------------------------
	protected void BindMapWidget(notnull SCR_MapEntity mapEnt)
	{
		CanvasWidget canvas = ResolveMapCanvas();
		if (canvas)
			mapEnt.SetMapWidget(canvas);
	}

	//------------------------------------------------------------------------------------------------
	protected void EnsureFit(notnull SCR_MapEntity mapEnt)
	{
		if (m_bZoomReady)
			return;

		CanvasWidget canvas = ResolveMapCanvas();
		if (!canvas)
			return;

		float screenW;
		float screenH;
		canvas.GetScreenSize(screenW, screenH);
		if (screenH < 8)
			return;
		if (screenW < 8)
			return;

		if (!mapEnt.UpdateZoomBounds())
			return;

		float minZoom = mapEnt.GetMinZoom();
		if (minZoom < 0.0001)
			return;

		mapEnt.ZoomOut();
		m_bZoomReady = true;
		if (m_wMapFrame)
			m_wMapFrame.SetVisible(true);
		if (m_wMapWidget)
			m_wMapWidget.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	protected void EnsurePips(notnull SCR_MapEntity mapEnt)
	{
		if (!m_PlayerPip)
		{
			m_PlayerPip = mapEnt.CreateCustomMapItem();
			if (m_PlayerPip)
			{
				m_PlayerPip.SetBaseType(EMapDescriptorType.MDT_CURPOS);
				m_PlayerPip.SetVisible(true);
				MapDescriptorProps props = m_PlayerPip.GetProps();
				if (props)
				{
					props.SetFrontColor(Color.FromSRGBA(89, 235, 224, 255));
					props.SetOutlineColor(Color.FromSRGBA(0, 0, 0, 255));
					props.SetIconSize(1, 0.4, 0.4);
					props.Activate(true);
					m_PlayerPip.SetProps(props);
				}
			}
		}

		if (!m_DropPip)
		{
			m_DropPip = mapEnt.CreateCustomMapItem();
			if (m_DropPip)
			{
				m_DropPip.SetBaseType(EMapDescriptorType.MDT_WAYPOINT);
				m_DropPip.SetVisible(true);
				MapDescriptorProps props = m_DropPip.GetProps();
				if (props)
				{
					props.SetFrontColor(Color.FromSRGBA(237, 158, 41, 255));
					props.SetOutlineColor(Color.FromSRGBA(0, 0, 0, 255));
					props.SetIconSize(1, 0.55, 0.55);
					props.Activate(true);
					m_DropPip.SetProps(props);
				}
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdatePips(MHJ_MapPicker picker)
	{
		IEntity player = SCR_PlayerController.GetLocalControlledEntity();
		if (!player)
			player = SCR_PlayerController.GetLocalMainEntity();

		if (m_PlayerPip && player)
		{
			vector pos = player.GetOrigin();
			m_PlayerPip.SetPos(pos[0], pos[2]);
			m_PlayerPip.SetAngle(player.GetAngles()[1]);
			m_PlayerPip.SetVisible(true);
		}

		if (m_DropPip && picker && picker.HasDrop())
		{
			m_DropPip.SetPos(picker.GetDropX(), picker.GetDropZ());
			m_DropPip.SetVisible(true);
		}
		else if (m_DropPip)
		{
			m_DropPip.SetVisible(false);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void RecyclePips()
	{
		if (m_PlayerPip)
		{
			m_PlayerPip.Recycle();
			m_PlayerPip = null;
		}

		if (m_DropPip)
		{
			m_DropPip.Recycle();
			m_DropPip = null;
		}
	}
}
