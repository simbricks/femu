#include "wrapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

extern void (*type_info_init)(void);

bool pci_initialized = false;
PCIDevice *pci_dev = NULL;
const TypeInfo *type_info = NULL;
PCIDeviceClass pci_dev_class;

static pthread_spinlock_t q2sb_tail_lock;
static size_t q2sb_tail = 0;

uint64_t mr_hw_addr_alloc = 0xf00000000000ULL;

static volatile struct qemu2sb *sb_op_alloc(void);
static void mmio_read(volatile struct sb2qemu *sb2q);
static void mmio_write(volatile struct sb2qemu *sb2q);

struct kvm_vint {
    EventNotifier *en;
    PCIDevice *dev;
    int vector;
    int id;
    struct kvm_vint *next;
};

int vint_id = 0;
struct kvm_vint *vints = NULL;

int qemu_main_init(void)
{
    pthread_spin_init(&q2sb_tail_lock, PTHREAD_PROCESS_PRIVATE);
    type_info_init();
    return 0;
}

void *qemu_main_thread(void *unused)
{
    size_t sb2q_head = 0;
    while (1) {
        volatile struct sb2qemu *sb2q = &sb2q_q[sb2q_head];
        if (sb2q->ready) {
            switch (sb2q->type) {
                case SB2Q_MMIO_READ:
                    mmio_read(sb2q);
                    break;

                case SB2Q_MMIO_WRITE:
                    mmio_write(sb2q);
                    break;

                default:
                    fprintf(stderr, "qmt: unhandled qemu message type: %x\n",
                        sb2q->type);
                    abort();
            }

            sb2q->ready = false;
            sb2q_head = (sb2q_head + 1) % SB2Q_SZ;
        }

        struct kvm_vint *vint = vints;
        while (vint != NULL) {
            if (vint->en && event_notifier_test_and_clear(vint->en)) {
                //fprintf(stderr, "notifying virq %d\n", vint->id);
                msix_notify(vint->dev, vint->vector);
            }
            vint = vint->next;
        }
    }
    return NULL;
}

static volatile struct qemu2sb *sb_op_alloc(void)
{
    pthread_spin_lock(&q2sb_tail_lock);
    volatile struct qemu2sb *q2sb = &q2sb_q[q2sb_tail];
    q2sb_tail = (q2sb_tail + 1) % Q2SB_SZ;
    pthread_spin_unlock(&q2sb_tail_lock);

    while (q2sb->ready);
    
    return q2sb;
}

static void mmio_read(volatile struct sb2qemu *sb2q)
{
    /*fprintf(stderr, "q: mmio_read: enter bar=%u addr=%lx len=%u\n",
        sb2q->mmio_read.bar, sb2q->mmio_read.addr, sb2q->mmio_read.len);*/

    volatile struct qemu2sb *q2sb = sb_op_alloc();

    MemoryRegion *mr = pci_dev->bar_mrs[sb2q->mmio_read.bar];
    q2sb->mmio_read_compl.data = mr->ops->read(mr->opaque, sb2q->mmio_read.addr,
            sb2q->mmio_read.len);
    /*fprintf(stderr, "q: mmio_read: enter bar=%u addr=%lx len=%u data=%lx\n",
        sb2q->mmio_read.bar, sb2q->mmio_read.addr, sb2q->mmio_read.len, 
        q2sb->mmio_read_compl.data);*/
    q2sb->mmio_read_compl.req_id = sb2q->mmio_read.req_id;

    q2sb->type = Q2SB_MMIO_READ_COMP;
    q2sb->ready = true;

    //fprintf(stderr, "q: mmio_read: exit\n");
}

