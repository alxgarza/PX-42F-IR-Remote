#include "irproto.h"

#include <string.h>
#include <strings.h>

/* How a protocol's bits become pulses. */
typedef enum {
    PULSE_DISTANCE, /* every pulse the same, the gap after it says 0 or 1; a stop pulse ends it */
    PULSE_WIDTH,    /* the pulse's length says 0 or 1; it ends on the last bit's pulse */
    MANCHESTER,     /* every bit two halves, on/off for 1, off/on for 0 */
} coding_t;

/* The bits of a code, sent bit 0 first; returns how many. */
typedef int (*pack_fn)(const ir_code_t *code, bool toggle, uint64_t *bits);

/* Timings in microseconds, from the Flipper firmware's encoders. */
typedef struct {
    const char *name;
    uint32_t carrier_hz;
    uint8_t address_bits, command_bits;
    uint8_t presses;
    coding_t coding;
    uint16_t preamble_on, preamble_off;
    uint16_t one_on, one_off, zero_on, zero_off; /* MANCHESTER: one_on is half a bit */
    int8_t double_bit;                           /* a bit twice as long (RC6's toggle), or -1 */
    uint32_t silence;                            /* the gap before a press */
    /* The gaps between transmissions: period - the previous one's length,
     * or (period 0) gap_first after the first and gap_later after others. */
    uint32_t period, gap_first, gap_later;
    /* What's sent while the key is held: this burst (on, off, ..., on), or
     * (none) the code again. */
    const uint16_t *repeat;
    uint8_t repeat_count;
    pack_fn pack;
} protocol_t;

/* value's low n bits, highest first from position pos (RC5 and RC6 send
 * their fields that way round). */
static uint64_t msb_first(uint32_t value, int n, int pos) {
    uint64_t out = 0;
    for (int k = 0; k < n; k++)
        out |= (uint64_t) ((value >> (n - 1 - k)) & 1) << (pos + k);
    return out;
}

static int pack_nec(const ir_code_t *c, bool toggle, uint64_t *bits) {
    (void) toggle;
    uint64_t a = c->address & 0xFF, k = c->command & 0xFF;
    *bits = a | (a ^ 0xFF) << 8 | k << 16 | (k ^ 0xFF) << 24;
    return 32;
}

static int pack_necext(const ir_code_t *c, bool toggle, uint64_t *bits) {
    (void) toggle;
    *bits = (uint64_t) (c->address & 0xFFFF) | (uint64_t) (c->command & 0xFFFF) << 16;
    return 32;
}

static int pack_nec42(const ir_code_t *c, bool toggle, uint64_t *bits) {
    (void) toggle;
    uint64_t a = c->address & 0x1FFF, k = c->command & 0xFF;
    *bits = a | (a ^ 0x1FFF) << 13 | k << 26 | (k ^ 0xFF) << 34;
    return 42;
}

static int pack_nec42ext(const ir_code_t *c, bool toggle, uint64_t *bits) {
    (void) toggle;
    *bits = (uint64_t) (c->address & 0x3FFFFFF) | (uint64_t) (c->command & 0xFFFF) << 26;
    return 42;
}

static int pack_samsung32(const ir_code_t *c, bool toggle, uint64_t *bits) {
    (void) toggle;
    uint64_t a = c->address & 0xFF, k = c->command & 0xFF;
    *bits = a | a << 8 | k << 16 | (k ^ 0xFF) << 24;
    return 32;
}

/* Start bit, 3 mode bits (0), toggle, address, command. */
static int pack_rc6(const ir_code_t *c, bool toggle, uint64_t *bits) {
    *bits = 1 | (uint64_t) toggle << 4 | msb_first(c->address, 8, 5) | msb_first(c->command, 8, 13);
    return 21;
}

/* Two start bits -- RC5X's second one is the command's bit 6, inverted,
 * always 1 there -- toggle, address, command; and RC5 sends a 1 as off/on,
 * the other way round from RC6. */
static int pack_rc5(const ir_code_t *c, bool toggle, uint64_t *bits) {
    uint64_t b = 1 | (c->protocol == IR_RC5 ? 2 : 0) | (uint64_t) toggle << 2 | msb_first(c->address, 5, 3)
                 | msb_first(c->command, 6, 8);
    *bits = ~b & 0x3FFF;
    return 14;
}

static int pack_sirc(const ir_code_t *c, bool toggle, uint64_t *bits) {
    (void) toggle;
    static const uint8_t address_bits[] = {[IR_SIRC] = 5, [IR_SIRC15] = 8, [IR_SIRC20] = 13};
    int n = address_bits[c->protocol];
    *bits = (uint64_t) (c->command & 0x7F) | (uint64_t) (c->address & ((1u << n) - 1)) << 7;
    return 7 + n;
}

/* Panasonic and friends: vendor (16 bits) with its parity, genres,
 * command, id, and a parity byte -- address holds id, vendor and genres. */
static int pack_kaseikyo(const ir_code_t *c, bool toggle, uint64_t *bits) {
    (void) toggle;
    uint8_t id = (c->address >> 24) & 3, genre1 = (c->address >> 4) & 0xF, genre2 = c->address & 0xF;
    uint16_t vendor = (c->address >> 8) & 0xFFFF, command = c->command & 0x3FF;
    uint8_t b[6];
    b[0] = (uint8_t) vendor;
    b[1] = (uint8_t) (vendor >> 8);
    uint8_t parity = b[0] ^ b[1];
    b[2] = (uint8_t) (((parity & 0xF) ^ (parity >> 4)) | genre1 << 4);
    b[3] = (uint8_t) (genre2 | (command & 0xF) << 4);
    b[4] = (uint8_t) (id << 6 | command >> 4);
    b[5] = b[2] ^ b[3] ^ b[4];
    *bits = 0;
    for (int i = 0; i < 6; i++)
        *bits |= (uint64_t) b[i] << (8 * i);
    return 48;
}

static int pack_rca(const ir_code_t *c, bool toggle, uint64_t *bits) {
    (void) toggle;
    uint64_t a = c->address & 0xF, k = c->command & 0xFF;
    *bits = a | k << 4 | (a ^ 0xF) << 12 | (k ^ 0xFF) << 16;
    return 24;
}

static const uint16_t nec_repeat[] = {9000, 2250, 560};
static const uint16_t samsung_repeat[] = {4500, 4500, 550, 1650, 550};

#define NEC_FAMILY(protocol_name, a_bits, k_bits, packer)                                                   \
    {                                                                                                     \
        .name = protocol_name, .carrier_hz = 38000, .address_bits = a_bits, .command_bits = k_bits,       \
        .presses = 1, .coding = PULSE_DISTANCE, .preamble_on = 9000, .preamble_off = 4500, .one_on = 560, \
        .one_off = 1690, .zero_on = 560, .zero_off = 560, .double_bit = -1, .silence = 110000,            \
        .period = 110000, .repeat = nec_repeat, .repeat_count = 3, .pack = packer                        \
    }

#define SIRC_FAMILY(protocol_name, a_bits)                                                                  \
    {                                                                                                     \
        .name = protocol_name, .carrier_hz = 40000, .address_bits = a_bits, .command_bits = 7,            \
        .presses = 3, .coding = PULSE_WIDTH, .preamble_on = 2400, .preamble_off = 600, .one_on = 1200,    \
        .one_off = 600, .zero_on = 600, .zero_off = 600, .double_bit = -1, .silence = 10000,              \
        .period = 45000, .pack = pack_sirc                                                                \
    }

#define RC5_FAMILY(protocol_name, k_bits)                                                                   \
    {                                                                                                     \
        .name = protocol_name, .carrier_hz = 36000, .address_bits = 5, .command_bits = k_bits,            \
        .presses = 1, .coding = MANCHESTER, .one_on = 888, .double_bit = -1, .silence = 27000,            \
        .gap_first = 27000, .gap_later = 27000, .pack = pack_rc5                                          \
    }

static const protocol_t protocols[IR_PROTOCOL_COUNT] = {
        [IR_NEC] = NEC_FAMILY("NEC", 8, 8, pack_nec),
        [IR_NECEXT] = NEC_FAMILY("NECext", 16, 16, pack_necext),
        [IR_NEC42] = NEC_FAMILY("NEC42", 13, 8, pack_nec42),
        [IR_NEC42EXT] = NEC_FAMILY("NEC42ext", 26, 16, pack_nec42ext),
        [IR_SAMSUNG32] = {.name = "Samsung32", .carrier_hz = 38000, .address_bits = 8, .command_bits = 8,
                          .presses = 1, .coding = PULSE_DISTANCE, .preamble_on = 4500, .preamble_off = 4500,
                          .one_on = 550, .one_off = 1650, .zero_on = 550, .zero_off = 550, .double_bit = -1,
                          .silence = 145000, .gap_first = 46000, .gap_later = 97000, .repeat = samsung_repeat,
                          .repeat_count = 5, .pack = pack_samsung32},
        [IR_RC6] = {.name = "RC6", .carrier_hz = 36000, .address_bits = 8, .command_bits = 8, .presses = 1,
                    .coding = MANCHESTER, .preamble_on = 2666, .preamble_off = 889, .one_on = 444,
                    .double_bit = 4, .silence = 27000, .gap_first = 27000, .gap_later = 27000,
                    .pack = pack_rc6},
        [IR_RC5] = RC5_FAMILY("RC5", 6),
        [IR_RC5X] = RC5_FAMILY("RC5X", 7),
        [IR_SIRC] = SIRC_FAMILY("SIRC", 5),
        [IR_SIRC15] = SIRC_FAMILY("SIRC15", 8),
        [IR_SIRC20] = SIRC_FAMILY("SIRC20", 13),
        [IR_KASEIKYO] = {.name = "Kaseikyo", .carrier_hz = 38000, .address_bits = 26, .command_bits = 10,
                         .presses = 1, .coding = PULSE_DISTANCE, .preamble_on = 3456, .preamble_off = 1728,
                         .one_on = 432, .one_off = 1296, .zero_on = 432, .zero_off = 432, .double_bit = -1,
                         .silence = 130000, .gap_first = 130000, .gap_later = 130000, .pack = pack_kaseikyo},
        [IR_RCA] = {.name = "RCA", .carrier_hz = 38000, .address_bits = 4, .command_bits = 8, .presses = 1,
                    .coding = PULSE_DISTANCE, .preamble_on = 4000, .preamble_off = 4000, .one_on = 500,
                    .one_off = 2000, .zero_on = 500, .zero_off = 1000, .double_bit = -1, .silence = 8000,
                    .gap_first = 8000, .gap_later = 8000, .pack = pack_rca},
        /* Flipper sends a 33rd bit, a 0, ending on its gap: here the stop
         * pulse, with that bit's 500us gap added to the 26ms between. */
        [IR_PIONEER] = {.name = "Pioneer", .carrier_hz = 40000, .address_bits = 8, .command_bits = 8,
                        .presses = 2, .coding = PULSE_DISTANCE, .preamble_on = 8500, .preamble_off = 4225,
                        .one_on = 500, .one_off = 1500, .zero_on = 500, .zero_off = 500, .double_bit = -1,
                        .silence = 26000, .gap_first = 26500, .gap_later = 26500, .pack = pack_nec},
};

