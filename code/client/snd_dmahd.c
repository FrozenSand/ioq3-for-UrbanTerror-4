/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

/*
===========================================================================
Changes to ID's source code: dmaHD HRTF Sound system
by p5yc0runn3r founder of Armed, Pissed & Dangerous clan.
===========================================================================
*/

#ifndef NO_DMAHD

/*
	IMPLEMENTATION DETAILS:

	1. Remove from static any unresolved extern globals:
	From snd_dma.c remove static keyword for the following:
		'static' int			s_soundStarted;
		'static' qboolean		s_soundMuted;
		'static' int			listener_number;
		'static' vec3_t			listener_origin;
		'static' vec3_t			listener_axis[3];
		'static' loopSound_t	loopSounds[MAX_GENTITIES];

	2. In "snd_local.h" add the following changes (channel_t structure changed + ch_side_t structure added):
		typedef struct 
		{
			int			vol; // Must be first member due to union (see channel_t)
			int 		offset;
			int 		bassvol;
			int 		bassoffset;
			int			reverbvol;
			int			reverboffset;
		} ch_side_t;

		typedef struct
		{
			int			allocTime;
			int			startSample;	// START_SAMPLE_IMMEDIATE = set immediately on next mix
			int			entnum;			// to allow overriding a specific sound
			int			entchannel;		// to allow overriding a specific sound
			int			master_vol;		// 0-255 volume before spatialization
			float		dopplerScale;
			float		oldDopplerScale;
			vec3_t		origin;			// only use if fixed_origin is set
			qboolean	fixed_origin;	// use origin instead of fetching entnum's origin
			sfx_t		*thesfx;		// sfx structure
			qboolean	doppler;
			union
			{
				int			leftvol;		// 0-255 volume after spatialization
				ch_side_t	l;
			};
			union
			{
				int			rightvol;		// 0-255 volume after spatialization
				ch_side_t	r;
			};
			vec3_t		sodrot;
		} channel_t;

	3. In "snd_local.h" add the following to sfx_t structure:
		qboolean		weaponsound;

	4. #include "snd_dmahd.h" in snd_mem.c and snd_dma.c

	5. Add the following at the top of S_LoadSound() in "snd_mem.c" after variables:
		#ifndef NO_DMAHD
			if (dmaHD_Enabled()) return dmaHD_LoadSound(sfx);
		#endif

	6. At the bottom of S_Base_Init() in "snd_dma.c" replace "return qtrue" with:
		#ifndef NO_DMAHD
			if (dmaHD_Enabled()) return dmaHD_Init(si);
		#endif

	7. Fix S_UpdateBackgroundTrack in "snd_dma.c"
	Replace: 
		fileSamples = bufferSamples * s_backgroundStream->info.rate / dma.speed;
	With:
		fileSamples = (bufferSamples * dma.speed) / s_backgroundStream->info.rate;

	8. (Skip if source file does not exist)
	Add the following to win_snd.c as a global extern:
		extern cvar_t		*s_khz;

	9. (Skip if source file does not exist)
	Fix SNDDMA_InitDS() in win_snd.c
	Replace:
		//	if (s_khz->integer == 44)
		//		dma.speed = 44100;
		//	else if (s_khz->integer == 22)
		//		dma.speed = 22050;
		//	else
		//		dma.speed = 11025;

		dma.speed = 22050;
	With:
		if (s_khz->integer >= 44) dma.speed = 44100;
		else if (s_khz->integer >= 32) dma.speed = 32000;
		else if (s_khz->integer >= 24) dma.speed = 24000;
		else if (s_khz->integer >= 22) dma.speed = 22050;
		else dma.speed = 11025;

	10. Compile, Run and Enjoy!! :)
*/

#include "client.h"
#include "snd_local.h"
#include "snd_codec.h"
#include "snd_dmahd.h"

void dmaHD_Update_Mix( void );
void S_UpdateBackgroundTrack(void);
void S_GetSoundtime(void);
qboolean S_ScanChannelStarts(void);

// used in dmaEX mixer.
#define							SOUND_FULLVOLUME		80
#define							SOUND_ATTENUATE			0.0007f

extern channel_t				s_channels[];
extern channel_t				loop_channels[];
extern int						numLoopChannels;

extern int						s_soundStarted;
extern qboolean					s_soundMuted;

extern int						listener_number;
vec3_t							g_listener_origin;
vec3_t							g_listener_axis[3];

extern int						s_soundtime;
extern int   					s_paintedtime;
static int						dmaHD_inwater;

// MAX_SFX may be larger than MAX_SOUNDS because of custom player sounds
#define MAX_SFX					4096 // This must be the same as the snd_dma.c
#define MAX_SOUNDBYTES			(256*1024*1024) // 256MiB MAXIMUM...
extern sfx_t					s_knownSfx[];
extern int						s_numSfx;

extern cvar_t					*s_mixahead;
extern cvar_t					*s_mixPreStep;
cvar_t							*dmaHD_Enable = NULL;
cvar_t							*dmaHD_Interpolation;
cvar_t							*dmaHD_Mixer;
cvar_t							*dmaEX_StereoSeparation;

extern loopSound_t				loopSounds[];

#ifdef MAX_RAW_STREAMS
extern int						s_rawend[MAX_RAW_STREAMS];
extern portable_samplepair_t	s_rawsamples[MAX_RAW_STREAMS][MAX_RAW_SAMPLES];
#else
extern int						s_rawend;
extern portable_samplepair_t	s_rawsamples[];
#endif

#define DMAHD_PAINTBUFFER_SIZE	65536
static portable_samplepair_t	dmaHD_paintbuffer[DMAHD_PAINTBUFFER_SIZE];
static int						dmaHD_snd_vol;

qboolean g_tablesinit = qfalse;
float g_voltable[256];

#define SMPCLAMP(a) (((a) < -32768) ? -32768 : ((a) > 32767) ? 32767 : (a))
#define VOLCLAMP(a) (((a) < 0) ? 0 : ((a) > 255) ? 255 : (a))

void dmaHD_InitTables()
{
	if (!g_tablesinit)
	{
		int i;
		float x, y;

		// Volume table.
		for (i = 0; i < 256; i++)
		{
			x = (i * (9.0 / 256.0)) + 1.0;
			y = 1.0 - log10f(x);
			g_voltable[i] = y;
		}

		g_tablesinit = qtrue;
	}
}

/*
===============================================================================
PART#01: dmaHD: dma sound EXtension : MEMORY
===============================================================================
*/

int g_dmaHD_allocatedsoundmemory = 0;

/*
======================
dmaHD_FreeOldestSound
======================
*/

