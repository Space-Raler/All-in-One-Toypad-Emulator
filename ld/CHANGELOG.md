# Changelog

## Unreleased (ToyBox Bluetooth)

- The Bluetooth link is now switched on/off by holding Left on the Emulate screen (like Infinity and
  Skylanders) instead of the Config setting, which froze the Flipper. The radio is started from the app's
  tick, not from inside a GUI callback, and a short on-screen message shows the link state.

- Web page keeps vehicle memory: when the game changes a vehicle (upgrades, levels) the Flipper sends the
  vehicle's data to the page (LD_TOKEN), the page keeps a copy, and "Saved vehicles" puts it back exactly
  as it was (same UID, same upgrades) with LD_PLACE_VEHICLE.
- Page can save a vehicle on the Flipper (its own Saved vehicles menu) and import the ones saved there.
- Saved vehicle file names now keep their letters and digits (names starting with "*" or containing
  spaces used to be cut short); the Saved vehicles menu no longer grows each time it is refreshed.

## v0.5

- Lots of code cleanup 🧼
- Toypad preset saving and loading
- Remove all placed tokens at once by holding "back" for a few seconds

## v0.4

- Added quick character switching for smoother gameplay.
- Fixed a issue preventing upgraded vehicles from being saved.
- First Flipper App store release

## v0.3

- Introduced favorites feature for quick access to characters.
- Added saving of upgraded vehicles.

## v0.2

- Implemented vehicle placement functionality.
- Introduced a new selection screen for easier navigation.

## v0.1

- Implemented character placement.
- First release of the app
