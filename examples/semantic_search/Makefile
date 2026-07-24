# SPU Semantic Search HTTP daemon

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2 -std=c11
CPPFLAGS += -I../../sdk/include -I../../emulator
LDFLAGS ?=
LDLIBS  ?= -lm

TARGET  = spu_searchd

.PHONY: all clean run

all: $(TARGET)

$(TARGET): spu_searchd.c ../../sdk/include/spu.h ../../emulator/spu_device.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< -L../../sdk -lspu \
		-Wl,-rpath,'$$ORIGIN/../../sdk' $(LDFLAGS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET) --port 8080

clean:
	rm -f $(TARGET)