void dmaHD_FreeOldestSound( void ) 
{
	int	i, oldest, used;
	sfx_t *sfx;
	short* buffer;

	oldest = Com_Milliseconds();
	used = 0;

	for (i = 1 ; i < s_numSfx ; i++) 
	{
		sfx = &s_knownSfx[i];
		if (sfx->inMemory && sfx->lastTimeUsed < oldest) 
		{
			used = i;
			oldest = sfx->lastTimeUsed;
		}
	}

	sfx = &s_knownSfx[used];

	Com_DPrintf("dmaHD_FreeOldestSound: freeing sound %s\n", sfx->soundName);

	i = (sfx->soundLength + (sfx->soundLength >> 1)) * sizeof(short);
	g_dmaHD_allocatedsoundmemory -= i;
	if (g_dmaHD_allocatedsoundmemory < 0) g_dmaHD_allocatedsoundmemory = 0;
	if ((buffer = (short*)sfx->soundData) != NULL) free(buffer);
	sfx->inMemory = qfalse;
	sfx->soundData = NULL;
}

/*
======================
dmaHD_AllocateSoundBuffer
======================
*/

short* dmaHD_AllocateSoundBuffer(int samples)
{
	int bytes = samples * sizeof(short);
	short* buffer;
	
	while (g_dmaHD_allocatedsoundmemory > 0 &&
		(g_dmaHD_allocatedsoundmemory + bytes) > MAX_SOUNDBYTES) dmaHD_FreeOldestSound();

	if (s_numSfx >= (MAX_SFX - 8)) dmaHD_FreeOldestSound();

	do
	{
		if ((buffer = (short*)malloc(bytes)) != NULL) break;
		dmaHD_FreeOldestSound();
	} while(g_dmaHD_allocatedsoundmemory > 0);

	if (buffer == NULL) Com_Error (ERR_FATAL, "Out of Memory");

	g_dmaHD_allocatedsoundmemory += bytes;

	return buffer;
}

// =======================================================
// DMAHD - Interpolation functions / No need to optimize a lot here since the sounds are interpolated 
// once on load and not on playback. This also means that at least twice more memory is used.
// =======================================================
// x0-----x1--t--x2-----x3 / x0/2/3/4 are know samples / t = 0.0 - 1.0 between x1 and x2 / returns y value at point t
static float dmaHD_InterpolateCubic(float x0, float x1, float x2, float x3, float t) {
    float a0,a1,a2,a3;a0=x3-x2-x0+x1;a1=x0-x1-a0;a2=x2-x0;a3=x1;
    return (a0*(t*t*t))+(a1*(t*t))+(a2*t)+(a3);
}
static float dmaHD_InterpolateHermite4pt3oX(float x0, float x1, float x2, float x3, float t) {
    float c0,c1,c2,c3;c0=x1;c1=0.5f*(x2-x0);c2=x0-(2.5f*x1)+(2*x2)-(0.5f*x3);c3=(0.5f*(x3-x0))+(1.5f*(x1-x2));
    return (((((c3*t)+c2)*t)+c1)*t)+c0;
}
static float dmaHD_NormalizeSamplePosition(float t, int samples) {
	while (t<0.0) t+=(float)samples; while (t>=(float)samples) t-=(float)samples; return t;
}
static int dmaHD_GetSampleRaw_8bit(int index, int samples, byte* data) {
	return (index < 0 || index >= samples) ? 0 : (int)(((byte)(data[index])-128)<<8);
}
static int dmaHD_GetSampleRaw_16bit(int index, int samples, byte* data) {
	return (index < 0 || index >= samples) ? 0 : (int)LittleShort(((short*)data)[index]);
}

// Get only decimal part (a - floor(a))
#define FLOAT_DECIMAL_PART(a) (a-(float)((int)a))

// t must be a float between 0 and samples
static int dmaHD_GetInterpolatedSampleHermite4pt3oX(float t, int samples, byte *data,
													int (*dmaHD_GetSampleRaw)(int, int, byte*))
{
	int x, val;

	t = dmaHD_NormalizeSamplePosition(t, samples);
	// Get points
	x = (int)t;
	// Interpolate
	val = (int)dmaHD_InterpolateHermite4pt3oX(
		(float)dmaHD_GetSampleRaw(x - 1, samples, data),
		(float)dmaHD_GetSampleRaw(x, samples, data),
		(float)dmaHD_GetSampleRaw(x + 1, samples, data),
		(float)dmaHD_GetSampleRaw(x + 2, samples, data), FLOAT_DECIMAL_PART(t));
	// Clamp
	return SMPCLAMP(val);
}

// t must be a float between 0 and samples
static int dmaHD_GetInterpolatedSampleCubic(float t, int samples, byte *data,
											int (*dmaHD_GetSampleRaw)(int, int, byte*))
{
	int x, val;

	t = dmaHD_NormalizeSamplePosition(t, samples);
	// Get points
	x = (int)t;
	// Interpolate
	val = (int)dmaHD_InterpolateCubic(
		(float)dmaHD_GetSampleRaw(x - 1, samples, data),
		(float)dmaHD_GetSampleRaw(x, samples, data),
		(float)dmaHD_GetSampleRaw(x + 1, samples, data),
		(float)dmaHD_GetSampleRaw(x + 2, samples, data), FLOAT_DECIMAL_PART(t));
	// Clamp
	return SMPCLAMP(val);
}

// t must be a float between 0 and samples
static int dmaHD_GetInterpolatedSampleLinear(float t, int samples, byte *data,
											 int (*dmaHD_GetSampleRaw)(int, int, byte*))
{
	int x, val;
	float c0, c1;

	t = dmaHD_NormalizeSamplePosition(t, samples);

	// Get points
	x = (int)t;
	
	c0 = (float)dmaHD_GetSampleRaw(x, samples, data);
	c1 = (float)dmaHD_GetSampleRaw(x+1, samples, data);
	
	val = (int)(((c1 - c0) * FLOAT_DECIMAL_PART(t)) + c0);
	// No need to clamp for linear
	return val;
}

// t must be a float between 0 and samples
static int dmaHD_GetNoInterpolationSample(float t, int samples, byte *data,
										  int (*dmaHD_GetSampleRaw)(int, int, byte*))
{
	int x;

	t = dmaHD_NormalizeSamplePosition(t, samples);

	// Get points
	x = (int)t;
	
	if (FLOAT_DECIMAL_PART(t) > 0.5) x++;

	return dmaHD_GetSampleRaw(x, samples, data);
}

int (*dmaHD_GetInterpolatedSample)(float t, int samples, byte *data, 
								   int (*dmaHD_GetSampleRaw)(int, int, byte*)) = 
	dmaHD_GetInterpolatedSampleHermite4pt3oX;

// =======================================================
// =======================================================

