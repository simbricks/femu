#include <stdbool.h>

#include "simbricks/pcie/proto.h"
#include "../qemu-compat.h"

struct dma_op {
    void *data;
    size_t len;
    uint64_t hwaddr;
    volatile bool ready;
};

enum sb2qemu_type {
    SB2Q_MMIO_READ,
    SB2Q_MMIO_WRITE,
};

struct sb2qemu {
    bool ready;
    uint8_t type;
    union {
        struct {
            uint8_t bar;
            uint8_t len;
            uint64_t addr;
            uint64_t req_id;
        } mmio_read;

        struct {
            uint8_t bar;
            uint8_t len;
            uint64_t addr;
            uint64_t data;
        } mmio_write;
    };
};

enum qemu2sb_type {
    Q2SB_MMIO_READ_COMP,
    Q2SB_DMA_WRITE,
    Q2SB_DMA_READ,
    Q2SB_INT,
};

struct qemu2sb {
    bool ready;
    uint8_t type;
    union {
        struct {
            uint64_t data;
            uint64_t req_id;
        } mmio_read_compl;

        struct {
            struct dma_op *op;
        } dma;

        struct {
            uint16_t vector;
            uint8_t inttype;
        } intr;
    };
};

#define SB2Q_SZ 128
#define Q2SB_SZ 128

extern bool pci_initialized;
extern PCIDevice *pci_dev;
extern struct SimbricksProtoPcieDevIntro dev_intro;

extern volatile bool sb_intx_en;
extern volatile bool sb_msi_en;
extern volatile bool sb_msix_en;

extern volatile struct sb2qemu sb2q_q[SB2Q_SZ];
extern volatile struct qemu2sb q2sb_q[Q2SB_SZ];

int qemu_main_init(void);
void *qemu_main_thread(void *unused);