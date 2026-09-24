/*********************************************************************
 * HyperSolar fork: binary telemetry push to the connected ps2client.
 * This file is subject to the terms and conditions of the PS2Link License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 */

#include <types.h>
#include <sysclib.h>
#include <thsemap.h>
#include <loadcore.h>

#include "ps2ip.h"
#include "hostlink.h"
#include "net_fio.h"
#include "tlmPush.h"

extern struct irx_export_table _exp_pkotlm;

// The socket is created by the first push, on the caller's thread, and is
// only ever used under tlm_sema, so callers on different threads serialize.
static int tlm_sock = -1;
static int tlm_sema = -1;

int tlmPushInit(void)
{
    iop_sema_t sema;

    sema.attr = 0;
    sema.option = 0;
    sema.initial = 1;
    sema.max = 1;
    tlm_sema = CreateSema(&sema);
    if (tlm_sema < 0) {
        return -1;
    }
    return RegisterLibraryEntries(&_exp_pkotlm) == 0 ? 0 : -1;
}

int pkoTlmPush(const void *data, int size)
{
    struct sockaddr_in addr;
    int ret;

    // Without a fileio client there is no PC to address; do not broadcast.
    if (data == NULL || size <= 0 || size > PKO_TLM_FRAME_MAX || remote_pc_addr == 0xffffffff) {
        return -1;
    }

    WaitSema(tlm_sema);
    if (tlm_sock < 0) {
        tlm_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    }
    if (tlm_sock < 0) {
        SignalSema(tlm_sema);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PKO_TLM_PORT);
    addr.sin_addr.s_addr = remote_pc_addr;
    ret = sendto(tlm_sock, (void *)data, size, 0, (struct sockaddr *)&addr, sizeof(addr));
    SignalSema(tlm_sema);
    return ret == size ? size : -1;
}
