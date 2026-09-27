/*********************************************************************
 * HyperSolar fork: console tty sent to the connected PC only (tty.c).
 * This file is subject to the terms and conditions of the PS2Link License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 */

#ifndef TTY_H
#define TTY_H

/* Installs "tty" and points stdin/stdout/stderr at it. */
int ttyInit(void);

/* Called once remote_pc_addr names the PC: sends the output held so far. */
void ttyHostConnected(void);

#endif /* TTY_H */
