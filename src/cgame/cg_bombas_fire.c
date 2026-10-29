/*
 * Bombas R77 - geometry contact for the ORIGINAL Claude/R69 Molotov flames.
 *
 * Why this exists (verified in R76 code and the user's screenshots):
 * the 7 fire MD3s (fire_patch.md3, fire_lick_patch_v0..5.md3) are rendered as
 * whole refEntities. The renderer cannot clip individual animated cards against
 * BSP edges, so on narrow supports (top of a staircase, the top of a rounded
 * wall, a ledge) every card whose root lies past the edge keeps burning in the
 * air ("wisi w powietrzu").
 *
 * R77 keeps EXACTLY the R76 path whenever every flame card of an entity stands
 * on real geometry (flat ground, ramps, roofs, the R76 staircase grade): the
 * entity is still added with trap_R_AddRefEntityToScene, untouched.
 * Only when at least one card root has no surface under it does the entity
 * switch to "card mode": the cgame reads the same MD3 (same 192 frames, same
 * per-card size/shape/timing, same shaders, same frame interpolation as
 * LerpMeshVertexes) and submits each card as a poly. Supported cards are
 * byte-for-byte the same geometry as the model path. Each unsupported card is
 * decided ONCE (cached) from real CG_Trace hits:
 *   - FALL:    a walkable surface exists straight below -> the card burns there,
 *              ignited a little later (free-fall time of the dripping fuel);
 *   - CURTAIN: a near-vertical face exists between the void and the fire ->
 *              the card is anchored on that face (stair riser/stringer side,
 *              curved wall), its own flame rising along it;
 *   - DROP:    nothing safe -> not rendered (never in the air, never in a wall).
 * No card is stretched, rescaled, re-textured or re-timed (only delayed).
 *
 * Wall splashes (BOMBAS_WALL_DECAL_TAG entities from the R70-R76 server) also
 * get a short burst of the same original tongues, each anchored by its own
 * trace on the actual (possibly curved) face, rising upward, then dying out
 * with their original animation before the wet char decal fades.
 *
 * cg_bombasFireContact 0 restores the exact R76 rendering at runtime.
 */

#include "cg_local.h"

#define BF_FRAMES          192
#define BF_MODELS          7
#define BF_MAX_CARDS       128
#define BF_POOL_CARDS      640
#define BF_CACHE           48
#define BF_WALL_CACHE      24
#define BF_WALL_CARDS      13
#define BF_SCRATCH         (320 * 1024)
#define BF_MD3_SCALE       (1.0f / 64.0f)
#define BF_FPS             (191.0f / 8.75f)

enum { BF_KEEP = 0, BF_DROP, BF_FALL, BF_CURTAIN, BF_PUSH };

typedef struct
{
	qhandle_t shader;
	byte autosprite;
	byte ground;          // flat ground card (embers)
	short firstAlive, lastAlive;
	float rootX, rootY, baseZ, maxRadius;
	float st[4][2];
	int data;             // index into bfXyz (in shorts)
} bfCard_t;

typedef struct
{
	int state;            // 0 untried, 1 ok, -1 failed
	int numCards;
	bfCard_t cards[BF_MAX_CARDS];
} bfModel_t;

typedef struct
{
	int entityNum, modelIndex, expiry, lastUsed;
	vec3_t origin;
	int cardMode;         // 0 = original refEntity (all supported), 1 = per-card
	byte mode[BF_MAX_CARDS];
	short delay[BF_MAX_CARDS];
	vec3_t shift[BF_MAX_CARDS];
} bfContact_t;

typedef struct
{
	int entityNum, time, time2, lastUsed, model, count;
	short card[BF_WALL_CARDS];
	int start[BF_WALL_CARDS], life[BF_WALL_CARDS];
	vec3_t anchor[BF_WALL_CARDS], normal[BF_WALL_CARDS];
} bfWall_t;

