//------------------------------------------------------------------------------------------------
//! Clickable world-XZ picker. Screen up is world +Z. Used only inside MHJ_HaloJumpMenu.
//! Live map: click/A sets drop, drag does not. Wheel zooms. Gamepad D-pad down leaves
//! the picker so altitude fields stay reachable. Grid + canvas markers are the fallback.
//------------------------------------------------------------------------------------------------
class MHJ_MapPicker : MUI_Node
{
	protected ref ScriptInvoker m_OnChanged;
	protected MHJ_MapHost m_MapHost;
	protected float m_fWorldMinX;
	protected float m_fWorldMinZ;
	protected float m_fWorldW;
	protected float m_fWorldH;
	protected float m_fDropX;
	protected float m_fDropZ;
	protected bool m_bHasDrop;
	protected vector m_vWind;
	protected bool m_bHasWind;
	protected bool m_bDragOrigin;
	protected bool m_bPanGesture;
	protected float m_fDragOx;
	protected float m_fDragOy;

	//------------------------------------------------------------------------------------------------
	void MHJ_MapPicker()
	{
		m_OnChanged = new ScriptInvoker();
		m_Style.m_WidthMode = MUI_SizeMode.Fill;
		m_Style.m_HeightMode = MUI_SizeMode.Exact;
		m_Style.m_fHeight = 400;
		m_Style.m_fMinHeight = 400;
		m_Style.m_fRadius = 10;
		m_Style.m_bInteractive = true;
		m_Style.m_bClipChildren = true;
		m_Style.m_Fill = Color.FromInt(0);
		m_fWorldMinX = 0;
		m_fWorldMinZ = 0;
		m_fWorldW = MHJ_Constants.WORLD_SIZE_FALLBACK;
		m_fWorldH = MHJ_Constants.WORLD_SIZE_FALLBACK;
		m_bHasDrop = false;
		m_bHasWind = false;
		m_vWind = vector.Zero;
	}

	//------------------------------------------------------------------------------------------------
	void SetMapHost(MHJ_MapHost host)
	{
		m_MapHost = host;
		InvalidatePaint();
	}

	//------------------------------------------------------------------------------------------------
	void InitWorld()
	{
		m_fWorldMinX = 0;
		m_fWorldMinZ = 0;
		m_fWorldW = MHJ_Constants.WORLD_SIZE_FALLBACK;
		m_fWorldH = MHJ_Constants.WORLD_SIZE_FALLBACK;

		ChimeraGame game = GetGame();
		if (game)
		{
			if (game.GetWorldEntity())
			{
				vector mins;
				vector maxs;
				game.GetWorldEntity().GetTerrain(0, 0).GetTerrainBoundBox(mins, maxs);
				m_fWorldMinX = mins[0];
				m_fWorldMinZ = mins[2];
				m_fWorldW = maxs[0] - mins[0];
				m_fWorldH = maxs[2] - mins[2];
				if (m_fWorldW < 1)
					m_fWorldW = MHJ_Constants.WORLD_SIZE_FALLBACK;
				if (m_fWorldH < 1)
					m_fWorldH = MHJ_Constants.WORLD_SIZE_FALLBACK;
			}
		}

		IEntity localChar = SCR_PlayerController.GetLocalControlledEntity();
		if (localChar)
		{
			vector origin = localChar.GetOrigin();
			SetDrop(origin[0], origin[2]);
		}
	}

	//------------------------------------------------------------------------------------------------
	ScriptInvoker GetOnChanged()
	{
		return m_OnChanged;
	}

	//------------------------------------------------------------------------------------------------
	float GetDropX()
	{
		return m_fDropX;
	}

	//------------------------------------------------------------------------------------------------
	float GetDropZ()
	{
		return m_fDropZ;
	}

	//------------------------------------------------------------------------------------------------
	bool HasDrop()
	{
		return m_bHasDrop;
	}

	//------------------------------------------------------------------------------------------------
	void SetWind(vector wind)
	{
		m_vWind = wind;
		m_bHasWind = true;
		InvalidatePaint();
	}

	//------------------------------------------------------------------------------------------------
	void SetDrop(float worldX, float worldZ)
	{
		m_fDropX = worldX;
		m_fDropZ = worldZ;
		ClampDrop();
		m_bHasDrop = true;
		InvalidatePaint();
	}

