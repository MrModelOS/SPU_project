# libsppu — userspace SDK for /dev/sppu

CC       ?= gcc
CFLAGS   ?= -Wall -Wextra -O2 -std=c11 -fPIC
CPPFLAGS += -Iinclude -I../emulator
LDFLAGS  ?=
LDLIBS   ?=

LIBNAME  = libsppu.so
BACKEND  = libsppu_backend.so
DEMO     = examples/sppu_demo

.PHONY: all clean test

all: $(LIBNAME) $(BACKEND) $(DEMO)

$(LIBNAME): src/libsppu.c include/sppu.h ../emulator/sppu_device.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -shared -o $@ src/libsppu.c $(LDFLAGS) $(LDLIBS)

$(BACKEND): src/sppu_backend.c include/sppu_backend.h include/sppu.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -shared -o $@ src/sppu_backend.c $(LDFLAGS) $(LDLIBS)

$(DEMO): examples/sppu_demo.c $(LIBNAME) include/sppu.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ examples/sppu_demo.c \
		-L. -lsppu -Wl,-rpath,'$$ORIGIN/..' $(LDFLAGS) $(LDLIBS)

test: $(DEMO)
	./$(DEMO)

clean:
	rm -f $(LIBNAME) $(BACKEND) $(DEMO)