static void mmio_write(volatile struct sb2qemu *sb2q)
{
    /*fprintf(stderr, "q: mmio_write: enter bar=%u addr=%lx len=%u data=%lx\n",
        sb2q->mmio_write.bar, sb2q->mmio_write.addr, sb2q->mmio_write.len,
        sb2q->mmio_write.data);*/

    MemoryRegion *mr = pci_dev->bar_mrs[sb2q->mmio_write.bar];
    mr->ops->write(mr->opaque, sb2q->mmio_write.addr, sb2q->mmio_write.data,
            sb2q->mmio_write.len);

    //fprintf(stderr, "q: mmio_write: exit\n");
}

/******************************************************************************/
/* Qemu Calls */

int64_t qemu_clock_get_ns(QEMUClockType type)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ((int64_t) ts.tv_sec * 1000000000ULL) + ts.tv_nsec;
}

static void props_init(DeviceClass *dc, void *dev)
{
    Property *prop;
    for (prop = dc->props; prop->name; prop++) {
        switch (prop->type) {
            case PROP_T_STR:
                *((const char **) ((uint8_t *) dev) + prop->offset) =
                    prop->def.str;
                break;

            case PROP_T_8:
                *(((uint8_t *) dev) + prop->offset) = prop->def.num;
                break;

            case PROP_T_16:
                *((uint16_t *) (((uint8_t *) dev) + prop->offset)) =
                    prop->def.num;
                break;

            case PROP_T_32:
                *((uint32_t *) (((uint8_t *) dev) + prop->offset)) =
                    prop->def.num;
                break;

            default:
                fprintf(stderr, "props_init: invalid type\n");
                abort();
        }
    }
}

void type_register_static(const TypeInfo *info)
{
    fprintf(stderr, "Registering type\n");
    fprintf(stderr, "Registering type: %s %zu\n", info->name, info->instance_size);
    type_info = info;
    memset(&pci_dev_class, 0, sizeof(pci_dev_class));
    type_info->class_init(&pci_dev_class.parent_class.parent_class, NULL);

    pci_dev = calloc(1, type_info->instance_size);
    props_init(&pci_dev_class.parent_class, pci_dev);

    Error *errp = NULL;
    pci_dev_class.realize(pci_dev, &errp);
    if (errp) {
        fprintf(stderr, "type_register_static: realize failed\n");
        abort();
    }

    pci_initialized = true;
}

/******************************************************************************/

#define STUB_BODY { \
    fprintf(stderr, "%s(): TODO\n", __FUNCTION__);\
    abort(); }

void error_report(const char *fmt, ...)
    STUB_BODY

void qemu_thread_create(QemuThread *thread, const char *name,
                        void *(*start_routine)(void *),
                        void *arg, int mode)
{
    int ret = pthread_create(&thread->pt, NULL, start_routine, arg);
    if (ret) {
        fprintf(stderr, "qemu_thread_create: pthread_create failed\n");
        abort();
    }
}

void *qemu_thread_join(QemuThread *thread)
    STUB_BODY

void device_class_set_props(DeviceClass *dc, Property *props)
{
    dc->props = props;
}

void memory_region_init_io(MemoryRegion *mr,
                           struct Object *owner,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size)
{
    memset(mr, 0, sizeof(*mr));
    mr->addr = mr_hw_addr_alloc;
    mr->size = int128_make64(size);
    mr->opaque = opaque;
    mr->ops = ops;

    mr_hw_addr_alloc += size;
}

void memory_region_unref(MemoryRegion *mr)
    STUB_BODY

uint64_t dma_buf_read(uint8_t *ptr, int32_t len, QEMUSGList *sg)
{
    int i;
    uint64_t dmalen = 0;

    //fprintf(stderr, "dma_buf_read: len=%u  sglen=%zu\n", len, sg->size);

    for (i = 0; i < sg->nsg; i++) {
        dmalen += sg->sg[i].len;
        if (dmalen > len) {
            fprintf(stderr, "dma_buf_read: buffer too small\n");
            abort();
        }

        if (pci_dma_write(sg->pcidev, sg->sg[i].base,
                ptr, sg->sg[i].len) != 0)
            abort();

        ptr += sg->sg[i].len;
    }

    return 0;
}

