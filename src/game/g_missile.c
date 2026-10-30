/*
 * Wolfenstein: Enemy Territory GPL Source Code
 * Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.
 *
 * ET: Legacy
 * Copyright (C) 2012-2024 ET:Legacy team <mail@etlegacy.com>
 *
 * This file is part of ET: Legacy - http://www.etlegacy.com
 *
 * ET: Legacy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ET: Legacy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ET: Legacy. If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, Wolfenstein: Enemy Territory GPL Source Code is also
 * subject to certain additional terms. You should have received a copy
 * of these additional terms immediately following the terms and conditions
 * of the GNU General Public License which accompanied the source code.
 * If not, please request a copy in writing from id Software at the address below.
 *
 * id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.
 */
/**
 * @file g_missile.c
 */

#include "g_local.h"

#ifdef FEATURE_OMNIBOT
#include "g_etbot_interface.h"
#endif


#define BOMBAS_MOLOTOV_LIFETIME 8750
#define BOMBAS_MOLOTOV_RADIUS 108.f
#define BOMBAS_MOLOTOV_SMOKE_TAG 0xB059

void G_FlameDamage(gentity_t *self, gentity_t *ignoreent);

qboolean G_BombasMolotovWeapon(int weapon, team_t team)
{
    static const char *assets[] = {
        "models/bombas/molotov/v_pineapple_molotov.md3",
        "models/bombas/molotov/pineapple_molotov.md3",
        "models/bombas/medic/nitro_label.jpg"
    };
    fileHandle_t file;
    int i;

    if (trap_Cvar_VariableIntegerValue("g_bombasMolotov") != 1 ||
        !(team == TEAM_AXIS || team == TEAM_ALLIES) ||
        weapon != WP_GRENADE_PINEAPPLE)
    {
        return qfalse;
    }

    for (i = 0; i < (int)(sizeof(assets) / sizeof(assets[0])); i++)
    {
        if (trap_FS_FOpenFile(assets[i], &file, FS_READ) <= 0)
        {
            return qfalse;
        }
        trap_FS_FCloseFile(file);
    }

    return qtrue;
}

static qboolean G_BombasMolotovProjectile(const gentity_t *ent)
{
    return ent->classname && !Q_stricmp(ent->classname, "bombas_molotov");
}

/* The three flame models still use the original R32 geometry/shaders.
 * Their 192 MD3 frames animate individually staggered flame tongues,
 * growing from zero, overlapping and fading smoothly. The original R44
 * sprite shaders/textures and gameplay lifetimes remain unchanged. */
#define BOMBAS_MOLOTOV_FIRE_FRAMES 192
static int G_BombasMolotovFireFrame(const gentity_t *fire)
{
    int elapsed = BOMBAS_MOLOTOV_LIFETIME - (fire->timestamp - level.time);
    int frame;
    if (elapsed <= 0)
    {
        return 0;
    }
    if (elapsed >= BOMBAS_MOLOTOV_LIFETIME)
    {
        return BOMBAS_MOLOTOV_FIRE_FRAMES - 1;
    }
    frame = (elapsed * (BOMBAS_MOLOTOV_FIRE_FRAMES - 1)) / BOMBAS_MOLOTOV_LIFETIME;
    return frame;
}

/* Each decorative floor patch must BIRTH at frame zero at its own delayed
 * reveal time. Previously all satellites sampled the central impact clock,
 * so a late patch appeared already bright and looked like a sudden fireball.
 * Keep the same global expiry and the original floor/wall spread traces. */
static int G_BombasMolotovVisualFrame(const gentity_t *visual)
{
    int begin = (int)visual->wait;
    int span = visual->timestamp - begin;
    int elapsed = level.time - begin;
    if (elapsed <= 0 || span <= 0) return 0;
    if (elapsed >= span) return BOMBAS_MOLOTOV_FIRE_FRAMES - 1;
    return (elapsed * (BOMBAS_MOLOTOV_FIRE_FRAMES - 1)) / span;
}

static void G_BombasMolotovBurnMark(gentity_t *ent, qboolean main, int begin)
{
    // Only the actual impact point starts charring immediately. Six existing
    // large scorch stages and all satellite timings remain bit-for-bit the
    // same after 1.5s. Sixteen ultra-light center stages at 90ms spacing
    // prevent a sudden discrete black patch from appearing at impact.
    const int earlyCount = main ? 16 : 0;
    int elapsed = level.time - begin;
    while (ent->count < earlyCount + 6)
    {
        int early = ent->count < earlyCount;
        int stage = early ? ent->count : ent->count - earlyCount;
        int when = early ? stage * 90 : 1500 + stage * 200;
        vec3_t origin;
        gentity_t *event;
        if (elapsed < when) break;
        VectorCopy(ent->r.currentOrigin, origin);
        origin[2] += 5.f;
        event = G_TempEntity(origin, EV_BOMBAS_MOLOTOV_SCORCH);
        event->s.eventParm = (stage << 1) | (main ? 1 : 0) | (early ? 0x80 : 0);
        ent->count++;
    }
}

static void G_BombasMolotovVisualThink(gentity_t *visual)
{
    if (level.time >= visual->timestamp)
    {
        G_FreeEntity(visual);
        return;
    }
    if (trap_PointContents(visual->r.currentOrigin, visual->s.number) & CONTENTS_WATER)
    {
        G_FreeEntity(visual);
        return;
    }

    if (level.match_pause != PAUSE_NONE)
    {
        visual->timestamp += level.frameTime;
        visual->wait += level.frameTime;
    }
    else if (visual->s.eType == ET_INVISIBLE && level.time >= visual->wait)
    {
        visual->s.eType = ET_GENERAL;
    }
    if (level.match_pause == PAUSE_NONE && level.time >= visual->wait)
    {
        G_BombasMolotovBurnMark(visual, qfalse, (int)visual->wait);
    }
    /* Smoke-only pass fades during final 1.35s; flame pass stays R57 native. */
    visual->s.time = visual->timestamp - 1350;
    visual->s.time2 = visual->timestamp;
    /* Each visual now follows its own local ignition clock and reaches
     * frame 191 at the same shared expiry time. */
    visual->s.frame = G_BombasMolotovVisualFrame(visual);
    /* 192 distinct MD3 fire phases over 8.75s, update each server frame
     * instead of 75ms to avoid skipping several soft-growth steps. */
    visual->nextthink = level.time + FRAMETIME;
}

static void G_BombasMolotovFireThink(gentity_t *fire)
{
    gentity_t *owner;

    if (level.time >= fire->timestamp)
    {
        G_FreeEntity(fire);
        return;
    }
    if (fire->r.ownerNum < 0 || fire->r.ownerNum >= level.maxclients)
    {
        G_FreeEntity(fire);
        return;
    }

    owner = &g_entities[fire->r.ownerNum];
    if (!owner->inuse || !owner->client || owner->client->pers.connected != CON_CONNECTED ||
        owner->client->pers.connectTime != fire->count2)
    {
        G_FreeEntity(fire);
        return;
    }

    if (level.match_pause != PAUSE_NONE)
    {
        fire->timestamp += level.frameTime;
        fire->nextthink = level.time + FRAMETIME;
        return;
    }

    if (trap_PointContents(fire->r.currentOrigin, fire->s.number) & CONTENTS_WATER)
    {
        G_FreeEntity(fire);
        return;
    }

    G_BombasMolotovBurnMark(fire, qtrue, fire->timestamp - BOMBAS_MOLOTOV_LIFETIME);

    /* Smoke-only model fades independently; untouched flame pass stays R57. */
    fire->s.time = fire->timestamp - 1350;
    fire->s.time2 = fire->timestamp;
    /* Same original R44 flame shaders and sprite shapes; R49's new MD3 frames
     * control individual, overlapping tongue rise/decay. */
    fire->s.frame = G_BombasMolotovFireFrame(fire);

    fire->s.constantLight = ((25 + ((level.time / 110 + fire->s.number * 3) % 13)) << 24) |
                            (24 << 16) | (116 << 8) | 255;
    G_FlameDamage(fire, NULL);
    fire->nextthink = level.time + FRAMETIME;
}

// Bombas R71: short, geometric wall-splash decals; unlike R70 these do not
// use upright MD3 quads. The client projects them onto actual BSP polygons.
// Cosmetic-only: no collision, gameplay damage, or changes to R69 ground.
#define BOMBAS_WALL_DECAL_TAG 0xB071

static void G_BombasWallDecalThink(gentity_t *wall)
{
    if (level.match_pause != PAUSE_NONE)
    {
        wall->s.time += level.frameTime;
        wall->s.time2 += level.frameTime;
        wall->nextthink = level.time + FRAMETIME;
        return;
    }
    if (level.time >= wall->s.time2)
    {
        G_FreeEntity(wall);
        return;
    }
    wall->nextthink = level.time + FRAMETIME;
}

static void G_BombasSpawnWallFlow(const vec3_t contact, const vec3_t normal,
                                  float requestedHeight, int expiry, int owner,
                                  int variation)
{
    vec3_t wallNormal, front, probe;
    trace_t hit;
    int i, active = 0;
    gentity_t *wall;
    float height;

    if (!g_bombasMolotovWalls.integer || expiry <= level.time + 400 ||
        fabsf(normal[2]) > .42f || requestedHeight < 10.f ||
        requestedHeight > 300.f)
    {
        return;
    }
    VectorSet(wallNormal, normal[0], normal[1], 0.f);
    if (VectorNormalize(wallNormal) < .72f) return;
    for (i = level.maxclients; i < level.num_entities; i++)
    {
        if (g_entities[i].inuse && g_entities[i].s.effect1Time == BOMBAS_WALL_DECAL_TAG)
            active++;
    }
    if (active >= 24) return;

    // R72: ONE continuous, geometry-clipped liquid trail from the bottle
    // contact toward the original floor pool. Don't chain disconnected MD3s.
    // Single projection may extend up to 170u for high stair landing walls.
    height = Com_Clamp(12.f, 172.f, requestedHeight + 5.f);
    // G_MissileImpact supplies the GRENADE'S CENTER, not the wall point.
    // The projectile collision hull extends 4u horizontally; R71 probed
    // only 3u behind that center, frequently missing the wall entirely.
    // Trace >=32u inward and use the resulting *actual BSP hit position*.
    VectorMA(contact, 14.f, wallNormal, front);
    VectorMA(contact, -32.f, wallNormal, probe);
    trap_Trace(&hit, front, NULL, NULL, probe, owner, MASK_SOLID);
    if (hit.startsolid || hit.allsolid || hit.fraction >= .99f ||
        DotProduct(hit.plane.normal, wallNormal) < .82f ||
        hit.surfaceFlags & (SURF_SKY | SURF_NOIMPACT))
    {
        return;
    }

    wall = G_Spawn();
    wall->classname = "bombas_wall_decal";
    wall->s.eType = ET_GENERAL;
    wall->s.effect1Time = BOMBAS_WALL_DECAL_TAG;
    wall->s.modelindex = 0; // recognized by R71 CG_General before model check
    wall->r.svFlags |= SVF_BROADCAST;
    wall->s.pos.trType = TR_STATIONARY;
    VectorCopy(wallNormal, wall->s.origin2);
    wall->s.time = level.time + 80 + 38 * (variation % 4);
    wall->s.time2 = wall->s.time + 2450 + 105 * (variation % 3);
    wall->s.angles2[0] = height;
    wall->s.angles2[1] = variation % 3 == 0 ? 39.f : 30.f;
    wall->s.angles2[2] = (float)(variation % 7);
    wall->think = G_BombasWallDecalThink;
    wall->nextthink = level.time + FRAMETIME;
    G_SetOrigin(wall, hit.endpos);
    trap_LinkEntity(wall);

}

static void G_BombasMolotovBurst(gentity_t *missile, const vec3_t position, const vec3_t impactNormal)
{
    gentity_t *owner;
    gentity_t *fire;
    vec3_t start, end;
    trace_t ground;
    int i;

    if (missile->r.ownerNum < 0 || missile->r.ownerNum >= level.maxclients)
    {
        G_FreeEntity(missile);
        return;
    }

    owner = &g_entities[missile->r.ownerNum];
    if (!owner->inuse || !owner->client || owner->client->pers.connected != CON_CONNECTED)
    {
        G_FreeEntity(missile);
        return;
    }

    VectorCopy(position, start);
    if (impactNormal)
    {
        VectorMA(start, 14.f, impactNormal, start);
    }
    VectorCopy(start, end);
    start[2] += 12.f;
    end[2] -= 512.f;
    trap_Trace(&ground, start, NULL, NULL, end, missile->s.number, MASK_SOLID);

    if (ground.fraction < 1.f && ground.plane.normal[2] >= .65f)
    {
        VectorCopy(ground.endpos, start);
        start[2] += 1.5f;
    }
    else
    {
        VectorCopy(position, start);
    }

    if (trap_PointContents(start, missile->s.number) & CONTENTS_WATER)
    {
        G_FreeEntity(missile);
        return;
    }

    // A bottle breaking on a real vertical wall must leave a brief wall
    // splash even if there is no walkable landing directly below it. The
    // original ground fire positioning and damage remain untouched.
    if (impactNormal && fabsf(impactNormal[2]) < .42f)
    {
        float drop = (ground.fraction < 1.f && ground.plane.normal[2] >= .65f)
                     ? position[2] - start[2] : 46.f;
        if (drop < 14.f) drop = 18.f;
        G_BombasSpawnWallFlow(position, impactNormal,
                              Com_Clamp(16.f, 170.f, drop),
                              level.time + BOMBAS_MOLOTOV_LIFETIME,
                              missile->s.number, missile->s.number);
    }

    fire = G_Spawn();
    fire->classname = "bombas_molotov_fire";
    fire->s.eType = ET_GENERAL;
    fire->s.modelindex = G_ModelIndex("models/bombas/molotov/fire_patch.md3");
    fire->s.modelindex2 = G_ModelIndex("models/bombas/molotov/fire_patch_smoke.md3");
    fire->s.effect1Time = BOMBAS_MOLOTOV_SMOKE_TAG;
    fire->s.loopSound = G_SoundIndex("sound/world/fire_01.wav");
    fire->s.constantLight = (42 << 24) | (22 << 16) | (112 << 8) | 255;
    fire->s.pos.trType = TR_STATIONARY;
    fire->s.apos.trType = TR_STATIONARY;
    fire->r.svFlags |= SVF_BROADCAST;
    fire->r.ownerNum = missile->r.ownerNum;
    fire->parent = owner;
    fire->count2 = owner->client->pers.connectTime;
    fire->speed = BOMBAS_MOLOTOV_RADIUS;
    fire->timestamp = level.time + BOMBAS_MOLOTOV_LIFETIME;
    fire->flameQuotaTime = level.time;
    fire->think = G_BombasMolotovFireThink;
    fire->nextthink = level.time + FRAMETIME;

    G_SetOrigin(fire, start);
    trap_LinkEntity(fire);
    G_Sound(fire, G_SoundIndex("sound/world/glassbreak1.wav"));

    {
        int activeVisuals = 0;
        for (i = level.maxclients; i < level.num_entities; ++i)
        {
            if (g_entities[i].inuse && g_entities[i].classname &&
                !Q_stricmp(g_entities[i].classname, "bombas_molotov_visual"))
            {
                ++activeVisuals;
            }
        }
        if (activeVisuals >= 72)
        {
            G_FreeEntity(missile);
            return;
        }
    }

    for (i = 0; i < 6; ++i)
    {
        gentity_t *visual;
        vec3_t point, floorEnd, losStart, losEnd, angles;
        trace_t patch, los;
        // R62: the old alternating 54/82-unit radii produced three outer
        // corners at 120-degree intervals: a visible TRIANGLE of fire.
        // Spread all six fire patches around a gently uneven circular edge.
        // Hash the impact time, entity and position once per throw; every
        // Molotov gets its own outline, but never changes shape mid-burn.
        unsigned int impactSeed = (unsigned int)level.time * 1664525u
                                ^ (unsigned int)missile->s.number * 1013904223u
                                ^ (unsigned int)((int)(start[0] * 8.f)) * 2246822519u
                                ^ (unsigned int)((int)(start[1] * 8.f)) * 3266489917u;
        unsigned int shapeSeed = impactSeed ^ (unsigned int)(i + 1) * 0x9e3779b9u;
        float rotation, angle, radius;
        shapeSeed ^= shapeSeed >> 16;
        shapeSeed *= 0x7feb352du;
        shapeSeed ^= shapeSeed >> 15;
        shapeSeed *= 0x846ca68bu;
        shapeSeed ^= shapeSeed >> 16;
        rotation = (float)((impactSeed >> 4) & 1023u) * (2.f * (float)M_PI / 1024.f);
        angle = rotation + (float)i * (2.f * (float)M_PI / 6.f)
                        + ((float)(shapeSeed & 255u) / 255.f - .5f) * .29f;
        radius = 65.f + (float)((shapeSeed >> 8) % 17u);

        VectorSet(point, start[0] + cosf(angle) * radius, start[1] + sinf(angle) * radius, start[2] + 46.f);
        VectorCopy(point, floorEnd);
        /* Cosmetic-only longer downward search for lower stair landings.
         * Retains the original LOS obstruction trace against walls. */
        floorEnd[2] -= 180.f;
        trap_Trace(&patch, point, NULL, NULL, floorEnd, missile->s.number, MASK_SOLID);
        if (patch.fraction >= 1.f || patch.startsolid || patch.plane.normal[2] < .65f)
        {
            continue;
        }

        VectorCopy(start, losStart);
        VectorCopy(patch.endpos, losEnd);
        losStart[2] += 18.f;
        losEnd[2] += 18.f;
        trap_Trace(&los, losStart, NULL, NULL, losEnd, missile->s.number, MASK_SOLID);
        if (los.startsolid || los.fraction < .98f)
        {
            // A low stair riser between two nearby floor samples is not
            // an arbitrary wall: visible downward edge, short height and
            // nearly vertical obstacle normal. Bridge ONLY that edge with
            // a narrow flowing ribbon. Do not propagate flames behind it.
            float step = los.endpos[2] - patch.endpos[2];
            if (i == 0 && !los.startsolid && los.fraction < .98f &&
                fabsf(los.plane.normal[2]) < .3f &&
                step >= 12.f && step <= 72.f &&
                patch.endpos[2] < start[2] - 8.f)
            {
                G_BombasSpawnWallFlow(los.endpos, los.plane.normal, step,
                                      fire->timestamp, missile->s.number, i);
            }
            continue;
        }

        // Short stair risers may be below the normal LOS at +18 units.
        // Probe a low path as well; add a cosmetic wall-to-floor bridge only
        // when a real vertical face separates an upper and lower landing.
        if (g_bombasMolotovWalls.integer && i == 3 &&
            patch.endpos[2] < start[2] - 12.f &&
            patch.endpos[2] >= start[2] - 72.f)
        {
            vec3_t nearTop, nearBottom;
            trace_t lowPath;
            VectorCopy(start, nearTop);
            VectorCopy(patch.endpos, nearBottom);
            nearTop[2] += 4.f;
            nearBottom[2] += 4.f;
            trap_Trace(&lowPath, nearTop, NULL, NULL, nearBottom,
                       missile->s.number, MASK_SOLID);
            if (!lowPath.startsolid && lowPath.fraction > .1f &&
                lowPath.fraction < .98f &&
                fabsf(lowPath.plane.normal[2]) < .30f)
            {
                float step = lowPath.endpos[2] - patch.endpos[2];
                if (step >= 12.f && step <= 72.f)
                    G_BombasSpawnWallFlow(lowPath.endpos,
                                          lowPath.plane.normal, step,
                                          fire->timestamp, missile->s.number, i);
            }
        }

        visual = G_Spawn();
        visual->classname = "bombas_molotov_visual";
        {
            static const int delays[6] = { 0, 580, 1290, 370, 1740, 970 };
            visual->s.eType = i == 0 ? ET_GENERAL : ET_INVISIBLE;
            visual->wait = (float)(level.time + delays[i]);
            visual->s.modelindex = G_ModelIndex(va("models/bombas/molotov/fire_lick_patch_v%d.md3", i));
            visual->s.modelindex2 = G_ModelIndex(va("models/bombas/molotov/fire_lick_patch_v%d_smoke.md3", i));
            visual->s.effect1Time = BOMBAS_MOLOTOV_SMOKE_TAG;
        }
        visual->r.svFlags |= SVF_BROADCAST;
        visual->s.pos.trType = TR_STATIONARY;
        visual->timestamp = fire->timestamp;
        visual->think = G_BombasMolotovVisualThink;
        visual->nextthink = level.time + 150;
        patch.endpos[2] += 1.5f;
        G_SetOrigin(visual, patch.endpos);
        VectorSet(angles, 0.f, angle * 180.f / (float)M_PI, 0.f);
        G_SetAngle(visual, angles);
        trap_LinkEntity(visual);
    }

    G_FreeEntity(missile);
}

static void G_BombasMolotovExpire(gentity_t *missile)
{
    G_BombasMolotovBurst(missile, missile->r.currentOrigin, NULL);
}

/**
 * @brief G_BounceMissile
 * @param[in,out] ent
 * @param[in] trace
 * @return true if missile actually bounced, otherwise false
 */
qboolean G_BounceMissile(gentity_t *ent, trace_t *trace)
{
	vec3_t    velocity;
	float     dot;
	int       hitTime;
	gentity_t *ground;

	// boom after 750 msecs
	if (GetWeaponTableData(ent->s.weapon)->type & WEAPON_TYPE_RIFLENADE)
	{
		ent->s.effect1Time = qtrue; // has bounced

		if ((ent->nextthink - level.time) < 3250)
		{
			G_ExplodeMissile(ent);
			return qtrue;
		}
	}

	// reflect the velocity on the trace plane
	hitTime = (int)(level.previousTime + (level.frameTime * trace->fraction));
	BG_EvaluateTrajectoryDelta(&ent->s.pos, hitTime, velocity, qfalse, ent->s.effect2Time);
	dot = DotProduct(velocity, trace->plane.normal);
	VectorMA(velocity, -2 * dot, trace->plane.normal, ent->s.pos.trDelta);

	// bounces without any velocity do not constitute proper bounces
	if (velocity[0] == 0.0 && velocity[1] == 0.0 && velocity[2] == 0.0)
	{
		return qfalse;
	}

	// record this for mover pushing
	if (trace->plane.normal[2] > 0.2f /*&& VectorLengthSquared( ent->s.pos.trDelta ) < Square(40)*/)
	{
		ent->s.groundEntityNum = trace->entityNum;
	}

	// set ground entity
	if (ent->s.groundEntityNum != ENTITYNUM_NONE)
	{
		ground = &g_entities[ent->s.groundEntityNum];
	}
	else
	{
		ground = NULL;
	}

	// allow ground entity to push missle
	if (ent->s.groundEntityNum != ENTITYNUM_WORLD && ground)
	{
		VectorMA(ent->s.pos.trDelta, 0.85f, ground->instantVelocity, ent->s.pos.trDelta);
	}

	if (ent->s.eFlags & EF_BOUNCE_HALF)
	{
		vec3_t relativeDelta;

		if (ent->s.eFlags & EF_BOUNCE)         // both flags marked, do a third type of bounce
		{
			VectorScale(ent->s.pos.trDelta, 0.35f, ent->s.pos.trDelta);
		}
		else
		{
			VectorScale(ent->s.pos.trDelta, 0.65f, ent->s.pos.trDelta);
		}

		// grenades on movers get scaled back much earlier
		if (ent->s.groundEntityNum != ENTITYNUM_WORLD)
		{
			VectorScale(ent->s.pos.trDelta, 0.5f, ent->s.pos.trDelta);
		}

		// calculate relative delta for stop calcs
		// FIXME: this condition is always true due to || 1 ... why ?
		if (ent->s.groundEntityNum == ENTITYNUM_WORLD || 1)
		{
			VectorCopy(ent->s.pos.trDelta, relativeDelta);
		}
		else
		{
			VectorSubtract(ent->s.pos.trDelta, ground->instantVelocity, relativeDelta);
		}

		// check for stop
		//if ( trace->plane.normal[2] > 0.2 && VectorLengthSquared( ent->s.pos.trDelta ) < Square(40) )
		if ((trace->plane.normal[2] > 0.2f && VectorLengthSquared(relativeDelta) < 1600)
		    || (!trace->fraction && !(GetWeaponTableData(ent->s.weapon)->type & WEAPON_TYPE_RIFLENADE))) // Square(40)
		{
			// make the world the owner of the ent, so the player can shoot it after it stops moving
			if (ent->r.contents == CONTENTS_CORPSE)
			{
				ent->r.ownerNum = ENTITYNUM_WORLD;
			}

			G_SetOrigin(ent, trace->endpos);
			ent->s.time = level.time; // final rotation value
			if (GetWeaponTableData(ent->s.weapon)->type & WEAPON_TYPE_RIFLENADE)
			{
				// explode one 750msecs after launchtime
				ent->nextthink = level.time + (750 - (level.time + 4000 - ent->nextthink));
			}
			return qtrue;
		}
	}

	SnapVector(ent->s.pos.trDelta);

	VectorAdd(ent->r.currentOrigin, trace->plane.normal, ent->r.currentOrigin);
	VectorCopy(ent->r.currentOrigin, ent->s.pos.trBase);

	SnapVector(ent->s.pos.trBase);
	ent->s.pos.trTime = level.time;
	return qtrue;
}

static void G_BombasEmbeddedKnifeThink(gentity_t *knife)
{
    vec3_t axis[3], position, normal;
    int i;
    if (level.time >= knife->timestamp || !knife->enemy || !knife->enemy->inuse ||
        knife->enemy->s.eType != ET_MOVER)
    {
        G_FreeEntity(knife);
        return;
    }
    AnglesToAxis(knife->enemy->r.currentAngles, axis);
    VectorCopy(knife->enemy->r.currentOrigin, position);
    VectorClear(normal);
    for (i = 0; i < 3; i++)
    {
        VectorMA(position, knife->pos1[i], axis[i], position);
        VectorMA(normal, knife->pos2[i], axis[i], normal);
    }
    G_SetOrigin(knife, position);
    vectoangles(normal, knife->s.apos.trBase);
    knife->s.apos.trBase[ROLL] = knife->pos3[0];
    trap_LinkEntity(knife);
    knife->nextthink = level.time + FRAMETIME;
}

/**
 * @brief G_MissileImpact
 * @param[in] ent
 * @param[in] trace
 * @param[in] impactDamage is how much damage the impact will do to func_explosives
 * @return true if missile exploded otherwise false
 */
void G_MissileImpact(gentity_t *ent, trace_t *trace, int impactDamage)
{
	gentity_t      *other = &g_entities[trace->entityNum];
	gentity_t      *temp;
	vec3_t         velocity;
	entity_event_t event       = EV_NONE;
	net_uint8_t    param       = 0;
	int            otherentnum = 0;

	if (G_BombasMolotovProjectile(ent))
	{
		G_BombasMolotovBurst(ent, trace->endpos, trace->plane.normal);
		return;
	}

	// handle func_explosives
	if (other->classname && Q_stricmp(other->classname, "func_explosive") == 0)
	{
		// the damage is sufficient to "break" the ent (health == 0 is non-breakable)
		if (other->health && impactDamage >= other->health)
		{
			// check for other->takedamage needs to be inside the health check since it is
			// likely that, if successfully destroyed by the missile, in the next runmissile()
			// update takedamage would be set to '0' and the func_explosive would not be
			// removed yet, causing a bounce.
			if (other->takedamage)
			{
				BG_EvaluateTrajectoryDelta(&ent->s.pos, level.time, velocity, qfalse, ent->s.effect2Time);
				G_Damage(other, ent, &g_entities[ent->r.ownerNum], velocity, ent->s.origin, impactDamage, 0, ent->methodOfDeath);
			}

			// its possible of the func_explosive not to die from this and it
			// should reflect the missile or explode it not vanish into oblivion
			if (other->health <= 0)
			{
				return;
			}
		}
	}

	// check for bounce
	if ((!other->takedamage || !ent->damage) && (ent->s.eFlags & (EF_BOUNCE | EF_BOUNCE_HALF)))
	{
		if (G_BounceMissile(ent, trace))
		{
			// sending bounce event for smoketrail is unnecessary
			if (ent->s.weapon == WP_SMOKETRAIL)
			{
				return;
			}

			G_AddEvent(ent, EV_GRENADE_BOUNCE, BG_FootstepForSurface(trace->surfaceFlags));
		}

		return;
	}

	// impact damage
	if (other->takedamage || other->dmgparent)
	{
		if (ent->damage)
		{
			BG_EvaluateTrajectoryDelta(&ent->s.pos, level.time, velocity, qfalse, ent->s.effect2Time);
			if (VectorLengthSquared(velocity) == 0.f)
			{
				velocity[2] = 1;    // stepped on a grenade
			}
			G_Damage(other->dmgparent ? other->dmgparent : other, ent, &g_entities[ent->r.ownerNum], velocity, ent->s.origin, ent->damage, 0, ent->methodOfDeath);
		}
		else     // if no damage value, then this is a splash damage grenade only
		{
			G_BounceMissile(ent, trace);
			return;
		}
	}

	if (ent->classname && !Q_stricmp(ent->classname, "bombas_throwing_knife") &&
	    trace->entityNum != ENTITYNUM_NONE &&
	    (trace->entityNum == ENTITYNUM_WORLD || trace->entityNum >= level.maxclients) &&
	    !(trace->surfaceFlags & SURF_SKY) && !other->client)
	{
		vec3_t origin, direction, flightAngles, targetAngles;
		gentity_t *oldest = NULL;
		int i, embedded = 0;
		for (i = level.maxclients; i < level.num_entities; i++)
		{
			gentity_t *knife = &g_entities[i];
			if (knife == ent || !knife->inuse || knife->s.eType != ET_GENERAL ||
			    knife->r.ownerNum != ent->r.ownerNum || !knife->classname ||
			    Q_stricmp(knife->classname, "bombas_throwing_knife"))
			{
				continue;
			}
			embedded++;
			if (!oldest || knife->timestamp < oldest->timestamp)
			{
				oldest = knife;
			}
		}
		if (embedded >= 6 && oldest)
		{
			G_FreeEntity(oldest);
		}
		BG_EvaluateTrajectory(&ent->s.apos, level.time, flightAngles, qtrue, ent->s.effect2Time);
		BG_EvaluateTrajectoryDelta(&ent->s.pos, level.time, direction, qfalse, ent->s.effect2Time);
		if (VectorNormalize(direction) < .001f)
		{
			VectorScale(trace->plane.normal, -1.f, direction);
		}
		if (DotProduct(direction, trace->plane.normal) > -.58f)
		{
			VectorMA(direction, -.58f - DotProduct(direction, trace->plane.normal), trace->plane.normal, direction);
			VectorNormalize(direction);
		}
		vectoangles(direction, targetAngles);
		targetAngles[PITCH] += 12.f * sinf(DEG2RAD(flightAngles[PITCH]));
		targetAngles[ROLL] = 12.f * cosf(DEG2RAD(flightAngles[PITCH]));
		AngleVectors(targetAngles, direction, NULL, NULL);
		if (DotProduct(direction, trace->plane.normal) > -.35f)
		{
			VectorMA(direction, -.5f - DotProduct(direction, trace->plane.normal), trace->plane.normal, direction);
			VectorNormalize(direction);
			vectoangles(direction, targetAngles);
			targetAngles[ROLL] = 12.f * cosf(DEG2RAD(flightAngles[PITCH]));
		}
		VectorMA(trace->endpos, -9.96f, direction, origin);
		VectorMA(origin, -2.3f, trace->plane.normal, origin);
		ent->s.apos.trType = TR_STATIONARY;
		VectorCopy(targetAngles, ent->s.apos.trBase);
		VectorClear(ent->s.apos.trDelta);
		ent->s.eType = ET_GENERAL;
		ent->s.weapon = WP_NONE;
		ent->s.modelindex = G_ModelIndex("models/weapons2/knife_kbar/knife_throw.md3");
		ent->r.contents = 0;
		ent->clipmask = 0;
		ent->think = G_FreeEntity;
		ent->timestamp = level.time + 20000;
		ent->nextthink = ent->timestamp;
		G_SetOrigin(ent, origin);
		if (trace->entityNum != ENTITYNUM_WORLD && other->inuse && other->s.eType == ET_MOVER)
		{
			vec3_t axis[3], offset;
			int i;
			AnglesToAxis(other->r.currentAngles, axis);
			VectorSubtract(origin, other->r.currentOrigin, offset);
			for (i = 0; i < 3; i++)
			{
				ent->pos1[i] = DotProduct(offset, axis[i]);
				ent->pos2[i] = DotProduct(direction, axis[i]);
			}
			ent->pos3[0] = targetAngles[ROLL];
			ent->enemy = other;
			ent->think = G_BombasEmbeddedKnifeThink;
			ent->nextthink = level.time + FRAMETIME;
		}
		trap_LinkEntity(ent);
		return;
	}

	// TODO: is it cheaper in bandwidth to just remove this ent and create a new
	// one, rather than changing the missile into the explosion?
	if (other->takedamage && other->client)
	{
		event       = EV_MISSILE_HIT;
		param       = DirToByte(trace->plane.normal);
		otherentnum = other->s.number;
	}
	else
	{
		// try projecting it in the direction it came from, for better decals
		vec3_t dir;

		BG_EvaluateTrajectoryDelta(&ent->s.pos, level.time, dir, qfalse, ent->s.effect2Time);
		BG_GetMarkDir(dir, trace->plane.normal, dir);

		event = EV_MISSILE_MISS;
		param = DirToByte(dir);
	}

	// splash damage (doesn't apply to person directly hit)
	if (ent->splashDamage)
	{
		G_RadiusDamage(trace->endpos, ent, ent->parent, ent->splashDamage, ent->splashRadius, other, ent->splashMethodOfDeath);
	}

	// the missile exploded right after being fired
	// client doesn't even received the new ent in a frame time delay
	if (ent->spawnTime + FRAMETIME >= level.time)
	{

		// temp impact mark event
		temp                   = G_TempEntity(trace->endpos, event);
		temp->s.otherEntityNum = otherentnum;
		temp->r.svFlags       |= SVF_BROADCAST;
		temp->s.eventParm      = param;
		temp->s.weapon         = ent->s.weapon;
		temp->s.clientNum      = ent->r.ownerNum;

		// give big weapons the shakey shakey
		if (GetWeaponTableData(ent->s.weapon)->attributes & WEAPON_ATTRIBUT_SHAKE)
		{
			temp                     = G_TempEntity(ent->r.currentOrigin, EV_SHAKE);
			temp->s.onFireStart      = ent->splashDamage * 4;
			temp->r.svFlags         |= SVF_BROADCAST;
			temp->r.snapshotCallback = qtrue;
		}

		G_FreeEntity(ent);
	}
	else
	{
		G_AddEvent(ent, event, param);
		G_SetOrigin(ent, trace->endpos);
		ent->s.otherEntityNum = otherentnum;

		// give big weapons the shakey shakey
		if (GetWeaponTableData(ent->s.weapon)->attributes & WEAPON_ATTRIBUT_SHAKE)
		{
			G_AddEvent(ent, EV_SHAKE, param);
			ent->s.onFireStart = ent->splashDamage * 4;
		}

		ent->s.eType        = ET_GENERAL;
		ent->freeAfterEvent = qtrue;
	}
}

