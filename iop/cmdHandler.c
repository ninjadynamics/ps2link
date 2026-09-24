/*********************************************************************
 * Copyright (C) 2003 Tord Lindstrom (pukko@home.se)
 * Copyright (C) 2003,2004 adresd (adresd_ps2dev@yahoo.com)
 * This file is subject to the terms and conditions of the PS2Link License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 */

#include <types.h>
#include <ioman.h>
#include <sysclib.h>
#include <stdio.h>
#include <thbase.h>
#include <intrman.h>
#include <sifman.h>
#include <sifrpc.h>
#include <modload.h>
#include <ps2lib_err.h>
#include <poweroff.h>
#include <sifcmd.h>

#include "ps2ip.h"
#include "hostlink.h"
#include "net_fsys.h"
#include "globals.h"

#define BUF_SIZE 1024
static char recvbuf[BUF_SIZE] __attribute__((aligned(16)));
static unsigned int rpc_data[1024 / 4] __attribute__((aligned(16)));

int excepscrdump = 1;

// Fork state. The EE passes the generation at module load; ee_ready and the
// EXECEE2 result are published by the EE through the naplink RPC thread and
// read by the command listener, which alone owns the UDP socket.
static unsigned int boot_generation = 0;
static volatile int ee_ready = 0;
static unsigned int exec_forward_id = 0;
static volatile unsigned int exec_result_id = 0;
static volatile int exec_result_status = 0;

#define PKO_DMA_DEST ((void *)0x200ff800)
//unsigned int *dma_ptr =(unsigned int*)(0x20100000-2048);

//////////////////////////////////////////////////////////////////////////
static void
pkoExecIop(char *buf, int len)
{
    pko_pkt_execiop_req *cmd;
    int retval;
    int arglen;
    char *path;
    char *args;
    unsigned int argc;
    int id;
    int i;

    cmd = (pko_pkt_execiop_req *)buf;

    dbgprintf("IOP cmd: EXECIOP\n");

    if (len != sizeof(pko_pkt_execiop_req)) {
        dbgprintf("IOP cmd: exec_iop got a broken packet (%d)!\n", len);
        return;
    }

    // Make sure arg vector is null-terminated
    cmd->argv[PKO_MAX_PATH - 1] = '\0';

    printf("IOP cmd: %ld args\n", ntohl(cmd->argc));

    path = &cmd->argv[0];
    args = &cmd->argv[strlen(cmd->argv) + 1];
    argc = ntohl(cmd->argc);

    printf("IOP binary path: %s\n", path);

    arglen = 0;
    for (i = 0; i < (argc - 1); i++) {
        printf("arg %d: %s (%d)\n", i, &args[arglen], arglen);
        arglen += strlen(&args[arglen]) + 1;
    }

    id = LoadStartModule(cmd->argv, arglen, args, &retval);

    if (id < 0) {
        printf("Error loading module: ");
        switch (-id) {
            case E_IOP_INTR_CONTEXT:
                printf("IOP is in exception context.\n");
                break;
            case E_IOP_DEPENDANCY:
                printf("Inter IRX dependancy error.\n");
                break;
            case E_LF_NOT_IRX:
                printf("Invalid IRX module.\n");
                break;
            case E_LF_FILE_NOT_FOUND:
                printf("Unable to open executable file.\n");
                break;
            case E_LF_FILE_IO_ERROR:
                printf("Error while accessing file.\n");
                break;
            case E_IOP_NO_MEMORY:
                printf("IOP is out of memory.\n");
                break;
            default:
                printf("Unknow error code: %d\n", -retval);
                break;
        }
    } else {
        printf("loadmodule: id %d, ret %d\n", id, retval);
    }
}

//////////////////////////////////////////////////////////////////////////
unsigned int
pkoSetSifDma(void *dest, void *src, unsigned int length, unsigned int mode)
{
    struct t_SifDmaTransfer sendData;
    int oldIrq;
    int id;

    sendData.src = (unsigned int *)src;
    sendData.dest = (unsigned int *)dest;
    sendData.size = length;
    sendData.attr = mode;

    CpuSuspendIntr(&oldIrq);
    id = sceSifSetDma(&sendData, 1);
    CpuResumeIntr(oldIrq);

    return id;
}

//////////////////////////////////////////////////////////////////////////
unsigned int
pkoSendSifCmd(unsigned int cmd, void *src, unsigned int len)
{
    unsigned int dmaId;
    unsigned int dmaLen;

    rpc_data[0] = cmd;

    memcpy(&rpc_data[1], src,
           (len > sizeof(rpc_data) ? sizeof(rpc_data) : len));

    len = len > sizeof(rpc_data) ? sizeof(rpc_data) : len;

    /* Round DMA size up to 16-byte boundary — real HW SIF DMA
       requires aligned transfer sizes.  rpc_data is 1KB so the
       extra bytes are just trailing zeros. */
    dmaLen = (len + 15) & ~15;

    dmaId = pkoSetSifDma(PKO_DMA_DEST, rpc_data, dmaLen, 4);

    if (dmaId == 0) {
        printf("IOP: sifSendCmd %x failed\n", cmd);
        return -1;
    }
    return 0;
}


//////////////////////////////////////////////////////////////////////////
static void
pkoExecEE(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_EXECEE, buf, len);
};
//////////////////////////////////////////////////////////////////////////
static void
pkoGSExec(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_GSEXEC, buf, len);
};
//////////////////////////////////////////////////////////////////////////
static void
pkoNetDump(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_NETDUMP, buf, len);
};
//////////////////////////////////////////////////////////////////////////
static void
pkoScrDump(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_SCRDUMP, buf, len);
};

//////////////////////////////////////////////////////////////////////////
static void
pkoPowerOff()
{
    PoweroffShutdown();
}

//////////////////////////////////////////////////////////////////////////
static void
pkoReset(char *buf, int len)
{
    dbgprintf("IOP cmd: RESET\n");

    if (len != sizeof(pko_pkt_reset_req)) {
        dbgprintf("IOP cmd: exec_ee got a broken packet (%d)!\n", len);
        return;
    }

    printf("unmounting\n");
    fsysUnmount();
    printf("unmounted\n");
    DelDrv("tty");

    pkoSendSifCmd(PKO_RPC_RESET, buf, len);
};

static void
pkoStopVU(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_STOPVU, buf, len);
};

static void
pkoStartVU(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_STARTVU, buf, len);
};

static void
pkoDumpMem(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_DUMPMEM, buf, len);
};

static void
pkoDumpReg(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_DUMPREG, buf, len);
};

static void
pkoWriteMem(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_WRITEMEM, buf, len);
};

//////////////////////////////////////////////////////////////////////////
void
cmdHandlerSetGeneration(unsigned int generation)
{
    boot_generation = generation;
}

// Called from the naplink RPC thread once the EE command handler is live.
// Until then a forwarded command could be cleared by its initialization.
void
cmdHandlerEeReady(void)
{
    ee_ready = 1;
}

// Called from the naplink RPC thread with the EE's EXECEE2 decision.
// The status is published before the ID that makes it visible.
void
cmdHandlerExecResult(unsigned int id, int status)
{
    exec_result_status = status;
    exec_result_id = id;
}

static void
pkoVersion(int sock, struct sockaddr_in *remote_addr)
{
    pko_pkt_version_rly reply;

    reply.cmd = htonl(PKO_VERSION_RLY);
    reply.len = htons(sizeof(reply));
    reply.protocol = htonl(PKO_HS_PROTOCOL);
    reply.marker = htonl(PKO_HS_MARKER);
    reply.features = htonl(PKO_HS_FEATURE_EXECEE2 | PKO_HS_FEATURE_RESET2 | PKO_HS_FEATURE_TLM_PUSH);
    reply.generation = htonl(boot_generation);
    reply.ee_ready = htonl(ee_ready);
    sendto(sock, &reply, sizeof(reply), 0, (struct sockaddr *)remote_addr, sizeof(*remote_addr));
}

static void
pkoExecEE2(int sock, struct sockaddr_in *remote_addr, char *buf, int len)
{
    pko_pkt_execee2_req *cmd = (pko_pkt_execee2_req *)buf;
    pko_pkt_execee2_rly reply;
    unsigned int id;

    if (len != sizeof(pko_pkt_execee2_req) || !ee_ready) {
        return;
    }

    id = ntohl(cmd->id);
    if (id == 0) {
        return;
    }

    // A retransmission of a decided request gets the EE's answer again.
    if (id == exec_result_id) {
        reply.cmd = htonl(PKO_EXECEE2_RLY);
        reply.len = htons(sizeof(reply));
        reply.id = htonl(id);
        reply.status = htonl(exec_result_status);
        sendto(sock, &reply, sizeof(reply), 0, (struct sockaddr *)remote_addr, sizeof(*remote_addr));
        return;
    }

    // One forward per ID: the EE executes a request at most once.
    if (id == exec_forward_id) {
        return;
    }
    exec_forward_id = id;
    cmd->argv[PKO_MAX_PATH - 1] = '\0';
    pkoSendSifCmd(PKO_RPC_EXECEE, buf, len);
}

static void
pkoReset2(int sock, struct sockaddr_in *remote_addr, char *buf, int len)
{
    pko_pkt_reset2_req *cmd = (pko_pkt_reset2_req *)buf;
    pko_pkt_reset2_rly reply;
    pko_pkt_reset_req reset;
    int accepted;

    if (len != sizeof(pko_pkt_reset2_req)) {
        return;
    }

    // Only the named generation resets; a late retransmission reaching the
    // successor reports that successor's generation instead.
    accepted = ntohl(cmd->generation) == boot_generation;
    reply.cmd = htonl(PKO_RESET2_RLY);
    reply.len = htons(sizeof(reply));
    reply.generation = htonl(boot_generation);
    reply.accepted = htonl(accepted);
    sendto(sock, &reply, sizeof(reply), 0, (struct sockaddr *)remote_addr, sizeof(*remote_addr));

    if (accepted) {
        reset.cmd = htonl(PKO_RESET_CMD);
        reset.len = htons(sizeof(reset));
        pkoReset((char *)&reset, sizeof(reset));
    }
}

//////////////////////////////////////////////////////////////////////////
static void
cmdListener(int sock)
{
    while (1) {
        struct sockaddr_in remote_addr;
        int len;
        int addrlen;
        pko_pkt_hdr *header;
        unsigned int cmd;

        addrlen = sizeof(remote_addr);
        len = recvfrom(sock, &recvbuf[0], BUF_SIZE, 0,
                       (struct sockaddr *)&remote_addr,
                       &addrlen);
        dbgprintf("IOP cmd: received packet (%d)\n", len);

        if (len < 0) {
            dbgprintf("IOP: cmdListener: recvfrom error (%d)\n", len);
            continue;
        }
        if (len < sizeof(pko_pkt_hdr)) {
            continue;
        }

        header = (pko_pkt_hdr *)recvbuf;
        cmd = ntohl(header->cmd);
        switch (cmd) {

            case PKO_EXECIOP_CMD:
                pkoExecIop(recvbuf, len);
                break;
            case PKO_EXECEE_CMD:
                pkoExecEE(recvbuf, len);
                break;
            case PKO_POWEROFF_CMD:
                pkoPowerOff();
                break;
            case PKO_RESET_CMD:
                pkoReset(recvbuf, len);
                break;
            case PKO_SCRDUMP_CMD:
                excepscrdump = 1;
                pkoScrDump(recvbuf, len);
                break;
            case PKO_NETDUMP_CMD:
                excepscrdump = 0;
                pkoNetDump(recvbuf, len);
                break;
            case PKO_START_VU:
                pkoStartVU(recvbuf, len);
                break;
            case PKO_STOP_VU:
                pkoStopVU(recvbuf, len);
                break;
            case PKO_DUMP_MEM:
                pkoDumpMem(recvbuf, len);
                break;
            case PKO_DUMP_REG:
                pkoDumpReg(recvbuf, len);
                break;
            case PKO_GSEXEC_CMD:
                pkoGSExec(recvbuf, len);
                break;
            case PKO_WRITE_MEM:
                pkoWriteMem(recvbuf, len);
                break;
            case PKO_VERSION_CMD:
                pkoVersion(sock, &remote_addr);
                break;
            case PKO_EXECEE2_CMD:
                pkoExecEE2(sock, &remote_addr, recvbuf, len);
                break;
            case PKO_RESET2_CMD:
                pkoReset2(sock, &remote_addr, recvbuf, len);
                break;
            default:
                dbgprintf("IOP cmd: Uknown cmd received\n");
                break;
        }
        dbgprintf("IOP cmd: waiting for next pkt\n");
    }
}

static void
cmdPowerOff(void *arg)
{
#ifdef PWOFFONRESET
    pkoPowerOff();
#else
    pko_pkt_reset_req reset;

    reset.cmd = htonl(PKO_RESET_CMD);
    reset.len = 0;

    pkoReset((char *)&reset, sizeof(reset));
#endif
}

//////////////////////////////////////////////////////////////////////////
static void
cmdThread(void *arg)
{
    struct sockaddr_in serv_addr;
    //    struct sockaddr_in remote_addr;
    int sock;
    int ret;

    dbgprintf("IOP cmd: Server Thread Started.\n");

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        dbgprintf("IOP cmd: Socket error %d\n", sock);
        ExitDeleteThread();
    }

    memset((void *)&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PKO_CMD_PORT);

    ret = bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (ret < 0) {
        dbgprintf("IOP cmd: Udp bind error (%d)\n", sock);
        ExitDeleteThread();
    }

    // Do tha thing
    dbgprintf("IOP cmd: Listening\n");

    cmdListener(sock);

    ExitDeleteThread();
}


//////////////////////////////////////////////////////////////////////////
int cmdHandlerInit(void)
{
    iop_thread_t thread;
    int pid;

    dbgprintf("IOP cmd: Starting thread\n");

    SifInitRpc(0);
    SetPowerButtonHandler(cmdPowerOff, NULL);

    thread.attr = 0x02000000;
    thread.option = 0;
    thread.thread = (void *)cmdThread;
    thread.stacksize = 0x800;
    thread.priority = 60; //0x1e;

    pid = CreateThread(&thread);
    if (pid >= 0) {
        int ret;
        ret = StartThread(pid, 0);
        if (ret < 0) {
            dbgprintf("IOP cmd: Could not start thread\n");
        }
    } else {
        dbgprintf("IOP cmd: Could not create thread\n");
    }
    return 0;
}
