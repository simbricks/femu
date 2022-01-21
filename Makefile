-include Makefile.local

bin_femu := femu-simbricks

FEMU_OBJS := femu.o dma.o intr.o nvme-admin.o nvme-io.o \
	nvme-util.o backend/dram.o bbssd/bb.o bbssd/ftl.o lib/pqueue.o \
	lib/rte_ring.o nand/nand.o nossd/nop.o ocssd/oc12.o ocssd/oc20.o \
	timing-model/timing.o zns/zns.o

OBJS := femu-simbricks.o qemu-compat/bitmap.o qemu-compat/bitops.o \
	qemu-compat/utils.o qemu-compat/wrapper-main.o qemu-compat/wrapper-qemu.o \
	$(addprefix femu/,$(FEMU_OBJS))

CPPFLAGS := -DFEMU_SIMBRICKS -Iinclude/ $(EXTRA_CPPFLAGS)
CFLAGS := -Wall -Wno-sign-compare -Wno-int-conversion \
	-Wno-implicit-fallthrough -g -O0 $(EXTRA_CFLAGS)
LDFLAGS := -g $(EXTRA_LDFLAGS)
LDLIBS := -lnicif_common -lpthread $(EXTRA_LDLIBS)

all: $(bin_femu)

$(bin_femu): $(OBJS)

clean:
	rm -rf $(bin_femu) $(OBJS)

.PHONY: all clean
