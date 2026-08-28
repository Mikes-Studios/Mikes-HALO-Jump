//------------------------------------------------------------------------------------------------
//! Task icons on the HALO planner. Spawn points stay off. UIIconsContainer is
//! required by vanilla OnMapOpen (NPE if missing). Copy the game mode's gadget
//! map task layout so Conflict vs vanilla icons match the paper map.
//! Planner icons clip to the map widget — vanilla never clips because the paper
//! map is fullscreen. HALO and GM Director both use MHJ_MapHost; clip and
//! spawn-point skip key off IsPlannerSession, not the HALO menu.
//!
//! Consumer: loaded with the addon. Do not instantiate.
//!
//! Extend: keep the planner spawn-point skip, null-check, gadget layout copy,
//! and map-widget clip.
//------------------------------------------------------------------------------------------------
modded class SCR_MapUIElementContainer
{
	//------------------------------------------------------------------------------------------------
	override void OnMapOpen(MapConfiguration config)
	{
		if (!MHJ_MapHost.IsPlannerSession())
		{
			super.OnMapOpen(config);
			return;
		}

		m_bShowSpawnPoints = false;
		MHJ_ApplyGadgetTaskLayout();

		if (!m_RootWidget)
			return;

		Widget icons = m_RootWidget.FindAnyWidget(m_sIconsContainer);
		if (!icons)
		{
			Print("[MHJ] UIIconsContainer missing; planner tasks skipped", LogLevel.WARNING);
			return;
		}

		icons.SetFlags(WidgetFlags.IGNORE_CURSOR | WidgetFlags.CLIPCHILDREN);
		super.OnMapOpen(config);
		MHJ_ClipIconsContainer();
		MHJ_IgnoreCursorTree(m_wIconsContainer);
	}

	//------------------------------------------------------------------------------------------------
	override void AddSpawnPoint(SCR_SpawnPoint spawnPoint)
	{
		if (MHJ_MapHost.IsPlannerSession())
			return;

		super.AddSpawnPoint(spawnPoint);
	}

	//------------------------------------------------------------------------------------------------
	override void InitSpawnPoints()
	{
		if (MHJ_MapHost.IsPlannerSession())
			return;

		super.InitSpawnPoints();
	}

	//------------------------------------------------------------------------------------------------
	override void OnTaskAdded(SCR_Task task)
	{
		super.OnTaskAdded(task);

		if (!MHJ_MapHost.IsPlannerSession())
			return;

		MHJ_ClipIconsContainer();
		MHJ_IgnoreCursorTree(m_wIconsContainer);
	}

	//------------------------------------------------------------------------------------------------
	override void UpdateIconPosition(Widget widget, SCR_MapUIElement icon, float x, float y)
	{
		super.UpdateIconPosition(widget, icon, x, y);
		if (!MHJ_MapHost.IsPlannerSession())
			return;

		MHJ_HideIconOutsideMap(widget, x, y);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_ApplyGadgetTaskLayout()
	{
		BaseGameMode gm = GetGame().GetGameMode();
		if (!gm)
			return;

		SCR_MapConfigComponent cfgComp = SCR_MapConfigComponent.Cast(gm.FindComponent(SCR_MapConfigComponent));
		if (!cfgComp)
			return;

		ResourceName path = cfgComp.GetGadgetMapConfig();
		if (path.IsEmpty())
			return;

		Resource container = BaseContainerTools.LoadContainer(path);
		if (!container)
			return;

		SCR_MapConfig mapConfig = SCR_MapConfig.Cast(BaseContainerTools.CreateInstanceFromContainer(container.GetResource().ToBaseContainer()));
		if (!mapConfig)
			return;
		if (!mapConfig.m_aUIComponents)
			return;

		ResourceName found;
		ResourceName campaignFound;
		foreach (SCR_MapUIBaseComponent comp : mapConfig.m_aUIComponents)
		{
			SCR_MapUIElementContainer src = SCR_MapUIElementContainer.Cast(comp);
			if (!src)
				continue;
			if (src.m_sTaskElement.IsEmpty())
				continue;

			found = src.m_sTaskElement;
			if (SCR_MapCampaignUI.Cast(src))
				campaignFound = src.m_sTaskElement;
		}

		if (!campaignFound.IsEmpty())
			m_sTaskElement = campaignFound;
		else if (!found.IsEmpty())
			m_sTaskElement = found;
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_ClipIconsContainer()
	{
		if (!m_wIconsContainer)
			return;

		m_wIconsContainer.SetFlags(WidgetFlags.CLIPCHILDREN | WidgetFlags.IGNORE_CURSOR);
		m_wIconsContainer.ClearFlags(WidgetFlags.DO_NOT_CLIP_RECT);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_HideIconOutsideMap(Widget widget, float x, float y)
	{
		if (!widget)
			return;
		if (!m_wIconsContainer)
			return;

		float sw;
		float sh;
		m_wIconsContainer.GetScreenSize(sw, sh);
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (workspace)
		{
			sw = workspace.DPIUnscale(sw);
			sh = workspace.DPIUnscale(sh);
		}

		if (sw < 8)
			return;
		if (sh < 8)
			return;

		float pad = 40;
		bool inside = true;
		if (x < -pad)
			inside = false;
		else if (y < -pad)
			inside = false;
		else if (x > sw + pad)
			inside = false;
		else if (y > sh + pad)
			inside = false;

		widget.SetVisible(inside);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_IgnoreCursorTree(Widget w)
	{
		if (!w)
			return;

		w.SetFlags(WidgetFlags.IGNORE_CURSOR | WidgetFlags.INHERIT_CLIPPING);
		w.ClearFlags(WidgetFlags.DO_NOT_CLIP_RECT);
		Widget child = w.GetChildren();
		while (child)
		{
			MHJ_IgnoreCursorTree(child);
			child = child.GetSibling();
		}
	}
}