static short       bfXyz[BF_POOL_CARDS * BF_FRAMES * 12];
static int         bfXyzUsed;
static bfModel_t   bfModels[BF_MODELS];
static bfContact_t bfContacts[BF_CACHE];
static bfWall_t    bfWalls[BF_WALL_CACHE];
static byte        bfScratch[BF_SCRATCH];
static polyVert_t  bfPolys[BF_MAX_CARDS * 4];
static int         bfLastTime;

static int BF_I32(const byte *p)
{
	return (int)((unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24));
}

static short BF_I16(const byte *p)
{
	return (short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}

static float BF_F32(const byte *p)
{
	int   i = BF_I32(p);
	float f;
	Com_Memcpy(&f, &i, sizeof(f));
	return f;
}

static unsigned int BF_Hash(unsigned int x)
{
	x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
	return x;
}

static float BF_Rand(unsigned int seed, int k)
{
	return (float)(BF_Hash(seed ^ ((unsigned int)(k + 1) * 0x9e3779b9U)) & 0xffffU) / 65535.f;
}

static const char *BF_ModelName(int v)
{
	return v ? va("models/bombas/molotov/fire_lick_patch_v%d.md3", v - 1) : "models/bombas/molotov/fire_patch.md3";
}

static qboolean BF_Read(fileHandle_t f, void *buf, int len, int *left)
{
	if (len < 0 || len > *left)
	{
		return qfalse;
	}
	trap_FS_Read(buf, len, f);
	*left -= len;
	return qtrue;
}

// Reads the same MD3 the renderer uses. Only exact 4-vertex cards are kept.
static qboolean BF_LoadModel(int v)
{
	bfModel_t    *m = &bfModels[v];
	fileHandle_t f;
	byte         head[108], sh[108];
	int          len, left, numFrames, numSurfaces, ofsSurfaces, s, skip;

	if (m->state)
	{
		return m->state > 0;
	}
	m->state = -1;
	len      = trap_FS_FOpenFile(BF_ModelName(v), &f, FS_READ);
	if (len <= 108 || !f)
	{
		if (f)
		{
			trap_FS_FCloseFile(f);
		}
		return qfalse;
	}
	left = len;
	if (!BF_Read(f, head, 108, &left) || BF_I32(head) != (('3' << 24) | ('P' << 16) | ('D' << 8) | 'I') ||
	    BF_I32(head + 4) != 15)
	{
		trap_FS_FCloseFile(f);
		return qfalse;
	}
	numFrames   = BF_I32(head + 76);
	numSurfaces = BF_I32(head + 84);
	ofsSurfaces = BF_I32(head + 100);
	if (numFrames != BF_FRAMES || numSurfaces < 1 || numSurfaces > 32 || ofsSurfaces < 108 || ofsSurfaces > len)
	{
		trap_FS_FCloseFile(f);
		return qfalse;
	}
	// MD3 layout is sequential (header, frames, tags, surfaces): skip to surfaces
	skip = ofsSurfaces - 108;
	while (skip > 0)
	{
		int chunk = skip > BF_SCRATCH ? BF_SCRATCH : skip;
		if (!BF_Read(f, bfScratch, chunk, &left))
		{
			trap_FS_FCloseFile(f);
			return qfalse;
		}
		skip -= chunk;
	}
	m->numCards = 0;
	for (s = 0; s < numSurfaces; ++s)
	{
		int  sFrames, numVerts, ofsShaders, ofsSt, ofsXyz, ofsEnd, q, fr, k;
		char shaderName[MAX_QPATH];
		qhandle_t hShader;
		qboolean autosprite, ground;

		if (!BF_Read(f, sh, 108, &left) || BF_I32(sh) != (('3' << 24) | ('P' << 16) | ('D' << 8) | 'I'))
		{
			break;
		}
		sFrames    = BF_I32(sh + 72);
		numVerts   = BF_I32(sh + 80);
		ofsShaders = BF_I32(sh + 92);
		ofsSt      = BF_I32(sh + 96);
		ofsXyz     = BF_I32(sh + 100);
		ofsEnd     = BF_I32(sh + 104);
		if (ofsEnd < 108 || ofsEnd - 108 > BF_SCRATCH || !BF_Read(f, bfScratch, ofsEnd - 108, &left))
		{
			break;
		}
		if (sFrames != BF_FRAMES || numVerts < 4 || (numVerts & 3) ||
		    ofsShaders < 108 || ofsShaders + 64 > ofsEnd || ofsSt < 108 || ofsSt + numVerts * 8 > ofsEnd ||
		    ofsXyz < 108 || ofsXyz + sFrames * numVerts * 8 > ofsEnd)
		{
			continue;
		}
		Q_strncpyz(shaderName, (const char *)bfScratch + ofsShaders - 108, sizeof(shaderName));
		autosprite = (strstr(shaderName, "/r52_") || strstr(shaderName, "r65core")) ? qtrue : qfalse;
		ground     = !Q_stricmp(shaderName, "models/bombas/molotov/embers") ? qtrue : qfalse;
		hShader    = trap_R_RegisterShader(shaderName);
		if (!hShader)
		{
			continue;
		}
		for (q = 0; q < numVerts / 4; ++q)
		{
			bfCard_t *c;
			float    sx = 0.f, sy = 0.f, sz = 0.f, maxR = 0.f;
			int      alive = 0;

			if (m->numCards >= BF_MAX_CARDS || bfXyzUsed + BF_FRAMES * 12 > (int)(sizeof(bfXyz) / sizeof(bfXyz[0])))
			{
				break;
			}
			c             = &m->cards[m->numCards];
			c->shader     = hShader;
			c->autosprite = (byte)autosprite;
			c->ground     = (byte)ground;
			c->data       = bfXyzUsed;
			c->firstAlive = -1;
			c->lastAlive  = -1;
			for (k = 0; k < 4; ++k)
			{
				c->st[k][0] = BF_F32(bfScratch + ofsSt - 108 + (q * 4 + k) * 8);
				c->st[k][1] = BF_F32(bfScratch + ofsSt - 108 + (q * 4 + k) * 8 + 4);
			}
			for (fr = 0; fr < BF_FRAMES; ++fr)
			{
				vec3_t vtx[4], mid;
				float  r, minz = 1e9f;
				for (k = 0; k < 4; ++k)
				{
					const byte *p = bfScratch + ofsXyz - 108 + ((fr * numVerts) + q * 4 + k) * 8;
					short      *o = &bfXyz[bfXyzUsed + fr * 12 + k * 3];
					o[0]     = BF_I16(p);
					o[1]     = BF_I16(p + 2);
					o[2]     = BF_I16(p + 4);
					vtx[k][0] = o[0] * BF_MD3_SCALE;
					vtx[k][1] = o[1] * BF_MD3_SCALE;
					vtx[k][2] = o[2] * BF_MD3_SCALE;
					if (vtx[k][2] < minz)
					{
						minz = vtx[k][2];
					}
				}
				VectorAdd(vtx[0], vtx[1], mid);
				VectorAdd(mid, vtx[2], mid);
				VectorAdd(mid, vtx[3], mid);
				VectorScale(mid, .25f, mid);
				r = Distance(vtx[0], mid) * (autosprite ? .707f : 1.f);
				if (r > 2.f)
				{
					if (c->firstAlive < 0)
					{
						c->firstAlive = fr;
					}
					c->lastAlive = fr;
					sx         += mid[0];
					sy         += mid[1];
					sz         += minz;
					alive++;
					if (r > maxR)
					{
						maxR = r;
					}
				}
			}
			if (!alive)
			{
				continue;   // never visible: its pool slot is overwritten by the next card
			}
			bfXyzUsed += BF_FRAMES * 12;
			c->rootX     = sx / alive;
			c->rootY     = sy / alive;
			c->baseZ     = sz / alive;
			c->maxRadius = maxR;
			m->numCards++;
		}
	}
	trap_FS_FCloseFile(f);
	if (s < numSurfaces || !m->numCards)
	{
		m->numCards = 0;   // truncated/unknown file: never render a partial fire, keep R76 path
		return qfalse;
	}
	m->state = 1;
	return qtrue;
}

static int BF_Variant(int modelIndex)
{
	const char *name;
	int        i;

	if (modelIndex < 1 || modelIndex >= MAX_MODELS)
	{
		return -1;
	}
	name = CG_ConfigString(CS_MODELS + modelIndex);
	if (!Q_stricmp(name, "models/bombas/molotov/fire_patch.md3"))
	{
		return 0;
	}
	for (i = 0; i < 6; ++i)
	{
		if (!Q_stricmp(name, va("models/bombas/molotov/fire_lick_patch_v%d.md3", i)))
		{
			return i + 1;
		}
	}
	return -1;
}

// Model-space vertex k of card c at a fractional frame (same lerp as LerpMeshVertexes).
static void BF_CardVertex(const bfCard_t *c, int frame, int oldframe, float backlerp, int k, vec3_t out)
{
	const short *n = &bfXyz[c->data + frame * 12 + k * 3];
	const short *o = &bfXyz[c->data + oldframe * 12 + k * 3];
	float       ns = BF_MD3_SCALE * (1.f - backlerp), os = BF_MD3_SCALE * backlerp;

	out[0] = o[0] * os + n[0] * ns;
	out[1] = o[1] * os + n[1] * ns;
	out[2] = o[2] * os + n[2] * ns;
}

static void BF_ToWorld(const refEntity_t *e, const vec3_t local, vec3_t out)
{
	VectorCopy(e->origin, out);
	VectorMA(out, local[0], e->axis[0], out);
	VectorMA(out, local[1], e->axis[1], out);
	VectorMA(out, local[2], e->axis[2], out);
}

static qboolean BF_Walkable(const trace_t *tr)
{
	return !tr->startsolid && !tr->allsolid && tr->fraction < 1.f && tr->plane.normal[2] >= .5f &&
	       !(tr->surfaceFlags & (SURF_SKY | SURF_NOIMPACT));
}

// Decide ONCE per card where it may burn. Returns number of unsupported cards.
static int BF_Classify(bfContact_t *ct, const bfModel_t *m, const refEntity_t *flame, unsigned int seed)
{
	int    i, unsupported = 0;
	vec3_t center;

	VectorCopy(flame->origin, center);
	for (i = 0; i < m->numCards; ++i)
	{
		const bfCard_t *c = &m->cards[i];
		vec3_t         local, root, from, to;
		trace_t        tr;

		ct->mode[i]  = BF_KEEP;
		ct->delay[i] = 0;
		VectorClear(ct->shift[i]);
		VectorSet(local, c->rootX, c->rootY, c->baseZ);
		BF_ToWorld(flame, local, root);
		VectorCopy(root, from); from[2] += 18.f;
		VectorCopy(root, to);   to[2]   -= 16.f;
		CG_Trace(&tr, from, NULL, NULL, to, ENTITYNUM_NONE, MASK_SOLID);
		if (BF_Walkable(&tr) && c->ground)
		{
			int  k, fr = c->firstAlive + (c->lastAlive - c->firstAlive) / 2;
			qboolean ok = qtrue;
			for (k = 0; k < 4 && ok; ++k)
			{
				vec3_t corner;
				BF_CardVertex(c, fr, fr, 0.f, k, local);
				BF_ToWorld(flame, local, corner);
				VectorCopy(corner, from); from[2] += 18.f;
				VectorCopy(corner, to);   to[2]   -= 16.f;
				CG_Trace(&tr, from, NULL, NULL, to, ENTITYNUM_NONE, MASK_SOLID);
				ok = BF_Walkable(&tr);
			}
			if (ok)
			{
				continue;
			}
		}
		else if (BF_Walkable(&tr))
		{
			continue;  // real surface within -16..+18u: exactly the R76 card
		}
		ct->mode[i] = BF_DROP;
		unsupported++;
	}
	if (!unsupported)
	{
		return 0;
	}
	for (i = 0; i < m->numCards; ++i)
	{
		const bfCard_t *c = &m->cards[i];
		vec3_t         local, root, from, to, dir;
		trace_t        down, face;
		float          h, drop;

		if (ct->mode[i] != BF_DROP)
		{
			continue;
		}
		if (c->ground)
		{
			continue;  // the flat embers glow never hangs over an edge
		}
		VectorSet(local, c->rootX, c->rootY, c->baseZ);
		BF_ToWorld(flame, local, root);
		if (CG_PointContents(root, ENTITYNUM_NONE) & MASK_SOLID)
		{
			// Root behind a wall face (fire against a wall base): a share of these
			// tongues stands ON the face instead of poking through it; rest dropped.
			if (BF_Rand(seed, i + 600) < .30f)
			{
				VectorSubtract(root, center, dir);
				dir[2] = 0.f;
				if (VectorNormalize(dir) > .5f)
				{
					VectorCopy(center, from); from[2] = root[2] + 6.f;
					VectorCopy(root, to);     to[2]   = root[2] + 6.f;
					CG_Trace(&face, from, NULL, NULL, to, ENTITYNUM_NONE, MASK_SOLID);
					if (!face.startsolid && !face.allsolid && face.fraction < 1.f && fabsf(face.plane.normal[2]) < .55f &&
					    !(face.surfaceFlags & (SURF_SKY | SURF_NOIMPACT)))
					{
						vec3_t anchor;
						VectorMA(face.endpos, 1.5f + .38f * c->maxRadius, face.plane.normal, anchor);
						anchor[2] = root[2];
						VectorCopy(anchor, from); from[2] += 18.f;
						VectorCopy(anchor, to);   to[2]   -= 16.f;
						CG_Trace(&down, from, NULL, NULL, to, ENTITYNUM_NONE, MASK_SOLID);
						if (BF_Walkable(&down))
						{
							VectorSubtract(anchor, root, ct->shift[i]);
							ct->mode[i] = BF_PUSH;
						}
					}
				}
			}
			continue;  // otherwise never drawn inside geometry
		}
		VectorCopy(root, from); from[2] += 2.f;
		VectorCopy(root, to);   to[2]   -= 230.f;
		CG_Trace(&down, from, NULL, NULL, to, ENTITYNUM_NONE, MASK_SOLID);
		if (!BF_Walkable(&down))
		{
			continue;  // no landing (sky, void, water edge...): DROP
		}
		drop = root[2] - down.endpos[2];
		h    = BF_Rand(seed, i);
		if (drop >= 14.f && h < .36f)
		{
			// CURTAIN: anchor on the vertical face separating the void from the fire.
			float zc = root[2] - Com_Clamp(9.f, 56.f, drop * (.16f + .46f * BF_Rand(seed, i + 300)));
			VectorSubtract(center, root, dir);
			dir[2] = 0.f;
			if (VectorNormalize(dir) > .5f)
			{
				VectorCopy(root, from); from[2] = zc;
				VectorMA(from, 104.f, dir, to);
				CG_Trace(&face, from, NULL, NULL, to, ENTITYNUM_NONE, MASK_SOLID);
				if (!face.startsolid && !face.allsolid && face.fraction < 1.f && fabsf(face.plane.normal[2]) < .55f &&
				    !(face.surfaceFlags & (SURF_SKY | SURF_NOIMPACT)))
				{
					vec3_t anchor;
					VectorMA(face.endpos, 1.5f + .38f * c->maxRadius, face.plane.normal, anchor);
					VectorSubtract(anchor, root, ct->shift[i]);
					ct->mode[i]  = BF_CURTAIN;
					ct->delay[i] = (short)(2 + sqrtf(2.f * (root[2] - zc) / 800.f) * BF_FPS);
					continue;
				}
			}
		}
		// less fuel survives longer drops: 55% short, 40% medium, 28% from high ledges
		if (h < .36f + (drop < 40.f ? .55f : drop < 120.f ? .40f : .28f))
		{
			ct->mode[i] = BF_FALL;
			VectorSet(ct->shift[i], 0.f, 0.f, down.endpos[2] + 1.f - root[2]);
			ct->delay[i] = (short)(3 + sqrtf(2.f * drop / 800.f) * BF_FPS);
		}
	}
	return unsupported;
}

static bfContact_t *BF_ContactCache(const centity_t *cent, int *isNew)
{
	const entityState_t *s = &cent->currentState;
	int                 k, best = 0;

	*isNew = 0;
	for (k = 0; k < BF_CACHE; ++k)
	{
		bfContact_t *a = &bfContacts[k];
		if (a->expiry == s->time2 && a->entityNum == s->number && a->modelIndex == s->modelindex &&
		    DistanceSquared(a->origin, cent->lerpOrigin) < 1.f)
		{
			return a;
		}
	}
	for (k = 1; k < BF_CACHE; ++k)
	{
		if (bfContacts[k].expiry < cg.time || bfContacts[k].lastUsed < bfContacts[best].lastUsed)
		{
			best = k;
		}
	}
	*isNew = 1;
	return &bfContacts[best];
}

/*
 * Called by CG_General for the 7 Molotov fire entities, AFTER CG_BombasTerrainPool
 * (so the R75/R76 slope/staircase orientation is already in flame->axis/origin).
 * Returns qtrue when the flames were submitted here (card mode); qfalse means the
 * caller must add the untouched refEntity exactly as R76 did.
 */
qboolean CG_BombasFireContact(centity_t *cent, const refEntity_t *flame)
{
	const entityState_t *s = &cent->currentState;
	bfContact_t         *ct;
	const bfModel_t     *m;
	int                 v, isNew, i, n, run;

	if (!cg_bombasFireContact.integer)
	{
		return qfalse;
	}
	if (cg.time < bfLastTime)
	{
		Com_Memset(bfContacts, 0, sizeof(bfContacts));
		Com_Memset(bfWalls, 0, sizeof(bfWalls));
	}
	bfLastTime = cg.time;
	v          = BF_Variant(s->modelindex);
	if (v < 0 || !BF_LoadModel(v))
	{
		return qfalse;
	}
	m  = &bfModels[v];
	ct = BF_ContactCache(cent, &isNew);
	if (isNew)
	{
		Com_Memset(ct, 0, sizeof(*ct));
		ct->entityNum  = s->number;
		ct->modelIndex = s->modelindex;
		ct->expiry     = s->time2;
		VectorCopy(cent->lerpOrigin, ct->origin);
		ct->cardMode = BF_Classify(ct, m, flame, (unsigned int)s->number * 2654435761U ^ (unsigned int)s->time2 * 40503U) > 0;
	}
	ct->lastUsed = cg.time;
	if (!ct->cardMode)
	{
		return qfalse;  // fully supported: identical R76 refEntity path
	}

	// submit cards as polys, batched per shader (cards are stored surface by surface)
	n   = 0;
	run = -1;
	for (i = 0; i <= m->numCards; ++i)
	{
		const bfCard_t *c = i < m->numCards ? &m->cards[i] : NULL;
		int            k, fr, ofr;

		if (c && run >= 0 && c->shader != m->cards[run].shader && n)
		{
			trap_R_AddPolysToScene(m->cards[run].shader, 4, bfPolys, n);
			n = 0;
		}
		if (!c)
		{
			if (n && run >= 0)
			{
				trap_R_AddPolysToScene(m->cards[run].shader, 4, bfPolys, n);
			}
			break;
		}
		run = i;
		if (ct->mode[i] == BF_DROP)
		{
			continue;
		}
		fr  = flame->frame - ct->delay[i];
		ofr = flame->oldframe - ct->delay[i];
		if (fr < 0)
		{
			continue;  // fuel still falling: not ignited yet
		}
		if (ofr < 0)
		{
			ofr = 0;
		}
		fr  = MIN(fr, BF_FRAMES - 1);
		ofr = MIN(ofr, BF_FRAMES - 1);
		for (k = 0; k < 4; ++k)
		{
			vec3_t     local;
			polyVert_t *p = &bfPolys[n * 4 + k];
			BF_CardVertex(c, fr, ofr, flame->backlerp, k, local);
			BF_ToWorld(flame, local, p->xyz);
			VectorAdd(p->xyz, ct->shift[i], p->xyz);
			p->st[0]       = c->st[k][0];
			p->st[1]       = c->st[k][1];
			p->modulate[0] = p->modulate[1] = p->modulate[2] = p->modulate[3] = 255;
		}
		n++;
	}
	return qtrue;
}

/*
 * Wall splash flames for BOMBAS_WALL_DECAL_TAG entities: up to 11 original tongues
 * (cards of the satellite MD3 picked by the wall variation), each anchored by its own
 * trace on the real face below/around the bottle contact. Curved walls give every
 * tongue its own hit point and normal. Tongues that find no face are simply skipped.
 */
void CG_BombasWallFlames(centity_t *cent)
{
	const entityState_t *s = &cent->currentState;
	bfWall_t            *w = NULL;
	const bfModel_t     *m;
	vec3_t              n, t, up = { 0.f, 0.f, 1.f };
	float               height, width;
	int                 k, best = 0, count;

	if (!cg_bombasFireContact.integer || cg.time <= s->time || cg.time >= s->time2)
	{
		return;
	}
	VectorCopy(s->origin2, n);
	n[2] = 0.f;
	if (VectorNormalize(n) < .72f)
	{
		return;
	}
	for (k = 0; k < BF_WALL_CACHE; ++k)
	{
		if (bfWalls[k].entityNum == s->number && bfWalls[k].time == s->time && bfWalls[k].time2 == s->time2)
		{
			w = &bfWalls[k];
			break;
		}
	}
	if (!w)
	{
		unsigned int seed = (unsigned int)s->number * 2246822519U ^ (unsigned int)s->time * 3266489917U;
		int          variant = 1 + (int)(BF_Hash(seed) % 6U), tries;

		for (k = 1; k < BF_WALL_CACHE; ++k)
		{
			if (bfWalls[k].time2 < cg.time || bfWalls[k].lastUsed < bfWalls[best].lastUsed)
			{
				best = k;
			}
		}
		w = &bfWalls[best];
		Com_Memset(w, 0, sizeof(*w));
		w->entityNum = s->number;
		w->time      = s->time;
		w->time2     = s->time2;
		w->model     = variant;
		if (!BF_LoadModel(variant))
		{
			w->count = 0;
		}
		else
		{
			m      = &bfModels[variant];
			height = Com_Clamp(12.f, 172.f, s->angles2[0]);
			width  = Com_Clamp(21.f, 52.f, s->angles2[1]);
			VectorSet(t, n[1], -n[0], 0.f);
			// 7 splash tongues around the shattered bottle, then up to 6 thinner ones
			// running down the face toward the existing floor fire (fuel drips).
			for (tries = 0; tries < 64 && w->count < BF_WALL_CARDS; ++tries)
			{
				const bfCard_t *c = &m->cards[BF_Hash(seed + tries * 7919U) % (unsigned int)m->numCards];
				vec3_t         probe, to;
				trace_t        tr;
				float          u, dz, fall;
				int            span = c->lastAlive - c->firstAlive;
				qboolean       splash = w->count < 7 ? qtrue : qfalse;

				if (!c->autosprite || span < 40 ||
				    (splash && (c->maxRadius < 11.f || c->maxRadius > 18.f)) ||
				    (!splash && (c->maxRadius < 5.f || c->maxRadius > 12.f)))
				{
					continue;
				}
				if (splash)
				{
					u  = (BF_Rand(seed, tries) - .5f) * width * 1.2f;
					dz = 14.f - 32.f * BF_Rand(seed, tries + 100);
				}
				else
				{
					u  = (BF_Rand(seed, tries) - .5f) * width * .64f;
					dz = -16.f - (height - 16.f) * .95f * powf(BF_Rand(seed, tries + 100), 1.35f);
				}
				VectorMA(cent->lerpOrigin, 14.f, n, probe);
				VectorMA(probe, u, t, probe);
				probe[2] += dz;
				VectorMA(probe, -42.f, n, to);
				CG_Trace(&tr, probe, NULL, NULL, to, ENTITYNUM_NONE, MASK_SOLID);
				if (tr.startsolid || tr.allsolid || tr.fraction >= 1.f || fabsf(tr.plane.normal[2]) > .6f ||
				    DotProduct(tr.plane.normal, n) < .25f || (tr.surfaceFlags & (SURF_SKY | SURF_NOIMPACT)))
				{
					continue;  // no real face there (corner, opening, edge): no tongue
				}
				w->card[w->count] = (short)(c - m->cards);
				VectorMA(tr.endpos, 1.5f + .40f * c->maxRadius, tr.plane.normal, w->anchor[w->count]);
				VectorCopy(tr.plane.normal, w->normal[w->count]);
				fall               = splash ? 0.f : sqrtf(2.f * (-dz) / 800.f) * 1000.f;
				w->start[w->count] = s->time + 20 + (int)(BF_Rand(seed, tries + 200) * (splash ? 160.f : 260.f)) + (int)fall;
				w->life[w->count]  = (int)((s->time2 - w->start[w->count]) *
				                           (splash ? .76f + .24f * BF_Rand(seed, tries + 300) : .58f + .32f * BF_Rand(seed, tries + 300)));
				if (w->life[w->count] > 300)
				{
					w->count++;
				}
			}
		}
	}
	w->lastUsed = cg.time;
	if (!w->count || !BF_LoadModel(w->model))
	{
		return;
	}
	m = &bfModels[w->model];
	count = 0;
	for (k = 0; k < w->count; ++k)
	{
		const bfCard_t *c = &m->cards[w->card[k]];
		float          a, fpos, back;
		int            fr, ofr, j;
		vec3_t         mid, wt;

		if (cg.time < w->start[k] || cg.time >= w->start[k] + w->life[k])
		{
			continue;
		}
		// original tongue lifetime compressed into the short wall burn (ignite, burn, die)
		a    = (float)(cg.time - w->start[k]) / (float)w->life[k];
		fpos = c->firstAlive + a * (float)(c->lastAlive - c->firstAlive);
		fr   = (int)ceilf(fpos);
		ofr  = (int)floorf(fpos);
		back = (float)fr - fpos;
		fr   = MIN(MAX(fr, 0), BF_FRAMES - 1);
		ofr  = MIN(MAX(ofr, 0), BF_FRAMES - 1);
		VectorSet(wt, w->normal[k][1], -w->normal[k][0], 0.f);
		// keep each original card's own height/size; base stays on the anchor
		{
			vec3_t vtx[4], local;
			float  r;
			for (j = 0; j < 4; ++j)
			{
				BF_CardVertex(c, fr, ofr, back, j, local);
				VectorSet(vtx[j], local[0] - c->rootX, local[1] - c->rootY, local[2] - c->baseZ);
			}
			VectorAdd(vtx[0], vtx[1], mid);
			VectorAdd(mid, vtx[2], mid);
			VectorAdd(mid, vtx[3], mid);
			VectorScale(mid, .25f, mid);
			r = Distance(vtx[0], mid) * .7071f;   // corner offsets along 2 axes -> same |v0-mid|
			{
				vec3_t center;
				VectorCopy(w->anchor[k], center);
				VectorMA(center, mid[0], wt, center);              // drift along the wall only
				VectorMA(center, mid[2], up, center);              // flame rises up the face
				for (j = 0; j < 4; ++j)
				{
					polyVert_t *p = &bfPolys[count * 4 + j];
					VectorCopy(center, p->xyz);
					VectorMA(p->xyz, (j == 0 || j == 3) ? r : -r, wt, p->xyz);
					VectorMA(p->xyz, (j < 2) ? r : -r, up, p->xyz);
					p->st[0]       = (j == 1 || j == 2) ? 1.f : 0.f;
					p->st[1]       = (j >= 2) ? 1.f : 0.f;
					p->modulate[0] = p->modulate[1] = p->modulate[2] = p->modulate[3] = 255;
				}
			}
		}
		// each tongue keeps its own shader (own animMap phase); submit one by one
		trap_R_AddPolysToScene(c->shader, 4, &bfPolys[count * 4], 1);
		count++;
	}
}
