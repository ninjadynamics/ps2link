/*********************************************************************
 * Copyright (C) 2003 Tord Lindstrom (pukko@home.se)
 * This file is subject to the terms and conditions of the PS2Link License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 */

#include <stdio.h>
#include <sysclib.h>
#include <loadcore.h>
#include <intrman.h>
#include <types.h>
#include <sifrpc.h>
#include <cdvdman.h>

#include "excepHandler.h"
#include "net_fsys.h"
#include "cmdHandler.h"
#include "nprintf.h"
#include "tlmPush.h"

#define MODNAME "ps2link"
IRX_ID(MODNAME, 1, 8);

////////////////////////////////////////////////////////////////////////
// main
//   start threads & init rpc & filesys
int _start(int argc, char **argv)
{
    int i;

    // The EE passes its boot generation as "gen=<hex>".
    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "gen=", 4) == 0) {
            unsigned int generation = 0;
            const char *p;

            for (p = argv[i] + 4; *p; p++) {
                generation = (generation << 4) | (unsigned int)(*p <= '9' ? *p - '0' : (*p | 0x20) - 'a' + 10);
            }
            cmdHandlerSetGeneration(generation);
        }
    }

    FlushDcache();
    CpuEnableIntr();

    sceCdInit(1);
    sceCdStop();

    SifInitRpc(0);

    fsysMount();
    printf("host: mounted\n");
    cmdHandlerInit();
    printf("IOP cmd thread started\n");
    naplinkRpcInit();
    printf("Naplink thread started\n");
    tlmPushInit();

    installExceptionHandlers();

    return 0;
}