/*
================
dmaHD_ResampleSfx

resample / decimate to the current source rate
================
*/
static void dmaHD_ResampleSfx( sfx_t *sfx, int inrate, int inwidth, byte *data, qboolean compressed) 
{
	short* buffer;
	int (*dmaHD_GetSampleRaw)(int, int, byte*) = 
		(inwidth == 2) ? dmaHD_GetSampleRaw_16bit : dmaHD_GetSampleRaw_8bit;
	float stepscale, idx_smp, sample;
	float lp_inva, lp_a, hp_a, lp_data, lp_last, hp_data, hp_last, hp_lastsample;
	int outcount, idx_hp, bassoutcount, idx_lp;
	
	stepscale = (float)inrate/(float)dma.speed;
	outcount = (int)((float)sfx->soundLength / stepscale);
	// Always use even numbered length.
	if ((outcount % 2) != 0) outcount += 1;
	// Bass buffer output samples count
	bassoutcount = (outcount / 2);

	// Create secondary buffer for bass sound while performing lowpass filter;
	buffer = dmaHD_AllocateSoundBuffer(outcount + bassoutcount);

	// Check if this is a weapon sound.
	sfx->weaponsound = (memcmp(sfx->soundName, "sound/weapons/", 14) == 0) ? qtrue : qfalse;

	// Get first sample from sound effect.
	idx_smp = 0.0;
	sample = dmaHD_GetInterpolatedSample(idx_smp, sfx->soundLength, data, dmaHD_GetSampleRaw);
	idx_smp += stepscale;

	// Set up high pass filter.
	idx_hp = 0;
	hp_last = sample;
	hp_lastsample = sample;
	buffer[idx_hp++] = sample;
	hp_a = 0.95f;

	// Set up Low pass filter.
	idx_lp = outcount;
	lp_last = sample;
	lp_a = 0.03f;
	lp_inva = (1 - lp_a);

	// Now do actual high/low pass on actual data.
	for (;idx_hp < outcount; idx_hp++)
	{ 
		sample = dmaHD_GetInterpolatedSample(idx_smp, sfx->soundLength, data, dmaHD_GetSampleRaw);
		idx_smp += stepscale;

		// High pass.
		hp_data = hp_a * (hp_last + sample - hp_lastsample);
		buffer[idx_hp] = SMPCLAMP(hp_data);
		hp_last = hp_data;
		hp_lastsample = sample;

		// Low pass.
		lp_data = lp_a * (float)sample + lp_inva * lp_last;
		if ((idx_hp % 2) != 0) buffer[idx_lp++] = SMPCLAMP((lp_last + lp_data) / 2.0f);
		lp_last = lp_data;
	}
	
	sfx->soundData = (sndBuffer*)buffer;
	sfx->soundLength = outcount;
}

//=============================================================================

qboolean dmaHD_LoadSound(sfx_t *sfx)
{
	byte *data;
	snd_info_t info;

	// Player specific sounds are never directly loaded.
	if (sfx->soundName[0] == '*') return qfalse;

	// Load it in.
	if (!(data = S_CodecLoad(sfx->soundName, &info))) return qfalse;

	// Information
	Com_DPrintf("^3Loading sound: ^7%s", sfx->soundName);
	if (info.width == 1) 
		Com_DPrintf(" [^28^3bit ^7-> ^216^3bit]", sfx->soundName);
	if (info.rate != dma.speed) 
		Com_DPrintf(" [^2%d^3Hz ^7-> ^2%d^3Hz]", info.rate, dma.speed);
	Com_DPrintf("\n");

	sfx->lastTimeUsed = Com_Milliseconds() + 1;

	// Do not compress.
	sfx->soundCompressionMethod = 0;
	sfx->soundLength = info.samples;
	sfx->soundData = NULL;
	dmaHD_ResampleSfx(sfx, info.rate, info.width, data + info.dataofs, qfalse);
	
	// Free data allocated by Codec
	Z_Free(data);

	return qtrue;
}

/*
===============================================================================
PART#02: dmaHD: dma sound EXtension : Mixing
===============================================================================
*/

static void dmaHD_PaintChannelFrom16_HHRTF(channel_t *ch, const sfx_t *sc, int count, int sampleOffset, int bufferOffset, int chan) 
{
	int data, vol, i, so, c, len;
	portable_samplepair_t *samp = &dmaHD_paintbuffer[bufferOffset];
	int* rawsamps;
	short *samples;
	ch_side_t* chs = (chan == 0) ? &ch->l : &ch->r;

	if (dmaHD_snd_vol <= 0) return;

	if (chs->bassvol > 0)
	{
		// Process low frequency
		samples = &((short*)sc->soundData)[sc->soundLength]; // Select bass frequency offset (just after high frequency)
		len = sc->soundLength >> 1; // Divide length by 2
		so = (sampleOffset - chs->bassoffset) >> 1;
		c = count >> 1;
		if (so < 0) { c += so; so = 0; } // [c -= (-so)] == [c += so]
		if ((so + c) > len) c = len - so;
		if (c > 0)
		{
			// Calculate volumes.
			vol = chs->bassvol * dmaHD_snd_vol;

			rawsamps = (int*)samp; rawsamps += chan;
			for (i = 0; i < c; i++) 
			{ 
				data = (samples[so++] * vol) >> 8;
				*rawsamps += data; rawsamps += 2; 
				*rawsamps += data; rawsamps += 2; 
			}
		}
	}

	if (chs->vol > 0)
	{
		// Process high frequency.
		samples = (short*)sc->soundData; // Select high frequency offset.
		len = sc->soundLength;
		so = sampleOffset - chs->offset;
		c = count;
		if (so < 0) { c += so; so = 0; } // [c -= (-so)] == [c += so]
		if ((so + c) > len) c = len - so;
		if (c > 0)
		{
			// Calculate volumes.
			vol = chs->vol * dmaHD_snd_vol;

			rawsamps = (int*)samp; rawsamps += chan;
			for (i = 0; i < c; i++) { *rawsamps += (samples[so++] * vol) >> 8; rawsamps += 2; }
		}
	}
}

