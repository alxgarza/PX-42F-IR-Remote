# IR Remote for the PX-42F

Turns the PX-42F calculator into an infrared remote control: a native app,
`IR.bin`, built on the PX-42F App Template (the framework's files `px42f/` and
`sim/`, and its README, come from there). It used to be part of the
calculator's firmware.

## Using it

1. Copy `build/IR.bin` to `BIN/APPS` on the calculator's drive.
2. Put your remotes, `.ir` files, in `BIN/APPS/IR`.
3. On the calculator: Settings → Games & Apps → Apps → IR.
4. Pick a remote with ↑ ↓ and ENTER.

The screen shows the keyboard with each key's label. A key sends its code, and
keeps repeating while held. ON goes back to the menu; held 2 s, it turns the
calculator off.

## Remote files

A remote file uses the Flipper Zero's `.ir` format, plus two lines of its own:

```
name: TV On/Off
label: Power
button: R0C5
protocol: NECext
address: 02 7D 00 00
command: 9F 60 00 00
```

- **`button:`** The key's place: row 0–6 from the top, column 0–5 from the
  left. The top two rows have 6 keys, the others 5; `R0C5` is XEQ and `R2C0` is
  ENTER. `R6C0` is ON, which leaves the remote, so it can't be used.
- **`label:`** What the key shows: 5 characters fit on the top two rows' keys,
  6 on the others; a longer one is cut. Without it, the key
  shows the start of the `name:`.
- **Codes without a button** are skipped. A Flipper file works as it is: add a
  `button:` line to each code you want.
- **Protocols.** All 14 of Flipper's are supported: NEC, NECext, NEC42,
  NEC42ext, Samsung32, RC5, RC5X, RC6, SIRC, SIRC15, SIRC20, Kaseikyo, RCA and
  Pioneer. Raw codes (`data:`) aren't sent.
- **Ready-made files.** [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB)
  works: copy a file, then add the `button:` lines.

`sim/drive/BIN/APPS/IR/` has two examples to copy: `Samsung TV.ir`, a complete
Samsung TV remote with its key map in the comments, and `Sample TV.ir`.

## Build, test, install

It needs the PX-42F-Plus firmware with app API version 2 or later.


- `make`: builds `build/IR.bin`.
- `make test`: runs `tests/test_ir.c` on your computer, with ASan/UBSan. It
  checks that every protocol sends exactly what the Flipper Zero sends
  (`tests/ir_vectors.h`), and that good and broken `.ir` files are read
  correctly. Run it after touching `src/irproto.c` or `src/irfile.c`.
- `make sim`: runs the app on the simulated drive. Screenshots are saved in
  `build/sim-out`, and each transmission is printed.
- `make install`: copies `IR.bin` to the calculator. First open Settings → USB
  Drive → Connect to Computer.

`tests/ir_vectors.h` is made by `tools/ir_vectors/make.sh` from the Flipper
firmware's own encoders. Make it again after changing a protocol.

## The files

| Path | What it is |
|---|---|
| `src/ir.c` | The app: the list of remotes, the key map, sending and repeating. |
| `src/irfile.c`, `irfile.h` | Reads `.ir` files. |
| `src/irproto.c`, `irproto.h` | The 14 protocols: a code into pulse timings, which the framework's `px->ir_send()` sends with the calculator's PIO. |