/**
 * @brief Explode a missile without an impact
 * @param[in,out] ent
 */
void G_ExplodeMissile(gentity_t *ent)
{
	vec3_t dir, origin;
	int    etype;

	if (G_BombasMolotovProjectile(ent))
	{
		G_BombasMolotovBurst(ent, ent->r.currentOrigin, NULL);
		return;
	}

	etype        = ent->s.eType;
	ent->s.eType = ET_GENERAL;

	// splash damage
	if (ent->splashDamage)
	{
		VectorCopy(ent->r.currentOrigin, origin);

		if (ent->s.weapon == WP_DYNAMITE)
		{
			origin[2] += 4;
		}

		if ((ent->s.weapon == WP_DYNAMITE && (ent->etpro_misc_1 & 1)) || ent->s.weapon == WP_SATCHEL)
		{
			etpro_RadiusDamage(origin, ent, ent->parent, ent->splashDamage, ent->splashRadius, ent, ent->splashMethodOfDeath, qtrue);
			G_TempTraceIgnorePlayersAndBodies();
			etpro_RadiusDamage(origin, ent, ent->parent, ent->splashDamage, ent->splashRadius, ent, ent->splashMethodOfDeath, qfalse);
			G_ResetTempTraceIgnoreEnts();
		}
		else
		{
			G_RadiusDamage(origin, ent, ent->parent, ent->splashDamage, ent->splashRadius, ent, ent->splashMethodOfDeath);
		}
	}

	SnapVector(ent->r.currentOrigin);
	G_SetOrigin(ent, ent->r.currentOrigin);

	// we don't have a valid direction, so just point straight up
	dir[0] = dir[1] = 0;
	dir[2] = 1;

	if (ent->accuracy == 1.f)
	{
		G_AddEvent(ent, EV_MISSILE_MISS_SMALL, DirToByte(dir));
	}
	else if (ent->accuracy == 2.f)
	{
		G_AddEvent(ent, EV_MISSILE_MISS_LARGE, DirToByte(dir));
	}
	else if (ent->accuracy == 3.f)
	{
		ent->freeAfterEvent = qtrue;
		trap_LinkEntity(ent);
		return;
	}
	else
	{
		G_AddEvent(ent, EV_MISSILE_MISS, DirToByte(dir));
		ent->s.clientNum = ent->r.ownerNum;
	}

	ent->freeAfterEvent = qtrue;

	trap_LinkEntity(ent);

	if (etype == ET_MISSILE)
	{
		if (ent->s.weapon == WP_LANDMINE)
		{
			mapEntityData_t *mEnt;

			if ((mEnt = G_FindMapEntityData(&mapEntityData[0], ent - g_entities)) != NULL)
			{
				G_FreeMapEntityData(&mapEntityData[0], mEnt);
			}

			if ((mEnt = G_FindMapEntityData(&mapEntityData[1], ent - g_entities)) != NULL)
			{
				G_FreeMapEntityData(&mapEntityData[1], mEnt);
			}
		}
		else if (ent->s.weapon == WP_DYNAMITE && (ent->etpro_misc_1 & 1))         // do some scoring
		{   // check if dynamite is in trigger_objective_info field
			vec3_t    mins, maxs;
			int       i, num, touch[MAX_GENTITIES];
			gentity_t *hit;

			ent->free = NULL; // no defused tidy up if we exploded

			// made this the actual bounding box of dynamite instead of range
			VectorAdd(ent->r.currentOrigin, GetWeaponFireTableData(ent->s.weapon)->hitBox[0], mins);
			VectorAdd(ent->r.currentOrigin, GetWeaponFireTableData(ent->s.weapon)->hitBox[1], maxs);
			num = trap_EntitiesInBox(mins, maxs, touch, MAX_GENTITIES);

			for (i = 0; i < num; i++)
			{
				hit = &g_entities[touch[i]];
				if (!hit->target)
				{
					continue;
				}

				if ((hit->s.eType != ET_OID_TRIGGER))
				{
					continue;
				}

				if (!(hit->spawnflags & (AXIS_OBJECTIVE | ALLIED_OBJECTIVE)))
				{
					continue;
				}

				if (hit->target_ent)
				{
					// only if it targets a func_explosive
					if (hit->target_ent->s.eType != ET_EXPLOSIVE)
					{
						continue;
					}

					if (hit->target_ent->constructibleStats.weaponclass < 1)
					{
						continue;
					}
				}

				if (((hit->spawnflags & AXIS_OBJECTIVE) && (ent->s.teamNum == TEAM_ALLIES)) || ((hit->spawnflags & ALLIED_OBJECTIVE) && (ent->s.teamNum == TEAM_AXIS)))
				{
					if (ent->parent->client && hit->target_ent && GetMODTableData(MOD_DYNAMITE)->weaponClassForMOD >= hit->target_ent->constructibleStats.weaponclass)
					{
						G_LogPrintf("Objective_Destroyed: %d %s\n", (int)(ent->parent - g_entities), hit->track);
						G_AddKillSkillPointsForDestruction(ent->parent, MOD_DYNAMITE, &hit->target_ent->constructibleStats);
					}

					G_UseTargets(hit, ent);
					hit->think     = G_FreeEntity;
					hit->nextthink = level.time + FRAMETIME;

					G_Script_ScriptEvent(hit, "destroyed", "");
				}
			}
		}

		// give big weapons the shakey shakey
		if (GetWeaponTableData(ent->s.weapon)->attributes & WEAPON_ATTRIBUT_SHAKE)
		{
			gentity_t *tent;

			tent = G_TempEntity(ent->r.currentOrigin, EV_SHAKE);

			tent->s.onFireStart      = ent->splashDamage * 4;
			tent->r.svFlags         |= SVF_BROADCAST;
			tent->r.snapshotCallback = qtrue;
		}
	}
}

/**
 * @brief MissileGroundCheck
 * @param[in,out] self
 */
void MissileGroundCheck(gentity_t *self)
{
	vec3_t  mins, maxs;
	vec3_t  start, end;
	trace_t tr;

	VectorCopy(self->r.currentOrigin, start);
	VectorCopy(self->r.currentOrigin, end);

	end[2] -= 4;

	VectorCopy(self->r.mins, mins);
	VectorCopy(self->r.maxs, maxs);

	trap_Trace(&tr, start, mins, maxs, end, self->s.number, MASK_MISSILESHOT);

	if (tr.fraction == 1.f)
	{
		self->s.groundEntityNum = ENTITYNUM_NONE;
	}
}

/**
 * @brief G_RunMissile
 * @param[in,out] ent
 */
