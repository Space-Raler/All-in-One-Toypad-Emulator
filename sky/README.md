# Skylanders Portal for Flipper Zero (Momentum)

Emulates the Skylanders Portal of Power (USB HID 1430:0150) so a console or PC
sees a portal with up to 8 figures. Figures come from 1024-byte dumps of figures
you own (.sky / .bin). Each game has its own folder: `/ext/apps_data/skybase/spyros_adventure/`, `/ext/apps_data/skybase/giants/`, `/ext/apps_data/skybase/swap_force/` and
`/ext/apps_data/skybase/trap_team/` and `/ext/apps_data/skybase/imaginators/` and `/ext/apps_data/skybase/superchargers/` (each also keeps its own `session.txt`).

## Build
Inside ToyBox this is built with the launcher (`ufbt` in the toybox folder); open it from
**Toy Pad Emulator → Skylanders →** Spyro's Adventure / Giants / Swap Force / Trap Team / SuperChargers / Imaginators.

## Use
- Per player, four rows: Figure 1, Figure 2, Item 1, Item 2 (P1 = slots 1-4, P2 = slots 5-8). The portal has no player/figure/item field, so this is only labeling. Any tag works in any slot; load Player 1's first. A Swap Force character uses both Figure rows (top + bottom half).
- Spyro's Adventure (the original): same portal and protocol; labels are Figure 1, Figure 2, Item 1, Item 2 per player. Tested working.
- Giants: same portal and protocol and the same labels as Spyro's Adventure. Not tested yet.
- Trap Team: pick it in the Skylanders menu. Same portal and protocol as Swap Force; only the row labels change to Figure 1, Trap 1, Item 1, Item 2 per player. Tested working.
- Imaginators: same portal and protocol again; labels are Figure 1, Crystal 1, Item 1, Item 2 per player (Item = magic items; creation crystals and Senseis load from your own 1024-byte dumps like any other tag). Tested working.
- SuperChargers: same portal and protocol; labels are Figure 1, Vehicle 1, Item 1, Item 2 per player. Tested working; vehicles load from your own 1024-byte dumps like any other tag.
- Up/Down: pick slot. OK: load a dump / remove the figure in that slot. Hold OK: add/remove the figure as a favorite.
- Favorites (per game: Swap Force and Trap Team each keep their own list in `favorites.txt` in their folder, up to 16): hold OK on a loaded figure to add or remove it. Quick switch: Right = next favorite, Left = previous favorite, loaded into the selected slot (replacing what is there; favorites already in another slot are skipped). The old "remove all" is gone; remove figures one at a time with OK.
- Hold Right: info for the slot (serial, type ID, `Fav: yes/no`, unsaved changes). Back closes it.
- Hold Left: toggle RW/RO. RW writes the game's changes back into the dump in place; `*` marks unsaved changes.
- Back: exit (restores normal USB). Figures and RW/RO are restored next launch.
- Bottom right: number of commands the game has sent, then the letters of the last few (R, A, S, Q, W...). Useful for debugging a game that does not detect the portal.
- Hold Right on an empty slot: shows the size and first 8 bytes (hex) of the last command the host sent, for debugging.
- `debug.txt` in the game's folder (recreated at each launch, stops after 600 lines): the traffic between the game and the emulator. `C` lines are commands from the game, `R` lines are our replies, `S` lines are status packets (logged only when the figure bits change), each with the length and first 8 bytes in hex. Send it when a game does not detect the portal or figures.
- Hold Back: Settings. Up/Down pick a row, Left/Right/OK change it, Back closes. Rows: **Audio** On/Off, **Volume** Low/Medium/High, **Lights** On/Off and **Test tone** (settings saved in `/ext/apps_data/skybase/settings.txt`, shared by all games). The bottom line shows the speaker state (Off / Idle / Play / Busy), how many audio packets the game has sent (`P`) and the loudest queued sample (`Pk`).
- Bottom line shows host state: Waiting / Disconnected / Host found / Host connected / Game talking (LED + vibration on connect/loss).
- Swap Force characters are two halves; load each half's dump separately.

