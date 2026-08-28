//------------------------------------------------------------------------------------------------
//! Display-only paper-map pins on the HALO planner and any other MHJ_MapHost
//! session (GM Director). Pins clip to MapFrame. Vanilla MarkersUI also binds
//! MapSelect / menu confirm which would steal click-to-drop. Strip those listeners
//! while a cursor-less map is open — not only when the HALO jump menu is open.
//! Null-check the game-mode marker manager so modes without one do not NPE.
//! Planner conf has no SCR_MapCursorModule, so vanilla CleanupMarkerEditWidget
//! and OnInputMapSelect must not call into a null cursor. Do not mod
//! SCR_MapMarkerBase — its snapshot serializers do not merge and RPCs that
//! pass that type fail to compile.
//!
//! Consumer: loaded with the addon. Do not instantiate.
//!
//! Extend: keep the planner input strip, cursor stamp, manager null-check,
//! MapFrame clip, cursor-less CleanupMarkerEditWidget, and OnInputMapSelect
//! guard; call super when the manager / cursor exists.
//------------------------------------------------------------------------------------------------
modded class SCR_MapMarkersUI
{
	//------------------------------------------------------------------------------------------------
	override void Init()
	{
		BaseGameMode gm = GetGame().GetGameMode();
		if (!gm)
			return;

		m_MarkerMgr = SCR_MapMarkerManagerComponent.Cast(gm.FindComponent(SCR_MapMarkerManagerComponent));
		if (!m_MarkerMgr)
			return;

		super.Init();
	}

	//------------------------------------------------------------------------------------------------
	override void OnMapOpen(MapConfiguration config)
	{
		if (!m_MarkerMgr)
			return;

		super.OnMapOpen(config);

		if (m_CursorModule)
		{
			if (!MHJ_MapHost.IsPlannerSession())
				return;
		}

		MHJ_StripPlannerInput();
		MHJ_IgnorePlannerMarkerCursor();
	}

	//------------------------------------------------------------------------------------------------
	override void OnMapClose(MapConfiguration config)
	{
		if (!m_MarkerMgr)
			return;

		super.OnMapClose(config);
	}

	//------------------------------------------------------------------------------------------------
	override void CleanupMarkerEditWidget()
	{
		if (m_MarkerEditRoot)
			m_MarkerEditRoot.RemoveFromHierarchy();

		m_bIsDelayed = false;

		if (m_CursorModule)
			m_CursorModule.HandleDialog(false);
	}

	//------------------------------------------------------------------------------------------------
	override void OnInputMapSelect(float value, EActionTrigger reason)
	{
		if (!m_CursorModule)
			return;

		super.OnInputMapSelect(value, reason);
	}

	//------------------------------------------------------------------------------------------------
	override void Update(float timeSlice)
	{
		super.Update(timeSlice);

		if (m_CursorModule)
		{
			if (!MHJ_MapHost.IsPlannerSession())
				return;
		}

		MHJ_IgnorePlannerMarkerCursor();
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_StripPlannerInput()
	{
		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;

		im.RemoveActionListener("MapQuickMarkerMenu", EActionTrigger.DOWN, OnInputQuickMarkerMenu);
		im.RemoveActionListener("MapMarkerDelete", EActionTrigger.DOWN, OnInputMarkerDelete);
		im.RemoveActionListener("MapSelect", EActionTrigger.DOWN, OnInputMapSelect);
		im.RemoveActionListener(UIConstants.MENU_ACTION_SELECT, EActionTrigger.DOWN, OnInputMenuConfirm);
		im.RemoveActionListener("MenuRefresh", EActionTrigger.DOWN, OnInputMenuConfirmAlter);
		im.RemoveActionListener(UIConstants.MENU_ACTION_BACK, EActionTrigger.DOWN, OnInputMenuBack);
		im.RemoveActionListener(UIConstants.MENU_ACTION_DOWN, EActionTrigger.DOWN, OnInputMenuDown);
		im.RemoveActionListener(UIConstants.MENU_ACTION_UP, EActionTrigger.DOWN, OnInputMenuUp);
		im.RemoveActionListener(UIConstants.MENU_ACTION_RIGHT, EActionTrigger.DOWN, OnInputMenuRight);
		im.RemoveActionListener(UIConstants.MENU_ACTION_LEFT, EActionTrigger.DOWN, OnInputMenuLeft);
	}

	//------------------------------------------------------------------------------------------------
	protected void MHJ_IgnorePlannerMarkerCursor()
	{
		if (!m_MapEntity)
			return;

		Widget mapRoot = m_MapEntity.GetMapMenuRoot();
		if (!mapRoot)
			return;

		Widget mapFrame = mapRoot.FindAnyWidget(SCR_MapConstants.MAP_FRAME_NAME);
		if (!mapFrame)
			return;

		mapFrame.SetFlags(WidgetFlags.CLIPCHILDREN);
		Widget child = mapFrame.GetChildren();
		while (child)
		{
			if (child.GetName() != SCR_MapConstants.MAP_WIDGET_NAME)
				MHJ_IgnoreCursorTree(child);

			child = child.GetSibling();
		}
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