uint64_t dma_buf_write(uint8_t *ptr, int32_t len, QEMUSGList *sg)
{
    int i;
    uint64_t dmalen = 0;

    //fprintf(stderr, "dma_buf_write: len=%u  sglen=%zu\n", len, sg->size);

    for (i = 0; i < sg->nsg; i++) {
        dmalen += sg->sg[i].len;
        if (dmalen > len) {
            fprintf(stderr, "dma_buf_write: buffer too small\n");
            abort();
        }

        if (pci_dma_read(sg->pcidev, sg->sg[i].base,
                ptr, sg->sg[i].len) != 0)
            abort();

        ptr += sg->sg[i].len;
    }

    return 0;
}

void *dma_memory_map(AddressSpace *as,
                     dma_addr_t addr, dma_addr_t *len,
                     DMADirection dir)
{
    return NULL;
}

int dma_memory_rw(AddressSpace *as, dma_addr_t addr,
                  void *buf, dma_addr_t len,
                  DMADirection dir)
{
    PCIDevice *dev = pci_dev; // FIXME
    if (dir == DMA_DIRECTION_FROM_DEVICE)
        return pci_dma_write(dev, addr, buf, len);
    else
        return pci_dma_read(dev, addr, buf, len);
}

uint16_t pci_get_word(const uint8_t *config)
{
    return *(uint16_t *) config;
}

void pci_set_byte(uint8_t *config, uint8_t val)
{
    *config = val;
}

void pci_set_word(uint8_t *config, uint16_t val)
{
    *(uint16_t *) config = val;
}

void pci_config_set_vendor_id(uint8_t *pci_config, uint16_t val)
{
    pci_set_word(&pci_config[PCI_VENDOR_ID], val);
}

void pci_config_set_device_id(uint8_t *pci_config, uint16_t val)
{
    pci_set_word(&pci_config[PCI_DEVICE_ID], val);
}

void pci_config_set_class(uint8_t *pci_config, uint16_t val)
{
    pci_set_word(&pci_config[PCI_CLASS_DEVICE], val);
}

void pci_config_set_prog_interface(uint8_t *pci_config, uint8_t val)
{
    pci_set_byte(&pci_config[PCI_CLASS_PROG], val);
}

AddressSpace *pci_get_address_space(PCIDevice *dev)
{
    return NULL;
}

void pci_register_bar(PCIDevice *pci_dev, int region_num,
                      uint8_t attr, MemoryRegion *memory)
{
    //fprintf(stderr, "pci_register_bar: %d\n", region_num);

    dev_intro.bars[region_num].len = int128_get64(memory->size);
    dev_intro.bars[region_num].flags =
        SIMBRICKS_PROTO_PCIE_BAR_64;

    pci_dev->bar_mrs[region_num] = memory;
}

void pci_irq_pulse(PCIDevice *pci_dev)
{
    volatile struct qemu2sb *q2sb = sb_op_alloc();
    q2sb->intr.inttype = SIMBRICKS_PROTO_PCIE_INT_LEGACY_HI;
    q2sb->type = Q2SB_INT;
    q2sb->ready = true;

    q2sb = sb_op_alloc();
    q2sb->intr.inttype = SIMBRICKS_PROTO_PCIE_INT_LEGACY_LO;
    q2sb->type = Q2SB_INT;
    q2sb->ready = true;
}

void pci_dma_sglist_init(QEMUSGList *qsg, PCIDevice *dev,
                         int alloc_hint)
{
    memset(qsg, 0, sizeof(*qsg));
    qsg->pcidev = dev;
}

