
# The default task list is very limited...
TASK-y ?= task-uart.o

# Library
LIBARCH = $(ADIR)/libarch.a
LIBARCH_OBJS = $(ADIR)/jiffies.o

# Determine bits per long
# FIXME: this does not work in case of a non x86 64 bits machine
MACHINE=$(shell uname -m)

# Keep default 32 bits in case of non x86_64
ifeq ($(MACHINE),x86_64)
 BITS_PER_LONG=64
endif


AOBJ = $(ADIR)/idle.o $(ADIR)/pipe.o $(ADIR)/eth.o

$(ADIR)/libarch.a: $(LIBARCH_OBJS)
	$(AR) r $@ $(LIBARCH_OBJS)

# We need librt for clock_gettime. Bah.
ARCH_EXTRA_LIBS = -lrt -lpthread -lc
