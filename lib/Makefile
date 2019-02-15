
# Library Makefile: just add the objects and names

LIBBATHOS = lib/libbathos.a
LIB_OBJS += lib/pipe.o
LIB_OBJS += lib/buffer_queue.o
LIB_OBJS += lib/buffer_queue_server.o
ifneq ($(CONFIG_ARCH_ATMEGA),y)
LIB_OBJS += lib/string.o
endif
LIB_OBJS += lib/stdio.o
LIB_OBJS += lib/stdlib.o
LIB_OBJS += lib/ctype.o
LIB_OBJS += lib/setup.o
LIB_OBJS += lib/udelay.o
LIB_OBJS += lib/event.o
LIB_OBJS += lib/statemachine.o
LIB_OBJS += lib/allocator.o
LIB_OBJS += lib/allocator-tables.o
LIB_OBJS += lib/shell.o
LIB_OBJS += lib/irq.o
LIB_OBJS += lib/cmdline.o

ifeq ($(CONFIG_HAVE_LWIP),y)
LIB_OBJS += lib/tcp-lwip-raw.o
LIB_OBJS += lib/tcp-server-lwip-pipe.o
LIB_OBJS += lib/tcp-server-connection-dev.o
endif

$(LIBBATHOS): $(LIB_OBJS)
	$(AR) r $@ $(LIB_OBJS)

lib/allocator-tables.c : $(SCRIPTS)/allocator_aux_gen
	$< > $@

$(SCRIPTS)/allocator_aux_gen: $(SCRIPTS)/allocator_aux_gen.c
	$(HOSTCC) $(HOST_CFLAGS) -DBITS_PER_LONG=$(BITS_PER_LONG) \
	-o $@ $<

LIBS += lib/libbathos.a
LDFLAGSS += -Llib -lbathos