int pci_dma_read(PCIDevice *dev, dma_addr_t addr,
                 void *buf, dma_addr_t len)
{
    //fprintf(stderr, "pci_dma_read: start\n");

    volatile struct qemu2sb *q2sb = sb_op_alloc();
    struct dma_op op;

    op.data = buf;
    op.len = len;
    op.hwaddr = addr;
    op.ready = false;

    q2sb->dma.op = &op;
    q2sb->type = Q2SB_DMA_READ;
    q2sb->ready = true;

    //fprintf(stderr, "pci_dma_read: waiting\n");
    while (!op.ready);
    //fprintf(stderr, "pci_dma_read: done\n");

    return 0;
}

int pci_dma_write(PCIDevice *dev, dma_addr_t addr,
                  const void *buf, dma_addr_t len)
{
    //fprintf(stderr, "pci_dma_write: start\n");

    volatile struct qemu2sb *q2sb = sb_op_alloc();
    struct dma_op op;

    op.data = (void *) buf;
    op.len = len;
    op.hwaddr = addr;
    op.ready = false;

    q2sb->dma.op = &op;
    q2sb->type = Q2SB_DMA_WRITE;
    q2sb->ready = true;

    //fprintf(stderr, "pci_dma_write: waiting\n");
    while (!op.ready);
    //fprintf(stderr, "pci_dma_write: done\n");

    return 0;
}

int pcie_endpoint_cap_init(PCIDevice *dev, uint8_t offset)
{
    return 0;
}


int msi_init(struct PCIDevice *dev, uint8_t offset,
             unsigned int nr_vectors, bool msi64bit,
             bool msi_per_vector_mask, Error **errp)
{
    dev_intro.pci_msi_nvecs = nr_vectors;
    //fprintf(stderr, "msi_init(%u)\n", nr_vectors);
    return 0;
}

bool msi_enabled(const PCIDevice *dev)
{
    return sb_msi_en;
}

void msi_notify(PCIDevice *dev, unsigned int vector)
{
    volatile struct qemu2sb *q2sb = sb_op_alloc();
    q2sb->intr.inttype = SIMBRICKS_PROTO_PCIE_INT_MSI;
    q2sb->intr.vector = vector;
    q2sb->type = Q2SB_INT;
    q2sb->ready = true;
}


int msix_init_exclusive_bar(PCIDevice *dev, unsigned short nentries,
                            uint8_t bar_nr, Error **errp)
{
    size_t bar_size = 4096;
    dev_intro.pci_msix_nvecs = nentries;
    dev_intro.pci_msix_table_bar = bar_nr;
    dev_intro.pci_msix_pba_bar = bar_nr;
    dev_intro.pci_msix_table_offset = 0;
    dev_intro.pci_msix_pba_offset = bar_size / 2;

    dev_intro.bars[bar_nr].len = bar_size;
    dev_intro.bars[bar_nr].flags =
        SIMBRICKS_PROTO_PCIE_BAR_64 | SIMBRICKS_PROTO_PCIE_BAR_DUMMY;

    // todo, no pba?

    //fprintf(stderr, "msix_init_exclusive_bar: bar=%u num=%u\n", bar_nr,
    //    nentries);
    return 0;
}

void msix_uninit_exclusive_bar(PCIDevice *dev)
    STUB_BODY

int msix_enabled(PCIDevice *dev)
{
    return sb_msix_en;
}

bool msix_is_masked(PCIDevice *dev, unsigned vector)
{
    // TODO
    return false;
}

void msix_set_pending(PCIDevice *dev, unsigned vector)
    STUB_BODY

int msix_vector_use(PCIDevice *dev, unsigned vector)
{
    return 0;
}

void msix_vector_unuse(PCIDevice *dev, unsigned vector)
{
}

void msix_notify(PCIDevice *dev, unsigned vector)
{
    volatile struct qemu2sb *q2sb = sb_op_alloc();
    q2sb->intr.inttype = SIMBRICKS_PROTO_PCIE_INT_MSIX;
    q2sb->intr.vector = vector;
    q2sb->type = Q2SB_INT;
    q2sb->ready = true;
}

