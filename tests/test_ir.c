/* The IR Remote's parts that have to be right without a calculator to try
 * them on (make test, on the Mac, with ASan/UBSan): every protocol sends
 * exactly what the Flipper Zero sends (tests/ir_vectors.h, made from its
 * encoders by tools/ir_vectors/make.sh), and .ir files, good and broken,
 * read as they should. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "irfile.h"
#include "irproto.h"
#include "ir_vectors.h"

static int failures;

#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
            failures++;                                 \
        }                                               \
    } while (0)

static void on_alarm(int sig) {
    (void) sig;
    static const char msg[] = "FAIL: a case hung (alarm)\n";
    write(1, msg, sizeof(msg) - 1);
    _exit(1);
}

/* Every protocol sends what the Flipper firmware sends for the same code
 * (tests/ir_vectors.h, made from its encoders): the first press, and two
 * transmissions more of a held key. */
static void test_ir_encode(void) {
    for (size_t v = 0; v < sizeof(ir_vectors) / sizeof(ir_vectors[0]); v++) {
        ir_code_t code = {ir_protocol_by_name(ir_vectors[v].protocol), ir_vectors[v].address, ir_vectors[v].command};
        CHECK(code.protocol != IR_PROTOCOL_COUNT, "%s: no such protocol", ir_vectors[v].protocol);
        if (code.protocol == IR_PROTOCOL_COUNT)
            continue;
        CHECK(ir_code_valid(&code), "%s: vector's code out of range", ir_vectors[v].protocol);
        uint32_t us[400];
        int n = 0;
        for (int t = 0; t < IR_VECTOR_TRANSMISSIONS; t++) {
            ir_burst_t b;
            ir_encode(&code, ir_vectors[v].toggle, t, &b);
            for (int i = 0; i < b.count; i++) {
                if (n % 2 != i % 2)
                    us[n - 1] += b.us[i]; /* a gap after a gap (Pioneer) */
                else if (n < 400)
                    us[n++] = b.us[i];
            }
        }
        CHECK(n == ir_vectors[v].count && memcmp(us, ir_vectors[v].us, sizeof(uint32_t) * (size_t) n) == 0,
              "%s %X/%X toggle %d: sends something else than Flipper", ir_vectors[v].protocol, ir_vectors[v].address,
              ir_vectors[v].command, ir_vectors[v].toggle);
    }
}

static bool load_ir(const char *text, ir_remote_t *remote, int *line, char *error) {
    return ir_remote_parse(text, strlen(text), remote, line, error, 32);
}

static int keys_used(const ir_remote_t *remote) {
    int n = 0;
    for (int r = 0; r < IR_ROWS; r++)
        for (int c = 0; c < IR_COLS; c++)
            n += remote->keys[r][c].used;
    return n;
}

static void test_ir_file_error(const char *text, int expected_line, const char *expected_error) {
    static ir_remote_t remote;
    int line = 0;
    char error[32] = "";
    bool ok = load_ir(text, &remote, &line, error);
    CHECK(!ok && line == expected_line && strcmp(error, expected_error) == 0,
          "expected line %d \"%s\", got %s line %d \"%s\"", expected_line, expected_error, ok ? "success" : "error",
          line, error);
}

