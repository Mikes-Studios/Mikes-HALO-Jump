//------------------------------------------------------------------------------------------------
//! Display-only paper-map pins on the HALO planner. Vanilla MarkersUI also binds
//! MapSelect / menu confirm which would steal click-to-drop. Strip those listeners
//! while the planner is open. Null-check the game-mode marker manager so modes
//! without one do not NPE. Do not mod SCR_MapMarkerBase — its snapshot serializers
//! do not merge and RPCs that pass that type fail to compile.
//!
//! Consumer: loaded with the addon. Do not instantiate.
//!
//! Extend: keep the planner input strip, cursor stamp, and the manager null-check;
//! call super when the manager exists.
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

		if (!MHJ_HaloJumpMenu.IsOpen())
			return;

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
	override void Update(float timeSlice)
	{
		super.Update(timeSlice);

		if (!MHJ_HaloJumpMenu.IsOpen())
			return;

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

		w.SetFlags(WidgetFlags.IGNORE_CURSOR);
		Widget child = w.GetChildren();
		while (child)
		{
			MHJ_IgnoreCursorTree(child);
			child = child.GetSibling();
		}
	}
}