int msix_set_vector_notifiers(PCIDevice *dev,
                              MSIVectorUseNotifier use_notifier,
                              MSIVectorReleaseNotifier release_notifier,
                              MSIVectorPollNotifier poll_notifier)
{
    if (sb_msix_en) {
        for (uint16_t i = 0; i < dev_intro.pci_msix_nvecs; i++) {
            MSIMessage m;
            m.address = 0;
            m.data = 0;
            use_notifier(dev, i, m);
        }
        //fprintf(stderr, "msix_set_vector_notifiers: msix enabled\n");
    }

    poll_notifier(dev, 0, dev_intro.pci_msix_nvecs);

    return 0;
}

void msix_unset_vector_notifiers(PCIDevice *dev)
{
}


void event_notifier_set_handler(EventNotifier *en,
                                EventNotifierHandler *handler)
{
    assert(handler == NULL);
}

int event_notifier_init(EventNotifier *en, int active)
{
    memset(en, 0, sizeof(*en));
    return 0;
}

void event_notifier_cleanup(EventNotifier *en)
    STUB_BODY

int event_notifier_set(EventNotifier *en)
{
    //fprintf(stderr, "Event Notifier Set!\n");
    en->event_set = true;
    return 0;
}

int event_notifier_test_and_clear(EventNotifier *en)
{
    return __sync_lock_test_and_set(&en->event_set, false);
}

void qemu_iovec_init(QEMUIOVector *qiov, int alloc_hint)
    STUB_BODY

void qemu_iovec_add(QEMUIOVector *qiov, void *base, size_t len)
    STUB_BODY

void qemu_iovec_destroy(QEMUIOVector *qiov)
    STUB_BODY

size_t qemu_iovec_to_buf(QEMUIOVector *qiov, size_t offset,
                         void *buf, size_t bytes)
    STUB_BODY

size_t qemu_iovec_from_buf(QEMUIOVector *qiov, size_t offset,
                           const void *buf, size_t bytes)
    STUB_BODY

void qemu_sglist_destroy(QEMUSGList *qsg)
{
    free(qsg->sg);
    qsg->sg = NULL;
}

void qemu_sglist_add(QEMUSGList *qsg, dma_addr_t base, dma_addr_t len)
{
    void *new_sg = reallocarray(qsg->sg, qsg->nsg + 1, sizeof(*qsg->sg));
    if (!new_sg)
        abort();

    qsg->sg = new_sg;
    int n = qsg->nsg;
    qsg->nsg++;

    qsg->sg[n].base = base;
    qsg->sg[n].len = len;

    qsg->size += len;
}

KVMState *kvm_state = NULL;

int kvm_irqchip_add_msi_route(KVMState *s, int vector, PCIDevice *dev)
{
    //fprintf(stderr, "adding msi intr route\n");
    struct kvm_vint *vint = calloc(1, sizeof(*vint));
    vint->dev = dev;
    vint->vector = vector;
    vint->next = vints;
    vint->id = ++vint_id;
    vints = vint;
    return vint_id;
}

int kvm_irqchip_update_msi_route(KVMState *s, int virq, MSIMessage msg,
                                 PCIDevice *dev)
{
    return 0;
}

void kvm_irqchip_commit_routes(KVMState *s)
{
}

void kvm_irqchip_release_virq(KVMState *s, int virq)
    STUB_BODY

int kvm_irqchip_add_irqfd_notifier_gsi(KVMState *s, EventNotifier *n,
                                       EventNotifier *rn, int virq)
{
    //fprintf(stderr, "setting notifier\n");
    struct kvm_vint *vi;
    for (vi = vints; vi != NULL; vi = vi->next) {
        if (vi->id == virq) {
            vi->en = n;
            return 0;
        }
    }
    //fprintf(stderr, "setting event notifier failed\n");
    return -1;
}

int kvm_irqchip_remove_irqfd_notifier_gsi(KVMState *s, EventNotifier *n,
                                          int virq)
    STUB_BODY
