CC      ?= gcc
CFLAGS  ?= -O2
CFLAGS  += -Wall -Wextra -std=gnu11 $(shell sdl2-config --cflags)
LDFLAGS += -s
LDLIBS  := $(shell sdl2-config --libs) -lm

SRC  := $(wildcard src/*.c)
OBJ  := $(SRC:.c=.o) src/sounds.o
HDR  := $(wildcard src/*.h)
BIN  := 3D_Corridors
WAVS := assets/light-hum.wav assets/faulty.wav

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

src/%.o: src/%.c $(HDR)
	$(CC) $(CFLAGS) -c -o $@ $<

src/sounds.o: src/sounds.S $(WAVS)
	$(CC) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean