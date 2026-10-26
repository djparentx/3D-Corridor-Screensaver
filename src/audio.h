#ifndef AUDIO_H
#define AUDIO_H

/* volume 0 = off, 3 = 0 dB, each step = 3 dB */
int  audio_start(int volume);
void audio_stop(void);

#endif
