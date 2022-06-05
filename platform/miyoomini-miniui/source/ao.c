#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <linux/input.h>
#include <SDL/SDL.h>
#include <sdkdir/mi_sys.h>
#include <sdkdir/mi_ao.h>

/*
#define	YIELD_WAIT	// Flag to wait with sched_yield() when the wait time is less than 10.5ms
			// ( callback function will be called at more precise timing,
			//   but may cause blocking/slowdown of other threads/daemons execution )
*/
#ifdef	YIELD_WAIT
#include <sched.h>
#ifndef	SCHED_IDLE
#define SCHED_IDLE	5
#endif
#endif

pthread_t 	audiothread_pt;
pthread_mutex_t	audiolock_mx;
uint32_t	audiothread_cancel;
SDL_AudioSpec	audiospec;
uint8_t		*audio_buffer;
int		audio_paused;
uint32_t	sdlaudio;

//
//	AO Audio playback thread
//
void* audiothread(void* param) {
	MI_AUDIO_Frame_t	AoSendFrame;
	struct timeval tod;
	int usleepclock;
	uint64_t startclock, targetclock, clock_freqframes;
	uint32_t framecounter, num_frames, i;
#ifdef	YIELD_WAIT
	int policy = sched_getscheduler(0);
	const struct sched_param scprm = {0};
#endif
	void (*fill)(void*, uint8_t*, int) = audiospec.callback;
	void *udata = audiospec.userdata;

	memset(&AoSendFrame, 0, sizeof(AoSendFrame));
	AoSendFrame.apVirAddr[0] = audio_buffer;
	AoSendFrame.u32Len = audiospec.size;
	memset(audio_buffer, 0, audiospec.size);

	// Buffer initial frames (calculate at least 20ms)
	num_frames = (uint32_t)((audiospec.freq-1) / (50*audiospec.samples)) +1;
	if (num_frames < 2) num_frames = 2;
	MI_AO_ClearChnBuf(0,0);
	for (i=num_frames; i>0; i--) MI_AO_SendFrame(0, 0, &AoSendFrame, 0);

	clock_freqframes = audiospec.samples * 1000000;
	framecounter = 0;
	gettimeofday(&tod, NULL);
	startclock = tod.tv_usec + tod.tv_sec * 1000000;

	while(!audiothread_cancel) {
		// Wait until next frame
		framecounter++;
		if (framecounter == (uint32_t)audiospec.freq) {
			framecounter = 0;
			startclock += clock_freqframes;
		}
		targetclock = framecounter * clock_freqframes / audiospec.freq + startclock;
		gettimeofday(&tod, NULL);
		usleepclock = targetclock - (tod.tv_usec + tod.tv_sec * 1000000);
		// check 300ms under/overrun (1frame max = 256ms at 8kHz/2048samples)
		if ((usleepclock < -300000)||(usleepclock > 300000)) {
			// reset buffer
			// fprintf(stderr,"underrun occur %d\n",usleepclock);
			MI_AO_ClearChnBuf(0,0);
			for (i=num_frames-1; i>0; i--) MI_AO_SendFrame(0, 0, &AoSendFrame, 0);
			framecounter = 0;
			gettimeofday(&tod, NULL);
			startclock = tod.tv_usec + tod.tv_sec * 1000000;
		} else if (usleepclock > 0) {
#ifdef	YIELD_WAIT
			// wait process for miyoomini with 10ms sleep precision
			if (usleepclock > 10500) usleep(usleepclock - 10500);	// 0.5ms margin
			// wait for less than 10.5ms with sched_yield()
			sched_setscheduler(0, SCHED_IDLE, &scprm);
			do { sched_yield(); gettimeofday(&tod, NULL);
			} while (targetclock > (tod.tv_usec + tod.tv_sec * 1000000));
			sched_setscheduler(0, policy, &scprm);
#else
			usleep(usleepclock);
#endif
		}

		// Request filling audio_buffer to callback function
		if (!audio_paused) {
			pthread_mutex_lock(&audiolock_mx);
			(*fill)(udata, audio_buffer, audiospec.size);
			pthread_mutex_unlock(&audiolock_mx);
		}

		// Playback
		MI_AO_SendFrame(0, 0, &AoSendFrame, 0);

		// Clear Buffer , per SDL1.2 spec (SDL2 does not clear)
		memset(audio_buffer, 0,audiospec.size);
	}

	return 0;
}