void G_RunMissile(gentity_t *ent)
{
	vec3_t  origin, angle;
	trace_t tr;

	// throwable ent (landmine, dynamite, satchel, grenade)
	// if ent already landed but the ground "disappeared" later on (i.e player got gibbed)
	// check for such change so the ent can still fall down and not be floating in the air
	if (GetWeaponTableData(ent->s.weapon)->firingMode & WEAPON_FIRING_MODE_THROWABLE)
	{
		MissileGroundCheck(ent);

		if (ent->s.groundEntityNum == ENTITYNUM_NONE)
		{
			if (ent->s.pos.trType != TR_GRAVITY)
			{
				ent->s.pos.trType = TR_GRAVITY;
				ent->s.pos.trTime = level.time;
			}
		}
	}

	// get current position
	BG_EvaluateTrajectory(&ent->s.pos, level.time, origin, qfalse, ent->s.effect2Time);
	BG_EvaluateTrajectory(&ent->s.apos, level.time, angle, qtrue, ent->s.effect2Time);

	// ignore body
	if ((ent->clipmask & CONTENTS_BODY) && (GetWeaponTableData(ent->s.weapon)->firingMode & WEAPON_FIRING_MODE_THROWABLE))
	{
		if (ent->s.pos.trDelta[0] == 0.f && ent->s.pos.trDelta[1] == 0.f && ent->s.pos.trDelta[2] == 0.f)
		{
			ent->clipmask &= ~CONTENTS_BODY;
		}
	}

	if (ent->r.contents != CONTENTS_CORPSE)
	{
		if (ent->count)
		{
			// is ent outside worldspace (X or Y coord)
			if (ent->r.currentOrigin[0] < level.mapcoordsMins[0] ||
			    ent->r.currentOrigin[1] > level.mapcoordsMins[1] ||
			    ent->r.currentOrigin[0] > level.mapcoordsMaxs[0] ||
			    ent->r.currentOrigin[1] < level.mapcoordsMaxs[1])
			{
				gentity_t *tent;

				tent              = G_TempEntity(ent->r.currentOrigin, EV_MORTAR_MISS);
				tent->s.clientNum = ent->r.ownerNum;
				tent->r.svFlags  |= SVF_BROADCAST;
				tent->s.density   = 1;  // angular

				//G_ExplodeMissile(ent);  // play explode sound
				G_FreeEntity(ent);      // and delete it
			}
			else
			{
				vec3_t tmp;

				VectorCopy(origin, tmp);
				tmp[2] = MAX_MAP_SIZE;
				trap_Trace(&tr, origin, NULL, NULL, tmp, ent->s.number, ent->clipmask);

				// are we in worldspace again
				if (tr.fraction == 1.f)
				{
					G_RunThink(ent);

					VectorCopy(origin, ent->r.currentOrigin);   // keep the previous origin to don't go too far
					VectorCopy(angle, ent->r.currentAngles);

					return;     // keep flying
				}

				// only re-enter through a skybox
				if (!(tr.surfaceFlags & SURF_SKY))
				{
					gentity_t *tent;

					tent              = G_TempEntity(ent->r.currentOrigin, EV_MORTAR_MISS);
					tent->s.clientNum = ent->r.ownerNum;
					tent->r.svFlags  |= SVF_BROADCAST;
					tent->s.density   = 0; // direct

					G_FreeEntity(ent);  // delete it
					return;
				}

				tmp[2] = -MAX_MAP_SIZE;
				trap_Trace(&tr, origin, NULL, NULL, tmp, ent->s.number, ent->clipmask);

				// is ent under the ground limit
				if (tr.fraction == 1.f)
				{
					gentity_t *tent;

					tent              = G_TempEntity(ent->r.currentOrigin, EV_MORTAR_MISS);
					tent->s.clientNum = ent->r.ownerNum;
					tent->r.svFlags  |= SVF_BROADCAST;
					tent->s.density   = 0;  // direct

					G_FreeEntity(ent);  // delete it
					return;
				}

				// is ent above the sky limit
				if (tr.surfaceFlags & SURF_SKY)
				{
					G_RunThink(ent);
					return; // keep flying
				}

				// back in the world, keep going like normal
				VectorCopy(origin, ent->r.currentOrigin);
				VectorCopy(angle, ent->r.currentAngles);
				ent->count  = 0;
				ent->count2 = 1;
			}
			return;
		}

		if (!ent->count2 && BG_GetSkyHeightAtPoint(origin) - BG_GetGroundHeightAtPoint(origin) > 1024)
		{
			ent->count2 = ent->r.currentOrigin[2] > origin[2];
		}
		else if ((ent->count2 == 1 || ent->count2 == 2) && (GetWeaponTableData(ent->s.weapon)->type & WEAPON_TYPE_MORTAR || ent->s.weapon == WP_MAPMORTAR))
		{
			vec3_t  impactpos;
			trace_t mortar_tr;

			if (ent->count2 == 1)
			{
				if (ent->s.pos.trType == TR_LINEAR)
				{
					// missile go down, play the falling sound
					G_AddEvent(ent, EV_MISSILE_FALLING, 0);
					ent->count2 = 2;
				}
				else
				{
					VectorSubtract(origin, ent->r.currentOrigin, impactpos);
					VectorMA(origin, 16, impactpos, impactpos);

					trap_Trace(&mortar_tr, origin, ent->r.mins, ent->r.maxs, impactpos, ent->r.ownerNum, ent->clipmask);

					if (mortar_tr.fraction != 1.f && !(mortar_tr.surfaceFlags & SURF_NOIMPACT))
					{
						// missile go down, play the falling sound
						G_AddEvent(ent, EV_MISSILE_FALLING, 0);
						ent->count2 = 2;
					}
				}
			}

			VectorSubtract(origin, ent->r.currentOrigin, impactpos);
			VectorMA(origin, 8, impactpos, impactpos);

			trap_Trace(&mortar_tr, origin, ent->r.mins, ent->r.maxs, impactpos, ent->r.ownerNum, ent->clipmask);

			if (mortar_tr.fraction != 1.f && !(mortar_tr.surfaceFlags & SURF_NOIMPACT))
			{
				G_AddEvent(ent, EV_MORTAR_IMPACT, 0);
				VectorCopy(mortar_tr.endpos, ent->s.origin2);           // impact point

				ent->count2 = 3;                                        // missile is about to impact, no more check in worldspace are required
			}
		}
	}

	// trace a line from the previous position to the current position,
	// ignoring interactions with the missile owner
	trap_Trace(&tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, origin, ent->r.ownerNum, ent->clipmask);

	VectorCopy(tr.endpos, ent->r.currentOrigin);
	VectorCopy(angle, ent->r.currentAngles);

	if (tr.startsolid)
	{
		tr.fraction = 0;
	}
	// store the last surfaceflags in case missile is traveling through a 2-sided skybox brush
	else
	{
		ent->lastSurfaceFlags = tr.surfaceFlags;
	}

	trap_LinkEntity(ent);

	if (tr.fraction != 1.f)
	{
		if (ent->r.contents != CONTENTS_CORPSE && (tr.surfaceFlags & SURF_SKY || ent->lastSurfaceFlags & SURF_SKY))
		{
			// goes through sky
			ent->count = 1;
			// omit unlinking entity for missile camera
			if (!g_cheats.integer && ent->s.weapon != WP_GRENADE_LAUNCHER && ent->s.weapon != WP_GRENADE_PINEAPPLE && ent->s.weapon != WP_GPG40 && ent->s.weapon != WP_M7 && ent->s.weapon != WP_MORTAR2_SET && ent->s.weapon != WP_MORTAR_SET && ent->s.weapon != WP_SMOKE_MARKER && ent->s.weapon != WP_SMOKE_BOMB)
			{
				trap_UnlinkEntity(ent);
			}
			G_RunThink(ent);
			return; // keep flying
		}

		if ((tr.surfaceFlags & SURF_NOIMPACT) &&
		    (!ent->classname || Q_stricmp(ent->classname, "bombas_throwing_knife") ||
		     (tr.surfaceFlags & SURF_SKY))) // never explode or bounce on sky
		{
			G_FreeEntity(ent);
			return;
		}

		if (ent->r.contents == CONTENTS_CORPSE)
		{
			if (ent->s.pos.trType != TR_STATIONARY)
			{
				G_MissileImpact(ent, &tr, GetWeaponFireTableData(ent->s.weapon)->impactDamage);
			}
		}
		else
		{
			G_MissileImpact(ent, &tr, GetWeaponFireTableData(ent->s.weapon)->impactDamage);
		}

		if (ent->s.eType != ET_MISSILE)
		{
			return;             // exploded
		}
	}
	else if (VectorLengthSquared(ent->s.pos.trDelta) != 0.f)           // free fall/no intersection
	{
		ent->s.groundEntityNum = ENTITYNUM_NONE;
	}

	// check think function after bouncing
	G_RunThink(ent);
}

/**
 * @brief G_PredictBounceMissile
 * @param[in] ent
 * @param[in,out] pos
 * @param[in] trace
 * @param[in] time
 */
void G_PredictBounceMissile(gentity_t *ent, trajectory_t *pos, trace_t *trace, int time)
{
	vec3_t velocity, origin;
	float  dot;
	int    hitTime;

	BG_EvaluateTrajectory(pos, time, origin, qfalse, ent->s.effect2Time);

	// reflect the velocity on the trace plane
	hitTime = time;
	BG_EvaluateTrajectoryDelta(pos, hitTime, velocity, qfalse, ent->s.effect2Time);
	dot = DotProduct(velocity, trace->plane.normal);
	VectorMA(velocity, -2 * dot, trace->plane.normal, pos->trDelta);

	if (ent->s.eFlags & EF_BOUNCE_HALF)
	{
		if (ent->s.eFlags & EF_BOUNCE)         // both flags marked, do a third type of bounce
		{
			VectorScale(pos->trDelta, 0.35f, pos->trDelta);
		}
		else
		{
			VectorScale(pos->trDelta, 0.65f, pos->trDelta);
		}

		// check for stop
		if (trace->plane.normal[2] > 0.2f && VectorLengthSquared(pos->trDelta) < Square(40))
		{
			VectorCopy(trace->endpos, pos->trBase);
			return;
		}
	}

	VectorAdd(origin, trace->plane.normal, pos->trBase);
	pos->trTime = time;
}

/**
 * @brief G_PredictMissile
 * @param[in,out] ent is the character that is checking to see what the missile is going to do
 * @param[in] duration
 * @param[out] endPos
 * @param[in] allowBounce
 * @return qfalse if the missile won't explode, otherwise it'll return the time is it expected to explode
 *
 */
