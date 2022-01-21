#ifndef QEMU_COMPAT_H_
#define QEMU_COMPAT_H_

#include <assert.h>
#include <inttypes.h>
#include <malloc.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "qemu-compat/int128.h"
#include "qemu-compat/pci_regs.h"

#define KiB (1ULL << 10)
#define MiB (1ULL << 20)
#define GiB (1ULL << 30)
#define TiB (1ULL << 40)
#define PiB (1ULL << 50)
#define EiB (1ULL << 60)

#define QEMU_PACKED __attribute__((packed))
#define likely(x) __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define g_malloc malloc
#define g_malloc0(x) calloc(1, (x))
#define g_try_malloc0(x) g_malloc0(x)
#define g_malloc0_n(n, x) calloc((n), (x))
#define g_new0(t, n) ((t *) calloc((n), sizeof(t)))
#define g_free free
#define g_realloc realloc
#define qemu_memalign memalign

// LITTLE ENDIAN ARCH ONLY
//#define le8_to_cpu(x) (x)
#define le16_to_cpu(x) (x)
#define le32_to_cpu(x) (x)
#define le64_to_cpu(x) (x)
//#define cpu_to_le8(x) (x)
#define cpu_to_le16(x) (x)
#define cpu_to_le32(x) (x)
#define cpu_to_le64(x) (x)

#define cat(x,y) x ## y
#define cat2(x,y) cat(x,y)
#define QEMU_BUILD_BUG_ON(x) \
    typedef char cat2(qemu_build_bug_on__,__LINE__)[(x)?-1:1] __attribute__((unused));

#define container_of(ptr, type, member) ({                      \
        const typeof(((type *) 0)->member) *__mptr = (ptr);     \
        (type *) ((char *) __mptr - offsetof(type, member));})

#define OBJECT_CHECK(type, ptr, tyname) ((type *) (ptr))

#define BDRV_SECTOR_BITS 9

typedef uint64_t dma_addr_t;
typedef uint64_t hwaddr;

typedef struct Error Error;

#include "qemu-compat/atomic.h"
#include "qemu-compat/bitops.h"
#include "qemu-compat/bitmap.h"
#include "qemu-compat/queue.h"
#include "qemu-compat/pci_regs.h"
#include "qemu-compat/pci_ids.h"




typedef struct Property {
    const char *name;
    size_t offset;
    enum PropType {
        PROP_T_STR,
        PROP_T_8,
        PROP_T_16,
        PROP_T_32,
    } type;
    union {
        const char *str;
        uint64_t num;
    } def;
} Property;

typedef struct MSIMessage {
    uint64_t address;
    uint32_t data;
} MSIMessage;

typedef struct MemoryRegionOps {
    uint64_t (*read)(void *opaque, hwaddr addr, unsigned size);
    void (*write)(void *opaque, hwaddr addr, uint64_t data, unsigned size);
    enum device_endian {
        DEVICE_LITTLE_ENDIAN,
    } endianness;
    struct {
        unsigned min_access_size;
        unsigned max_access_size;
    } impl;
} MemoryRegionOps;

typedef struct MemoryRegion {
    hwaddr addr;
    Int128 size;

    void *opaque;
    const MemoryRegionOps *ops;
} MemoryRegion;


typedef struct PCIDevice {
    uint8_t config[PCI_CFG_SPACE_EXP_SIZE];
    MemoryRegion *bar_mrs[PCI_STD_NUM_BARS];
} PCIDevice;

typedef struct QemuUUID {
    uint8_t data[16];
} QemuUUID;

typedef struct EventNotifier {
    volatile bool event_set;
} EventNotifier;

typedef struct QemuThread {
    pthread_t pt;
} QemuThread;

typedef struct QEMUTimer {
} QEMUTimer;

typedef struct AddressSpace {
} AddressSpace;

typedef struct VMStateDescription {
    const char *name;
    bool unmigratable;
} VMStateDescription;

typedef struct Object {
} Object;
#define OBJECT(o) ((Object *) (o))

typedef struct ObjectClass {
} ObjectClass;

enum device_category {
    DEVICE_CATEGORY_STORAGE,
    DEVICE_CATEGORY_MAX,
};

typedef struct DeviceClass {
    ObjectClass parent_class;
    const char *desc;
    const VMStateDescription *vmsd;
    DECLARE_BITMAP(categories, DEVICE_CATEGORY_MAX);

    Property *props;
} DeviceClass;
#define DEVICE_CLASS(oc) ((DeviceClass *) (oc))

typedef struct PCDeviceClass {
    DeviceClass parent_class;

    void (*realize)(PCIDevice *dev, Error **errp);
    void (*exit)(PCIDevice *pci_dev);

    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t revision;
    uint16_t class_id;
} PCIDeviceClass;
#define PCI_DEVICE_CLASS(oc) ((PCIDeviceClass *) (oc))

#define INTERFACE_PCIE_DEVICE "pcie-device"
typedef struct InterfaceInfo {
    const char *type;
} InterfaceInfo;


#define type_init(ti) void (*type_info_init)(void) = ti;
#define TYPE_PCI_DEVICE "pci-device"
typedef struct TypeInfo {
    const char *name;
    const char *parent;
    size_t instance_size;

    void (*class_init)(ObjectClass *klass, void *data);

    InterfaceInfo *interfaces;
} TypeInfo;


#define DEFINE_PROP_STRING(n, ty, field) \
    { .name = n, .offset = offsetof(ty, field), .type = PROP_T_STR, \
      .def.str = NULL, }
#define DEFINE_PROP_UINT8(n, ty, field, d) \
    { .name = n, .offset = offsetof(ty, field), .type = PROP_T_8, \
      .def.num = d, }
#define DEFINE_PROP_UINT16(n, ty, field, d) \
    { .name = n, .offset = offsetof(ty, field), .type = PROP_T_16, \
      .def.num = d, }
#define DEFINE_PROP_UINT32(n, ty, field, d) \
    { .name = n, .offset = offsetof(ty, field), .type = PROP_T_32, \
      .def.num = d, }
#define DEFINE_PROP_END_OF_LIST() \
    { .name = NULL, }

typedef struct QEMUIOVector {
    struct iovec *iov;
    int niov;
    int nalloc;
    size_t size;
} QEMUIOVector;


typedef enum {
    DMA_DIRECTION_TO_DEVICE = 0,
    DMA_DIRECTION_FROM_DEVICE = 1,
} DMADirection;

typedef struct ScatterGatherEntry {
    dma_addr_t base;
    dma_addr_t len;
} ScatterGatherEntry;

typedef struct QEMUSGList {
    ScatterGatherEntry *sg;
    int nsg;
    /*int nalloc;*/
    size_t size;
    /*DeviceState *dev;*/
    AddressSpace *as;
    PCIDevice *pcidev;
} QEMUSGList;

#define QEMU_THREAD_JOINABLE 1

void strpadcpy(char *buf, int buf_size, const char *str, char pad);
size_t g_strlcpy(char *dest, const char *src, size_t dest_size);
char* g_strdup_printf(const char *format, ...);

/******************************************************************************/
/* Stubs */

typedef struct KVMState {
} KVMState;

extern KVMState *kvm_state;

int kvm_irqchip_add_msi_route(KVMState *s, int vector, PCIDevice *dev);
int kvm_irqchip_update_msi_route(KVMState *s, int virq, MSIMessage msg,
                                 PCIDevice *dev);
void kvm_irqchip_commit_routes(KVMState *s);
void kvm_irqchip_release_virq(KVMState *s, int virq);
int kvm_irqchip_add_irqfd_notifier_gsi(KVMState *s, EventNotifier *n,
                                       EventNotifier *rn, int virq);
int kvm_irqchip_remove_irqfd_notifier_gsi(KVMState *s, EventNotifier *n,
                                          int virq);


/******************************************************************************/
/* Defined in simbricks adapter */

void error_report(const char *fmt, ...);

void type_register_static(const TypeInfo *info);
void device_class_set_props(DeviceClass *dc, Property *props);

typedef enum {
    QEMU_CLOCK_REALTIME = 0,
} QEMUClockType;
int64_t qemu_clock_get_ns(QEMUClockType type);

void qemu_thread_create(QemuThread *thread, const char *name,
                        void *(*start_routine)(void *),
                        void *arg, int mode);
void *qemu_thread_join(QemuThread *thread);

void memory_region_init_io(MemoryRegion *mr,
                           struct Object *owner,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size);
void memory_region_unref(MemoryRegion *mr);

