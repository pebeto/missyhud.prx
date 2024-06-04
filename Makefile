TARGET = missyhud
OBJS = src/main.o include/blit.o include/hook.o

BUILD_PRX = 1
USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1

CFLAGS = -O2 -G0 -Wall
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

INCDIR = include
LIBS = -lpspuser -lpsppower

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak
