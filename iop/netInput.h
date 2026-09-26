/*********************************************************************
 * HyperSolar fork (P4): network controller input.
 * This file is subject to the terms and conditions of the PS2Link License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 */

#ifndef NET_INPUT_H
#define NET_INPUT_H

int netInputInit(void);
/* Called from the naplink RPC thread; ee_addr 0 unregisters and returns
 * after the last transfer into the old record has completed. */
void netInputRegister(unsigned int ee_addr, unsigned int record_size);

#endif /* NET_INPUT_H */
