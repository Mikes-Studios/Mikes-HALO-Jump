//------------------------------------------------------------------------------------------------
//! One named HALO drop. Filled by MHJ_DropSiteCatalog consumers (I&A AOIs, etc.).
//!
//! Consumer: read-only after Collect. Do not instantiate from UI code except via the catalog.
//------------------------------------------------------------------------------------------------
class MHJ_DropSite
{
	string m_sName;
	float m_fX;
	float m_fZ;
}