	//------------------------------------------------------------------------------------------------
	override void OnClicked()
	{
		if (m_bPanGesture)
		{
			m_bPanGesture = false;
			return;
		}

		if (TryApplyLivePointer())
			return;

		float lx;
		float ly;
		if (!m_Runtime)
			return;
		if (!m_Runtime.GetLocalPointer(lx, ly))
			return;
		ApplyPointerUv(lx, ly);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDrag(float x, float y)
	{
		if (!m_bDragOrigin)
		{
			m_fDragOx = x;
			m_fDragOy = y;
			m_bDragOrigin = true;
		}

		if (IsLiveMap())
		{
			float dx = x - m_fDragOx;
			float dy = y - m_fDragOy;
			if (Math.AbsFloat(dx) + Math.AbsFloat(dy) > 8)
				m_bPanGesture = true;
			if (m_bPanGesture && m_MapHost)
				m_MapHost.PanDrag();
			return;
		}

		ApplyPointerUv(x, y);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDragEnd(float x, float y)
	{
		m_bDragOrigin = false;
		if (m_MapHost)
			m_MapHost.EndPan();
	}

	//------------------------------------------------------------------------------------------------
	override void OnMouseWheel(int wheel)
	{
		if (!IsLiveMap())
			return;
		if (!m_MapHost)
			return;

		if (wheel > 0)
			m_MapHost.ZoomByStep(1);
		else if (wheel < 0)
			m_MapHost.ZoomByStep(-1);
	}

	//------------------------------------------------------------------------------------------------
	override bool HandleNavAxis(int dirX, int dirY)
	{
		if (IsLiveMap())
		{
			if (dirY > 0)
			{
				InputManager im = GetGame().GetInputManager();
				float analogY = 0;
				if (im)
					analogY = im.GetActionValue("MapPanVGamepad");
				if (Math.AbsFloat(analogY) < 0.4)
					return false;
				return true;
			}

			if (m_MapHost)
				m_MapHost.PanByNav(dirX, dirY);
			return true;
		}

		if (dirY != 0)
			return false;
		if (!m_bHasDrop)
			InitWorld();

		float step = 100;
		SetDrop(m_fDropX + dirX * step, m_fDropZ);
		if (m_OnChanged)
			m_OnChanged.Invoke();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void PaintForeground(MUI_RenderSurface surface)
	{
		float op = GetDrawOpacity();
		if (op < 0.01)
			return;

		float x = DrawX();
		float y = DrawY();
		float w = m_World.m_fW;
		float h = m_World.m_fH;
		MUI_ThemeData theme = GetTheme();

		bool live = false;
		if (m_MapHost)
			live = m_MapHost.IsLive();

		if (!live)
		{
			surface.FillRect(x, y, w, h, MUI_ColorUtil.Fade(theme.Deep, op), m_Style.m_fRadius);

			int lines = 8;
			int i;
			for (i = 1; i < lines; i++)
			{
				float u = i * 1.0 / lines;
				float gx = x + w * u;
				float gy = y + h * u;
				surface.DrawLine(gx, y, gx, y + h, MUI_ColorUtil.Fade(theme.Grid, op), 1);
				surface.DrawLine(x, gy, x + w, gy, MUI_ColorUtil.Fade(theme.Grid, op), 1);
			}

			IEntity localChar = SCR_PlayerController.GetLocalControlledEntity();
			if (localChar)
				PaintMarker(surface, localChar.GetOrigin(), theme.Cyan, op, 5);

			if (m_bHasDrop)
			{
				vector drop;
				drop[0] = m_fDropX;
				drop[1] = 0;
				drop[2] = m_fDropZ;
				PaintMarker(surface, drop, theme.Accent, op, 7);
				PaintWind(surface, drop, theme, op);
			}
		}

		surface.StrokeRect(x, y, w, h, MUI_ColorUtil.Fade(theme.Border, op), 1.4, m_Style.m_fRadius);
		if (live)
			PaintLiveChrome(surface, x, y, w, h, theme, op);
		else
			surface.DrawText(x + 10, y + 8, w - 20, 18, "CLICK MAP TO SET DROP", theme.FONT_SMALL, MUI_ColorUtil.Fade(theme.TextMuted, op), true, false, true, false);
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsLiveMap()
	{
		if (!m_MapHost)
			return false;
		return m_MapHost.IsLive();
	}

	//------------------------------------------------------------------------------------------------
	protected void PaintLiveChrome(MUI_RenderSurface surface, float x, float y, float w, float h, MUI_ThemeData theme, float op)
	{
		string hint = "CLICK TO SET DROP  ·  DRAG TO PAN  ·  WHEEL TO ZOOM";
		if (m_MapHost && !m_MapHost.IsMouseAim())
		{
			hint = "A SET DROP AT CENTRE  ·  STICK PANS  ·  TRIGGERS ZOOM  ·  DOWN FOR ALTITUDE";
			float cx = x + w * 0.5;
			float cy = y + h * 0.5;
			Color reticle = MUI_ColorUtil.Fade(theme.Accent, op);
			surface.DrawLine(cx - 14, cy, cx - 4, cy, reticle, 1.5);
			surface.DrawLine(cx + 4, cy, cx + 14, cy, reticle, 1.5);
			surface.DrawLine(cx, cy - 14, cx, cy - 4, reticle, 1.5);
			surface.DrawLine(cx, cy + 4, cx, cy + 14, reticle, 1.5);
			surface.StrokeCircle(cx, cy, 5, reticle, 1.2);
		}

		surface.DrawText(x + 10, y + 8, w - 20, 18, hint, theme.FONT_SMALL, MUI_ColorUtil.Fade(theme.TextMuted, op), true, false, true, false);
	}

	//------------------------------------------------------------------------------------------------
	protected bool TryApplyLivePointer()
	{
		if (!m_MapHost)
			return false;
		if (!m_MapHost.IsLive())
			return false;

		float worldX;
		float worldZ;
		if (!m_MapHost.ScreenToWorldDrop(worldX, worldZ))
			return false;

		SetDrop(worldX, worldZ);
		if (m_OnChanged)
			m_OnChanged.Invoke();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyPointerUv(float lx, float ly)
	{
		if (m_World.m_fW < 1)
			return;
		if (m_World.m_fH < 1)
			return;
		if (m_fWorldW < 1)
			return;
		if (m_fWorldH < 1)
			return;

		float nx = (lx - m_World.m_fX) / m_World.m_fW;
		float ny = (ly - m_World.m_fY) / m_World.m_fH;
		if (nx < 0)
			nx = 0;
		if (nx > 1)
			nx = 1;
		if (ny < 0)
			ny = 0;
		if (ny > 1)
			ny = 1;

		SetDrop(m_fWorldMinX + nx * m_fWorldW, m_fWorldMinZ + (1 - ny) * m_fWorldH);
		if (m_OnChanged)
			m_OnChanged.Invoke();
	}

	//------------------------------------------------------------------------------------------------
	protected void ClampDrop()
	{
		if (m_fDropX < m_fWorldMinX)
			m_fDropX = m_fWorldMinX;
		if (m_fDropZ < m_fWorldMinZ)
			m_fDropZ = m_fWorldMinZ;
		if (m_fDropX > m_fWorldMinX + m_fWorldW)
			m_fDropX = m_fWorldMinX + m_fWorldW;
		if (m_fDropZ > m_fWorldMinZ + m_fWorldH)
			m_fDropZ = m_fWorldMinZ + m_fWorldH;
	}

	//------------------------------------------------------------------------------------------------
	protected void PaintMarker(MUI_RenderSurface surface, vector worldPos, Color color, float op, float radius)
	{
		if (m_fWorldW < 1)
			return;
		if (m_fWorldH < 1)
			return;

		float nx = (worldPos[0] - m_fWorldMinX) / m_fWorldW;
		float nz = (worldPos[2] - m_fWorldMinZ) / m_fWorldH;
		if (nx < 0)
			nx = 0;
		if (nx > 1)
			nx = 1;
		if (nz < 0)
			nz = 0;
		if (nz > 1)
			nz = 1;

		float px = DrawX() + nx * m_World.m_fW;
		float py = DrawY() + (1 - nz) * m_World.m_fH;
		surface.FillCircle(px, py, radius, MUI_ColorUtil.Fade(color, op));
	}

	//------------------------------------------------------------------------------------------------
	protected void PaintWind(MUI_RenderSurface surface, vector drop, MUI_ThemeData theme, float op)
	{
		if (!m_bHasWind)
			return;

		float spd = m_vWind.Length();
		if (spd < 0.4)
			return;

		float px;
		float py;
		if (!WorldToCanvas(drop, px, py))
			return;

		float len = 22 + spd * 5;
		if (len > 70)
			len = 70;
		float sx = (m_vWind[0] / spd) * len;
		float sy = -(m_vWind[2] / spd) * len;
		Color c = MUI_ColorUtil.Fade(theme.Cyan, op);
		surface.DrawLine(px, py, px + sx, py + sy, c, 2);
		surface.FillCircle(px + sx, py + sy, 3.5, c);
	}

	//------------------------------------------------------------------------------------------------
	protected bool WorldToCanvas(vector worldPos, out float px, out float py)
	{
		if (m_fWorldW < 1)
			return false;
		if (m_fWorldH < 1)
			return false;

		float nx = (worldPos[0] - m_fWorldMinX) / m_fWorldW;
		float nz = (worldPos[2] - m_fWorldMinZ) / m_fWorldH;
		if (nx < 0)
			nx = 0;
		if (nx > 1)
			nx = 1;
		if (nz < 0)
			nz = 0;
		if (nz > 1)
			nz = 1;

		px = DrawX() + nx * m_World.m_fW;
		py = DrawY() + (1 - nz) * m_World.m_fH;
		return true;
	}
}