int G_PredictMissile(gentity_t *ent, int duration, vec3_t endPos, qboolean allowBounce)
{
	vec3_t       origin;
	trace_t      tr;
	int          time;
	trajectory_t pos = ent->s.pos;
	vec3_t       org;
	gentity_t    backupEnt;

	BG_EvaluateTrajectory(&pos, level.time, org, qfalse, ent->s.effect2Time);

	backupEnt = *ent;

	for (time = level.time + FRAMETIME; time < level.time + duration; time += FRAMETIME)
	{
		// get current position
		BG_EvaluateTrajectory(&pos, time, origin, qfalse, ent->s.effect2Time);

		// trace a line from the previous position to the current position,
		// ignoring interactions with the missile owner
		trap_Trace(&tr, org, ent->r.mins, ent->r.maxs, origin, ent->r.ownerNum, ent->clipmask);

		VectorCopy(tr.endpos, org);

		if (tr.startsolid)
		{
			*ent = backupEnt;
			return qfalse;
		}

		if (tr.fraction != 1.f)
		{
			// never explode or bounce on sky
			if  (tr.surfaceFlags & SURF_NOIMPACT)
			{
				*ent = backupEnt;
				return qfalse;
			}

			if (allowBounce && (ent->s.eFlags & (EF_BOUNCE | EF_BOUNCE_HALF)))
			{
				G_PredictBounceMissile(ent, &pos, &tr, time - FRAMETIME + (int)((float)FRAMETIME * tr.fraction));
				pos.trTime = time;
				continue;
			}

			// exploded, so drop out of loop
			break;
		}
	}

	//if (!allowBounce && tr.fraction < 1 && tr.entityNum > level.maxclients) {
	//    // go back a bit in time, so we can catch it in the air
	//    time -= 200;
	//    if (time < level.time + FRAMETIME)
	//        time = level.time + FRAMETIME;
	//    BG_EvaluateTrajectory( &pos, time, org );
	//}


	// get current position
	VectorCopy(org, endPos);
	// set the entity data back
	*ent = backupEnt;

	if (allowBounce && (ent->s.eFlags & (EF_BOUNCE | EF_BOUNCE_HALF)))
	{
		return ent->nextthink;
	}
	else        // it will probably explode before it times out
	{
		return time;
	}
}

//=============================================================================
// Server side Flamethrower
//=============================================================================

/**
 * @brief G_BurnTarget
 * @param self
 * @param[in,out] body
 * @param[in] directhit
 */
void G_BurnTarget(gentity_t *self, gentity_t *body, qboolean directhit)
{
	float   radius, dist;
	vec3_t  point, v;
	trace_t tr;

	if (!body->takedamage)
	{
		return;
	}

	// don't catch fire if invulnerable or same team in no FF
	if (body->client)
	{
		if (body->client->ps.powerups[PW_INVULNERABLE] >= level.time)
		{
			body->flameQuota  = 0;
			body->s.onFireEnd = level.time - 1;
			return;
		}

		//if( !self->count2 && body == self->parent )
		//  return;

		if (!(g_friendlyFire.integer) && OnSameTeam(body, self->parent) &&
		    !(body == self->parent && self->classname && !Q_stricmp(self->classname, "bombas_molotov_fire")))
		{
			return;
		}
	}

	// don't catch fire if under water or invulnerable
	if (body->waterlevel >= 3)
	{
		body->flameQuota  = 0;
		body->s.onFireEnd = level.time - 1;
		return;
	}

	if (!body->r.bmodel)
	{
		VectorCopy(body->r.currentOrigin, point);
		if (body->client)
		{
			point[2] += body->client->ps.viewheight;
		}
		VectorSubtract(point, self->r.currentOrigin, v);
	}
	else
	{
		int i;

		for (i = 0 ; i < 3 ; i++)
		{
			if (self->s.origin[i] < body->r.absmin[i])
			{
				v[i] = body->r.absmin[i] - self->r.currentOrigin[i];
			}
			else if (self->r.currentOrigin[i] > body->r.absmax[i])
			{
				v[i] = self->r.currentOrigin[i] - body->r.absmax[i];
			}
			else
			{
				v[i] = 0;
			}
		}
	}

	radius = self->speed;

	dist = VectorLength(v);

	// The person who shot the flame only burns when within 1/2 the radius
	if (body->s.number == self->r.ownerNum && dist >= (radius * 0.5f) &&
	    !(self->classname && !Q_stricmp(self->classname, "bombas_molotov_fire")))
	{
		return;
	}
	if (!directhit && dist >= radius)
	{
		return;
	}

	// Non-clients that take damage get damaged here
	if (!body->client)
	{
		if (body->health > 0 && level.time + DEFAULT_SV_FRAMETIME >= body->lastBurnedFrameTime)
		{
            // Same physical damage, distinct Nitro Solvent cause of death.
            const meansOfDeath_t burnMOD = (self->classname && !Q_stricmp(self->classname, "bombas_molotov_fire"))
                                           ? MOD_BOMBAS_NITRO : MOD_FLAMETHROWER;
            G_Damage(body, self->parent, self->parent, vec3_origin, self->r.currentOrigin, 2, 0, burnMOD);
			body->lastBurnedFrameTime = level.time;
		}
		return;
	}

	// do a trace to see if there's a wall btwn. body & flame centroid -- prevents damage through walls
	G_TempTraceIgnoreBodies();
	trap_Trace(&tr, self->r.currentOrigin, NULL, NULL, point, body->s.number, MASK_SHOT);
	G_ResetTempTraceIgnoreEnts();

	if (tr.fraction < 1.0f)
	{
		return;
	}

	// now check the damageQuota to see if we should play a pain animation
	// first reduce the current damageQuota with time
	if (body->flameQuotaTime && body->flameQuota > 0)
	{
		body->flameQuota -= (int)(((float)(level.time - body->flameQuotaTime) / 1000) * 2.5f);
		if (body->flameQuota < 0)
		{
			body->flameQuota = 0;
		}
	}

	G_BurnMeGood(self->parent, body, self, directhit);

	if (self->count && self->parent->client)
	{
        const meansOfDeath_t burnMOD = (self->classname && !Q_stricmp(self->classname, "bombas_molotov_fire"))
                                       ? MOD_BOMBAS_NITRO : MOD_FLAMETHROWER;
        G_addStats(body, self->parent, GetWeaponTableData(WP_FLAMETHROWER)->damage, burnMOD);
		self->count = 0;
	}
}

/**
 * @brief G_FlameDamage
 * @param[in] self
 * @param[in] ignoreent
 */
void G_FlameDamage(gentity_t *self, gentity_t *ignoreent)
{
	gentity_t *body;
	vec3_t    mins, maxs;
	float     radius    = self->speed;
	float     boxradius = (float)(M_SQRT2 * (double)radius); // radius * sqrt(2) for bounding box enlargement
	int       entityList[MAX_GENTITIES];
	int       i, e, numListedEntities;

	for (i = 0 ; i < 3 ; i++)
	{
		mins[i] = self->r.currentOrigin[i] - boxradius;
		maxs[i] = self->r.currentOrigin[i] + boxradius;
	}

	numListedEntities = trap_EntitiesInBox(mins, maxs, entityList, MAX_GENTITIES);

	for (e = 0 ; e < numListedEntities ; e++)
	{
		body = &g_entities[entityList[e]];

		if (body == ignoreent)
		{
			continue;
		}

		G_BurnTarget(self, body, qfalse);
	}
}

/**
 * @brief G_RunFlamechunk
 * @param[in,out] ent
 */
void G_RunFlamechunk(gentity_t *ent)
{
	vec3_t    vel;
	vec3_t    neworg;
	trace_t   tr;
	float     speed;
	float     deltaTime  = (level.time - ent->s.pos.trTime) / 1000.f;
	gentity_t *ignoreent = NULL;

	// vel was only being set if (level.time - ent->timestamp > 50
	// However, below, it was being used when we hit something and it was uninitialized
	VectorCopy(ent->s.pos.trDelta, vel);
	speed = VectorNormalize(vel);

	// Adust the current speed of the chunk
	if (level.time - ent->timestamp <= DEFAULT_SV_FRAMETIME)
	{
		speed = FLAME_START_SPEED;
	}
	else if (level.time - ent->timestamp <= ent->s.pos.trDuration)
	{
		speed -= deltaTime * FLAME_FRICTION_PER_SEC;

		if (speed < FLAME_MIN_SPEED)
		{
			speed = FLAME_MIN_SPEED;
		}

		VectorScale(vel, speed, ent->s.pos.trDelta);
	}

	ent->s.pos.trTime = level.time;

	// Move the chunk
	VectorMA(ent->r.currentOrigin, deltaTime, ent->s.pos.trDelta, neworg);

	trap_Trace(&tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, neworg, ent->r.ownerNum, MASK_SHOT | MASK_WATER);

	if (tr.startsolid)
	{
		VectorClear(ent->s.pos.trDelta);
		ent->count2++;
	}
	else if (tr.fraction != 1.0f && !(tr.surfaceFlags & SURF_NOIMPACT))
	{
		float dot;

		VectorCopy(tr.endpos, ent->r.currentOrigin);

		dot = DotProduct(vel, tr.plane.normal);
		VectorMA(vel, -2.f * dot, tr.plane.normal, vel);
		VectorNormalize(vel);
		speed *= 0.5f * (0.25f + 0.75f * ((dot + 1.0f) * 0.5f));

		if (speed < FLAME_MIN_SPEED)
		{
			speed = FLAME_MIN_SPEED;
		}

		VectorScale(vel, speed, ent->s.pos.trDelta);

		if (tr.entityNum != ENTITYNUM_WORLD && tr.entityNum != ENTITYNUM_NONE)
		{
			ignoreent = &g_entities[tr.entityNum];
			G_BurnTarget(ent, ignoreent, qtrue);
		}

		ent->count2++;
	}
	else
	{
		VectorCopy(neworg, ent->r.currentOrigin);
	}

	// Do damage to nearby entities, every 100ms
	// technically this is unnecessary as the dps is gated further down the call chain,
	// but by keeping this here, we avoid tracing all ents in the box every frame
	if (ent->flameQuotaTime <= level.time)
	{
		ent->flameQuotaTime = level.time + FRAMETIME;
		G_FlameDamage(ent, ignoreent);
	}

	// Show debugging bbox
	if (g_debugBullets.integer > 3)
	{
		const float  size = ent->speed / 2;
		const vec3_t mins = { -size, -size, -size };
		const vec3_t maxs = { size, size, size };

		G_RailBox(ent->r.currentOrigin, mins, maxs, colorRed, ent->s.number);
	}

	// Adjust the size
	if (ent->speed < FLAME_START_MAX_SIZE)
	{
		ent->speed += (10.f * ((float)level.frameTime / DEFAULT_SV_FRAMETIME));

		if (ent->speed > FLAME_START_MAX_SIZE)
		{
			ent->speed = FLAME_START_MAX_SIZE;
		}
	}

	// Remove after 2 seconds
	if (level.time - ent->timestamp > (FLAME_LIFETIME - 150))       // increased to 350 from 250 to match visuals better
	{
		G_FreeEntity(ent);
		return;
	}

	G_RunThink(ent);
}

/**
 * @brief fire_flamechunk
 * @param[in,out] self
 * @param[in] start
 * @param[in] dir
 * @return
 */
