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


#define SKEET_CLASSHASH   284875700
#define SKEET_MAX_OFFSET        128
#define STAT_PMOVE                8
#define STAT_AMOVE               13
#define EVT_FIRE_WEAPON          31
#define EVT_GENERAL_SOUND        59


static void  QDECL SV_LogPrintf(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
static void  SV_SendSoundToClient(playerState_t *ps, const char *name);
static int   SV_SkeetHashOffsetFinder(unsigned int base);
static int   SV_SkeetLaunchTime(void);
static int   SV_SkeetRand(void);
static void  SV_SkeetRandSeed(unsigned int seed);
static int   SV_UnitsToMeters(float distance);


typedef struct {
	int points;
	int min;
	int max;
} skeetscore_t;


skeetscore_t skeetscores[] = {
	{ 1,    0,  5000  },
	{ 2, 5000,  90000 },
	{ 4, 9000,  14000 },
	{ 8, 14000, SKEET_MAX_TRACE },
};


int mx = 123456789;
int my = 362436069;
int mz = 521288629;
int mw = 886751235;


// ====================================================================================


void SV_SkeetThink(void) {

	int i;
	svEntity_t *sEnt;
	sharedEntity_t *gEnt;

	if (sv_skeetshoot->integer <= 0 || sv_gametype->integer != GT_FFA) {
		return;
	}

	for (i = 0; i < SKEET_MAX; i++) {

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
			if (Distance(sEnt->skeetInfo.origin, gEnt->r.currentOrigin) >= SKEET_MAX_TRACE) {
				SV_SkeetRespawn(sEnt, gEnt);
			}
		} else {
			if (sv.time > sEnt->skeetInfo.shootTime) {
				// This will just calculate the direction vector and apply
				// skeet velocity to it: the skeet entity will start moving
				// in the world starting from the next server frame.
				SV_SkeetLaunch(sEnt, gEnt);
			}
		}

	}

}

