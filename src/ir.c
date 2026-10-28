/* IR Remote for the PX-42F: the calculator as an infrared remote control.
 * Each remote is a .ir file in the app's folder, /BIN/APPS/IR/ (irfile.h:
 * Flipper Zero's format plus the key each code goes on). The app lists
 * them; the one chosen is shown as a map of the keyboard with each key's
 * label, and a key sends its code (irproto.h), repeating while it's held.
 * ON goes back to the menu. */
#include <stdio.h>
#include <string.h>

#include "irfile.h"
#include "irproto.h"
#include "px.h"

#define MAX_REMOTES  32
#define FILE_MAX     (64 * 1024) /* a remote file's text: Flipper's big ones are ~40 KB */
#define KEY_STUCK_MS 60000       /* a key held this long is stuck, not a person */

/* The screen: a title bar, then the keyboard's 7 rows (font: 8 x 12). */
#define TITLE_HEIGHT 13
#define ROW_Y0       14
#define ROW_HEIGHT   15
#define CHAR_W       8
#define LIST_ROWS    7

static char file_text[FILE_MAX];
static ir_remote_t remote;

static void title_bar(const char *left, const char *right) {
    px->fill(0, 0, PX_SCREEN_W, TITLE_HEIGHT - 1, 1);
    px->text(2, 0, left, PX_FONT_SMALL, 1);
    px->text(PX_SCREEN_W - 2 - px->text_width(right, PX_FONT_SMALL), 0, right, PX_FONT_SMALL, 1);
}

static void dotted_line(int y) {
    for (int x = 0; x < PX_SCREEN_W; x += 2)
        px->pixel(x, y, 1);
}

/* A message and a hint, until a key: false if the app is to end. */
static bool message(const char *title, const char *const *lines, int count) {
    px->clear();
    title_bar(title, "");
    for (int i = 0; i < count; i++)
        px->text(2, 20 + i * 13, lines[i], PX_FONT_SMALL, 0);
    px->text(2, 109, "Any key: back", PX_FONT_SMALL, 0);
    px->show();
    while (1) {
        int key = px->key(-1);
        if (key == PX_KEY_QUIT)
            return false;
        if (!(key & PX_KEY_RELEASED))
            return true;
    }
}

/* ----- choosing a remote ----- */

/* The remote picked from names, or -1 to end the app. */
static int choose(char (*names)[PX_FILE_NAME_MAX + 1], int count, int selected) {
    while (1) {
        int top = selected < LIST_ROWS ? 0 : selected - LIST_ROWS + 1;
        px->clear();
        title_bar("IR Remote", "");
        for (int row = 0; row < LIST_ROWS && top + row < count; row++) {
            int y = 14 + row * 13;
            bool on = top + row == selected;
            if (on)
                px->fill(0, y - 1, PX_SCREEN_W, 13, 1);
            px->text(4, y, names[top + row], PX_FONT_SMALL, on);
        }
        px->text(2, 109, "Enter:Use  ON:Back", PX_FONT_SMALL, 0);
        px->show();

        int key = px->key(-1);
        if (key == PX_KEY_QUIT)
            return -1;
        if (key == PX_KEY_ENTER)
            return selected;
        if (key == PX_KEY_ARROW_UP)
            selected = (selected + count - 1) % count;
        else if (key == PX_KEY_ARROW_DN)
            selected = (selected + 1) % count;
    }
}

/* ----- the remote ----- */

/* A key's box on the map, laid out like the keyboard: 6 keys in the top
 * two rows, 5 wider ones in each row below (ENTER's too). */
static void key_box(int row, int col, int *x, int *w) {
    *w = row < 2 ? 41 : 49;
    *x = 2 + col * *w;
}

static void draw_key(int row, int col, const char *label, bool inverted) {
    int x, w;
    key_box(row, col, &x, &w);
    int y = ROW_Y0 + row * ROW_HEIGHT;
    if (inverted)
        px->fill(x, y, w - 1, ROW_HEIGHT - 2, 1);
    char text[IR_LABEL_MAX + 1];
    int n = (int) strlen(label);
    if (n > w / CHAR_W)
        n = w / CHAR_W;
    memcpy(text, label, (size_t) n);
    text[n] = 0;
    px->text(x + (w - 1 - n * CHAR_W) / 2, y + 1, text, PX_FONT_SMALL, inverted);
}