gentity_t *fire_flamechunk(gentity_t *self, vec3_t start, vec3_t dir)
{
	gentity_t *bolt;

	// Only spawn every other frame
	if (self->count2)
	{
		self->count2--;
		return NULL;
	}

	self->count2 = 1;

	bolt = G_Spawn();
	G_PreFilledMissileEntity(bolt, WP_FLAMETHROWER, self->s.weapon, self->s.number, TEAM_FREE, -1, self, start, dir);

	// when holding down '+attack', flamechunks are created every 100ms
	// however if the player is tapping '+attack', they can create flamechunks at odd intervals,
	// which can cause the DPS to increase dramatically, especially on higher 'sv_fps' values,
	// since each individual flamechunk has their own 100ms cycle for timestamp at which keeps track when they last dealt damage
	// make sure the damage cooldown timestamp always aligns to 100ms intervals to keep the DPS always consistent
	bolt->flameQuotaTime   = (level.time + FRAMETIME) - (level.time % FRAMETIME);
	bolt->count2           = 0; // how often it bounced off of something
	bolt->count            = 1; // this chunk can add hit
	bolt->s.pos.trDuration = 550;
	bolt->speed            = FLAME_START_SIZE; // 'speed' will be the current size radius of the chunk

	return bolt;
}

//=============================================================================

/**
 * @brief DynaSink
 * @param[in,out] self
 */
void DynaSink(gentity_t *self)
{
	self->clipmask   = 0;
	self->r.contents = 0;

	if (self->timestamp < level.time)
	{
		self->think     = G_FreeEntity;
		self->nextthink = level.time + FRAMETIME;
		return;
	}

	self->s.pos.trBase[2] -= 0.5f;
	self->nextthink        = level.time + 50;
}

/**
 * @brief DynaFree
 * @param[in,out] self
 */
void DynaFree(gentity_t *self)
{
	// see if the dynamite was planted near a constructable object that would have been destroyed
	int       entityList[MAX_GENTITIES];
	int       numListedEntities;
	int       e;
	vec3_t    org;
	gentity_t *hit;

	self->free = NULL;

	if (self->think != G_ExplodeMissile)
	{
		return; // we weren't armed, so no defused event
	}

	VectorCopy(self->r.currentOrigin, org);
	org[2] += 4;    // move out of ground

	G_TempTraceIgnorePlayersAndBodies();
	numListedEntities = EntsThatRadiusCanDamage(org, self->splashRadius, entityList);
	G_ResetTempTraceIgnoreEnts();

	for (e = 0; e < numListedEntities; e++)
	{
		hit = &g_entities[entityList[e]];

		if (hit->s.eType != ET_CONSTRUCTIBLE)
		{
			continue;
		}

		// invulnerable
		if (hit->spawnflags & 2)
		{
			continue;
		}

		// not dynamite-able
		if (!(hit->spawnflags & 32))
		{
			continue;
		}

		G_Script_ScriptEvent(hit, "defused", "");
	}
}

/**
 * @brief G_ChainFree
 * @param[in] self
 */
void G_ChainFree(gentity_t *self)
{
	float     dist;
	gentity_t *ent;
	int       entityList[MAX_GENTITIES];
	int       numListedEntities;
	vec3_t    mins, maxs;
	vec3_t    v;
	int       i, e;
	float     boxradius;

	boxradius = M_SQRT2 * GetWeaponTableData(self->s.weapon)->splashRadius; // radius * sqrt(2) for bounding box enlargement --
	// bounding box was checking against radius / sqrt(2) if collision is along box plane
	for (i = 0 ; i < 3 ; i++)
	{
		mins[i] = self->s.origin[i] - boxradius;
		maxs[i] = self->s.origin[i] + boxradius;
	}

	numListedEntities = trap_EntitiesInBox(mins, maxs, entityList, MAX_GENTITIES);

	for (e = 0 ; e < numListedEntities ; e++)
	{
		ent = &g_entities[entityList[e]];

		if (ent == self)
		{
			continue;
		}
		if (!ent->takedamage && (!ent->dmgparent || !ent->dmgparent->takedamage)
		    && !(self->methodOfDeath == MOD_DYNAMITE && ent->s.weapon == WP_DYNAMITE))
		{
			continue;
		}

		G_AdjustedDamageVec(ent, self->s.origin, v);

		dist = VectorLength(v);
		if (dist >= GetWeaponTableData(self->s.weapon)->splashRadius)
		{
			continue;
		}

		// dyno chaining
		// only if within blast radius and both on the same objective or both or no objectives
		if (self->methodOfDeath == MOD_DYNAMITE && ent->s.weapon == WP_DYNAMITE)
		{
			G_DPrintf("dyno chaining: inflictor: %p, ent: %p\n", self->onobjective, ent->onobjective);

			if (self->onobjective == ent->onobjective && ent->s.effect1Time)
			{
				// free the other dynamite now too since they are peers
				ent->nextthink = level.time;

				ent->think = G_ChainFree;
			}
		}
	}

	G_FreeEntity(self);
}

/**
 * @brief Remove any items that the player should no longer have, on disconnect/class change etc
 * changed to just set the parent to NULL
 * @param[in] ent
 * @param[in] modType
 */
void G_FadeItems(gentity_t *ent, int modType)
{
	gentity_t *e = &g_entities[MAX_CLIENTS];
	int       i;

	for (i = MAX_CLIENTS ; i < level.num_entities ; i++, e++)
	{
		if (!e->inuse)
		{
			continue;
		}

		if (e->s.eType != ET_MISSILE)
		{
			continue;
		}

		if (e->methodOfDeath != modType)
		{
			continue;
		}

		if (e->parent != ent)
		{
			continue;
		}

		e->parent     = NULL;
		e->r.ownerNum = ENTITYNUM_NONE;

		// don't sink flying item
		if (e->s.pos.trType != TR_STATIONARY)
		{
			G_FreeEntity(e);
		}
		else
		{
			G_MagicSink(e);
		}
	}
}

/**
 * @brief G_CountTeamLandmines
 * @param[in] team
 * @return
 */
int G_CountTeamLandmines(team_t team)
{
	gentity_t *e = &g_entities[MAX_CLIENTS];
	int       i;
	int       cnt = 0;

	for (i = MAX_CLIENTS ; i < level.num_entities ; i++, e++)
	{
		if (!e->inuse)
		{
			continue;
		}

		if (e->s.eType != ET_MISSILE)
		{
			continue;
		}

		if (e->methodOfDeath != MOD_LANDMINE)
		{
			continue;
		}

		if (e->s.teamNum == team && e->s.effect1Time == 1)
		{
			cnt++;
		}
	}

	return cnt;
}

/**
 * @brief G_SweepForLandmines
 * @param[in] origin
 * @param[in] radius
 * @param[in] team
 * @return
 */
qboolean G_SweepForLandmines(vec3_t origin, float radius, int team)
{
	gentity_t *e = &g_entities[MAX_CLIENTS];
	int       i;
	vec3_t    dist;

	radius *= radius;

	for (i = MAX_CLIENTS; i < level.num_entities; i++, e++)
	{
		if (!e->inuse)
		{
			continue;
		}

		if (e->s.eType != ET_MISSILE)
		{
			continue;
		}

		if (e->methodOfDeath != MOD_LANDMINE)
		{
			continue;
		}

		if (e->s.teamNum != team && e->s.effect1Time == 1)
		{
			VectorSubtract(origin, e->r.currentOrigin, dist);
			if (VectorLengthSquared(dist) > radius)
			{
				continue;
			}

			return qtrue;   // found one
		}
	}

	return qfalse;
}

/**
 * @brief G_FindSatchel
 * @param[in] ent
 * @return
 */
gentity_t *G_FindSatchel(gentity_t *ent)
{
	gentity_t *e = &g_entities[MAX_CLIENTS];
	int       i;

	for (i = MAX_CLIENTS ; i < level.num_entities ; i++, e++)
	{
		if (!e->inuse)
		{
			continue;
		}

		if (e->s.eType != ET_MISSILE)
		{
			continue;
		}

		if (e->methodOfDeath != MOD_SATCHEL)
		{
			continue;
		}

		if (e->parent != ent)
		{
			continue;
		}

		return e;
	}

	return NULL;
}

/**
 * Removes any weapon objects lying around in the map when they disconnect/switch team
 */

/**
 * @brief G_ExplodeMines
 * @param[in] ent
 */
void G_ExplodeMines(gentity_t *ent)
{
	G_FadeItems(ent, MOD_LANDMINE);
}

/**
 * @brief G_ExplodeSatchels
 * @param[in] ent
 * @return
 */
qboolean G_ExplodeSatchels(gentity_t *ent)
{
	gentity_t *e = &g_entities[MAX_CLIENTS];
	vec3_t    dist;
	int       i;
	qboolean  blown = qfalse;

	for (i = MAX_CLIENTS ; i < level.num_entities; i++, e++)
	{
		if (!e->inuse)
		{
			continue;
		}

		if (e->s.eType != ET_MISSILE)
		{
			continue;
		}

		if (e->methodOfDeath != MOD_SATCHEL)
		{
			continue;
		}

		VectorSubtract(e->r.currentOrigin, ent->r.currentOrigin, dist);
		if (VectorLengthSquared(dist) > Square(2000))
		{
			continue;
		}

		if (e->parent != ent)
		{
			continue;
		}

		G_ExplodeMissile(e);
		blown = qtrue;
	}

	return blown;
}

/**
 * @brief G_FreeSatchel
 * @param[in,out] ent
 */
void G_FreeSatchel(gentity_t *ent)
{
	gentity_t *other;

	ent->free = NULL;

	if (ent->s.eType != ET_MISSILE)
	{
		return;
	}

	other = &g_entities[ent->s.clientNum];

	if (!other->client || other->client->pers.connected != CON_CONNECTED)
	{
		return;
	}

	if (other->client->sess.playerType != PC_COVERTOPS)
	{
		return;
	}

	other->client->ps.ammoclip[WP_SATCHEL_DET] = 0;
	other->client->ps.ammoclip[WP_SATCHEL]     = 1;
	if (other->client->ps.weapon == WP_SATCHEL_DET)
	{
		G_AddEvent(other, EV_NOAMMO, 0);
	}
}

void LandminePostThink(gentity_t *self);

/**
 * @brief LandMineTrigger
 * @param[in,out] self
 */
void LandMineTrigger(gentity_t *self)
{
	self->r.snapshotCallback = qfalse;
	self->r.contents         = CONTENTS_CORPSE;
	trap_LinkEntity(self);
	self->nextthink     = level.time + FRAMETIME;
	self->think         = LandminePostThink;
	self->s.effect1Time = 2;    // triggered
	// communicate trigger time to client
	self->s.time = level.time;
}

/**
 * @brief LandMinePostTrigger
 * @param[out] self
 */
void LandMinePostTrigger(gentity_t *self)
{
	self->nextthink = level.time + 300;
	self->think     = G_ExplodeMissile;
}

