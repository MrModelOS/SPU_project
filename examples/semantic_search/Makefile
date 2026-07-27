# SPPU Semantic Search HTTP daemon

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2 -std=c11
CPPFLAGS += -I../../sdk/include -I../../emulator
LDFLAGS ?=
LDLIBS  ?= -lm

TARGET  = sppu_searchd

.PHONY: all clean run

all: $(TARGET)

$(TARGET): sppu_searchd.c ../../sdk/include/sppu.h ../../emulator/sppu_device.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< -L../../sdk -lsppu \
		-Wl,-rpath,'$$ORIGIN/../../sdk' $(LDFLAGS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET) --port 8080

clean:
	rm -f $(TARGET)
