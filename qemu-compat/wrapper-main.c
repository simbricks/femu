#include "wrapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "simbricks/nicif/nicif.h"
#include "simbricks/pcie/if.h"

struct SimbricksProtoPcieDevIntro dev_intro;
static struct SimbricksNicIf nicif;

volatile struct qemu2sb q2sb_q[Q2SB_SZ];
volatile struct sb2qemu sb2q_q[SB2Q_SZ];
static size_t sb2q_tail = 0;

volatile bool sb_intx_en = false;
volatile bool sb_msi_en = false;
volatile bool sb_msix_en = false;


static void translate_dev_intro(void)
{
    dev_intro.pci_vendor_id = pci_get_word(pci_dev->config + PCI_VENDOR_ID);
    dev_intro.pci_device_id = pci_get_word(pci_dev->config + PCI_DEVICE_ID);
    dev_intro.pci_class = pci_dev->config[PCI_CLASS_DEVICE + 1];
    dev_intro.pci_subclass = pci_dev->config[PCI_CLASS_DEVICE];
    dev_intro.pci_revision = pci_dev->config[PCI_REVISION_ID];
    dev_intro.pci_progif = pci_dev->config[PCI_CLASS_PROG];
}

static void mmio_read(volatile struct SimbricksProtoPcieH2DRead *r)
{
    //fprintf(stderr, "sb: mmio_read: enter (%ld)\n", r->req_id);

    volatile struct sb2qemu *sb2q = &sb2q_q[sb2q_tail];
    while (sb2q->ready);

    sb2q->type = SB2Q_MMIO_READ;
    sb2q->mmio_read.req_id = r->req_id;
    sb2q->mmio_read.bar = r->bar;
    sb2q->mmio_read.len = r->len;
    sb2q->mmio_read.addr = r->offset;

    // TODO: barrier
    sb2q->ready = true;
    sb2q_tail = (sb2q_tail + 1) % SB2Q_SZ;

    //fprintf(stderr, "sb: mmio_read: exit\n");
}

static void mmio_write(volatile struct SimbricksProtoPcieH2DWrite *w)
{
    //fprintf(stderr, "sb: mmio_write: enter\n");

    volatile union SimbricksProtoPcieD2H *d2h;
    volatile struct SimbricksProtoPcieD2HWritecomp *wc;

    // send completion immediately
    while ((d2h = SimbricksPcieIfD2HOutAlloc(&nicif.pcie, 0)) == NULL);
    wc = &d2h->writecomp;
    wc->req_id = w->req_id;

    SimbricksPcieIfD2HOutSend(&nicif.pcie, d2h, SIMBRICKS_PROTO_PCIE_D2H_MSG_WRITECOMP);

    volatile struct sb2qemu *sb2q = &sb2q_q[sb2q_tail];
    while (sb2q->ready);

    sb2q->type = SB2Q_MMIO_WRITE;
    sb2q->mmio_write.bar = w->bar;
    sb2q->mmio_write.len = w->len;
    sb2q->mmio_write.addr = w->offset;
    sb2q->mmio_write.data = 0;
    assert(w->len <= sizeof(sb2q->mmio_write.data));
    memcpy((void *) &sb2q->mmio_write.data, (const void *) w->data, w->len);

    // TODO: barrier
    sb2q->ready = true;
    sb2q_tail = (sb2q_tail + 1) % SB2Q_SZ;

    //fprintf(stderr, "sb: mmio_write: exit\n");
}

static void dma_read_comp(volatile struct SimbricksProtoPcieH2DReadcomp *rc)
{
    struct dma_op *op = (void *) rc->req_id;
    //fprintf(stderr, "dma_read_comp op=%p\n", op);
    memcpy(op->data, (const void *) rc->data, op->len);

    op->ready = true;
}

static void dma_write_comp(volatile struct SimbricksProtoPcieH2DWritecomp *wc)
{
    //fprintf(stderr, "dma_write_comp\n");
}

