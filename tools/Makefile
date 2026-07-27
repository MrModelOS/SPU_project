# sppu_search CLI tool

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2 -std=c11
CPPFLAGS += -I../sdk/include -I../emulator
LDFLAGS ?=
LDLIBS  ?= -lm

TARGET  = sppu_search

.PHONY: all clean

all: $(TARGET)

$(TARGET): sppu_search.c ../sdk/include/sppu.h ../emulator/sppu_device.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< -L../sdk -lsppu \
		-Wl,-rpath,'$$ORIGIN/../sdk' $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET)
