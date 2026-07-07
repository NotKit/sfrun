# Build the LD_PRELOAD compatibility shim.
#
# Native build on the device, or cross-build for arm64 with:
#   make CC=aarch64-linux-gnu-gcc
#
# The shim resolves libwayland-client symbols via dlsym(RTLD_NEXT) at runtime,
# so it does not link against it here.

CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra -fPIC -fvisibility=hidden
LDFLAGS ?= -shared -ldl

TARGET  := libsailfish-shim.so
SRC     := sailfish_shim.c

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean
