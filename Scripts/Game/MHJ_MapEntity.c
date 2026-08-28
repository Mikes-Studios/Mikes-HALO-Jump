//------------------------------------------------------------------------------------------------
//! Vanilla OpenMap binds the canvas with FindAnyWidget("MapWidget") then SetFrame /
//! delayed SetSizeInUnits with no null check. A runtime CreateWidget MapWidget is
//! often missed, which crashes Workbench at 0x20. Abort OpenMap unless the canvas
//! is already in the tree; MHJ_MapHost spawns it from MHJ_MapHost.layout.
//!
//! Consumer: loaded with the addon. Do not instantiate.
//!
//! Extend: keep the canvas, planner-camera, and zoom-bounds guards; call super only when safe.
//------------------------------------------------------------------------------------------------
modded class SCR_MapEntity
{
	//------------------------------------------------------------------------------------------------
	override void OpenMap(MapConfiguration config)
	{
		if (!config)
			return;
		if (!config.RootWidgetRef)
		{
			Print("[MHJ] OpenMap aborted: no root widget", LogLevel.ERROR);
			return;
		}

		CanvasWidget canvas = CanvasWidget.Cast(config.RootWidgetRef.FindAnyWidget(SCR_MapConstants.MAP_WIDGET_NAME));
		if (!canvas)
		{
			Print("[MHJ] OpenMap aborted: FindAnyWidget missed MapWidget", LogLevel.ERROR);
			return;
		}

		super.OpenMap(config);

		if (config.MapEntityMode != EMapEntityMode.PLAIN)
		{
			if (config.MapEntityMode != EMapEntityMode.MHJ_PLANNER)
				return;
		}

		PlayerController plc = GetGame().GetPlayerController();
		if (!plc)
			return;

		plc.SetCharacterCameraRenderActive(true);
	}

	//------------------------------------------------------------------------------------------------
	override void OnMapOpen(MapConfiguration config)
	{
		if (!m_MapWidget)
		{
			Print("[MHJ] OnMapOpen skipped: MapWidget is null", LogLevel.ERROR);
			m_bIsOpen = false;
			m_iDelayCounter = FRAME_DELAY;
			return;
		}

		super.OnMapOpen(config);
	}

	//------------------------------------------------------------------------------------------------
	//! Vanilla lets the view centre travel to the map edge, which leaves empty
	//! widget area. MapWidget does not clear that, so pan trails. Keep the map
	//! filling any MHJ_MapHost pane (HALO planner and GM Director).
	protected override bool FitPanBounds(inout float panX, inout float panY, bool center)
	{
		if (!MHJ_MapHost.IsPlannerSession())
			return super.FitPanBounds(panX, panY, center);

		if (!m_MapWidget)
			return super.FitPanBounds(panX, panY, center);
		if (!m_Workspace)
			return super.FitPanBounds(panX, panY, center);

		float windowWidth;
		float windowHeight;
		m_MapWidget.GetScreenSize(windowWidth, windowHeight);
		windowWidth = m_Workspace.DPIUnscale(windowWidth);
		windowHeight = m_Workspace.DPIUnscale(windowHeight);

		if (center)
		{
			panX = windowWidth * 0.5 - panX;
			panY = windowHeight * 0.5 - panY;
		}

		float width = m_iMapSizeX * m_fZoomPPU;
		float height = m_iMapSizeY * m_fZoomPPU;
		width = m_Workspace.DPIUnscale(width);
		height = m_Workspace.DPIUnscale(height);

		bool adjusted = false;
		if (width <= windowWidth)
		{
			panX = (windowWidth - width) * 0.5;
		}
		else
		{
			float minX = windowWidth - width;
			if (panX < minX)
			{
				panX = minX;
				adjusted = true;
			}
			if (panX > 0)
			{
				panX = 0;
				adjusted = true;
			}
		}

		if (height <= windowHeight)
		{
			panY = (windowHeight - height) * 0.5;
		}
		else
		{
			float minY = windowHeight - height;
			if (panY < minY)
			{
				panY = minY;
				adjusted = true;
			}
			if (panY > 0)
			{
				panY = 0;
				adjusted = true;
			}
		}

		if (adjusted)
			return false;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool UpdateZoomBounds()
	{
		if (!m_MapWidget)
			return false;

		float screenW;
		float screenH;
		m_MapWidget.GetScreenSize(screenW, screenH);
		if (screenH < 8)
			return false;
		if (screenW < 8)
			return false;

		float ppu = m_MapWidget.PixelPerUnit();
		if (ppu < 0.0001)
			return false;

		return super.UpdateZoomBounds();
	}

	//------------------------------------------------------------------------------------------------
	override void UpdateViewPort()
	{
		if (m_fZoomPPU < 0.0001)
			return;

		super.UpdateViewPort();
	}
}