#define LANDMINE_TRIGGER_DIST 64.0f

/**
 * @brief Check if an entity will set off a landmine
 * @param[in] ent
 * @param[in] mine
 * @return
 */
qboolean sEntWillTriggerMine(gentity_t *ent, gentity_t *mine)
{
	// player types are the only things that set off mines (human and bot)
	if (ent->s.eType == ET_PLAYER && ent->client)
	{
		vec3_t dist;

		VectorSubtract(mine->r.currentOrigin, ent->r.currentOrigin, dist);
		// have to be within the trigger distance AND on the ground -- if we jump over a mine, we don't set it off
		//      (or if we fly by after setting one off)
		if ((VectorLengthSquared(dist) <= Square(LANDMINE_TRIGGER_DIST)) && (Q_fabs(dist[2]) < 45))
		{
			return qtrue;
		}
	}

	return qfalse;
}

/**
 * @brief Landmine waits for 2 seconds then primes, which sets think to checking for "enemies"
 * @param[in,out] self
 *
 * @note 107     11      20      0       0       0       0       fire gren
 */
void G_LandmineThink(gentity_t *self)
{
	int       entityList[MAX_GENTITIES];
	int       i, cnt;
	vec3_t    range = { LANDMINE_TRIGGER_DIST, LANDMINE_TRIGGER_DIST, LANDMINE_TRIGGER_DIST };
	vec3_t    mins, maxs;
	qboolean  trigger = qfalse;
	gentity_t *ent    = NULL;

	self->nextthink = level.time + FRAMETIME;

	if (level.time - self->missionLevel > 200)
	{
		self->s.density = 0; // time out the covert ops visibile thing, or we could get other clients being able to see mine later, etc
	}

	VectorSubtract(self->r.currentOrigin, range, mins);
	VectorAdd(self->r.currentOrigin, range, maxs);

	cnt = trap_EntitiesInBox(mins, maxs, entityList, MAX_GENTITIES);

	for (i = 0; i < cnt; i++)
	{
		ent = &g_entities[entityList[i]];

		if (!ent->client)
		{
			continue;
		}

		if (!g_friendlyFire.integer && self->s.teamNum == ent->client->sess.sessionTeam)
		{
			continue;
		}

#ifdef FEATURE_OMNIBOT
		if (!(g_OmniBotFlags.integer & OBF_TRIGGER_MINES) && ent->r.svFlags & SVF_BOT)
		{
			if (self->s.teamNum == ent->client->sess.sessionTeam)
			{
				continue;
			}

			if (G_LandmineSpotted(self))
			{
				continue;
			}
		}
#endif

		// use the unified trigger check to see if we are close enough to prime the mine
		if (sEntWillTriggerMine(ent, self))
		{
			trigger = qtrue;
			break;
		}
	}

	if (trigger)
	{
#ifdef FEATURE_OMNIBOT
		Bot_Event_PreTriggerMine(ent - g_entities, self);
#endif
		LandMineTrigger(self);
	}
}

/**
 * @brief LandminePostThink
 * @param[in,out] self
 */
void LandminePostThink(gentity_t *self)
{
	int       entityList[MAX_GENTITIES];
	int       i, cnt;
	vec3_t    range = { LANDMINE_TRIGGER_DIST, LANDMINE_TRIGGER_DIST, LANDMINE_TRIGGER_DIST };
	vec3_t    mins, maxs;
	qboolean  trigger = qfalse;
	gentity_t *ent    = NULL;

	self->nextthink = level.time + FRAMETIME;

	if (level.time - self->missionLevel > 5000)
	{
		self->s.density = 0; // time out the covert ops visibile thing, or we could get other clients being able to see mine later, etc
	}

	VectorSubtract(self->r.currentOrigin, range, mins);
	VectorAdd(self->r.currentOrigin, range, maxs);

	cnt = trap_EntitiesInBox(mins, maxs, entityList, MAX_GENTITIES);

	for (i = 0; i < cnt; i++)
	{
		ent = &g_entities[entityList[i]];

		if (!ent->client || (!g_friendlyFire.integer && self->s.teamNum == ent->client->sess.sessionTeam))
		{
			continue;
		}

		// use the unifed trigger check to see if we're still standing on the mine, so we don't set it off
		if (sEntWillTriggerMine(ent, self))
		{
			trigger = qtrue;
			break;
		}
	}

	if (!trigger)
	{
#ifdef FEATURE_OMNIBOT
		Bot_Event_PostTriggerMine(ent - g_entities, self);
#endif
		LandMinePostTrigger(self);
	}
}

/**
 * @brief G_LandminePrime
 * @param[out] self
 */
void G_LandminePrime(gentity_t *self)
{
	self->nextthink = level.time + FRAMETIME;
	self->think     = G_LandmineThink;
}

/**
 * @brief G_LandmineSnapshotCallback
 * @param[in] entityNum
 * @param[in] clientNum
 * @return
 */
qboolean G_LandmineSnapshotCallback(int entityNum, int clientNum)
{
	gentity_t *ent   = &g_entities[entityNum];
	gentity_t *clEnt = &g_entities[clientNum];
	int       i;

	// don't send if landmine is not in pvs
	if (!trap_InPVS(clEnt->client->ps.origin, ent->r.currentOrigin))
	{
		return qfalse;
	}

	if (BG_IsSkillAvailable(clEnt->client->sess.skill, SK_BATTLE_SENSE, SK_BATTLE_SENSE_TRAP_AWARENESS))
	{
		return qtrue;
	}

	if (!G_LandmineArmed(ent))
	{
		return qtrue;
	}

	if (G_LandmineSpotted(ent))
	{
		return qtrue;
	}

	if (ent->s.teamNum == clEnt->client->sess.sessionTeam)
	{
		return qtrue;
	}

	// fix for covops spotting
	if (clEnt->client->sess.playerType == PC_COVERTOPS && (clEnt->client->ps.eFlags & EF_ZOOMING) && (clEnt->client->ps.stats[STAT_KEYS] & (1 << INV_BINOCS)))
	{
		return qtrue;
	}

	// shoutcasters can see landmines
	if (clEnt->client->sess.sessionTeam == TEAM_SPECTATOR && clEnt->client->sess.shoutcaster)
	{
		return qtrue;
	}

	// check also following shoutcasters
	for (i = 0; i < level.numConnectedClients; i++)
	{
		gclient_t *cl = &level.clients[level.sortedClients[i]];

		if (cl->sess.sessionTeam == TEAM_SPECTATOR &&
		    cl->sess.spectatorState == SPECTATOR_FOLLOW &&
		    cl->sess.spectatorClient == (clEnt - g_entities) &&
		    cl->sess.shoutcaster)
		{
			return qtrue;
		}
	}

	return qfalse;
}

/**
 * @brief fire_missile
 * @param[in] self
 * @param[in] start
 * @param[in] dir
 * @param[in] weapon
 * @return
 *
 * @note IMPORTANT!!! : This accepts a /non-normalized/ direction vector to allow specification
 * of how hard it's thrown.  Please scale the vector before calling.
 */
gentity_t *fire_missile(gentity_t *self, vec3_t start, vec3_t dir, int weapon)
{
	gentity_t *bolt;

	bolt = G_Spawn();
	G_PreFilledMissileEntity(bolt, weapon, weapon,
	                         self->s.number, self->client ? self->client->sess.sessionTeam : self->s.teamNum, // store team so we can generate red or blue smoke
	                         self->client ? self->client->ps.clientNum : self->s.clientNum,
	                         self, start, dir);

	if (self->client && G_BombasMolotovWeapon(weapon, self->client->sess.sessionTeam))
	{
		bolt->classname = "bombas_molotov";
        if (self->r.svFlags & SVF_BOT)
        {
            G_Printf("Bombas: bot %d actually threw Molotov\n", self->s.number);
        }
		bolt->s.eFlags &= ~(EF_BOUNCE | EF_BOUNCE_HALF);
		bolt->damage = 0;
		bolt->splashDamage = 0;
		bolt->think = G_BombasMolotovExpire;
		bolt->nextthink = level.time + 2500;
	}

	// no self->client for shooter_grenade's
	// if grenade time left, add it to next think and reset it, else add default value
	if (GetWeaponTableData(weapon)->grenadeTime && self->client && self->client->ps.grenadeTimeLeft)
	{
		bolt->nextthink                  = level.time + self->client->ps.grenadeTimeLeft;
		self->client->ps.grenadeTimeLeft = 0;   // reset grenade timer
	}

	if (weapon == WP_DYNAMITE)
	{
		trap_SendServerCommand(self - g_entities, "cp \"Dynamite is set, but NOT armed!\"");
		// nope - this causes the dynamite to impact on the players bb when he throws it.
		// will try setting it when it settles
		//bolt->r.ownerNum            = ENTITYNUM_WORLD;  // make the world the owner of the dynamite, so the player can shoot it without modifying the bullet code to ignore players id for hits
	}
	else if (weapon == WP_SATCHEL)
	{
		// satchel crosshair ID
		bolt->s.otherEntityNum = self->s.number;
	}

	return bolt;
}

//=============================================================================

/**
 * @brief fire_flamebarrel
 * @param[in] self
 * @param[in] start
 * @param[in] dir
 * @return
 */
gentity_t *fire_flamebarrel(gentity_t *self, vec3_t start, vec3_t dir)
{
	gentity_t *bolt;

	bolt = G_Spawn();

	VectorNormalize(dir);

	// for explosion type
	bolt->accuracy = 3;

	bolt->classname    = "flamebarrel";
	bolt->nextthink    = level.time + 3000;
	bolt->think        = G_ExplodeMissile;
	bolt->s.eType      = ET_FLAMEBARREL;
	bolt->s.eFlags     = EF_BOUNCE_HALF;
	bolt->r.svFlags    = SVF_BLANK;
	bolt->s.weapon     = WP_PANZERFAUST;
	bolt->r.ownerNum   = self->s.number;
	bolt->parent       = self;
	bolt->damage       = 100;
	bolt->splashDamage = 20;
	bolt->splashRadius = 60;

	bolt->methodOfDeath       = MOD_EXPLOSIVE;
	bolt->splashMethodOfDeath = MOD_EXPLOSIVE;

	bolt->clipmask = MASK_MISSILESHOT;

	bolt->s.pos.trType = TR_GRAVITY;
	bolt->s.pos.trTime = level.time + MISSILE_PRESTEP_TIME;     // move a bit on the very first frame
	VectorCopy(start, bolt->s.pos.trBase);
	VectorScale(dir, 900 + (crandom() * 100), bolt->s.pos.trDelta);
	SnapVector(bolt->s.pos.trDelta);            // save net bandwidth
	VectorCopy(start, bolt->r.currentOrigin);

	return bolt;
}
