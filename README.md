# Mike's HALO Jump

Walk up to a sign, pick a drop on the map, and jump. Freefall, then a steerable canopy that auto-opens at the altitude you chose.

| | |
|---|---|
| Addon ID | `MikesHALOJump` |
| GUID | `C4E8A27B1F906D53` |
| Engine | Arma Reforger `58D0FB3206B6F859` |
| UI | Mike's UI `B3F91C6A4E275D08` |
| Prefix | `MHJ_` |
| License | [APL-SA](https://www.bohemia.net/community/licenses/arma-public-license-share-alike) |

Reforger has no parachute. This addon uses a scripted character command (same pattern as the engine `SCR_CharacterCommandFly` example): gravity off, scripted descent, WASD steering. The canopy mesh is parented to the jumper when it opens. Freefall still uses the standing character — there is no custom HALO animation graph.

## Place the sign

Restart Workbench (or Resource Manager → Refresh) after pulling this addon so `.meta` files register.

| Where | Prefab |
|---|---|
| World Editor | `Prefabs/MHJ_HaloJumpSign.et` |
| Game Master | `PrefabsEditable/Auto/MHJ/E_MHJ_HaloJumpSign.et` |

It inherits the vanilla US vehicle-repair sign (mesh + collision). Look at it and use **HALO Jump**. The planner shows the live world map (native `MapWidget` / `SCR_MapEntity` PLAIN). Click a drop, set jump altitude (AGL) and canopy-open altitude (AGL), then **Jump**.

Does **not** depend on Mike's Map HUD. If Map HUD is also loaded, it yields while the planner is open and resumes after close.

On PC: click sets drop, drag pans, wheel zooms. On controller: left stick pans, triggers zoom, **A** sets drop at the centre reticle, D-pad down moves to altitude fields.

## Add to a project or server

```
Dependencies {
  "58D0FB3206B6F859" "B3F91C6A4E275D08" "C4E8A27B1F906D53"
}
```

## In the air

| Phase | What happens |
|---|---|
| Exit | Short unstable tumble, then you can fly the body. |
| Freefall | Gravity and drag. **W** tracks toward heading. **S** slows the fall. **A/D** turns. Wind drifts you. |
| Canopy | Opens on its own at your open altitude, with a snatch. **W** dives (steep path, TAS builds). **S** flares and rotates that speed into a horizontal swoop. Land into the wind. **W** held near the ground skips auto-flare. |
| Land | Command ends near the ground. A clean flare is safe; leftover ground speed steps into a short run. A heavy arrival hurts the legs; a crater can kill. |

The ram-air canopy (`Prefabs/MHJ_DeployedCanopy.et`) appears at open altitude and is deleted on landing.

## Licence

<a rel="license" href="https://www.bohemia.net/community/licenses/arma-public-license-share-alike" target="_blank"><img src="https://data.bistudio.com/images/license/APL-SA.png" alt="APL-SA"><br>This work is licensed under the Arma Public License Share Alike</a>

Full text: `license.txt`.

The Parachute MK3 mesh, textures, and materials are adapted from [Alphaegen's Parachute Framework](https://github.com/Alphaegen/ArmaReforgerParachutes) (Workshop `65930CB4CD0237B2`). See `Assets/ParachuteMK3/ATTRIBUTION.txt`. That project has no character animation graphs (vanilla sit-in-seat). This addon does **not** depend on that Workshop item; flight stays a ScriptedCommand, not a cargo vehicle.
