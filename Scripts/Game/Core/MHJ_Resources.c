//------------------------------------------------------------------------------------------------
//! Resource GUIDs for the drop planner map session.
//! MAP_HOST_LAYOUT is MapFrame + MapWidget + UIIconsContainer. Workbench Play needs
//! Resource Manager to have registered the .meta GUID (restart Workbench after pull).
//! PLAIN_MAP_CONF is vanilla MapPlain.conf so Play still works if addon GUIDs are missing.
//------------------------------------------------------------------------------------------------
class MHJ_Resources
{
	static const string MAP_HOST_LAYOUT = "{C4E8A27B1F906DB2}UI/Layouts/MHJ_MapHost.layout";
	static const string PLAIN_MAP_CONF = "{A786DD4868598F15}Configs/Map/MapPlain.conf";
	static const string OWN_PLAIN_MAP_CONF = "{C4E8A27B1F906DA1}Configs/Map/MHJ_PlainMap.conf";
}