static void dmaHD_PaintChannelFrom16_dmaEX2(channel_t *ch, const sfx_t *sc, int count, int sampleOffset, int bufferOffset) 
{
	int data, rvol, lvol, i, j, so, c, len;
	portable_samplepair_t *samp = &dmaHD_paintbuffer[bufferOffset];
	short *samples;

	if (dmaHD_snd_vol <= 0) return;

	if (ch->l.bassvol > 0)
	{
		// Process low frequency.
		samples = &((short*)sc->soundData)[sc->soundLength]; // Select bass frequency offset (just after high frequency)
		len = sc->soundLength >> 1; // Divide length by 2
		so = (sampleOffset - ch->l.offset) >> 1;
		c = count >> 1;
		if (so < 0) { c += so; so = 0; } // [c -= (-so)] == [c += so]
		if ((so + c) > len) c = len - so;
		if (c > 0)
		{
			// Calculate volumes.
			lvol = ch->l.bassvol * dmaHD_snd_vol;

			//#pragma omp parallel for private(data, j)
			for (i = 0; i < c; i++) 
			{ 
				data = (samples[so + i] * lvol) >> 8;
				j = (i << 1);
				samp[j].left += data;
				samp[j].right += data;
				samp[j + 1].left += data;
				samp[j + 1].right += data;
			}
		}
	}
	
	if (ch->l.vol > 0 || ch->r.vol > 0)
	{
		// Process high frequency.
		samples = (short*)sc->soundData; // Select high frequency offset.
		len = sc->soundLength;
		so = sampleOffset - ch->l.offset;
		c = count;
		if (so < 0) { c += so; so = 0; } // [c -= (-so)] == [c += so]
		if ((so + c) > len) c = len - so;
		if (c > 0)
		{
			// Calculate volumes.
			lvol = ch->l.vol * dmaHD_snd_vol;
			rvol = ch->r.vol * dmaHD_snd_vol;

			// Behind viewer?
			if (ch->fixed_origin && ch->sodrot[0] < 0) 
			{
				if (ch->r.vol > ch->l.vol) lvol = -lvol;
				else rvol = -rvol;
			}

			//#pragma omp parallel for
			for (i = 0; i < c; i++)
			{ 
				samp[i].left += (samples[so + i] * lvol) >> 8;
				samp[i].right += (samples[so + i] * rvol) >> 8;
			}
		}
	}

	// Process high frequency reverb.
	if (ch->l.reverbvol > 0 || ch->r.reverbvol > 0)
	{
		samples = (short*)sc->soundData; // Select high frequency offset.
		len = sc->soundLength;
		so = sampleOffset - ch->l.reverboffset;
		c = count;
		if (so < 0) { c += so; so = 0; } // [c -= (-so)] == [c += so]
		if ((so + c) > len) c = len - so;
		if (c > 0)
		{
			// Calculate volumes for reverb.
			lvol = ch->l.reverbvol * dmaHD_snd_vol;
			rvol = ch->r.reverbvol * dmaHD_snd_vol;

			//#pragma omp parallel for
			for (i = 0; i < c; i++)
			{ 
				samp[i].left += (samples[so + i] * lvol) >> 8;
				samp[i].right += (samples[so + i] * rvol) >> 8;
			}
		}
	}
}

static void dmaHD_PaintChannelFrom16_dmaEX(channel_t *ch, const sfx_t *sc, int count, int sampleOffset, int bufferOffset) 
{
	int ldata, rdata, rvol, lvol, i, j, so, c, len;
	portable_samplepair_t *samp = &dmaHD_paintbuffer[bufferOffset];
	short *samples;

	if (dmaHD_snd_vol <= 0) return;

	if (ch->l.vol > 0 || ch->r.vol > 0)
	{
		// Process low frequency.
		samples = &((short*)sc->soundData)[sc->soundLength]; // Select bass frequency offset (just after high frequency)
		len = sc->soundLength >> 1; // Divide length by 2
		so = (sampleOffset - ch->l.offset) >> 1;
		c = count >> 1;
		if (so < 0) { c += so; so = 0; } // [c -= (-so)] == [c += so]
		if ((so + c) > len) c = len - so;
		if (c > 0)
		{
			// Calculate volumes.
			lvol = ch->l.vol * dmaHD_snd_vol;
			rvol = ch->r.vol * dmaHD_snd_vol;

			// Behind viewer?
			if (ch->fixed_origin && ch->sodrot[0] < 0) 
			{
				if (lvol < rvol) lvol = -lvol; else rvol = -rvol;
			}

			//#pragma omp parallel for private(ldata, rdata, j)
			for (i = 0; i < c; i++) 
			{ 
				ldata = (samples[so + i] * lvol) >> 8;
				rdata = (samples[so + i] * rvol) >> 8;
				j = (i << 1);
				samp[j].left += ldata;
				samp[j].right += rdata;
				samp[j + 1].left += ldata;
				samp[j + 1].right += rdata;
			}
		}

		// Process high frequency.
		samples = (short*)sc->soundData; // Select high frequency offset.
		len = sc->soundLength;
		so = sampleOffset - ch->l.offset;
		c = count;
		if (so < 0) { c += so; so = 0; } // [c -= (-so)] == [c += so]
		if ((so + c) > len) c = len - so;
		if (c > 0)
		{
			// Calculate volumes.
			lvol = ch->l.vol * dmaHD_snd_vol;
			rvol = ch->r.vol * dmaHD_snd_vol;

			// Behind viewer?
			if (ch->fixed_origin && ch->sodrot[0] < 0) 
			{
				if (lvol < rvol) lvol = -lvol; else rvol = -rvol;
			}

			//#pragma omp parallel for
			for (i = 0; i < c; i++)
			{ 
				samp[i].left += (samples[so + i] * lvol) >> 8;
				samp[i].right += (samples[so + i] * rvol) >> 8;
			}
		}
	}
}

static void dmaHD_PaintChannelFrom16(channel_t *ch, const sfx_t *sc, int count, int sampleOffset, int bufferOffset) 
{
	switch (dmaHD_Mixer->integer)
	{
	// HHRTF
	case 10:
	case 11:
		//#pragma omp parallel sections
		{
			//#pragma omp section
			{
				dmaHD_PaintChannelFrom16_HHRTF(ch, sc, count, sampleOffset, bufferOffset, 0); // LEFT
			}
			//#pragma omp section
			{
				dmaHD_PaintChannelFrom16_HHRTF(ch, sc, count, sampleOffset, bufferOffset, 1); // RIGHT
			}
		}
		break;
	// dmaEX2
	case 20:
		dmaHD_PaintChannelFrom16_dmaEX2(ch, sc, count, sampleOffset, bufferOffset);
		break;
	case 21:
		// No reverb.
		ch->l.reverbvol = ch->r.reverbvol = 0;
		dmaHD_PaintChannelFrom16_dmaEX2(ch, sc, count, sampleOffset, bufferOffset);
		break;
	// dmaEX
	case 30:
		dmaHD_PaintChannelFrom16_dmaEX(ch, sc, count, sampleOffset, bufferOffset);
		break;
	}
}

