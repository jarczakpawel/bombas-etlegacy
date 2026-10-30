# R88 Bombas native throwing knife counter: 3/3 2/3 1/3 0/3

Base: R87 feature/bombas-r87-m1897 3fdd11326 (production VPS a5ad2b2, native CI universal v087 SHA a2099e92ee65, M1897 asset SHA dc5fa7bca770). R88 branch only, no modifications to production as of this QA.

### Server authoritative / no network protocol change

The real 3-per-life limit remains in `g_weapon.c` for `G_BombasThrowKnife`, not in the client. `G_BombasKnifeHudSpawn` resets the authoritative counter on respawn and sends a reliable `bombas_knives 3 <spawn_count>` command to a real player. On each **accepted** throw only, the server sends a reliable updated `bombas_knives 2/1/0 <spawn_count>` command. Failed throws and cooldown cannot decrement. The client verifies `0..3`, remembers spawn count, and shows the value only when the actual knife/kabar is selected and the server has explicitly advertised support (no wrong indicator on other servers). Red 0/3. No new playerState fields, no protocol changes, no changes to MOUSE1 slash or MOUSE2 throw binding, no changes to bots, weapons, R77 flame effects, R80 Nitro or R87 Winchester.

### Official Linux64 native positive test

`cmake --build source/etlegacy-r87-m1897/build-r87 --target qagame cgame -j4` returned 0 for both. Isolated port UDP28223, two official ET: Legacy 2.86.2 Linux64 clients on Xvfb, Soldier w56 with actual knife switched via key 1. HUD screenshot visibly `3/3`, real three native `MOUSE2` throws (server log `Bombas_ThrowKnife: 0 314 1`, then `0 315 2`, then `0 314 3`) and corresponding screenshots `after_1.jpg` => 2/3, `after_2.jpg` => 1/3, `after_3.jpg` => RED 0/3. Fourth actual MOUSE2 press **did not** create a fourth server throw event (limit 3). Native console `/kill`, actual `MOD_SUICIDE`, respawn after ordinary wave produced HUD again `3/3` in `reset_3_of_3.jpg`, confirms correct server-to-client reset. Screenshots and full log in `reports/r88_knife_hud/`. No `Error parsing`, unknown command or crashes observed.

Remaining release gate: five native CI builds (Linux32/64, Windows32/64, macOS FAT x86_64/ARM64) and exact SHA-named universal client v088, pure auto FastDL old v087 to v088, OmniBot regression and deploy guard, normal live deployment. Real Windows/macOS runtime not physically tested. Do not claim these as passed yet.
