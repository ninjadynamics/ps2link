/*********************************************************************
 * HyperSolar fork: console tty sent to the connected PC only.
 * This file is subject to the terms and conditions of the PS2Link License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 *
 * Replaces PS2SDK's udptty, which sends every console write to
 * 255.255.255.255:18194. Every device on the LAN had to take the PS2's
 * console as broadcast traffic: an ELF load prints a progress dot about every
 * 27 ms, one broadcast frame each, and a Dreamcast running dcload-ip P7 in the
 * same LAN lost its game to that burst. Output goes to the PC that owns the
 * fileio connection (remote_pc_addr) on the same port ps2client listens on.
 * Writes before a PC connects (the IOP boot after a reset) wait in a small
 * buffer that is sent on connect; past its size the newest text is dropped.
 */

#include <types.h>
#include <stdio.h>
#include <sysclib.h>
#include <thsemap.h>
#include <ioman.h>

#include "ps2ip.h"
#include "net_fio.h"
#include "tty.h"

#define TTY_PORT        18194
#define TTY_PENDING_MAX 4096
#define TTY_DGRAM_MAX   1024

static int tty_sock = -1;
static int tty_sema = -1;
static char tty_pending[TTY_PENDING_MAX];
static int tty_pending_len;

static void tty_send(const char *buf, int size)
{
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TTY_PORT);
    addr.sin_addr.s_addr = remote_pc_addr;
    while (size > 0) {
        const int n = size < TTY_DGRAM_MAX ? size : TTY_DGRAM_MAX;
        sendto(tty_sock, (void *)buf, n, 0, (struct sockaddr *)&addr, sizeof(addr));
        buf += n;
        size -= n;
    }
}

static int tty_dev_init(iop_device_t *device)
{
    (void)device;
    return 0;
}

static int tty_dev_deinit(iop_device_t *device)
{
    (void)device;
    return 0;
}

static int tty_stdout_fd(void)
{
    return 1;
}

/* Always reports size consumed: the IOP's stdio retries a short write forever. */
static int tty_dev_write(iop_file_t *file, void *buf, int size)
{
    (void)file;
    if (size <= 0)
        return 0;
    WaitSema(tty_sema);
    if (remote_pc_addr != 0xffffffff) {
        tty_send(buf, size);
    } else {
        int n = TTY_PENDING_MAX - tty_pending_len;
        if (n > size)
            n = size;
        memcpy(tty_pending + tty_pending_len, buf, n);
        tty_pending_len += n;
    }
    SignalSema(tty_sema);
    return size;
}

static int tty_dev_eio(void)
{
    return -5; /* EIO */
}

static iop_device_ops_t tty_ops = {
    &tty_dev_init,
    &tty_dev_deinit,
    (void *)&tty_dev_eio, // format
    (void *)&tty_stdout_fd, // open
    (void *)&tty_stdout_fd, // close
    (void *)&tty_dev_eio, // read
    (void *)&tty_dev_write,
    (void *)&tty_dev_eio, // lseek
    (void *)&tty_dev_eio, // ioctl
    (void *)&tty_dev_eio, // remove
    (void *)&tty_dev_eio, // mkdir
    (void *)&tty_dev_eio, // rmdir
    (void *)&tty_dev_eio, // dopen
    (void *)&tty_dev_eio, // dclose
    (void *)&tty_dev_eio, // dread
    (void *)&tty_dev_eio, // getstat
    (void *)&tty_dev_eio, // chstat
};

static iop_device_t tty_device = {
    "tty",
    IOP_DT_CHAR | IOP_DT_CONS,
    1,
    "TTY via UDP to the ps2link host",
    &tty_ops,
};

int ttyInit(void)
{
    iop_sema_t sema;

    sema.attr = 0;
    sema.option = 0;
    sema.initial = 1;
    sema.max = 1;
    tty_sema = CreateSema(&sema);
    if (tty_sema < 0)
        return -1;
    tty_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (tty_sock < 0)
        return -1;

    DelDrv(tty_device.name);
    if (AddDrv(&tty_device) < 0)
        return -1;

    close(0);
    open("tty00:", 0x1000 | O_RDWR);
    close(1);
    open("tty00:", O_WRONLY);
    close(2);
    open("tty00:", O_WRONLY);
    return 0;
}

void ttyHostConnected(void)
{
    WaitSema(tty_sema);
    if (tty_pending_len) {
        tty_send(tty_pending, tty_pending_len);
        tty_pending_len = 0;
    }
    SignalSema(tty_sema);
}
