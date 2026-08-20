//------------------------------------------------------------------------------------------------
//! Task icons on the HALO planner. Spawn points stay off. UIIconsContainer is
//! required by vanilla OnMapOpen (NPE if missing). Copy the game mode's gadget
//! map task layout so Conflict vs vanilla icons match the paper map.
//!
//! Consumer: loaded with the addon. Do not instantiate.
//!
//! Extend: keep the planner spawn-point skip, null-check, and gadget layout copy.
//------------------------------------------------------------------------------------------------
modded class SCR_MapUIElementContainer
{
	//------------------------------------------------------------------------------------------------
	override void OnMapOpen(MapConfiguration config)
	{
		if (!MHJ_HaloJumpMenu.IsOpen())
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

		icons.SetFlags(WidgetFlags.IGNORE_CURSOR);
		super.OnMapOpen(config);
		MHJ_IgnoreCursorTree(m_wIconsContainer);
	}

	//------------------------------------------------------------------------------------------------
	override void AddSpawnPoint(SCR_SpawnPoint spawnPoint)
	{
		if (MHJ_HaloJumpMenu.IsOpen())
			return;

		super.AddSpawnPoint(spawnPoint);
	}

	//------------------------------------------------------------------------------------------------
	override void InitSpawnPoints()
	{
		if (MHJ_HaloJumpMenu.IsOpen())
			return;

		super.InitSpawnPoints();
	}

	//------------------------------------------------------------------------------------------------
	override void OnTaskAdded(SCR_Task task)
	{
		super.OnTaskAdded(task);

		if (!MHJ_HaloJumpMenu.IsOpen())
			return;

		MHJ_IgnoreCursorTree(m_wIconsContainer);
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
	protected void MHJ_IgnoreCursorTree(Widget w)
	{
		if (!w)
			return;

		w.SetFlags(WidgetFlags.IGNORE_CURSOR);
		Widget child = w.GetChildren();
		while (child)
		{
			MHJ_IgnoreCursorTree(child);
			child = child.GetSibling();
		}
	}
}
