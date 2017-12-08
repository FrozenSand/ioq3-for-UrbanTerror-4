/*
 * Copyright (C) 2017 Daniele Pantaleone <danielepantaleone@me.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef USE_SKEETMOD

#include "server.h"


#define SKEET_CLASSHASH             284875700
#define SKEET_MAX_OFFSET                  128
#define SKEET_MAX_TRACE                 28000
#define SKEET_MAX_TIME                   8000
#define SKEET_MIN_SPAWN_TIME             1000
#define SKEET_MAX_SPAWN_TIME             4000
#define STAT_PMOVE                          8
#define STAT_AMOVE                         13
#define EVT_FIRE_WEAPON                    31

#define DEG_TO_RAD(a)   ((a) * M_PI / 180.0f)


static qboolean SV_SkeetExpired(svEntity_t *sEnt, sharedEntity_t *gEnt);
static int      SV_SkeetHashOffsetFinder(unsigned int base);
static void     SV_SkeetHitReport(client_t *cl, float distance, int points);
static void     SV_SkeetLogHit(client_t *cl, playerState_t *ps, float distance, int points);
static void     SV_SkeetLogMiss(client_t *cl);
static void     SV_SkeetPointsNotify(client_t *cl, int points);


typedef struct {
	int points;
	int min;
	int max;
} skeetscore_t;


skeetscore_t skeetscores[] = {
	{ 1,     0,  4000  },
	{ 2,  4000,  6000  },
	{ 4,  6000,  8000  },
	{ 6,  8000,  10000 },
	{ 8, 10000, SKEET_MAX_TRACE },
};


// ====================================================================================


void SV_SkeetThink(void) {

	int i;
	svEntity_t *sEnt;
	sharedEntity_t *gEnt;

	if (sv_skeetshoot->integer <= 0 || sv_gametype->integer != GT_FFA) {
		return;
	}

	for (i = 0; i < MAX_SKEETS; i++) {

		if (!(sEnt = sv.skeets[i])) {
			break;
		}

		if (!sEnt->skeetInfo.valid) {
			continue;
		}

		gEnt = SV_GEntityForSvEntity(sEnt);
		if (!gEnt) {
			continue;
		}

		if (sEnt->skeetInfo.moving) {
			if (SV_SkeetExpired(sEnt, gEnt)) {
				SV_SkeetRespawn(sEnt, gEnt);
			}
		} else {
			if (sv.time > sEnt->skeetInfo.shootTime) {
				SV_SkeetLaunch(sEnt, gEnt);
			}
		}

	}

}

void SV_SkeetLaunch(svEntity_t *sEnt, sharedEntity_t *gEnt) {

	float fan;
	float angle;

	vec3_t skeetPos = {0,0,0};
	vec3_t skeetDir = {0,0,0};

	if (sv_skeetshoot->integer <= 0 || sv_gametype->integer != GT_FFA) {
		return;
	}

	if (!sEnt->skeetInfo.valid) {
		return;
	}

	if (sEnt->skeetInfo.moving) {
		return;
	}

	fan = DEG_TO_RAD(Com_Clamp(0, 360, sv_skeetfansize->integer));
	angle = SV_XORShiftRandRange(-(fan / 2), (fan / 2));
	VectorSet(skeetDir, sinf(angle), cosf(angle), 1.0f);
	VectorNormalize(skeetDir);
	VectorRotateZ(skeetDir, DEG_TO_RAD(Com_Clamp(-360, +360, sv_skeetrotate->integer)), skeetDir);
	VectorClear(skeetPos);
	VectorMA(skeetPos, sv_skeetspeed->integer, skeetDir, skeetPos);
	VectorCopy(skeetPos, gEnt->s.pos.trDelta);

	gEnt->s.pos.trTime = sv.time - 50;
	gEnt->s.pos.trType = TR_GRAVITY;
	sEnt->skeetInfo.moving = qtrue;

}

void SV_SkeetRespawn(svEntity_t *sEnt, sharedEntity_t *gEnt) {

	if (sv_skeetshoot->integer <= 0 || sv_gametype->integer != GT_FFA) {
		return;
	}

	if (!sEnt->skeetInfo.valid) {
		return;
	}

	if (!sEnt->skeetInfo.moving) {
		return;
	}

	SV_UnlinkEntity(gEnt);
	VectorCopy(sEnt->skeetInfo.origin, gEnt->r.currentOrigin);
	sEnt->skeetInfo.moving = qfalse;
	sEnt->skeetInfo.shootTime = sv.time + SV_XORShiftRandRange(SKEET_MIN_SPAWN_TIME, SKEET_MAX_SPAWN_TIME);
	gEnt->s.pos.trTime = sv.time;
	gEnt->s.pos.trType = TR_STATIONARY;
	SV_LinkEntity(gEnt);

}

void SV_SkeetReset(svEntity_t *sEnt, sharedEntity_t *gEnt) {

	if (sv_skeetshoot->integer <= 0 || sv_gametype->integer != GT_FFA) {
		return;
	}

	if (!sEnt->skeetInfo.valid) {
		return;
	}

	if (!sEnt->skeetInfo.moving) {
		return;
	}

	SV_UnlinkEntity(gEnt);
	VectorCopy(sEnt->skeetInfo.origin, gEnt->r.currentOrigin);
	sEnt->skeetInfo.valid = qfalse;
	sEnt->skeetInfo.moving = qfalse;
	sEnt->skeetInfo.shootTime = 0;
	gEnt->s.pos.trTime = sv.time;
	gEnt->s.pos.trType = TR_STATIONARY;
	SV_LinkEntity(gEnt);

}

void SV_SkeetInit(void) {

	int i, j;
	int offset;
	unsigned int base;
	svEntity_t *sEnt;
	sharedEntity_t *gEnt;

	if (sv_skeetshoot->integer <= 0 || sv_gametype->integer != GT_FFA) {
		return;
	}

	base = sizeof(entityState_t) + sizeof(entityShared_t);
	offset = base + SV_SkeetHashOffsetFinder(base);
	if (offset <= base) {
		return;
	}

	Com_Printf("SkeetMOD: initializing skeetshoot mod\n");

	SV_FindConfigstringIndex(sv_skeethitsound->string, CS_SOUNDS, MAX_SOUNDS, qtrue);
	SV_XORShiftRandSeed((unsigned int) svs.time);

	for (i = 0, j = 0, sEnt = sv.svEntities; i < MAX_GENTITIES && j < MAX_SKEETS; i++, sEnt++) {

		if (!sEnt) {
			continue;
		}

		gEnt = SV_GEntityForSvEntity(sEnt);
		if (!gEnt) {
			continue;
		}

		if (*(int *)((byte *)sv.gentities + sv.gentitySize * (i) + offset) == SKEET_CLASSHASH) {
			Com_Printf("SkeetMOD: entity %d marked as skeet\n", i);
			sv.skeets[j] = sEnt;
			sv.skeets[j]->skeetInfo.valid = qtrue;
			sv.skeets[j]->skeetInfo.moving = qfalse;
			sv.skeets[j]->skeetInfo.shootTime = sv.time + SV_XORShiftRandRange(SKEET_MIN_SPAWN_TIME, SKEET_MAX_SPAWN_TIME);
			VectorCopy(gEnt->r.currentOrigin, sv.skeets[j]->skeetInfo.origin);
			j++;
		}

	}

	Com_Printf("SkeetMOD: %d skeets available\n", j);

	if (sv_skeetprotect->integer > 0) {
		Com_Printf("SkeetMOD: enabling respawn protection\n");
		Cvar_Set("g_respawnProtection", "604800");
	}

}

void SV_SkeetParseGameRconCommand(const char *text) {

	int i;
	svEntity_t *sEnt;
	sharedEntity_t *gEnt;

	Cmd_TokenizeString(text);

	if (!Cmd_Argc()) {
		return;
	}

	if (!Q_stricmp(Cmd_Argv(0), "restart")) {
		if (sv_skeetshoot->integer > 0 && sv_gametype->integer == GT_FFA) {
			for (i = 0; i < MAX_SKEETS; i++) {
				if (!(sEnt = sv.skeets[i]))
					break;
				gEnt = SV_GEntityForSvEntity(sEnt);
				if (!gEnt)
					continue;
				SV_SkeetReset(sEnt, gEnt);
			}
			SV_SkeetInit();
		}
	}

}

void SV_SkeetParseGameServerCommand(int clientNum, const char *text) {

	int s[32];
	char cmd[MAX_NAME_LENGTH];
	char auth[MAX_NAME_LENGTH];
	client_t *cl;

	if (clientNum < 0 || clientNum >= sv_maxclients->integer) {
		return;
	}

#ifdef USE_AUTH
	// scan the game command looking for the score single one
	if (sscanf(text, "%s %i %i %i %i %i %i %i %i %i %i %i %i %s",      // NOLINT
			   cmd, &s[0], &s[1], &s[2], &s[3], &s[4], &s[5], &s[6],
			   &s[7], &s[8], &s[9], &s[10], &s[11], auth) != EOF) {
		if (!Q_stricmp("scoress", cmd) && (Q_stricmp("---", auth) != 0)) {
			cl = &svs.clients[s[0]];
			if ((Q_stricmp(cl->auth, auth) != 0)) {
				Q_strncpyz(cl->auth, auth, MAX_NAME_LENGTH);
			}
		}
	}
#endif

}

void SV_SkeetBackupPowerups(client_t *cl) {

	int i;
	playerState_t *ps;

	if (!cl) {
		return;
	}

	if (sv_skeetshoot->integer <= 0 || sv_gametype->integer != GT_FFA) {
		return;
	}

	ps = SV_GameClientNum(cl - svs.clients);

	for (i = 0; i < MAX_POWERUPS; i++) {
		cl->powerups[i] = ps->powerups[i];
	}

}

void SV_SkeetRestorePowerups(client_t *cl) {

	int i;
	playerState_t *ps;

	if (!cl) {
		return;
	}

	if (sv_skeetshoot->integer <= 0 || sv_gametype->integer != GT_FFA) {
		return;
	}

	ps = SV_GameClientNum(cl - svs.clients);

	for (i = 0; i < MAX_POWERUPS; i++) {
		ps->powerups[i] = cl->powerups[i];
	}

	ps->persistant[PERS_KILLED] = 0;
	ps->stats[STAT_PMOVE] &= ~(1 << 2);
	ps->stats[STAT_PMOVE] &= ~(1 << 3);
	ps->stats[STAT_AMOVE] &= ~(1 << 0);

}

void SV_SkeetScore(client_t *cl, playerState_t *ps, trace_t *tr) {

	int i;
	int points = 1;

	float distance = 0;

	if (sv_skeetshoot->integer <= 0 || sv_gametype->integer != GT_FFA) {
		return;
	}

	distance = Distance(ps->origin, tr->endpos);

	if (sv_skeetpoints->integer > 0) {
		points = sv_skeetpoints->integer;
	} else {
		for (i = 0; i < sizeof(skeetscores) / sizeof(skeetscores[0]); i++) {
			if (distance >= skeetscores[i].min && distance < skeetscores[i].max) {
				points = skeetscores[i].points;
				break;
			}
		}
	}

	ps->persistant[PERS_SCORE] += points;

	SV_SkeetHitReport(cl, distance, points);
	SV_SkeetPointsNotify(cl, points);
	SV_SendScoreboardSingleMessageToAllClients(cl, ps);
	SV_SkeetLogHit(cl, ps, distance, points);

}

void SV_SkeetClientEvents(client_t *cl) {

	int i;
	int event;
	playerState_t *ps;

	if (!cl) {
		return;
	}

	ps = SV_GameClientNum(cl - svs.clients);

	if (cl->lastEventSequence < ps->eventSequence - MAX_PS_EVENTS) {
		cl->lastEventSequence = ps->eventSequence - MAX_PS_EVENTS;
	}

	for (i = cl->lastEventSequence; i < ps->eventSequence; i++) {
		event = ps->events[i & (MAX_PS_EVENTS - 1)];
		if (event == EVT_FIRE_WEAPON) {
			if (!SV_SkeetShoot(cl, ps))
				SV_SkeetLogMiss(cl);
			SV_SkeetRestorePowerups(cl);
		}
	}

	cl->lastEventSequence = ps->eventSequence;

}

qboolean SV_SkeetShoot(client_t *cl, playerState_t *ps) {

	svEntity_t *sEnt;
	sharedEntity_t *gEnt;
	sharedEntity_t *self;
	trace_t trace;
	vec3_t mins = { -0.5f, -0.5f, -0.5f };
	vec3_t maxs = {  0.5f,  0.5f,  0.5f };
	vec3_t muzzle = { 0, 0, 0 };
	vec3_t forward =  {0, 0, 0 };
	vec3_t right = { 0, 0, 0 };
	vec3_t end = { 0, 0, 0 };
	vec3_t up = { 0, 0, 0 };

	if (sv_skeetshoot->integer <= 0 || sv_gametype->integer != GT_FFA) {
		return qfalse;
	}

	self = SV_GentityNum(cl - svs.clients);
	if (!self) {
		return qfalse;
	}

	AngleVectors(ps->viewangles, forward, right, up);   // angle vectors of the client
	VectorCopy(self->s.pos.trBase, muzzle);             // muzzle origin
	muzzle[2] += ps->viewheight;                        // muzzle on the eye point
	VectorMA(muzzle, SKEET_MAX_TRACE, forward, end);    // end point of the trace

	SV_Trace(&trace, muzzle, mins, maxs, end, cl - svs.clients, MASK_SHOT, qfalse);

	sEnt = &sv.svEntities[trace.entityNum];
	if (!sEnt) {
		return qfalse;
	}

	if (!sEnt->skeetInfo.valid || !sEnt->skeetInfo.moving) {
		return qfalse;
	}

	gEnt = SV_GEntityForSvEntity(sEnt);
	if (!gEnt) {
		return qfalse;
	}

	SV_SendSoundToClient(cl, sv_skeethitsound->string);  // send the hit sound
	SV_SkeetScore(cl, ps, &trace);                       // increase score
	SV_SkeetRespawn(sEnt, gEnt);                         // respawn the skeet
	return qtrue;

}


// ====================================================================================

static int SV_SkeetHashOffsetFinder(unsigned int base) {

	int i, j;
	svEntity_t *sEnt;
	sharedEntity_t *gEnt;

	if (sv_skeetshoot->integer <= 0 || sv_gametype->integer != GT_FFA) {
		return -1;
	}

	Com_Printf("SkeetMOD: searching skeet classhash offset\n");

	for (i = 0, sEnt = sv.svEntities; i < MAX_GENTITIES; i++, sEnt++) {

		if (!sEnt) {
			continue;
		}

		gEnt = SV_GEntityForSvEntity(sEnt);
		if (!gEnt) {
			continue;
		}

		for (j = 0; j < base - SKEET_MAX_OFFSET; j++) {
			if (*(int *)((byte *)sv.gentities + sv.gentitySize * (i) + base + j) == SKEET_CLASSHASH) {
				Com_Printf("SkeetMOD: base=%d, offset=%d\n", base, j);
				return j;
			}
		}
	}

	Com_Printf("SkeetMOD: skeet classhash offset not found\n");
	return -1;

}

static void SV_SkeetHitReport(client_t *cl, float distance, int points) {

	char name[MAX_NAME_LENGTH];

	if (sv_skeethitreport->integer <= 0)
		return;

	Q_strncpyz(name, cl->name, sizeof(name));
	Q_CleanStr(name);
	SV_SendServerCommand(NULL, "print \"%s%s %sscores %s%d %spoints by hitting a skeet at %s%d %smeters\n\"",
						 S_COLOR_YELLOW, name, S_COLOR_WHITE, S_COLOR_YELLOW, points, S_COLOR_WHITE,
						 S_COLOR_YELLOW, SV_UnitsToMeters(distance), S_COLOR_WHITE);

}

static void SV_SkeetLogHit(client_t *cl, playerState_t *ps, float distance, int points) {
	if (Cvar_VariableIntegerValue("g_loghits") <= 0)
		return;
	SV_LogPrintf("SkeetShoot: %i: %f %i %i\n", (int)(cl - svs.clients), distance, points, ps->persistant[PERS_SCORE]);
}

static void SV_SkeetLogMiss(client_t *cl) {
	if (Cvar_VariableIntegerValue("g_loghits") <= 0)
		return;
	SV_LogPrintf("SkeetMiss: %i\n", (int)(cl - svs.clients));
}

static void SV_SkeetPointsNotify(client_t *cl, int points) {
	if (sv_skeetpointsnotify->integer <= 0)
		return;
	SV_SendServerCommand(cl, "cp \"%s+%d\n\"", S_COLOR_GREEN, points);
}

static qboolean SV_SkeetExpired(svEntity_t *sEnt, sharedEntity_t *gEnt) {
	if (!sEnt->skeetInfo.moving)
		return qfalse;
	if (Distance(sEnt->skeetInfo.origin, gEnt->r.currentOrigin) >= SKEET_MAX_TRACE)
		return qtrue;
	if (sEnt->skeetInfo.shootTime > 0 && (sv.time - sEnt->skeetInfo.shootTime) >= SKEET_MAX_TIME)
		return qtrue;
	return qfalse;
}

#endif
