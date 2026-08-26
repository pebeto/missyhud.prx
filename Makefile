TARGET = missyhud
OBJS = src/main.o src/control.o src/worker.o src/gui.o src/utils.o include/blit.o include/hook.o

MISSYHUD_MAJOR_VERSION := 0
MISSYHUD_MINOR_VERSION := 6

BUILD_PRX = 1
USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1

WARNINGS = -Wall -Wextra -Wformat-overflow=2 -Wformat-truncation=2 -Wstrict-prototypes

CFLAGS = -O2 -G0 $(WARNINGS) -DMAJOR_VERSION=$(MISSYHUD_MAJOR_VERSION) -DMINOR_VERSION=$(MISSYHUD_MINOR_VERSION)
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

INCDIR = include
LIBS = -lpspuser -lpsppower -lpspge_driver -lpspsystemctrl_kernel

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak
