//------------------------------------------------------------------------------------------------
//! Click binder so drop-site buttons can call MHJ_HaloJumpMenu.SelectDropSite.
//! Kept alive by the menu's m_aSiteBinds array (missing ref → GC → dead clicks).
//!
//! Consumer: MHJ_HaloJumpMenu.BuildSiteButtons only.
//------------------------------------------------------------------------------------------------
class MHJ_DropSiteBind
{
	protected MHJ_HaloJumpMenu m_Menu;
	protected int m_iIndex;

	//------------------------------------------------------------------------------------------------
	void Init(MHJ_HaloJumpMenu menu, int index)
	{
		m_Menu = menu;
		m_iIndex = index;
	}

	//------------------------------------------------------------------------------------------------
	void OnClicked()
	{
		if (!m_Menu)
			return;
		m_Menu.SelectDropSite(m_iIndex);
	}
}
