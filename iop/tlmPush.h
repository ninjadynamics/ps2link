/*********************************************************************
 * HyperSolar fork: binary telemetry push to the connected ps2client.
 * This file is subject to the terms and conditions of the PS2Link License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 */

#ifndef TLM_PUSH_H
#define TLM_PUSH_H

int tlmPushInit(void);

/* Export 4 of library "pkotlm" v1.1. Sends one complete frame (1 to
 * PKO_TLM_FRAME_MAX bytes) as one UDP datagram to the PC that owns the fileio
 * connection, on PKO_TLM_PORT. Frames are never split or combined. Returns
 * size once the network stack accepted the datagram, or a negative value;
 * acceptance is not host receipt. The caller owns data only for the call. */
int pkoTlmPush(const void *data, int size);

#endif /* TLM_PUSH_H */
