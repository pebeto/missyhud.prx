TARGET = missyhud
OBJS = src/main.o src/control.o src/worker.o src/gui.o src/utils.o include/blit.o include/hook.o

MISSYHUD_MAJOR_VERSION := 0
MISSYHUD_MINOR_VERSION := 5

BUILD_PRX = 1
USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1

CFLAGS = -O2 -G0 -Wall -DMAJOR_VERSION=$(MISSYHUD_MAJOR_VERSION) -DMINOR_VERSION=$(MISSYHUD_MINOR_VERSION)
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

INCDIR = include
LIBS = -lpspuser -lpsppower

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak
