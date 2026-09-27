/*********************************************************************
 * HyperSolar fork (P4, P5): network controller input.
 * This file is subject to the terms and conditions of the PS2Link License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 *
 * The command listener hands over controller datagrams (hostlink.h) from the
 * fileio PC; each accepted one is DMAed, verbatim, into the EE record the
 * running program registered. Only the header is validated here: the
 * payload belongs to the host modules and the program, so new controllers or
 * payload versions never need a ps2link rebuild. Nothing is queued: the newest
 * datagram replaces the last, and the program derives edges and expiry at its
 * own input boundary.
 */

#include <types.h>
#include <sysclib.h>
#include <stdio.h>
#include <thbase.h>
#include <thsemap.h>
#include <intrman.h>
#include <sifman.h>

#include "ps2ip.h"
#include "hostlink.h"
#include "net_fio.h"
#include "netInput.h"

/* input_sema owns the registration and the DMA source, shared by the
 * listener and the naplink RPC thread. */
static int input_sema = -1;
static unsigned int input_ee_addr = 0;  /* physical EE address, 0 = none */
static PkoInputRecord input_record __attribute__((aligned(16)));
static int input_dma_id = 0;
static unsigned int input_session = 0;
static unsigned int input_last_seq = 0;

/* The source is reused and the destination may be released: the previous
 * transfer must have completed first. */
static void input_dma_wait(void)
{
    while (input_dma_id && sceSifDmaStat(input_dma_id) >= 0)
        DelayThread(100);
    input_dma_id = 0;
}

void netInputRegister(unsigned int ee_addr, unsigned int record_size)
{
    if (input_sema < 0)
        return;
    if (ee_addr != 0 && record_size != sizeof(PkoInputRecord)) {
        printf("IOP: net input: record size %u, expected %u\n",
               record_size, (unsigned int)sizeof(PkoInputRecord));
        return;
    }
    WaitSema(input_sema);
    input_dma_wait();
    input_ee_addr = ee_addr & 0x1fffffff;
    SignalSema(input_sema);
}

static unsigned int rd16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int rd32(const unsigned char *p)
{
    return rd16(p) | (rd16(p + 2) << 16);
}

static void input_publish(const unsigned char *datagram, int len)
{
    struct t_SifDmaTransfer dma;
    int state;

    input_dma_wait();
    input_record.seq_head = input_last_seq;
    memcpy(input_record.datagram, datagram, len);
    memset(input_record.datagram + len, 0, sizeof(input_record.datagram) - len);
    input_record.seq_tail = input_last_seq;

    dma.src = &input_record;
    dma.dest = (void *)input_ee_addr;
    dma.size = sizeof(input_record);
    dma.attr = 0;
    CpuSuspendIntr(&state);
    input_dma_id = sceSifSetDma(&dma, 1);
    CpuResumeIntr(state);
}

static void input_accept(const unsigned char *datagram, int len)
{
    const unsigned int session = rd32(datagram + 8);
    const unsigned int seq = rd32(datagram + 12);

    if (session == 0 || seq == 0)
        return;
    if (session == input_session && seq <= input_last_seq)
        return;  /* duplicate or reordered */
    input_session = session;
    input_last_seq = seq;
    WaitSema(input_sema);
    if (input_ee_addr)
        input_publish(datagram, len);
    SignalSema(input_sema);
}

/* The command listener's thread (cmdHandler.c) owns the socket. */
void netInputAccept(const struct sockaddr_in *from, const unsigned char *datagram, int len)
{
    /* Only the fileio PC may drive the pad; the datagram is complete. */
    if (len < PKO_INPUT_HEADER_SIZE || len > PKO_INPUT_WIRE_MAX ||
        remote_pc_addr == 0xffffffff ||
        from->sin_addr.s_addr != remote_pc_addr ||
        memcmp(datagram, PKO_INPUT_MAGIC, 4) != 0 ||
        rd16(datagram + 6) != (unsigned int)len)
        return;
    input_accept(datagram, len);
}

int netInputInit(void)
{
    iop_sema_t sema;

    sema.attr = 0;
    sema.option = 0;
    sema.initial = 1;
    sema.max = 1;
    input_sema = CreateSema(&sema);
    return input_sema < 0 ? -1 : 0;
}
