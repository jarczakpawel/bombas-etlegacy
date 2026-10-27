# Bombas Molotov - server-side ET:Legacy module

**Status 2026-10-27:** compiled Linux x86_64 server-only qagame and asset PK3 installed on the primary Bombas server port 27960. `g_bombasMolotov 1` is enabled. Stock ET:Legacy clients download the passive PK3. Rendering and functionality still need actual client-side verification.

Configured on primary Bombas using `set g_bombasMolotov "1"` in `bombas.cfg`. Changing it to `0` disables extra-grenade grant and conversion without affecting the native grenades. Source and package SHA are in `reports/qagame_release.json` and `reports/bombas_medic_molotov_release.json` of the parent repository.

The existing `weaponbank 4` has `WP_GRENADE_LAUNCHER` and `WP_GRENADE_PINEAPPLE`. Grant one opposite-faction grenade on ordinary spawn; a second selection cycles to it in the stock cgame. Revives retain previous ammunition. The server identifies the opposite weapon and creates a nonbouncing Molotov projectile on throw. On impact/expiry it spawns a stationary 3.5-second animated model and server-side fire zone (radius 88) and plays stock glass-break sound. Flames reuse native burn/damage, team/friendly-fire, invulnerability, underwater and wall-trace handling. Owner slot reuse aborts the patch to avoid misattribution; pause delays its expiry and damage.

The model `fire_patch.md3` is built by the independent script `source/bombas-medic/build_fire_patch.py` using the stock shader `entityonfire1` from pak0.pk3, so a normal PK3 can supply the visuals without new native client code.

The API for projectile selection remains client-neutral; this does NOT prove that its custom held-weapon skins, animations, sound presentation and projectile model are correct on each operating system. Both Molotov/Nitro variant models are bundled in the deployed package, but their actual appearance must be checked in both teams. Do not overwrite `grenade.weap` / `pineapple.weap` blindly: they specify original hand `.mdc` animation files and team models, and the stock grenade must continue rendering normally.

The server-only module and 39-file PK3 are deployed to port 27960, following a single server restart while 0 human players were connected. The server runs, diagnostics show 0 failures and 1 pre-existing transmitter.pk3 CRC warning. No human-client grenade switching or Molotov burn sequence has yet been verified in-game, and no claims are made about visual fidelity on Windows/macOS/Android. No second server exists.
