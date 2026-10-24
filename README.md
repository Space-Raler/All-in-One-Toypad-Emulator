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

- **LEGO Dimensions emulator**: Developed by [SegerEnd](https://github.com/SegerEnd/Flipper-Zero-LD-Toypad-Emulator).
- **Disney Infinity (FlipBase)**: Derived from RPCS3's Infinity emulation. You can use your phone or computer to connect via Bluetooth and change characters using your own dumps here: [Disney Infinity BLE Site](https://infinitybase-emu.netlify.app).
- **Skylanders (SkyBase)**: Based on RPCS3's portal behavior. You can use your phone or computer to connect via Bluetooth and change characters using your own dumps here: [Skylanders BLE Site](https://space-raler.github.io/skylanderportalweb/)

The bundled work is distributed under GPL-2.0-only because of FlipBase. The LEGO emulator source and the Skylanders source had no license file in the archives provided; check their upstream licenses before redistributing. Not affiliated with LEGO, Disney or Activision.
