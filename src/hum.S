    .section .rodata
    .balign 4
    .global hum_wav_start
    .global hum_wav_end
hum_wav_start:
    .incbin "assets/light-hum.wav"
hum_wav_end:

    .section .note.GNU-stack,"",%progbits
