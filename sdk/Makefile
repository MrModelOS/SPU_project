# libspu — userspace SDK for /dev/spu

CC       ?= gcc
CFLAGS   ?= -Wall -Wextra -O2 -std=c11 -fPIC
CPPFLAGS += -Iinclude -I../emulator
LDFLAGS  ?=
LDLIBS   ?=

LIBNAME  = libspu.so
DEMO     = examples/spu_demo

.PHONY: all clean test

all: $(LIBNAME) $(DEMO)

$(LIBNAME): src/libspu.c include/spu.h ../emulator/spu_device.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -shared -o $@ src/libspu.c $(LDFLAGS) $(LDLIBS)

$(DEMO): examples/spu_demo.c $(LIBNAME) include/spu.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ examples/spu_demo.c \
		-L. -lspu -Wl,-rpath,'$$ORIGIN/..' $(LDFLAGS) $(LDLIBS)

test: $(DEMO)
	./$(DEMO)

clean:
	rm -f $(LIBNAME) $(DEMO)
