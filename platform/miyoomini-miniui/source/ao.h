#ifndef __AO_H__
#define __AO_H__

#include <SDL/SDL.h>

//	Open AO Audio / in place of SDL_OpenAudio
//		signed 16bit only
//		sampling rate: 8/11.025/12/16/22.05/24/32/44.1/48kHz
//		audiospec.samples should be 2048 or less
int	AO_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);

//	Close AO Audio / in place of SDL_CloseAudio
void	AO_CloseAudio(void);

//	Pause AO Audio / in place of SDL_PauseAudio
void	AO_PauseAudio(int pause_on);

//	Lock Audio Mutex / in place of SDL_LockAudio
void	AO_LockAudio(void);

//	Unlock Audio Mutex / in place of SDL_UnlockAudio
void	AO_UnlockAudio(void);

#endif

