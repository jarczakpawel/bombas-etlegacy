# Bombas Molotov - native ET:Legacy server game feature (disabled by default)

**Status:** source patch, not compiled and not deployed. Requires the downloadable `models/bombas/molotov/fire_patch.md3` PK3 and team-specific Nitro bottle/grenade assets to be installed before activation.

To opt in after implementation is verified, configure `set g_bombasMolotov 1` on the server before map start. An absent/other value means disabled. Never configure this on public production while client artwork and behavior are still provisional.

The existing `weaponbank 4` has `WP_GRENADE_LAUNCHER` and `WP_GRENADE_PINEAPPLE`. Grant one opposite-faction grenade on ordinary spawn; a second selection cycles to it in the stock cgame. Revives retain previous ammunition. The server identifies the opposite weapon and creates a nonbouncing Molotov projectile on throw. On impact/expiry it spawns a stationary 3.5-second animated model and server-side fire zone (radius 88) and plays stock glass-break sound. Flames reuse native burn/damage, team/friendly-fire, invulnerability, underwater and wall-trace handling. Owner slot reuse aborts the patch to avoid misattribution; pause delays its expiry and damage.

The model `fire_patch.md3` is built by the independent script `source/bombas-medic/build_fire_patch.py` using the stock shader `entityonfire1` from pak0.pk3, so a normal PK3 can supply the visuals without new native client code.

The API for projectile selection remains client-neutral; this does NOT prove that its custom held-weapon skins, animations, sound presentation and projectile model are correct on each operating system. Missing Molotov/Nitro grenade variant models mean release is incomplete. Do not overwrite `grenade.weap` / `pineapple.weap` blindly: they specify original hand `.mdc` animation files and team models, and the stock grenade must continue rendering normally.

No compile, real client session or game-server behavior test has been performed. The production Bombas server was not modified.
