# Workshop identity

Arma Reforger Workshop identity is the **project GUID** in `addon.gproj`. Resource GUIDs in `.meta` files, layouts, and prefabs are shared and must not be regenerated. The canopy prefab `{C4E8A27B1F906D90}` is a resource GUID; do not change it when switching project identity.

Never load this addon’s production and experimental projects in the same Workbench session. Testers must not enable both in-game either.

## This addon

| | Production (`main`) | Experimental (`experimental`) |
|---|---|---|
| Folder | `F:\Mikes-HALO-Jump` | `F:\Mikes-HALO-Jump-Exp` |
| ID | `MikesHALOJump` | `MikesHALOJumpExp` |
| GUID | `C4E8A27B1F906D53` | `3F8C0B6E5A172D94` |
| Title | Mike's HALO Jump | Mike's HALO Jump (Experimental) |
| Depends on | engine + `B3F91C6A4E275D08` | engine + `7A4E19C2D8B063F5` |

## Stack

Publish experimental in this order: **UI → HALO → I&A**. Same order on production after promote.

| Addon | Production GUID | Experimental GUID |
|---|---|---|
| Mike's UI | `B3F91C6A4E275D08` | `7A4E19C2D8B063F5` |
| Mike's HALO Jump | `C4E8A27B1F906D53` | `3F8C0B6E5A172D94` |
| Mikes Invade and Annex | `6556458885927F2F` | `1D9B47A0E6C358F2` |

Map HUD stays production-only. Do not enable it on a server that loads experimental UI.

## Daily work

1. Open `F:\Mikes-Experimental.code-workspace`.
2. Commit on `experimental` in the `*-Exp` worktrees.
3. In Workbench, open the Exp project (not the production folder) and publish. First publish: Unlisted or Test.

## Promote to production

From I&A: `tools/Promote-Experimental.ps1`. It merges `experimental` into `main` in all three repos and **keeps `main`’s `addon.gproj`**. Then open the production folders in Workbench and publish.

Do not merge `addon.gproj` from `experimental` into `main`. That would retarget the live Workshop listings.