static void *simbricks_thread(void *unused)
{
    size_t q2sb_head = 0;

    while (1) {
        volatile union SimbricksProtoPcieH2D *msg =
            SimbricksPcieIfH2DInPoll(&nicif.pcie, 0);
        if (msg) {
            // uint8_t type = msg->base.header.own_type & SIMBRICKS_PROTO_PCIE_H2D_MSG_MASK;
            uint8_t type = SimbricksPcieIfH2DInType(&nicif.pcie, msg);
            switch (type) {
                case SIMBRICKS_PROTO_PCIE_H2D_MSG_READ:
                    mmio_read(&msg->read);
                    break;

                case SIMBRICKS_PROTO_PCIE_H2D_MSG_WRITE:
                    mmio_write(&msg->write);
                    break;

                case SIMBRICKS_PROTO_PCIE_H2D_MSG_DEVCTRL:
                    sb_intx_en = msg->devctrl.flags &
                        SIMBRICKS_PROTO_PCIE_CTRL_INTX_EN;
                    sb_msi_en = msg->devctrl.flags &
                        SIMBRICKS_PROTO_PCIE_CTRL_MSI_EN;
                    sb_msix_en = msg->devctrl.flags &
                        SIMBRICKS_PROTO_PCIE_CTRL_MSIX_EN;
                    break;

                case SIMBRICKS_PROTO_PCIE_H2D_MSG_READCOMP:
                    dma_read_comp(&msg->readcomp);
                    break;

                case SIMBRICKS_PROTO_PCIE_H2D_MSG_WRITECOMP:
                    dma_write_comp(&msg->writecomp);
                    break;

                default:
                    fprintf(stderr, "sbt: unhandled simbricks message type: %x\n",
                        type);
                    abort();
            }

            SimbricksPcieIfH2DInDone(&nicif.pcie, msg);
        }

        volatile struct qemu2sb *q2sb = &q2sb_q[q2sb_head];
        if (q2sb->ready) {
            volatile union SimbricksProtoPcieD2H *d2h;
            struct dma_op *op;
            while ((d2h = SimbricksPcieIfD2HOutAlloc(&nicif.pcie, 0)) == NULL);

            switch (q2sb->type) {
                case Q2SB_MMIO_READ_COMP:
                    /*fprintf(stderr, "sbt: read compl id=%ld\n",
                        q2sb->mmio_read_compl.req_id);*/
                    d2h->readcomp.req_id = q2sb->mmio_read_compl.req_id;
                    *((volatile uint64_t *) d2h->readcomp.data) =
                        q2sb->mmio_read_compl.data;
                    // d2h->readcomp.own_type =
                    //     SIMBRICKS_PROTO_PCIE_D2H_MSG_READCOMP |
                    //     SIMBRICKS_PROTO_PCIE_D2H_OWN_HOST;
                    SimbricksPcieIfD2HOutSend(&nicif.pcie, d2h, SIMBRICKS_PROTO_PCIE_D2H_MSG_READCOMP);
                    break;

                case Q2SB_DMA_READ:
                    op = q2sb->dma.op;
                    //fprintf(stderr, "sbt: dma read op=%p addr=%lx len=%zu\n", op, op->hwaddr, op->len);
                    d2h->read.req_id = (uint64_t) op;
                    d2h->read.offset = op->hwaddr;
                    d2h->read.len = op->len;
                    // d2h->readcomp.own_type =
                    //     SIMBRICKS_PROTO_PCIE_D2H_MSG_READ |
                    //     SIMBRICKS_PROTO_PCIE_D2H_OWN_HOST;
                    SimbricksPcieIfD2HOutSend(&nicif.pcie, d2h, SIMBRICKS_PROTO_PCIE_D2H_MSG_READ);
                    break;

                case Q2SB_DMA_WRITE:
                    op = q2sb->dma.op;
                    //fprintf(stderr, "sbt: dma write op=%p addr=%lx len=%zu\n", op, op->hwaddr, op->len);
                    d2h->write.req_id = 0;
                    d2h->write.offset = op->hwaddr;
                    d2h->write.len = op->len;
                    assert(sizeof(d2h) + op->len <= nicif.pcie.base.out_elen);
                    memcpy((void *) d2h->write.data, op->data, op->len);

                    op->ready = true;

                    // d2h->readcomp.own_type =
                    //     SIMBRICKS_PROTO_PCIE_D2H_MSG_WRITE |
                    //     SIMBRICKS_PROTO_PCIE_D2H_OWN_HOST;
                    SimbricksPcieIfD2HOutSend(&nicif.pcie, d2h, SIMBRICKS_PROTO_PCIE_D2H_MSG_WRITE);
                    break;

                case Q2SB_INT:
                    //fprintf(stderr, "sbt: interrupt vec=%u ty=%u\n",
                    //        q2sb->intr.vector, q2sb->intr.inttype);
                    d2h->interrupt.vector = q2sb->intr.vector;
                    d2h->interrupt.inttype = q2sb->intr.inttype;
                    // d2h->interrupt.own_type =
                    //     SIMBRICKS_PROTO_PCIE_D2H_MSG_INTERRUPT |
                    //     SIMBRICKS_PROTO_PCIE_D2H_OWN_HOST;
                    SimbricksPcieIfD2HOutSend(&nicif.pcie, d2h, SIMBRICKS_PROTO_PCIE_D2H_MSG_INTERRUPT);
                    break;

                default:
                    fprintf(stderr, "sbt: unhandled qemu message type: %x\n",
                        q2sb->type);
                    abort();
            }

            q2sb->ready = false;
            q2sb_head = (q2sb_head + 1) % Q2SB_SZ;
            //fprintf(stderr, "sbt: qemu msg done\n");
        }

    }
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s PCI-SOCKET SHM\n", argv[0]);
        return EXIT_FAILURE;
    }

    

    struct SimbricksBaseIfParams params;
    SimbricksPcieIfDefaultParams(&params);
    params.sync_mode = kSimbricksBaseIfSyncDisabled;
    params.sock_path = argv[1];
    const char *shmPath = argv[2];
    qemu_main_init();
    translate_dev_intro();

    if (SimbricksNicIfInit(&nicif, shmPath, NULL, &params, &dev_intro) != 0) {
        fprintf(stderr, "NicIf Init failed\n");
        return EXIT_FAILURE;
    }

    pthread_t simbricks_thr;
    pthread_create(&simbricks_thr, NULL, simbricks_thread, NULL);

    qemu_main_thread(NULL);
    return 0;
}