ir_protocol_t ir_protocol_by_name(const char *name) {
    for (int p = 0; p < IR_PROTOCOL_COUNT; p++)
        if (strcasecmp(name, protocols[p].name) == 0)
            return (ir_protocol_t) p;
    return IR_PROTOCOL_COUNT;
}

const char *ir_protocol_name(ir_protocol_t protocol) {
    return protocols[protocol].name;
}

bool ir_code_valid(const ir_code_t *code) {
    const protocol_t *p = &protocols[code->protocol];
    return (uint64_t) code->address >> p->address_bits == 0 && (uint64_t) code->command >> p->command_bits == 0;
}

int ir_presses(ir_protocol_t protocol) {
    return protocols[protocol].presses;
}

/* Appends a stretch of on or off, joined to the last one if that's the same
 * (us[0] is off, so on is at the odd places). */
static void put(ir_burst_t *b, bool on, uint32_t us) {
    if (us == 0)
        return;
    if (((b->count - 1) % 2 == 1) == on)
        b->us[b->count - 1] += us;
    else if (b->count < IR_MAX_TIMINGS)
        b->us[b->count++] = us;
}

static void put_code(const protocol_t *p, const ir_code_t *code, bool toggle, ir_burst_t *b) {
    uint64_t bits;
    int n = p->pack(code, toggle, &bits);
    put(b, true, p->preamble_on);
    put(b, false, p->preamble_off);
    for (int i = 0; i < n; i++) {
        bool one = (bits >> i) & 1;
        if (p->coding == MANCHESTER) {
            uint32_t half = p->one_on * (i == p->double_bit ? 2u : 1u);
            put(b, one, half);
            if (!(one && i == n - 1)) /* it ends on its last pulse */
                put(b, !one, half);
        } else {
            put(b, true, one ? p->one_on : p->zero_on);
            if (p->coding == PULSE_WIDTH && i == n - 1)
                break;
            put(b, false, one ? p->one_off : p->zero_off);
        }
    }
    if (p->coding == PULSE_DISTANCE)
        put(b, true, p->zero_on); /* the stop pulse */
}

/* Transmission n without its gap. */
static void put_transmission(const protocol_t *p, const ir_code_t *code, bool toggle, int n, ir_burst_t *b) {
    if (n == 0 || p->repeat == NULL) {
        put_code(p, code, toggle, b);
        return;
    }
    for (int i = 0; i < p->repeat_count; i++)
        put(b, i % 2 == 0, p->repeat[i]);
}

static uint32_t length_us(const ir_burst_t *b) {
    uint32_t sum = 0;
    for (int i = 1; i < b->count; i++)
        sum += b->us[i];
    return sum;
}

void ir_encode(const ir_code_t *code, bool toggle, int n, ir_burst_t *out) {
    const protocol_t *p = &protocols[code->protocol];
    uint32_t gap = n == 0 ? p->silence : n == 1 ? p->gap_first : p->gap_later;
    if (n > 0 && p->period != 0) {
        ir_burst_t previous = {.count = 1};
        put_transmission(p, code, toggle, n - 1, &previous);
        gap = p->period - length_us(&previous);
    }
    out->carrier_hz = p->carrier_hz;
    out->count = 1;
    out->us[0] = gap;
    put_transmission(p, code, toggle, n, out);
}
