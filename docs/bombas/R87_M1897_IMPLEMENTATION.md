# R87 Winchester M1897 (development, not released)

Base commit: fdf3d50cba5688e7e5e5e25532df18c7b4635776 (R86, live).

The licensed Jaymod 2.0.0 M97 models, textures, sounds and parser-adapted weapon config are tracked in the enclosing Bombas project at `source/bombas-medic/r87-winchester/`, complete with an SHA-256 manifest. Packaged with `scripts/package_r87_winchester_assets.py`. Original Jaymod revision bbe44e0cca806d6b51260d48d4c11eab2b85bfe9, Apache-2.0 plus original id Software license. Do not use unlicensed replacement assets.

Do not release any M1897 server/client until all of the following are implemented and demonstrated together:

- Preserve all existing weapon values 0-55 and events. Add new IDs only at the end of shared weapon/MOD/item/stats tables and all dependent arrays in `src/game/bg_public.h`, `bg_misc.c`, `bg_classes.c`, `g_weapon.c`, `g_client.c`, `etbot/g_etbot_interface.cpp`, the relevant UI/cgame files and Lua interfaces. Maximum weapon bitset supports 64; do not exhaust these slots (StG44, BAR and PPSh are still planned).
- Server-authoritative close-range pellets with efficient network event aggregation, anti-lag, damage falloff, headshots/team damage, wall/world impact and honest kill credit. Do not spam one multicast event per pellet unnecessarily.
- Native engine `bg_pmove.c` must predict pump and interruptible individual-shell reload deterministically on client and server. Jaymod's `holdable[]`/`pmext` implementation cannot be copied verbatim, since ET: Legacy uses a different shared state. No new client executable or incompatible network fields.
- Render all MD3 model parts/skins and show firing, reload, pump, shell ejection, icons, first/third person and sounds with the existing client weapon parser. M97 `scaleFirstPerson` was unsupported and removed from the adapted `.weap` file.
- Correct soldier/class selection, ammunition, skill unlock, ammo packs, loadouts, pickups, dropped guns, special maps, team balance, bot ranges and cooldowns; no accidental changes to Nitro dual grenades or previous Class Stealing.
- Build five existing native client distributions and the Linux64 dedicated game module, exercise real ET:Legacy 2.86.2 human clients and mixed Omni-Bot 0.93 on stock/special maps, verify pure PK3 auto download with old cache, exact SHA, cgame compatibility, frame times, gameplay, and server rollback.

The R87 asset PK3 currently on VPS `/opt/bombas/releases/m1897_assets_r87_STAGED_20e1260d7e3f.pk3` is intentionally not in the active `/opt/bombas/legacy/`. Do NOT activate it until corresponding game and cgame release gates are met. Existing live R86 is untouched.