uint64_t dma_buf_read(uint8_t *ptr, int32_t len, QEMUSGList *sg);
uint64_t dma_buf_write(uint8_t *ptr, int32_t len, QEMUSGList *sg);
void *dma_memory_map(AddressSpace *as,
                     dma_addr_t addr, dma_addr_t *len,
                     DMADirection dir);
int dma_memory_rw(AddressSpace *as, dma_addr_t addr,
                  void *buf, dma_addr_t len,
                  DMADirection dir);

uint16_t pci_get_word(const uint8_t *config);
void pci_set_byte(uint8_t *config, uint8_t val);
void pci_set_word(uint8_t *config, uint16_t val);
void pci_config_set_vendor_id(uint8_t *pci_config, uint16_t val);
void pci_config_set_device_id(uint8_t *pci_config, uint16_t val);
void pci_config_set_class(uint8_t *pci_config, uint16_t val);
void pci_config_set_prog_interface(uint8_t *pci_config, uint8_t val);
AddressSpace *pci_get_address_space(PCIDevice *dev);
void pci_register_bar(PCIDevice *pci_dev, int region_num,
                      uint8_t attr, MemoryRegion *memory);
void pci_irq_pulse(PCIDevice *pci_dev);
void pci_dma_sglist_init(QEMUSGList *qsg, PCIDevice *dev,
                         int alloc_hint);
int pci_dma_read(PCIDevice *dev, dma_addr_t addr,
                 void *buf, dma_addr_t len);
int pci_dma_write(PCIDevice *dev, dma_addr_t addr,
                  const void *buf, dma_addr_t len);
int pcie_endpoint_cap_init(PCIDevice *dev, uint8_t offset);

typedef int (*MSIVectorUseNotifier)(PCIDevice *dev, unsigned int vector,
                                      MSIMessage msg);
typedef void (*MSIVectorReleaseNotifier)(PCIDevice *dev, unsigned int vector);
typedef void (*MSIVectorPollNotifier)(PCIDevice *dev,
                                      unsigned int vector_start,
                                      unsigned int vector_end);

int msi_init(struct PCIDevice *dev, uint8_t offset,
             unsigned int nr_vectors, bool msi64bit,
             bool msi_per_vector_mask, Error **errp);
bool msi_enabled(const PCIDevice *dev);
void msi_notify(PCIDevice *dev, unsigned int vector);

int msix_init_exclusive_bar(PCIDevice *dev, unsigned short nentries,
                            uint8_t bar_nr, Error **errp);
void msix_uninit_exclusive_bar(PCIDevice *dev);
int msix_enabled(PCIDevice *dev);
bool msix_is_masked(PCIDevice *dev, unsigned vector);
void msix_set_pending(PCIDevice *dev, unsigned vector);
int msix_vector_use(PCIDevice *dev, unsigned vector);
void msix_vector_unuse(PCIDevice *dev, unsigned vector);
void msix_notify(PCIDevice *dev, unsigned vector);
int msix_set_vector_notifiers(PCIDevice *dev,
                              MSIVectorUseNotifier use_notifier,
                              MSIVectorReleaseNotifier release_notifier,
                              MSIVectorPollNotifier poll_notifier);
void msix_unset_vector_notifiers(PCIDevice *dev);

typedef void EventNotifierHandler(EventNotifier *);
void event_notifier_set_handler(EventNotifier *en,
                                EventNotifierHandler *handler);
int event_notifier_init(EventNotifier *en, int active);
void event_notifier_cleanup(EventNotifier *en);
int event_notifier_set(EventNotifier *en);
int event_notifier_test_and_clear(EventNotifier *en);

void qemu_iovec_init(QEMUIOVector *qiov, int alloc_hint);
void qemu_iovec_add(QEMUIOVector *qiov, void *base, size_t len);
void qemu_iovec_destroy(QEMUIOVector *qiov);
size_t qemu_iovec_to_buf(QEMUIOVector *qiov, size_t offset,
                         void *buf, size_t bytes);
size_t qemu_iovec_from_buf(QEMUIOVector *qiov, size_t offset,
                           const void *buf, size_t bytes);

void qemu_sglist_destroy(QEMUSGList *qsg);
void qemu_sglist_add(QEMUSGList *qsg, dma_addr_t base, dma_addr_t len);

#endif // ifndef QEMU_COMPAT_H_
