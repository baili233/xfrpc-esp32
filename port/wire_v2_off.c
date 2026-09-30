// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: wire protocol v2 is not compiled in this build
 * (CONFIG_XFRPC_ENABLE_WIRE_V2 is off), so wire_protocol_is_v2() is a
 * compile-time constant. That is the single hinge for the whole v2
 * subsystem: the branches guarded by it in src/control.c fold away, and
 * because src/wire_v2.c and src/aead_stream.c are not in the build there is
 * nothing left referencing the v2/AEAD helpers.
 *
 * The signature must stay identical to the one in src/wire_v2.h.
 */

#include "wire_v2.h"

int wire_protocol_is_v2(void)
{
	return 0;
}