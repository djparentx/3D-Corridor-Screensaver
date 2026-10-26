CC      ?= gcc
CFLAGS  ?= -O2
CFLAGS  += -Wall -Wextra -std=gnu11 $(shell sdl2-config --cflags)
LDFLAGS += -s
LDLIBS  := $(shell sdl2-config --libs) -lm

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o) src/hum.o
HDR := $(wildcard src/*.h)
BIN := 3D_Backrooms
WAV := assets/light-hum.wav

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

src/%.o: src/%.c $(HDR)
	$(CC) $(CFLAGS) -c -o $@ $<

src/hum.o: src/hum.S $(WAV)
	$(CC) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean