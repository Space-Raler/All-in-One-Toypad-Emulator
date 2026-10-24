# ToyBox: Toy Pad Emulator for Flipper Zero (Momentum)

One app, three USB toy-to-life bases. Open **Apps → USB → Toy Pad Emulator**, pick one:

- **LEGO Dimensions**: the LEGO Dimensions Toy Pad emulator (`ld/`)
- **Disney Infinity**: the FlipBase Infinity base emulator (`inf/`)
- **Skylanders**: the Skylanders **Portal of Power** emulator (`sky/`, USB 1430:0150). Picking
  it opens a second menu to choose the game; **Spyro's Adventure**, **Giants**, **Swap Force**, **Trap Team**, **SuperChargers** or **Imaginators** (all but Swap Force only change the slot labels and folder). Tested working on hardware: Spyro's Adventure, Swap Force, Trap Team, SuperChargers and Imaginators. Giants is the only one not tested yet.

Back in an emulator's main menu returns to the picker; Back in the Skylanders game menu returns
to the main picker; Back in the main picker exits. Each emulator keeps its own controls and save
data exactly as before (see `ld/README.md`, `inf/README.md`, `sky/README.md`). Figures/saves stay
where they were (`apps_data/flipbase` for Infinity, `apps_data/skybase/<game>` for Skylanders (spyros_adventure, giants, swap_force, trap_team, superchargers, imaginators): 1024-byte
dumps of figures you own).

The Skylanders portal is one generic Portal of Power; the game menu only picks the game's label.
The game list is a small table in `toybox_app.c` (`toybox_sky_labels`) if more are added later.

## LEGO Dimensions from a web page (Bluetooth)

Like the Skylanders portal, the LEGO Toy Pad can be controlled from a phone or computer:

1. On the Flipper open **Toy Pad Emulator -> LEGO Dimensions -> Emulate** and **hold Left** to turn the
   Bluetooth link on (hold Left again to turn it off). A message shows *BLE link on*, then
   *Waiting for PC* / *PC connected*. There is no Config setting for it any more.
2. Plug the Flipper into the console (Emulate stays open on the Flipper).
3. Open `browser_app/lego_dimensions.html` in Chrome (Android / desktop) or Bluefy (iPhone), tap
   **Connect Flipper**, and tap characters or vehicles to put them on the pad. Tap a spot first
   to choose where a token goes. Changes made with the Flipper's own buttons show up on the page too.

The page only names tokens (character / vehicle id); the Flipper builds them exactly as its own menus do,
so nothing is written to the SD card. It needs the rebuilt `toybox.fap` (the bundled `dist/` file is old).
The protocol is documented in `ld/ld_ble.h`; host test: `ld/tests/ld_ble_test.c`.

## Build (Momentum)

    ufbt update --index-url=https://up.momentum-fw.dev/firmware/directory.json
    ufbt                      # run inside this folder
    # copy dist/toybox.fap to SD:/apps/USB/

Remove the old standalone `ldtoypad.fap` / `flipbase.fap` / `skybase.fap` if you no longer want them.

## What changed vs. the originals

Sources are unchanged apart from small fixes needed to run both in one process:

- `ld/`: `emulator` and `burtle` are no longer reused after being freed; a double free of
  the emulate view on exit is removed; the "quick switching" flag resets on each launch;
  exit waits for the USB mode to be restored.
- `sky/`: on exit it now waits for the USB mode to be restored before freeing the portal state.
  Its own `application.fam` was removed (the launcher's manifest builds it).
- `sky/`: portal speaker audio (Trap Team) plays on the Flipper speaker, with an Audio on/off, Volume and Lights on/off Settings screen (hold Back in the Skylanders screen). New files: `sky/core/sky_audio.c`, `sky/sky_audio_hw.c`. The game's light commands (ring colour, fades, backlight) are shown on the Flipper's LED and backlight (Settings -> Lights).
- The launcher waits for the USB mode to be restored between emulators, and has a second-level
  menu for Skylanders.
- `fap_icon_assets_symbol="ldtoypad"` keeps the LEGO icon header name unchanged.

## Credits and license

- **LEGO Dimensions emulator**: Developed by [SegerEnd](https://github.com/SegerEnd/Flipper-Zero-LD-Toypad-Emulator), [BLE site](https://space-raler.github.io/legodimensionsweb/)
- **Disney Infinity (FlipBase)**: Derived from RPCS3's Infinity emulation. You can use your phone or computer to connect via Bluetooth and change characters using your own dumps here: [Disney Infinity BLE Site]([https://infinitybase-emu.netlify.app](https://space-raler.github.io/disneyinfinityweb/).
- **Skylanders (SkyBase)**: Based on RPCS3's portal behavior. You can use your phone or computer to connect via Bluetooth and change characters using your own dumps here: [Skylanders BLE Site](https://space-raler.github.io/skylanderportalweb/).

The bundled work is distributed under GPL-2.0-only because of FlipBase. The LEGO emulator source and the Skylanders source had no license file in the archives provided; check their upstream licenses before redistributing. Not affiliated with LEGO, Disney or Activision.


## Bluefy browser SD-card import/export extension

This source archive adds a protocol extension for the Skylanders BLE link. The matching browser page is in `browser_app/index.html`. To use **Find dumps on Flipper** or **Save selected to Flipper**, rebuild and install the updated `toybox.fap` from source with `ufbt` (see Build above). The prebuilt file under `dist/` is the original binary and does **not** include the new file-transfer commands. The extension reads only 1024-byte files under `/ext/apps_data/skybase/` and only overwrites existing dump files in that tree.