void dmaHD_TransferPaintBuffer(int endtime)
{
	int		lpos;
	int		ls_paintedtime;
	int		i;
	int		val;
	int*     snd_p;  
	int      snd_linear_count;
	short*   snd_out;
	short*   snd_outtmp;
	unsigned long *pbuf = (unsigned long *)dma.buffer;

	snd_p = (int*)dmaHD_paintbuffer;
	ls_paintedtime = s_paintedtime;
	
	while (ls_paintedtime < endtime)
	{
		// handle recirculating buffer issues
		lpos = ls_paintedtime & ((dma.samples >> 1) - 1);

		snd_out = (short *)pbuf + (lpos << 1);

		snd_linear_count = (dma.samples >> 1) - lpos;
		if (ls_paintedtime + snd_linear_count > endtime)
			snd_linear_count = endtime - ls_paintedtime;

		snd_linear_count <<= 1;

		// write a linear blast of samples
		for (snd_outtmp = snd_out, i = 0; i < snd_linear_count; ++i)
		{
			val = *snd_p++ >> 8;
			*snd_outtmp++ = SMPCLAMP(val);
		}

		ls_paintedtime += (snd_linear_count>>1);

		if( CL_VideoRecording( ) )
			CL_WriteAVIAudioFrame( (byte *)snd_out, snd_linear_count << 1 );
	}
}

void dmaHD_PaintChannels( int endtime ) 
{
	int 	i;
	int 	end;
	channel_t *ch;
	sfx_t	*sc;
	int		ltime, count;
	int		sampleOffset;
#ifdef MAX_RAW_STREAMS
	int		stream;
#endif

	dmaHD_snd_vol = 
#ifdef MAX_RAW_STREAMS // For using Mitsu's build...
		(s_muted->integer) ? 0 : 
#endif
		s_volume->value*256;

	while ( s_paintedtime < endtime ) 
	{
		// if paintbuffer is smaller than DMA buffer
		// we may need to fill it multiple times
		end = endtime;
		if ( endtime - s_paintedtime > DMAHD_PAINTBUFFER_SIZE ) 
		{
			end = s_paintedtime + DMAHD_PAINTBUFFER_SIZE;
		}

#ifdef MAX_RAW_STREAMS
		// clear the paint buffer and mix any raw samples...
		Com_Memset(dmaHD_paintbuffer, 0, sizeof (dmaHD_paintbuffer));
		for (stream = 0; stream < MAX_RAW_STREAMS; stream++) 
		{
			if (s_rawend[stream] >= s_paintedtime)
			{
				// copy from the streaming sound source
				const portable_samplepair_t *rawsamples = s_rawsamples[stream];
				const int stop = (end < s_rawend[stream]) ? end : s_rawend[stream];
				for ( i = s_paintedtime ; i < stop ; i++ ) 
				{
					const int s = i&(MAX_RAW_SAMPLES-1);
					dmaHD_paintbuffer[i-s_paintedtime].left += rawsamples[s].left;
					dmaHD_paintbuffer[i-s_paintedtime].right += rawsamples[s].right;
				}
			}
		}
#else
		// clear the paint buffer to either music or zeros
		if ( s_rawend < s_paintedtime ) 
		{
			Com_Memset(dmaHD_paintbuffer, 0, (end - s_paintedtime) * sizeof(portable_samplepair_t));
		}
		else 
		{
			// copy from the streaming sound source
			int		s;
			int		stop;

			stop = (end < s_rawend) ? end : s_rawend;

			for ( i = s_paintedtime ; i < stop ; i++ ) 
			{
				s = i&(MAX_RAW_SAMPLES-1);
				dmaHD_paintbuffer[i-s_paintedtime].left = s_rawsamples[s].left;
				dmaHD_paintbuffer[i-s_paintedtime].right = s_rawsamples[s].right;
			}
			for ( ; i < end ; i++ ) 
			{
				dmaHD_paintbuffer[i-s_paintedtime].left = 0;
				dmaHD_paintbuffer[i-s_paintedtime].right = 0;
			}
		}
#endif

		// paint in the channels.
		ch = s_channels;
		for ( i = 0; i < MAX_CHANNELS ; i++, ch++ ) 
		{
			if (!ch->thesfx) continue;

			ltime = s_paintedtime;
			sc = ch->thesfx;
			sampleOffset = ltime - ch->startSample;
			count = end - ltime;
			if (count > 0) dmaHD_PaintChannelFrom16(ch, sc, count, sampleOffset, 0);
		}

		// paint in the looped channels.
		ch = loop_channels;
		for (i = 0; i < numLoopChannels ; i++, ch++)
		{		
			if (!ch->thesfx) continue;

			ltime = s_paintedtime;
			sc = ch->thesfx;

			if (sc->soundData == NULL || sc->soundLength == 0) continue;
			// we might have to make two passes if it is a looping sound effect and the end of the sample is hit
			do 
			{
				sampleOffset = (ltime % sc->soundLength);
				count = end - ltime;
				if (count > 0) 
				{	
					dmaHD_PaintChannelFrom16(ch, sc, count, sampleOffset, ltime - s_paintedtime);
					ltime += count;
				}
			} while (ltime < end);
		}

		// transfer out according to DMA format
		dmaHD_TransferPaintBuffer(end);
		s_paintedtime = end;
	}
}

/*
===============================================================================
PART#03: dmaHD: dma sound EXtension : main
===============================================================================
*/


/*
=================
dmaHD_SpatializeReset

Reset/Prepares channel before calling dmaHD_SpatializeOrigin
=================
*/
void dmaHD_SpatializeReset (channel_t* ch)
{
	VectorClear(ch->sodrot);
	memset(&ch->l, 0, sizeof(ch_side_t));
	memset(&ch->r, 0, sizeof(ch_side_t));
}

/*
=================
dmaHD_SpatializeOrigin

Used for spatializing s_channels
=================
*/

#define CALCVOL(dist) (((tmp = (int)((float)ch->master_vol * g_voltable[ \
			(((idx = (dist / iattenuation)) > 255) ? 255 : idx)])) < 0) ? 0 : tmp)
#define CALCSMPOFF(dist) (dist * dma.speed) >> ismpshift

