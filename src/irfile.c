#include "irfile.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int ir_row_keys(int row) {
    return row < 2 ? 6 : 5;
}

/* One entry as read so far: its fields, each with the line it was on
 * (0 = not there). */
typedef struct {
    int line;
    char name[IR_NAME_MAX + 1];
    char label[IR_LABEL_MAX + 1];
    char button[8], protocol[16], address[16], command[16];
    int label_line, button_line, protocol_line, address_line, command_line, raw_line;
} entry_t;

typedef struct {
    ir_remote_t *remote;
    int *error_line;
    char *error;
    size_t error_size;
} reader_t;

static bool fail(reader_t *r, int line, const char *why) {
    *r->error_line = line;
    snprintf(r->error, r->error_size, "%s", why);
    return false;
}

/* "02 7D 00 00": up to 4 bytes, the first the lowest. */
static bool parse_bytes(const char *s, uint32_t *value) {
    *value = 0;
    int n = 0;
    while (*s) {
        char *end;
        unsigned long byte = strtoul(s, &end, 16);
        if (end == s || end - s > 2 || n == 4 || (*end != 0 && *end != ' '))
            return false;
        *value |= (uint32_t) byte << (8 * n++);
        for (s = end; *s == ' '; s++) {
        }
    }
    return n > 0;
}

/* Checks a finished entry and puts it on its key. */
static bool finish(reader_t *r, const entry_t *e) {
    if (e->button_line == 0)
        return true; /* on no key: skipped */

    char rc, cc, extra;
    int row, col;
    if (sscanf(e->button, "%c%d%c%d%c", &rc, &row, &cc, &col, &extra) != 4 || toupper(rc) != 'R'
        || toupper(cc) != 'C')
        return fail(r, e->button_line, "Bad button (R0C0 to R6C4)");
    if (row < 0 || row >= IR_ROWS || col < 0 || col >= ir_row_keys(row))
        return fail(r, e->button_line, "No such key");
    if (row == 6 && col == 0)
        return fail(r, e->button_line, "R6C0 is ON: can't be used");
    ir_key_t *key = &r->remote->keys[row][col];
    if (key->used)
        return fail(r, e->button_line, "Key used twice");

    if (e->raw_line != 0)
        return fail(r, e->raw_line, "Raw codes aren't supported");
    if (e->protocol_line == 0)
        return fail(r, e->line, "No protocol");
    if (e->address_line == 0)
        return fail(r, e->line, "No address");
    if (e->command_line == 0)
        return fail(r, e->line, "No command");
    ir_code_t code = {.protocol = ir_protocol_by_name(e->protocol)};
    if (code.protocol == IR_PROTOCOL_COUNT)
        return fail(r, e->protocol_line, "Unknown protocol");
    if (!parse_bytes(e->address, &code.address))
        return fail(r, e->address_line, "Bad address");
    if (!parse_bytes(e->command, &code.command))
        return fail(r, e->command_line, "Bad command");
    if (!ir_code_valid(&code))
        return fail(r, e->address_line, "Too big for the protocol");

    key->used = true;
    key->code = code;
    snprintf(key->name, sizeof(key->name), "%s", e->name);
    snprintf(key->label, sizeof(key->label), "%.*s", (int) sizeof(key->label) - 1,
             e->label_line != 0 ? e->label : e->name); /* a long name, cut to fit the key */
    return true;
}

typedef struct {
    const char *p, *end;
} text_t;

/* The next line, without its end or surrounding spaces -- the start of a
 * longer one (a raw code's data), the rest skipped. False at the end. */
static bool read_line(text_t *t, char *line, size_t size) {
    if (t->p >= t->end)
        return false;
    size_t n = 0;
    for (; t->p < t->end && *t->p != '\n'; t->p++)
        if (n < size - 1 && *t->p != 0)
            line[n++] = *t->p;
    if (t->p < t->end)
        t->p++; /* the '\n' */
    line[n] = 0;
    while (n > 0 && isspace((unsigned char) line[n - 1]))
        line[--n] = 0;
    size_t start = 0;
    while (isspace((unsigned char) line[start]))
        start++;
    memmove(line, line + start, n - start + 1);
    return true;
}

/* field = value, cut to fit; and the line it's on. */
#define SET(field, line_field)                                     \
    do {                                                           \
        snprintf(e.field, sizeof(e.field), "%s", value);           \
        e.line_field = line;                                       \
    } while (0)

bool ir_remote_parse(const char *text_in, size_t length, ir_remote_t *remote, int *error_line, char *error,
                     size_t error_size) {
    text_t f = {text_in, text_in + length};
    memset(remote, 0, sizeof(*remote));
    reader_t r = {remote, error_line, error, error_size};
    entry_t e = {0};
    bool in_entry = false;
    char text[96];
    for (int line = 1; read_line(&f, text, sizeof(text)); line++) {
        char *colon = strchr(text, ':');
        if (text[0] == '#' || colon == NULL)
            continue;
        *colon = 0;
        const char *key = text, *value = colon + 1;
        while (*value == ' ')
            value++;

        if (strcasecmp(key, "name") == 0) {
            if (in_entry && !finish(&r, &e))
                return false;
            memset(&e, 0, sizeof(e));
            in_entry = true;
            e.line = line;
            snprintf(e.name, sizeof(e.name), "%s", value);
        } else if (!in_entry) {
            continue; /* Flipper's "Filetype:" and "Version:" */
        } else if (strcasecmp(key, "label") == 0) {
            SET(label, label_line);
        } else if (strcasecmp(key, "button") == 0) {
            SET(button, button_line);
        } else if (strcasecmp(key, "protocol") == 0) {
            SET(protocol, protocol_line);
        } else if (strcasecmp(key, "address") == 0) {
            SET(address, address_line);
        } else if (strcasecmp(key, "command") == 0) {
            SET(command, command_line);
        } else if (strcasecmp(key, "data") == 0) {
            e.raw_line = line;
        }
    }
    return !in_entry || finish(&r, &e);
}
