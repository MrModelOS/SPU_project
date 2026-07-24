# SPU Emulator — Makefile
# C11, GCC, POSIX

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -std=c11
LDFLAGS := -lm

SRCS    := main.c spu_pci.c
OBJS    := $(SRCS:.c=.o)
TARGET  := spu_emulator

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET)
