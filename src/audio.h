#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#define FLICKER_RANGE  8.0   /* metres: two panels */

/* volume 0 = off, 3 = 0 dB, each step = 3 dB */
int  audio_start(int volume);
void audio_stop(void);

/* one-shot faulty sound for panel (pi,pj) at world (x,z) */
void audio_flicker(int32_t pi, int32_t pj, double x, double z);

/* update flicker voice gains from the camera position */
void audio_listener(double x, double z);

#endif