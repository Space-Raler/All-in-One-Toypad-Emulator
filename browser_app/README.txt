Skylanders Portal browser page with Flipper SD-card dump transfer controls.

Open index.html in Bluefy on iPhone. Connect to the Flipper's BLE serial service as usual. Use “Find dumps on Flipper” to list/read 1024-byte dumps from /ext/apps_data/skybase/. Select imported figures and use “Save selected to Flipper” to overwrite their existing dump paths.

Requires the rebuilt ToyBox Flipper firmware from this source archive. The old dist/toybox.fap binary does not support the new file-transfer protocol.


LEGO Dimensions: open lego_dimensions.html the same way. On the Flipper open Toy Pad Emulator > LEGO Dimensions >
Emulate and hold Left to turn the Bluetooth link on, plug into the console, then Connect in the page.
(index.html is the Skylanders page; it now includes the batched dump list decoder.)


LEGO Dimensions page (lego_dimensions.html): Saved vehicles
- A vehicle keeps its upgrades in its own memory. The page asks the Flipper for it and keeps a copy in this
  browser (IndexedDB). With "Remember upgrades automatically" on, a vehicle the game changes is saved by itself.
  Tap a saved vehicle to put it back with its upgrades. Export/Import makes a JSON backup.
- "Save on Flipper" stores the vehicle in the Flipper's own Saved vehicles menu; "Import from Flipper" copies those into the page.
- Minifigures have no memory to keep (the game stores their progress in its own save).
- Needs the rebuilt toybox.fap (the prebuilt dist/toybox.fap predates this).
