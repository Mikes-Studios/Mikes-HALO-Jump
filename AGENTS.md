# AGENTS.md

This repository is a **mode-agnostic HALO jump addon** for Arma Reforger, not a game mode.

1. Prefix `MHJ_`. Do not add I&A-specific code here.
2. **Do not edit Mikes-UI** to host this menu. Subclass `MUI_MenuBase` here.
3. **Do not edit GameSources.** One cargo craft (`MHJ_DeployedCanopy.et`) for the whole jump. Native `StartCommand_Fall` is only the GetIn bridge. Freefall is the belly-pitched cargo slot; open is `GetInVehicle` to the sit slot on the same vehicle — no GetOut between phases. No `ScriptedCommand`. No `StartCommand_Vehicle` after GetIn. Drive the occupied craft with `SetVelocity` + `ForceNodeMovement`, not `SetOrigin`.
4. Consume Mikes-UI via addon GUID `B3F91C6A4E275D08`.
5. Enforce: no ternary; every `new` in a `ref`; colors via `Color.FromSRGBA`; MUI paint via `DrawX/Y` + `GetDrawOpacity()`.
6. Server-authoritative jump start. UI is local. Call `super` unless replacing.
7. This addon is **APL-SA** (adapted Parachute MK3 assets from Alphaegen). Do not add a Workshop dependency on `65930CB4CD0237B2`. Do not copy that project's vehicle/compartment scripts.
