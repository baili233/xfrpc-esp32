// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: client state events shared between the xfrpc core and the
 * public API (include/xfrpc.h). Kept in its own header because src/ has
 * its own xfrpc.h and a direct include would be ambiguous.
 */

#ifndef XFRPC_EVENTS_H
#define XFRPC_EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    XFRPC_STATE_INIT,        /* before xfrpc_start() */
    XFRPC_STATE_CONNECTING,  /* task started, connecting to frps */
    XFRPC_STATE_CONNECTED,   /* TCP connection established */
    XFRPC_STATE_LOGIN_OK,    /* frps accepted the login */
    XFRPC_STATE_RECONNECTING,/* control connection lost, retrying */
    XFRPC_STATE_FATAL,       /* unrecoverable error, task exited */
    XFRPC_STATE_STOPPED,     /* xfrpc_stop() completed, task exited */
} xfrpc_state_t;

/**
 * Called by the xfrpc core to report a state transition to the user
 * callback registered in xfrpc_start(). Runs on the xfrpc event task.
 */
void xfrpc_report_state(xfrpc_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* XFRPC_EVENTS_H */