void dmaHD_SpatializeOrigin_HHRTF(vec3_t so, channel_t* ch, qboolean b3d)
{
	// so = sound origin/[d]irection/[n]ormalized/[rot]ated/[d]irection [l]eft/[d]irection [r]ight
	vec3_t sod, sodl, sodr;
	// lo = listener origin/[l]eft/[r]ight
	vec3_t lol, lor;
    // distance to ears/[l]eft/[r]ight
	int distl, distr; // using int since calculations are integer based.
	// temp, index
	int tmp, idx;
	float t;

	int iattenuation = (dmaHD_inwater) ? 2 : 6;
	int ismpshift = (dmaHD_inwater) ? 19 : 17;

	// Increase attenuation for weapon sounds since they would be very loud!
	if (ch->thesfx && ch->thesfx->weaponsound) iattenuation *= 2;

	// Calculate sound direction.
	VectorSubtract(so, g_listener_origin, sod);
	// Rotate sound origin to listener axis
	VectorRotate(sod, g_listener_axis, ch->sodrot);

	// Origin for ears (~20cm apart)
	lol[0] = 0.0; lol[1] = 40; lol[2] = 0.0; // left
	lor[0] = 0.0; lor[1] = -40; lor[2] = 0.0; // right

	// Calculate sound direction.
	VectorSubtract(ch->sodrot, lol, sodl); // left
	VectorSubtract(ch->sodrot, lor, sodr); // right

	VectorNormalize(ch->sodrot);
	// Calculate length of sound origin direction vector.
	distl = (int)VectorNormalize(sodl); // left
	distr = (int)VectorNormalize(sodr); // right
	
	// Close enough to be at full volume?
	if (distl < 0) distl = 0; // left
	if (distr < 0) distr = 0; // right

	// Distance 384units = 1m
	// 340.29m/s (speed of sound at sea level)
	// Do surround effect with doppler.
	// 384.0 * 340.29 = 130671.36
	// Most similar is 2 ^ 17 = 131072; so shift right by 17 to divide by 131072

	// 1484m/s in water
	// 384.0 * 1484 = 569856
	// Most similar is 2 ^ 19 = 524288; so shift right by 19 to divide by 524288

	ch->l.bassoffset = ch->l.offset = CALCSMPOFF(distl); // left
	ch->r.bassoffset = ch->r.offset = CALCSMPOFF(distr); // right
	// Calculate volume at ears
	ch->l.bassvol = ch->l.vol = CALCVOL(distl); // left
	ch->r.bassvol = ch->r.vol = CALCVOL(distr); // right

	// Sound originating from inside head of left ear (i.e. from right)
	if (ch->sodrot[1] < 0) ch->l.vol *= (1.0 + (ch->sodrot[1] * 0.7f));
	// Sound originating from inside head of right ear (i.e. from left)
	if (ch->sodrot[1] > 0) ch->r.vol *= (1.0 - (ch->sodrot[1] * 0.7f));

	// Calculate HRTF function (lowpass filter) parameters
	//if (ch->fixed_origin)
	{
		// Sound originating from behind viewer
		if (ch->sodrot[0] < 0) 
		{
			ch->l.vol *= (1.0 + (ch->sodrot[0] * 0.05f));
			ch->r.vol *= (1.0 + (ch->sodrot[0] * 0.05f));
			// 2ms max
			//t = -ch->sodrot[0] * 0.04f; if (t > 0.005f) t = 0.005f;
			t = (dma.speed * 0.001f);
			ch->l.offset -= t;
			ch->r.offset += t;
			ch->l.bassoffset -= t;
			ch->r.bassoffset += t;
		}
	}

	if (b3d)
	{
		// Sound originating from above viewer (decrease bass)
		// Sound originating from below viewer (increase bass)
		ch->l.bassvol *= ((1 - ch->sodrot[2]) * 0.5);
		ch->r.bassvol *= ((1 - ch->sodrot[2]) * 0.5);
	}
	// Normalize volume
	ch->l.vol *= 0.5;
	ch->r.vol *= 0.5;

	if (dmaHD_inwater)
	{
		// Keep bass in water.
		ch->l.vol *= 0.2;
		ch->r.vol *= 0.2;
	}
}

void dmaHD_SpatializeOrigin_dmaEX2(vec3_t so, channel_t* ch)
{
	// so = sound origin/[d]irection/[n]ormalized/[rot]ated
	vec3_t sod;
    // distance to head
	int dist; // using int since calculations are integer based.
	// temp, index
	int tmp, idx, vol;
	vec_t dot;

	int iattenuation = (dmaHD_inwater) ? 2 : 6;
	int ismpshift = (dmaHD_inwater) ? 19 : 17;

	// Increase attenuation for weapon sounds since they would be very loud!
	if (ch->thesfx && ch->thesfx->weaponsound) iattenuation *= 2;

	// Calculate sound direction.
	VectorSubtract(so, g_listener_origin, sod);
	// Rotate sound origin to listener axis
	VectorRotate(sod, g_listener_axis, ch->sodrot);

	VectorNormalize(ch->sodrot);
	// Calculate length of sound origin direction vector.
	dist = (int)VectorNormalize(sod); // left
	
	// Close enough to be at full volume?
	if (dist < 0) dist = 0; // left

	// Distance 384units = 1m
	// 340.29m/s (speed of sound at sea level)
	// Do surround effect with doppler.
	// 384.0 * 340.29 = 130671.36
	// Most similar is 2 ^ 17 = 131072; so shift right by 17 to divide by 131072

	// 1484m/s in water
	// 384.0 * 1484 = 569856
	// Most similar is 2 ^ 19 = 524288; so shift right by 19 to divide by 524288

	ch->l.offset = CALCSMPOFF(dist);
	// Calculate volume at ears
	vol = CALCVOL(dist);
	ch->l.vol = vol;
	ch->r.vol = vol;
	ch->l.bassvol = vol;

	dot = -ch->sodrot[1];
	ch->l.vol *= 0.5 * (1.0 - dot);
	ch->r.vol *= 0.5 * (1.0 + dot);

	// Calculate HRTF function (lowpass filter) parameters
	if (ch->fixed_origin)
	{
		// Reverberation
		dist += 768;
		ch->l.reverboffset = CALCSMPOFF(dist);
		vol = CALCVOL(dist);
		ch->l.reverbvol = vol;
		ch->r.reverbvol = vol;
		ch->l.reverbvol *= 0.5 * (1.0 + dot);
		ch->r.reverbvol *= 0.5 * (1.0 - dot);

		// Sound originating from behind viewer: decrease treble + reverb
		if (ch->sodrot[0] < 0) 
		{
			ch->l.vol *= (1.0 + (ch->sodrot[0] * 0.5));
			ch->r.vol *= (1.0 + (ch->sodrot[0] * 0.5));
		}
		else // from front...
		{
			// adjust reverb for each ear.
			if (ch->sodrot[1] < 0) ch->r.reverbvol = 0;
			else if (ch->sodrot[1] > 0) ch->l.reverbvol = 0;
		}
		
		// Sound originating from above viewer (decrease bass)
		// Sound originating from below viewer (increase bass)
		ch->l.bassvol *= ((1 - ch->sodrot[2]) * 0.5);
	}
	else
	{
		// Reduce base volume by half to keep overall valume.
		ch->l.bassvol *= 0.5;
	}

	if (dmaHD_inwater)
	{
		// Keep bass in water.
		ch->l.vol *= 0.2;
		ch->r.vol *= 0.2;
	}
}

