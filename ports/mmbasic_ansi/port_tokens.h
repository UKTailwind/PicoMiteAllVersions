/*
 * ports/mmbasic_ansi/port_tokens.h — token table for the ANSI terminal port.
 *
 * Inherits the host_native token set and adds MODE + QUIT. MODE
 * switches the framebuffer between preset resolutions (see
 * ansi_mode.c). QUIT exits the process — useful in the alt-screen
 * REPL where the user can't always see what they typed and Ctrl-C
 * handling varies by terminal.
 */

#ifndef PORT_TOKENS_ANSI_H
#define PORT_TOKENS_ANSI_H

/* Pull in the host_native token set first; do NOT match its own
 * PORT_TOKENS_H guard or the inner include becomes a no-op. */
#include "../host_native/port_tokens.h"

#undef HAL_PORT_VIDEO_CMD_TOKENS
#define HAL_PORT_VIDEO_CMD_TOKENS \
    { (unsigned char *)"Camera",  T_CMD, 0, cmd_camera }, \
    { (unsigned char *)"Refresh", T_CMD, 0, cmd_refresh }, \
    { (unsigned char *)"MODE",    T_CMD, 0, cmd_mode },   \
    { (unsigned char *)"QUIT",    T_CMD, 0, cmd_quit },

#endif /* PORT_TOKENS_ANSI_H */