void SV_SkeetLaunch(svEntity_t *sEnt, sharedEntity_t *gEnt) {

	vec3_t vel = {0, 0, 0};

	if (sv_skeetshoot->integer <= 0 || sv_gametype->integer != GT_FFA) {
		return;
	}

	if (!sEnt->skeetInfo.valid) {
		return;
	}

	if (sEnt->skeetInfo.moving) {
		return;
	}

	vel[0] = SKEET_MIN_X + ((float)SV_SkeetRand() / ((float)SKEET_MAX_RAND / (SKEET_MAX_X - SKEET_MIN_X)));
	vel[1] = SKEET_MIN_Y + ((float)SV_SkeetRand() / ((float)SKEET_MAX_RAND / (SKEET_MAX_Y - SKEET_MIN_Y)));
	vel[2] = 1.0f;

	VectorNormalize(vel);
	VectorMultiply(vel, sv_skeetspeed->integer);
	VectorCopy(vel, gEnt->s.pos.trDelta);

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
	sEnt->skeetInfo.shootTime = sv.time + SV_SkeetLaunchTime();
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
	if (offset <= 0) {
		return;
	}

	Com_Printf("SkeetMOD: initializing skeetshoot mod\n");

	SV_SkeetRandSeed((unsigned int) svs.time);

	for (i = 0, j = 0, sEnt = sv.svEntities; i < MAX_GENTITIES && j < SKEET_MAX; i++, sEnt++) {

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
			sv.skeets[j]->skeetInfo.shootTime = sv.time + SV_SkeetLaunchTime();
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
			for (i = 0; i < SKEET_MAX; i++) {
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
	char name[MAX_NAME_LENGTH];
	char *score;
	float distance = 0;
	client_t *dst;

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

	if (sv_skeethitreport->integer) {
		Q_strncpyz(name, cl->name, sizeof(name));
		Q_CleanStr(name);
		SV_SendServerCommand(NULL, "print \"%s%s %sscores %s%d %spoints by hitting a skeet at %s%d %smeters\n\"",
							 S_COLOR_YELLOW, name, S_COLOR_WHITE, S_COLOR_YELLOW, points, S_COLOR_WHITE,
							 S_COLOR_YELLOW, SV_UnitsToMeters(distance), S_COLOR_WHITE);
	}

	if (sv_skeetpointsnotify->integer > 0) {
		SV_SendServerCommand(cl, "cp \"%s+%d\n\"", S_COLOR_GREEN, points);
	}

	// ------- SCOREBOARD UPDATE ------------

#ifdef USE_AUTH
	score = va("scoress %i %i %i %i %i %i %i %i %i %i %i %i %s", (int)(cl - svs.clients),
			   ps->persistant[PERS_SCORE], cl->ping, sv.time / 60000, 0,
			   ps->persistant[PERS_KILLED], 0, 0, 0, 0, 0, 0, cl->auth);
#else
	score = va("scoress %i %i %i %i %i %i %i %i %i %i %i %i ---", (int)(cl - svs.clients),
			   ps->persistant[PERS_SCORE], cl->ping, sv.time / 60000, 0,
			   ps->persistant[PERS_KILLED], 0, 0, 0, 0, 0, 0);
#endif

	for (i = 0, dst = svs.clients; i < sv_maxclients->integer; i++, dst++) {
		if (dst->state != CS_ACTIVE)
			continue;
		SV_SendServerCommand(dst, "%s", score);

	}

	if (Cvar_VariableIntegerValue("g_loghits")) {
		SV_LogPrintf("SkeetShoot: %i: %f %i %i\n", (int)(cl - svs.clients), distance, points, ps->persistant[PERS_SCORE]);
	}

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
			if (!SV_SkeetShoot(cl, ps)) {
				if (Cvar_VariableIntegerValue("g_loghits")) {
					SV_LogPrintf("SkeetMiss: %i\n", (int)(cl - svs.clients));
				}
			}
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

	SV_SendSoundToClient(ps, sv_skeethitsound->string);  // send the hit sound
	SV_SkeetScore(cl, ps, &trace);                       // increase score
	SV_SkeetRespawn(sEnt, gEnt);                         // respawn the skeet
	return qtrue;

}


// ====================================================================================

static void QDECL SV_LogPrintf(const char *fmt, ...) {

	va_list argptr;
	fileHandle_t file;
	fsMode_t mode;
	char *filename;
	char buffer[MAX_STRING_CHARS];
	int min, tens, sec;
	int logsync;

	filename = Cvar_VariableString("g_log");
	if (!filename[0]) {
		return;
	}

	logsync = Cvar_VariableIntegerValue("g_logSync");
	mode = logsync ? FS_APPEND_SYNC : FS_APPEND;

	FS_FOpenFileByMode(filename, &file, mode);
	if (!file) {
		return;
	}

	sec  = sv.time / 1000;
	min  = sec / 60;
	sec -= min * 60;
	tens = sec / 10;
	sec -= tens * 10;

	Com_sprintf(buffer, sizeof(buffer), "%3i:%i%i ", min, tens, sec);

	va_start(argptr, fmt);
	vsprintf(buffer + 7, fmt, argptr);
	va_end(argptr);

	FS_Write(buffer, strlen(buffer), file);
	FS_FCloseFile(file);

}

static void SV_SendSoundToClient(playerState_t *ps, const char *name) {

	int i;
	int bits;
	int index = -1;
	int max = MAX_SOUNDS;
	int start = CS_SOUNDS;
	char buffer[MAX_STRING_CHARS];

	if (!name || !name[0]) {
		// invalid name supplied
		index = 0;
	}

	for (i = 1 ; i < max ; i++) {
		SV_GetConfigstring(start + i, buffer, sizeof(buffer));
		if (!buffer[0]) {
			break;
		}
		if (!Q_stricmp(buffer, name)) {
			index = i;
			break;
		}
	}

	if (index == -1) {
		// if there is no config string for this sound,
		// create a new one but do not overflow the limits
		if (i == max) {
			return;
		}
		SV_SetConfigstring(start + i, name);
		index = i;
	}

	bits = ps->externalEvent & EV_EVENT_BITS;
	bits = (bits + EV_EVENT_BIT1) & EV_EVENT_BITS;
	ps->externalEvent = EVT_GENERAL_SOUND | bits;
	ps->externalEventParm = index;
	ps->externalEventTime = sv.time;

}

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

static int SV_SkeetLaunchTime(void) {
	return (SV_SkeetRand() % (SKEET_MAX_SPAWN_TIME - SKEET_MIN_SPAWN_TIME) + SKEET_MIN_SPAWN_TIME);
}

static void SV_SkeetRandSeed(unsigned int seed) {
	mw = seed;
}

static int SV_SkeetRand(void) {
	int t = (mx ^ (mx << 11)) & SKEET_MAX_RAND;
	mx = my; my = mz; mz = mw;
	mw = (mw ^ (mw >> 19) ^ (t ^ (t >> 8)));
	return mw;
}

static int SV_UnitsToMeters(float distance) {
	return (int)((distance / 8) * 0.3048);
}

#endif
