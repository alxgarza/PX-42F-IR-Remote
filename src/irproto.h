#ifndef IRPROTO_H
#define IRPROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "px_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Infrared remote-control codes, as Flipper Zero .ir files write them
 * ("protocol: NECext", "address: 02 7D 00 00", "command: 9F 60 00 00"), and
 * the signal each one is sent as: the same timings, bit for bit, as the
 * Flipper firmware's own encoders (tests/test_parsers.c holds vectors made
 * with them), so codes from Flipper-IRDB work as they are. */

typedef enum {
    IR_NEC,
    IR_NECEXT,
    IR_NEC42,
    IR_NEC42EXT,
    IR_SAMSUNG32,
    IR_RC6,
    IR_RC5,
    IR_RC5X,
    IR_SIRC,
    IR_SIRC15,
    IR_SIRC20,
    IR_KASEIKYO,
    IR_RCA,
    IR_PIONEER,
    IR_PROTOCOL_COUNT
} ir_protocol_t;

typedef struct {
    ir_protocol_t protocol;
    uint32_t address;
    uint32_t command;
} ir_code_t;

/* The protocol of that name (as in .ir files, e.g. "NECext"), or
 * IR_PROTOCOL_COUNT. */
ir_protocol_t ir_protocol_by_name(const char *name);
const char *ir_protocol_name(ir_protocol_t protocol);

/* False if address or command has more bits than the protocol carries. */
bool ir_code_valid(const ir_code_t *code);

/* A key press sends one or more transmissions: the code, and while the key
 * is held, the protocol's repeat (NEC's short repeat code, Sony's whole
 * code again...). A short press sends ir_presses() of them -- Sony's
 * receivers want the code three times. */
int ir_presses(ir_protocol_t protocol);

/* One transmission: microseconds, alternately off and on, starting with
 * the gap before it (off) and ending with the last pulse (on). The infrared
 * light is switched at carrier_hz while on. The framework's own type
 * (px_app.h), so px->ir_send() takes it as it is. */
#define IR_MAX_TIMINGS PX_IR_MAX_TIMINGS
typedef px_ir_burst_t ir_burst_t;

/* Transmission n (0 = the first) of a press of code. toggle is RC5's and
 * RC6's toggle bit, which changes from one press to the next (so a
 * receiver tells a new press from a held key). */
void ir_encode(const ir_code_t *code, bool toggle, int n, ir_burst_t *out);

#ifdef __cplusplus
}
#endif

#endif
