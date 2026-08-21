//------------------------------------------------------------------------------------------------
//! Optional named-drop list for the HALO planner. Empty unless a consumer fills it.
//! I&A registers via modded Collect. Other modes can Insert on GetOnCollect.
//!
//! Consumer: MHJ_HaloJumpMenu.Collect at BuildUI. Do not call from HUD.
//!
//! Extend: mod Collect, or GetOnCollect().Insert. Keep payloads to name + world XZ.
//------------------------------------------------------------------------------------------------
class MHJ_DropSiteCatalog
{
	protected static ref ScriptInvoker s_OnCollect;

	//------------------------------------------------------------------------------------------------
	static ScriptInvoker GetOnCollect()
	{
		if (!s_OnCollect)
			s_OnCollect = new ScriptInvoker();
		return s_OnCollect;
	}

	//------------------------------------------------------------------------------------------------
	static void Collect(notnull array<ref MHJ_DropSite> outSites)
	{
		if (!s_OnCollect)
			return;

		s_OnCollect.Invoke(outSites);
	}
}
