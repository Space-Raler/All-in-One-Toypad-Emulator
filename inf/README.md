# FlipBase

A Flipper Zero app (Momentum firmware) that acts as a **Disney Infinity base** over USB, so the
game sees characters, play sets and power discs without a physical base.

> **Status:** tested working on Disney Infinity 1.0, 2.0 and 3.0 (characters, play sets, power
> discs). Unofficial, not affiliated with or endorsed by Disney.

## Use

1. Build: `ufbt update --index-url=https://up.momentum-fw.dev/firmware/directory.json`, then `ufbt`
   in this folder. Copy `dist/flipbase.fap` to `SD:/apps/USB/`.
2. Open **Apps → USB → FlipBase**, plug the Flipper into the console, start the game.
3. Pick a row (Player 1/2, Play set, Disc slots). **OK** on an empty row: **File** (a 320-byte
   `.bin` dump of a figure you own, from `SD:/apps_data/flipbase/`) or **Fav** (favorites list).
   The app does not create figures; you supply your own dumps.
4. **Left/Right** swaps through favorites, **hold OK** stars the figure, **hold Right** saves
   `debug.txt`. The game's progress is written back to the figure's file on the SD card.

The top line shows the connection state and buzzes when a working connection drops.

## Bluetooth link to a PC app (optional)

**Hold Left** on the slot list to switch a Bluetooth LE serial link on or off (default off).
A PC app (`infinity_base_gui`, a separate download) can then connect, drop figures on a drawn
base and have them appear to the console. The PC loads/removes figures; when the game changes
a figure the Flipper sends the new data back to the PC (or keeps it as `unsent_slotN.bin` on
the SD card if the PC has gone). Protocol: `ble_proto.h`; Flipper side: `ble_link.c`.

Switching the link on drops any other Bluetooth connection to the Flipper, and the PC clears
the Flipper's base when it connects.

## Notes

- You must supply your own figure dumps. The app ships none and cannot generate them. **Do not
  upload figure files or logs.**
- Keep your own backups of `apps_data/flipbase`.
- The USB protocol, the figure format and the figure list follow
  [RPCS3](https://github.com/RPCS3/rpcs3)'s Disney Infinity emulation (`Infinity.cpp`,
  `infinity_dialog.cpp`). The `tests/` folder checks this code byte-for-byte against RPCS3's own
  code (`make RPCS3=/path/to/rpcs3` in `tests/`). The tests make their own in-memory figures with
  a blank-figure generator that is compiled only when `-DINF_ENABLE_BLANK_FIGURES` is set (the test
  Makefile sets it); it is not part of the app build.
- The Flipper USB structure was modeled on the Lego Dimensions toy pad emulator for Flipper Zero.
- Written with help from Claude (Anthropic).

## Related projects

- [Disney-Infinity-NFC](https://github.com/skylandersNFC/Disney-Infinity-NFC) — a separate,
  unaffiliated project with its own tools and figure dump collection. Linked for reference only.
  **FlipBase does not use, include, bundle, or depend on any of that project's dump files.**
  It only loads dumps that you supply yourself, from figures you own.

## License

GPL-2.0-only (see `LICENSE`), because it derives from RPCS3. Disney and Disney Infinity are
trademarks of their owners; they are named here only to say what the app works with.