void dmaHD_SpatializeOrigin_dmaEX(vec3_t origin, channel_t* ch)
{
    vec_t		dot;
    vec_t		dist;
    vec_t		lscale, rscale, scale;
    vec3_t		source_vec;
	int tmp;

	const float dist_mult = SOUND_ATTENUATE;
	
	// calculate stereo seperation and distance attenuation
	VectorSubtract(origin, g_listener_origin, source_vec);

	// VectorNormalize returns original length of vector and normalizes vector.
	dist = VectorNormalize(source_vec);
	dist -= SOUND_FULLVOLUME;
	if (dist < 0) dist = 0; // close enough to be at full volume
	dist *= dist_mult;		// different attenuation levels
	
	VectorRotate(source_vec, g_listener_axis, ch->sodrot);

	dot = -ch->sodrot[1];
	// DMAEX - Multiply by the stereo separation CVAR.
	dot *= dmaEX_StereoSeparation->value;

	rscale = 0.5 * (1.0 + dot);
	lscale = 0.5 * (1.0 - dot);
	if ( rscale < 0 ) rscale = 0;
	if ( lscale < 0 ) lscale = 0;

	// add in distance effect
	scale = (1.0 - dist) * rscale;
	tmp = (ch->master_vol * scale);
	if (tmp < 0) tmp = 0;
	ch->r.vol = tmp;

	scale = (1.0 - dist) * lscale;
	tmp = (ch->master_vol * scale);
	if (tmp < 0) tmp = 0;
	ch->l.vol = tmp;
}

void dmaHD_SpatializeOrigin(vec3_t so, channel_t* ch)
{
	switch(dmaHD_Mixer->integer)
	{
	// HHRTF
	case 10: dmaHD_SpatializeOrigin_HHRTF(so, ch, qtrue); break;
	case 11: dmaHD_SpatializeOrigin_HHRTF(so, ch, qfalse); break;
	// dmaEX2
	case 20:
	case 21: dmaHD_SpatializeOrigin_dmaEX2(so, ch); break;
	// dmaEX
	case 30: dmaHD_SpatializeOrigin_dmaEX(so, ch); break;
	}
}

/*
==============================================================
continuous looping sounds are added each frame
==============================================================
*/

/*
==================
dmaHD_AddLoopSounds

Spatialize all of the looping sounds.
All sounds are on the same cycle, so any duplicates can just
sum up the channel multipliers.
==================
*/
void dmaHD_AddLoopSounds () 
{
	int			i, time;
	channel_t	*ch;
	loopSound_t	*loop;
	static int	loopFrame;

	numLoopChannels = 0;

	time = Com_Milliseconds();

	loopFrame++;
	//#pragma omp parallel for private(loop, ch)
	for (i = 0 ; i < MAX_GENTITIES; i++) 
	{
		if (numLoopChannels >= MAX_CHANNELS) continue;
		
		loop = &loopSounds[i];
		// already merged into an earlier sound
		if (!loop->active || loop->mergeFrame == loopFrame) continue;

		// allocate a channel
		ch = &loop_channels[numLoopChannels];

		dmaHD_SpatializeReset(ch);
		ch->fixed_origin = qtrue;
		ch->master_vol = (loop->kill) ? 127 : 90; // 3D / Sphere
		dmaHD_SpatializeOrigin(loop->origin, ch);

		loop->sfx->lastTimeUsed = time;

		ch->master_vol = 127;
		// Clip volumes.
		ch->l.vol = VOLCLAMP(ch->l.vol);
		ch->r.vol = VOLCLAMP(ch->r.vol);
		ch->l.bassvol = VOLCLAMP(ch->l.bassvol);
		ch->r.bassvol = VOLCLAMP(ch->r.bassvol);
		ch->l.reverbvol = VOLCLAMP(ch->l.reverbvol);
		ch->r.reverbvol = VOLCLAMP(ch->r.reverbvol);
		ch->thesfx = loop->sfx;
		ch->doppler = qfalse;
		
		//#pragma omp critical
		{
			numLoopChannels++;
		}
	}
}

//=============================================================================

/*
============
dmaHD_Respatialize

Change the volumes of all the playing sounds for changes in their positions
============
*/
void dmaHD_Respatialize( int entityNum, const vec3_t head, vec3_t axis[3], int inwater )
{
	int			i;
	channel_t	*ch;
	vec3_t		origin;

	if (!s_soundStarted || s_soundMuted) return;

	dmaHD_inwater = inwater;

	listener_number = entityNum;
	VectorCopy(head, g_listener_origin);
	VectorCopy(axis[0], g_listener_axis[0]);
	VectorCopy(axis[1], g_listener_axis[1]);
	VectorCopy(axis[2], g_listener_axis[2]);

	// update spatialization for dynamic sounds	
	//#pragma omp parallel for private(ch)
	for (i = 0 ; i < MAX_CHANNELS; i++) 
	{
		ch = &s_channels[i];
		if (!ch->thesfx) continue;
		
		dmaHD_SpatializeReset(ch);
		// Anything coming from the view entity will always be full volume
		if (ch->entnum == listener_number) 
		{
			ch->l.vol = ch->master_vol;
			ch->r.vol = ch->master_vol;
			ch->l.bassvol = ch->master_vol;
			ch->r.bassvol = ch->master_vol;
			switch(dmaHD_Mixer->integer)
			{
			case 10: case 11: case 20: case 21:
				if (dmaHD_inwater)
				{
					ch->l.vol *= 0.2;
					ch->r.vol *= 0.2;
				}
				break;
			}
		} 
		else 
		{
			if (ch->fixed_origin) { VectorCopy( ch->origin, origin ); }
			else { VectorCopy( loopSounds[ ch->entnum ].origin, origin ); }

			dmaHD_SpatializeOrigin(origin, ch);
		}
	}

	// add loopsounds
	dmaHD_AddLoopSounds();
}

/*
============
dmaHD_Update

Called once each time through the main loop
============
*/
void dmaHD_Update( void ) 
{
	if (!s_soundStarted || s_soundMuted) return;
	// add raw data from streamed samples
	S_UpdateBackgroundTrack();
	// mix some sound
	dmaHD_Update_Mix();
}

void dmaHD_Update_Mix(void) 
{
	unsigned endtime;
	int samps;
	static int lastTime = 0.0f;
	int mixahead, op, thisTime, sane;
	static int lastsoundtime = -1;

	if (!s_soundStarted || s_soundMuted) return;
	
	thisTime = Com_Milliseconds();

	// Updates s_soundtime
	S_GetSoundtime();

	if (s_soundtime <= lastsoundtime) return;
	lastsoundtime = s_soundtime;

	// clear any sound effects that end before the current time,
	// and start any new sounds
	S_ScanChannelStarts();

	if ((sane = thisTime - lastTime) < 1) sane = 1;
	mixahead = (int)((float)dma.speed * s_mixahead->value);
	op = (int)((float)(dma.speed * sane) * 0.01);
	if (op < mixahead) mixahead = op;
	
	// mix ahead of current position
	endtime = s_soundtime + mixahead;

	// never mix more than the complete buffer
	samps = dma.samples >> (dma.channels-1);
	if ((endtime - s_soundtime) > samps) endtime = (s_soundtime + samps);

	SNDDMA_BeginPainting ();

	dmaHD_PaintChannels (endtime);

	SNDDMA_Submit ();

	lastTime = thisTime;
}

