    .section .rodata

    .balign 4
    .global hum_wav_start
    .global hum_wav_end
hum_wav_start:
    .incbin "assets/light-hum.wav"
hum_wav_end:

    .balign 4
    .global faulty_wav_start
    .global faulty_wav_end
faulty_wav_start:
    .incbin "assets/faulty.wav"
faulty_wav_end:

    .section .note.GNU-stack,"",%progbits
