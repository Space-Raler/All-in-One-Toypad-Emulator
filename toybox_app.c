/*
 * ToyBox: one Flipper Zero app that bundles three USB toy-to-life base emulators.
 * Pick which base to emulate, plug the Flipper into the console, play.
 *
 *   LEGO Dimensions Toy Pad   (ld/,  SegerEnd/Flipper-Zero-LD-Toypad-Emulator)
 *   Disney Infinity base       (inf/, FlipBase)
 *   Skylanders Portal of Power (sky/, SkyBase) -> then pick the game (Swap Force)
 *
 * Each emulator is the original app, started through its own entry point. Only one
 * runs at a time; Back in an emulator's main menu returns here, Back here exits.
 * Skylanders has a second menu (which game); Back there returns to the main picker.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>

/* Entry points of the bundled apps (ld/ldtoypad.c, inf/flipbase_app.c, sky/skybase.c). */
extern int32_t ldtoypad_app(void* p);
extern int32_t flipbase_app(void* p);
extern int32_t skybase_app(void* p);

typedef enum {
    ToyBoxEntryLego,
    ToyBoxEntryInfinity,
    ToyBoxEntrySkylanders,
    ToyBoxEntryCount,
} ToyBoxEntry;

/* Skylanders games. All use the same USB Portal of Power (VID 1430, PID 0150) emulation;
 * a game is a separate entry so more can be listed here later. */
typedef enum {
    ToyBoxSkySpyrosAdventure,
    ToyBoxSkyGiants,
    ToyBoxSkySwapForce,
    ToyBoxSkyTrapTeam,
    ToyBoxSkySuperChargers,
    ToyBoxSkyImaginators,
    ToyBoxSkyCount,
} ToyBoxSkyGame;

static const char* const toybox_main_labels[ToyBoxEntryCount] = {
    [ToyBoxEntryLego] = "LEGO Dimensions",
    [ToyBoxEntryInfinity] = "Disney Infinity",
    [ToyBoxEntrySkylanders] = "Skylanders",
};

static const char* const toybox_sky_labels[ToyBoxSkyCount] = {
    [ToyBoxSkySpyrosAdventure] = "Spyro's Adventure",
    [ToyBoxSkyGiants] = "Giants",
    [ToyBoxSkySwapForce] = "Swap Force",
    [ToyBoxSkyTrapTeam] = "Trap Team",
    [ToyBoxSkySuperChargers] = "SuperChargers",
    [ToyBoxSkyImaginators] = "Imaginators",
};

#define TOYBOX_NO_CHOICE (-1)

typedef struct {
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    int32_t choice; /* ToyBoxEntry picked by the user, or TOYBOX_NO_CHOICE on Back */
} ToyBoxMenu;

static void toybox_submenu_callback(void* context, uint32_t index) {
    ToyBoxMenu* menu = context;
    menu->choice = (int32_t)index;
    view_dispatcher_stop(menu->view_dispatcher);
}

static uint32_t toybox_navigation_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE; /* Back: leave the menu, choice stays TOYBOX_NO_CHOICE */
}

/* Show a menu of `count` labels. Returns the picked index, or TOYBOX_NO_CHOICE when the
 * user pressed Back. The menu is fully torn down before returning, because the emulators
 * attach their own fullscreen view dispatcher to the GUI. */
static int32_t toybox_menu_run(
    const char* header,
    const char* const* labels,
    uint32_t count,
    int32_t preselect) {
    ToyBoxMenu menu = {.choice = TOYBOX_NO_CHOICE};

    Gui* gui = furi_record_open(RECORD_GUI);

    menu.view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(menu.view_dispatcher, gui, ViewDispatcherTypeFullscreen);

    menu.submenu = submenu_alloc();
    submenu_set_header(menu.submenu, header);
    for(uint32_t i = 0; i < count; i++) {
        submenu_add_item(menu.submenu, labels[i], i, toybox_submenu_callback, &menu);
    }
    if(preselect != TOYBOX_NO_CHOICE) {
        submenu_set_selected_item(menu.submenu, (uint32_t)preselect);
    }
    view_set_previous_callback(submenu_get_view(menu.submenu), toybox_navigation_exit_callback);

    view_dispatcher_add_view(menu.view_dispatcher, 0, submenu_get_view(menu.submenu));
    view_dispatcher_switch_to_view(menu.view_dispatcher, 0);

    view_dispatcher_run(menu.view_dispatcher);

    view_dispatcher_remove_view(menu.view_dispatcher, 0);
    submenu_free(menu.submenu);
    view_dispatcher_free(menu.view_dispatcher);
    furi_record_close(RECORD_GUI);

    return menu.choice;
}

/* The emulators put the USB stack back into its previous mode when they exit, but the
 * switch is carried out asynchronously by the USB thread. Wait for it (max 2 s) so the
 * next emulator reads the right "previous" mode and never inherits a half-switched one. */
static void toybox_wait_usb_mode(FuriHalUsbInterface* expected) {
    for(uint8_t i = 0; i < 40 && furi_hal_usb_get_config() != expected; i++) {
        furi_delay_ms(50);
    }
}

int32_t toybox_app(void* p) {
    UNUSED(p);

    FuriHalUsbInterface* base_usb_mode = furi_hal_usb_get_config();

    int32_t choice = TOYBOX_NO_CHOICE;
    while(true) {
        choice = toybox_menu_run("Toy Pad Emulator", toybox_main_labels, ToyBoxEntryCount, choice);
        if(choice == TOYBOX_NO_CHOICE) break;

        switch(choice) {
        case ToyBoxEntryLego:
            ldtoypad_app(NULL);
            break;
        case ToyBoxEntryInfinity:
            flipbase_app(NULL);
            break;
        case ToyBoxEntrySkylanders: {
            /* Second level: which Skylanders game. Back returns to the main picker with
             * Skylanders still selected. */
            int32_t game = toybox_menu_run("Skylanders Portal", toybox_sky_labels, ToyBoxSkyCount, 0);
            switch(game) {
            case ToyBoxSkySpyrosAdventure:
                skybase_app((void*)5); /* same portal, Spyro's Adventure labels + own folder */
                break;
            case ToyBoxSkyGiants:
                skybase_app((void*)4); /* same portal, Giants labels + own folder */
                break;
            case ToyBoxSkySwapForce:
                skybase_app(NULL);
                break;
            case ToyBoxSkyTrapTeam:
                skybase_app((void*)1); /* same portal, Trap Team slot labels */
                break;
            case ToyBoxSkyImaginators:
                skybase_app((void*)2); /* same portal, Imaginators labels + own folder */
                break;
            case ToyBoxSkySuperChargers:
                skybase_app((void*)3); /* same portal, SuperChargers labels + own folder */
                break;
            default:
                break;
            }
            break;
        }
        default:
            break;
        }

        toybox_wait_usb_mode(base_usb_mode);
    }

    return 0;
}