/*
================
dmaHD_Enabled
================
*/
qboolean dmaHD_Enabled() 
{
	if (dmaHD_Enable == NULL)
		dmaHD_Enable = Cvar_Get("dmaHD_enable", "1", CVAR_ARCHIVE);

	return (dmaHD_Enable->integer);
}

// ====================================================================
// User-setable variables
// ====================================================================

void dmaHD_SoundInfo(void) 
{	
	int i;
	for (i = 0; i < 19; i++) Com_Printf("^7-^6-" );
	Com_Printf("\n" );
	Com_Printf("^2p5yc0runn3r's ^3dma^1HD ^7sound information\n");
	
	if (!s_soundStarted) 
	{
		Com_Printf ("  ^1Sound system not started...\n");
	} 
	else 
	{
		Com_Printf("  ^3dma^1HD ^7Mixer ^3type: ^2%d", dmaHD_Mixer->integer);
		switch (dmaHD_Mixer->integer / 10)
		{
			case 1: Com_Printf(" ^7- ^2Hybrid^7-^2HRTF"); 
				switch (dmaHD_Mixer->integer % 10)
				{
					case 0: Com_Printf(" ^0[^63D^0]"); break;
					case 1: Com_Printf(" ^0[^62D^0]"); break;
				}
				break;
			case 2: Com_Printf(" ^7- ^2dma^1EX^22");
				switch (dmaHD_Mixer->integer % 10)
				{
					case 1: Com_Printf(" ^0[^6No reverb^0]"); break;
				}
				break;
			case 3: Com_Printf(" ^7- ^2dma^1EX");
				break;
		}
		Com_Printf("\n");
		Com_Printf("  ^2%d^3ch ^7- ^2%d^3Hz ^7- ^2%d^3bps\n", dma.channels, dma.speed, dma.samplebits);
		if (s_numSfx > 0 || g_dmaHD_allocatedsoundmemory > 0)
		{
			Com_Printf("  ^2%d^3sounds ^7in ^2%.2f^3MiB\n", s_numSfx, (float)g_dmaHD_allocatedsoundmemory / 1048576.0f);
		}
		else
		{
			Com_Printf("  ^1No sounds loaded yet\n");
		}
	}
	for (i = 0; i < 19; i++) Com_Printf("^7-^6-" );
	Com_Printf("\n" );
}

void dmaHD_SoundList(void) 
{
	int i;
	sfx_t *sfx;
	
	for (i = 0; i < 19; i++) Com_Printf("^7-^6-" );
	Com_Printf("\n" );
	Com_Printf("^2p5yc0runn3r's ^3dma^1HD ^7sound list\n");

	if (s_numSfx > 0 || g_dmaHD_allocatedsoundmemory > 0)
	{
		for (sfx = s_knownSfx, i = 0; i < s_numSfx; i++, sfx++)
		{
			Com_Printf("  %s%s ^2%.2f^3KiB\n", 
				(sfx->inMemory ? S_COLOR_GREEN : S_COLOR_RED), sfx->soundName, 
				(float)sfx->soundLength / 1024.0f);
		}
		Com_Printf("  ^2%d^3sounds ^7in ^2%.2f^3MiB\n", s_numSfx, (float)g_dmaHD_allocatedsoundmemory / 1048576.0f);
	}
	else
	{
		Com_Printf("  ^1No sounds loaded yet\n");
	}
	for (i = 0; i < 19; i++) Com_Printf("^7-^6-" );
	Com_Printf("\n" );
}


/*
================
dmaHD_Init
================
*/
qboolean dmaHD_Init(soundInterface_t *si) 
{
	if (!si) return qfalse;

	// Return if not enabled.
	if (!dmaHD_Enabled()) return qtrue;

	dmaHD_Mixer = Cvar_Get("dmaHD_mixer", "10", CVAR_ARCHIVE);
	if (dmaHD_Mixer->integer != 10 && dmaHD_Mixer->integer != 11 &&
		dmaHD_Mixer->integer != 20 && dmaHD_Mixer->integer != 21 &&
		dmaHD_Mixer->integer != 30)
	{
		Cvar_Set("dmaHD_Mixer", "10");
		dmaHD_Mixer = Cvar_Get("dmaHD_mixer", "10", CVAR_ARCHIVE);
	}

	dmaEX_StereoSeparation = Cvar_Get("dmaEX_StereoSeparation", "0.9", CVAR_ARCHIVE);
	
	if (dmaEX_StereoSeparation->value < 0.1) 
	{
		Cvar_Set("dmaEX_StereoSeparation", "0.1");
		dmaEX_StereoSeparation = Cvar_Get("dmaEX_StereoSeparation", "0.9", CVAR_ARCHIVE);
	}
	else if (dmaEX_StereoSeparation->value > 2.0) 
	{
		Cvar_Set("dmaEX_StereoSeparation", "2.0");
		dmaEX_StereoSeparation = Cvar_Get("dmaEX_StereoSeparation", "0.9", CVAR_ARCHIVE);
	}


	dmaHD_Interpolation = Cvar_Get("dmaHD_interpolation", "3", CVAR_ARCHIVE);
	if (dmaHD_Interpolation->integer == 0)
	{
		dmaHD_GetInterpolatedSample = dmaHD_GetNoInterpolationSample;
	}
	else if (dmaHD_Interpolation->integer == 1)
	{
		dmaHD_GetInterpolatedSample = dmaHD_GetInterpolatedSampleLinear;
	}
	else if (dmaHD_Interpolation->integer == 2)
	{
		dmaHD_GetInterpolatedSample = dmaHD_GetInterpolatedSampleCubic;
	}
	else //if (dmaHD_Interpolation->integer == 3) // DEFAULT
	{
		dmaHD_GetInterpolatedSample = dmaHD_GetInterpolatedSampleHermite4pt3oX;
	}

	dmaHD_InitTables();

	// Override function pointers to dmaHD version, the rest keep base.
	si->SoundInfo = dmaHD_SoundInfo;
	si->Respatialize = dmaHD_Respatialize;
	si->Update = dmaHD_Update;
	si->SoundList = dmaHD_SoundList;

	return qtrue;
}

#endif//NO_DMAHD