## Bluetooth (PC portal app)
A PC app (`skylanders_gui.py`) loads figures onto the portal over Bluetooth LE: drag figures onto a window instead of picking a file on the Flipper.
- **Switch on/off:** hold **Left** on the portal screen. The bottom line alternates with `BLE: waiting for PC` / `BLE: PC connected`. Off by default and every time you open the app. (RW/RO moved to Settings: hold Back, row **Save to dump**.)
- **While connected the PC is in charge:** on connect it clears the portal and sends its figures. Figures from the SD card are dropped. Slots filled by the PC show `(PC)`; OK on one removes it.
- **Game changes** (XP, gold) to a PC figure are sent back to the PC (SAVE), which writes them into its dump. If the PC is not there, or you switch Bluetooth off or leave the app, the data is kept as `unsent_slotN.bin` in the game's folder on the SD card. A SAVE takes about 1.5 s to send (52 notifications of 20 bytes).
- Switching Bluetooth on drops any phone connected to the Flipper's Bluetooth.
- **Protocol:** version 2 of `inf/ble_proto.h` (1024-byte figures; STATUS = version, usb_state, present mask, game). `sky_ble.c` handles messages, `sky_ble_bridge.c` joins it to the byte stream, `sky_ble_link.c` is the radio.
- **Host tests** (from `sky/`):
  `gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -I. -Icore -I../inf core/sky_core.c ../inf/ble_proto.c sky_ble.c tests/sky_ble_test.c -o /tmp/sbt && /tmp/sbt`
  `gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -I. -Icore -I../inf core/sky_core.c ../inf/ble_proto.c sky_ble.c sky_ble_bridge.c tests/sky_ble_bridge_test.c -o /tmp/sbb && /tmp/sbb`

## Audio (Trap Team's portal speaker)
When a game switches the portal speaker on (`M` command) and streams 8 kHz 16-bit PCM to the USB OUT endpoint, ToyBox plays it on the Flipper's speaker (PWM + DMA, `sky_audio_hw.c`). The portable part (conversion, gain, ring buffer) is `core/sky_audio.c`. Turn it off in Settings if you don't want it. If the speaker is in use by something else the Settings screen shows Busy and the app retries every 2 s. Volume levels are the `audio_gain_table` in `skybase.c`.
If you hear nothing: open Settings and check the `P:` packet count. If it stays 0 while the game plays sounds, the game is not sending audio on the OUT endpoint. The first four OUT packets are also written to `debug.txt` as `O` lines.
**Status:** tested on a Flipper with Trap Team: the Settings test tone and the game's audio both play. The portable audio code and the `M` handling are also host-tested. Settings (hold Back) has a Test tone row and a diagnostics line (Spk, P, Pk) for troubleshooting.

## Lights
The game's light commands are shown on the Flipper: the ring colour (`C`, and `L`/`J` for side 0) goes to the RGB LED, a `J` fade is interpolated over its duration (capped at 5 s), and `L` side 1 sets the screen backlight brightness (side 3 sets it to full). The core (`core/sky_core.c`) only records the requested state; the app applies it from its main loop, because the LED driver must not be called from the USB interrupt.
- **Settings -> Lights On/Off** (default On). Off gives the LED and backlight back to the system and ignores the game's light commands, so a game that dims the backlight can never leave you with a dark screen. Any button press also restores the backlight.
- The LED and backlight are handed back when the host disconnects and when you leave the app.
- **Status:** the command parsing is host-tested (`tests/lights_test.c`). The `C` layout is the well-known one; the `L` and `J` byte layouts are assumed (see the comment on `SkyLights` in `core/sky_core.h`) and the LED/backlight behaviour has not been tried on a Flipper yet. To check or fix a layout, play a game and read the `C` lines in `debug.txt` (first byte `4C` = `L`, `4A` = `J`, `43` = `C`).

## Tests (host)
    gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -Icore core/sky_core.c tests/core_test.c -o /tmp/t && /tmp/t
    gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -Icore core/sky_audio.c tests/audio_test.c -o /tmp/at && /tmp/at
    gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Icore core/sky_core.c tests/speaker_flag_test.c -o /tmp/mt && /tmp/mt
    gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -Icore core/sky_core.c tests/lights_test.c -o /tmp/lt && /tmp/lt
    for f in usb_skylander.c skybase.c sky_audio_hw.c; do gcc -std=gnu11 -fsyntax-only -Itests/stubs -I. $f; done   # run from toybox/sky   # stub-header syntax check only


## Browser SD-card dump transfer (protocol v2 extension)

The Bluefy browser can request a list of 1024-byte dump files under `/ext/apps_data/skybase`, read dumps over the existing BLE serial service, and write selected dumps back to an existing dump path. This uses additional SkyBase protocol messages (`DUMP_LIST`, `DUMP_READ`, `DUMP_WRITE_BEGIN/CHUNK/END`), so the Flipper app must be rebuilt and reinstalled from this source; the prebuilt `dist/toybox.fap` in an older archive does not implement these messages. File operations are restricted to existing dump files under the SkyBase folder; metadata/settings files and unsent-save files are excluded.
