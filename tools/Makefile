# spu_search CLI tool

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2 -std=c11
CPPFLAGS += -I../sdk/include -I../emulator
LDFLAGS ?=
LDLIBS  ?= -lm

TARGET  = spu_search

.PHONY: all clean

all: $(TARGET)

$(TARGET): spu_search.c ../sdk/include/spu.h ../emulator/spu_device.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< -L../sdk -lspu \
		-Wl,-rpath,'$$ORIGIN/../sdk' $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET)