static void test_ir_file(void) {
    /* A Flipper file, with buttons and a label added, a raw code on no key,
     * and its long data line. */
    static const char good[] =
            "Filetype: IR signals file\n"
            "Version: 1\n"
            "#\n"
            "name: TV On/Off\n"
            "label: Power\n"
            "button: R0C5\n"
            "type: parsed\n"
            "protocol: NECext\n"
            "address: 02 7D 00 00\n"
            "command: 9F 60 00 00\n"
            "#\n"
            "name: Learned\n"
            "type: raw\n"
            "frequency: 38000\n"
            "duty_cycle: 0.330000\n"
            "data: 9024 4512 579 552 579 552 579 1683 579 552 579 552 579 552 579 552 579 552 579 1683 579 1683\n"
            "#\n"
            "name: Vol+\r\n"
            "button: r6c4\r\n"
            "protocol: NEC\r\n"
            "address: 07 00 00 00\r\n"
            "command: 02 00 00 00\r\n"
            "#\n"
            "name: Input Source Select Long\n"
            "button: R2C0\n"
            "protocol: Samsung32\n"
            "address: 07\n"
            "command: 01 00";
    static ir_remote_t remote;
    int line = 0;
    char error[32] = "";
    CHECK(load_ir(good, &remote, &line, error), "good .ir file: line %d: %s", line, error);
    CHECK(keys_used(&remote) == 3, "good .ir file: %d keys, expected 3", keys_used(&remote));
    const ir_key_t *power = &remote.keys[0][5], *vol = &remote.keys[6][4], *input = &remote.keys[2][0];
    CHECK(power->used && strcmp(power->name, "TV On/Off") == 0 && strcmp(power->label, "Power") == 0
                  && power->code.protocol == IR_NECEXT && power->code.address == 0x7D02
                  && power->code.command == 0x609F,
          "R0C5 read as \"%s\" \"%s\" %d %X %X", power->name, power->label, power->code.protocol,
          power->code.address, power->code.command);
    CHECK(vol->used && strcmp(vol->label, "Vol+") == 0 && vol->code.protocol == IR_NEC && vol->code.address == 7
                  && vol->code.command == 2,
          "R6C4 (CR LF lines, no label) read wrong");
    CHECK(input->used && strcmp(input->name, "Input Source Select Lon") == 0
                  && strcmp(input->label, "Input Sour") == 0 && input->code.address == 7 && input->code.command == 1,
          "R2C0 (long name, short bytes) read as \"%s\" \"%s\"", input->name, input->label);

    CHECK(load_ir("", &remote, &line, error) && keys_used(&remote) == 0, "an empty file is an empty remote");

    static const char entry[] = "name: A\nbutton: R1C1\nprotocol: NEC\naddress: 01 00 00 00\ncommand: 02 00 00 00\n";
    test_ir_file_error("name: A\nbutton: R6C0\nprotocol: NEC\naddress: 01\ncommand: 02\n", 2,
                       "R6C0 is ON: can't be used");
    test_ir_file_error("name: A\nbutton: R2C5\nprotocol: NEC\naddress: 01\ncommand: 02\n", 2, "No such key");
    test_ir_file_error("name: A\nbutton: X1\nprotocol: NEC\naddress: 01\ncommand: 02\n", 2,
                       "Bad button (R0C0 to R6C4)");
    test_ir_file_error("name: A\nbutton: R1C1x\nprotocol: NEC\naddress: 01\ncommand: 02\n", 2,
                       "Bad button (R0C0 to R6C4)");
    char twice[256];
    snprintf(twice, sizeof(twice), "%s#\n%s", entry, entry);
    test_ir_file_error(twice, 8, "Key used twice");
    test_ir_file_error("name: A\nbutton: R1C1\nprotocol: Foo\naddress: 01\ncommand: 02\n", 3, "Unknown protocol");
    test_ir_file_error("name: A\nbutton: R1C1\ntype: raw\ndata: 100 200\n", 4, "Raw codes aren't supported");
    test_ir_file_error("name: A\nbutton: R1C1\nprotocol: NEC\naddress: 01 01\ncommand: 02\n", 4,
                       "Too big for the protocol");
    test_ir_file_error("name: A\nbutton: R1C1\nprotocol: NEC\naddress: 0G\ncommand: 02\n", 4, "Bad address");
    test_ir_file_error("name: A\nbutton: R1C1\nprotocol: NEC\naddress: 01\ncommand: 02 00 00 00 00\n", 5,
                       "Bad command");
    test_ir_file_error("name: A\nbutton: R1C1\nprotocol: NEC\naddress: 01\n", 1, "No command");
    /* A file cut off mid-entry is checked like any other. */
    test_ir_file_error("name: A\nbutton: R1C1\nprotocol: NEC\naddr", 1, "No address");
}

int main(void) {
    signal(SIGALRM, on_alarm);
    alarm(10);
    test_ir_encode();
    test_ir_file();
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures != 0;
}