//
//	Open AO Audio in place of SDL_OpenAudio
//		signed 16bit only
//		sampling rate: 8/11.025/12/16/22.05/24/32/44.1/48kHz
//		audiospec.samples should be 2048 or less
//		rev5 : check specs strictly, added arg obtained
//
int AO_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained){
	MI_AUDIO_Attr_t	attr;
	const int freqtable[] = { 8000,11025,12000,16000,22050,24000,32000,44100,48000 };
	uint32_t i;

	memcpy(&audiospec, desired, sizeof(audiospec));
	audiospec.format = AUDIO_S16SYS;
	for (i=0; i<(sizeof(freqtable)/sizeof(int)); i++) {
		if (audiospec.freq <= freqtable[i]) { audiospec.freq = freqtable[i]; break; }
	} if (audiospec.freq > 48000) audiospec.freq = 48000;
	if (audiospec.samples > 2048) audiospec.samples = 2048;
	else if (audiospec.samples < 8) audiospec.samples = 8;
	audiospec.size = audiospec.samples * audiospec.channels * 2;

	memset(&attr, 0, sizeof(attr));
	attr.eSamplerate = (MI_AUDIO_SampleRate_e)audiospec.freq;
	attr.eSoundmode = (MI_AUDIO_SoundMode_e)(audiospec.channels - 1);
	attr.u32ChnCnt = audiospec.channels;
	attr.u32PtNumPerFrm = audiospec.samples;

	if (MI_AO_SetPubAttr(0,&attr)) {
		// SetPubAttr fail : try open with SDL
		sdlaudio = 1;
		return SDL_OpenAudio(desired, obtained);
	} else	sdlaudio = 0;

	if (MI_AO_Enable(0)) return -1;
	if (MI_AO_EnableChn(0,0)) return -1;
	if (MI_AO_SetMute(0,FALSE)) return -1;
	//if (MI_AO_SetVolume(0,0)) return -1;

	audio_buffer = (uint8_t*)malloc(audiospec.size);
	if (audio_buffer == NULL) return -1;
	if (obtained != NULL) { memcpy(obtained, &audiospec, sizeof(audiospec)); }

	audiolock_mx = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	audio_paused = 1;
	audiothread_cancel = 0;
	pthread_create(&audiothread_pt, NULL, &audiothread, NULL);

	return 0;
}

//
//	Close AO Audio in place of SDL_CloseAudio
//
void AO_CloseAudio(void){
	if (sdlaudio) return SDL_CloseAudio();

	audiothread_cancel = 1;
	pthread_join(audiothread_pt, NULL);

	MI_AO_ClearChnBuf(0,0);
	MI_AO_DisableChn(0,0);
	MI_AO_Disable(0);

	if (audio_buffer) free(audio_buffer);
}

//
//	Pause AO Audio in place of SDL_PauseAudio
//
void AO_PauseAudio(int pause_on){
	if (sdlaudio) return SDL_PauseAudio(pause_on);

	audio_paused = pause_on;
	// unnecessary if buffer clearing is of SDL1.2 spec
	// MI_AO_SetMute(0, (pause_on ? TRUE : FALSE));
}

//
//	Lock Audio Mutex in place of SDL_LockAudio
//
void AO_LockAudio(void){
	if (sdlaudio) return SDL_LockAudio();

	pthread_mutex_lock(&audiolock_mx);
}

//
//	Unlock Audio Mutex in place of SDL_UnlockAudio
//
void AO_UnlockAudio(void){
	if (sdlaudio) return SDL_UnlockAudio();

	pthread_mutex_unlock(&audiolock_mx);
}

