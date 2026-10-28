#ifndef IRFILE_H
#define IRFILE_H

#include <stdbool.h>
#include <stddef.h>

#include "irproto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A remote control: /BIN/APPS/IR/<name>.ir on the drive, in the Flipper Zero's
 * format plus two lines -- which key sends each code, and a short label to
 * show on it:
 *
 *   name: TV On/Off
 *   label: Power
 *   button: R0C5
 *   protocol: NECext
 *   address: 02 7D 00 00
 *   command: 9F 60 00 00
 *
 * button is the key's place on the keyboard, row 0-6 from the top, column
 * 0-5 from the left (the top two rows have 6 keys, the others 5; ENTER is
 * R2C0): R0C5 is XEQ. R6C0 is ON, which leaves the remote, so it can't be
 * used. Entries with no button are skipped -- a Flipper file can be used as
 * it is, with buttons added to the codes wanted -- and so are Flipper's
 * header lines, "type:" and comments (#). Raw codes (data:) aren't sent. */

#define IR_ROWS      7
#define IR_COLS      6
#define IR_LABEL_MAX 10 /* read this long; a key shows 5 (top two rows) or 6 */
#define IR_NAME_MAX  23

typedef struct {
    bool used;
    char name[IR_NAME_MAX + 1];
    char label[IR_LABEL_MAX + 1];
    ir_code_t code;
} ir_key_t;

typedef struct {
    ir_key_t keys[IR_ROWS][IR_COLS];
} ir_remote_t;

/* How many keys row (0-6) has. */
int ir_row_keys(int row);

/* Reads a remote from the text of its file. False if it has a mistake,
 * with the line it's on and what's wrong (at most 30 characters, to fit a
 * line of the screen). */
bool ir_remote_parse(const char *text, size_t length, ir_remote_t *remote, int *error_line, char *error,
                     size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