/* The whole map; pressed_row/col (or -1) shown pressed, and the name of
 * what was last sent in the title bar. */
static void draw(const char *name, int pressed_row, int pressed_col, const char *sent) {
    char left[16], right[20];
    snprintf(left, sizeof(left), "%.14s", name);
    snprintf(right, sizeof(right), "%.15s", sent);
    px->clear();
    title_bar(left, right);
    for (int row = 0; row < IR_ROWS; row++) {
        if (row > 0)
            dotted_line(ROW_Y0 + row * ROW_HEIGHT - 2);
        for (int col = 0; col < ir_row_keys(row); col++) {
            const ir_key_t *key = &remote.keys[row][col];
            if (row == 6 && col == 0)
                draw_key(row, col, "Menu", true); /* ON: back to the menu */
            else if (key->used)
                draw_key(row, col, key->label, row == pressed_row && col == pressed_col);
        }
    }
    px->show();
}

/* Sends a key's code: what the protocol wants for a short press (Sony: the
 * code 3 times), then on repeating until the key is let go. False if the
 * app is to end. */
static bool send(int key, const ir_key_t *k) {
    static bool toggle; /* RC5 and RC6 tell presses apart by it */
    toggle = !toggle;
    bool held = true;
    uint32_t start = px->millis();
    for (int n = 0; n < ir_presses(k->code.protocol) || held; n++) {
        ir_burst_t burst;
        ir_encode(&k->code, toggle, n, &burst);
        px->ir_send(&burst);
        for (int e; (e = px->key(0)) != PX_KEY_NONE;) {
            if (e == PX_KEY_QUIT)
                return false;
            if (e == (key | PX_KEY_RELEASED))
                held = false;
        }
        if (px->millis() - start > KEY_STUCK_MS)
            break;
    }
    return true;
}

/* Until ON, which ends the app. */
static void run_remote(const char *name) {
    const char *sent = "";
    draw(name, -1, -1, sent);
    while (1) {
        int key = px->key(-1);
        if (key == PX_KEY_QUIT)
            return;
        if (key & PX_KEY_RELEASED)
            continue;
        int row = PX_KEY_ROW(key), col = PX_KEY_COL(key);
        const ir_key_t *k = &remote.keys[row][col];
        if (!k->used)
            continue;
        draw(name, row, col, k->name);
        if (!send(key, k))
            return;
        sent = k->name;
        draw(name, -1, -1, sent);
    }
}

/* Reads <name>.ir: false, after a screen saying why, if it can't be used
 * (*quit: the app is to end). */
static bool load(const char *name, bool *quit) {
    char file[PX_FILE_NAME_MAX + 4];
    snprintf(file, sizeof(file), "%s.ir", name);
    int n = px->load(file, file_text, FILE_MAX);
    char head[40], where[16], error[40] = "";
    int line = 0;
    if (n < 0)
        snprintf(error, sizeof(error), "Can't read the file");
    else if (n == FILE_MAX)
        snprintf(error, sizeof(error), "Too big (64 KB at most)");
    else if (!ir_remote_parse(file_text, (size_t) n, &remote, &line, error, sizeof(error)))
        ;
    else
        return true;
    px->tone(10);
    snprintf(head, sizeof(head), "Can't use %.18s:", name);
    snprintf(where, sizeof(where), "Line %d:", line);
    const char *lines[] = {head, line > 0 ? where : "", error};
    *quit = !message("IR Remote", lines, 3);
    return false;
}

int app_main(void) {
    static char names[MAX_REMOTES][PX_FILE_NAME_MAX + 1];
    int count = px->list("", ".ir", names, MAX_REMOTES);
    if (count == 0) {
        static const char *const lines[] = {"No remotes yet.", "", "Copy .ir files to the", "BIN/APPS/IR folder",
                                            "in USB Drive mode."};
        message("IR Remote", lines, 5);
        return PX_EXIT_MENU;
    }
    int selected = 0;
    while (1) {
        selected = choose(names, count, selected);
        if (selected < 0)
            return PX_EXIT_MENU;
        bool quit = false;
        if (load(names[selected], &quit)) {
            run_remote(names[selected]);
            return PX_EXIT_MENU;
        }
        if (quit)
            return PX_EXIT_MENU;
    }
}
