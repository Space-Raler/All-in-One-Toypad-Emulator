#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "core/sky_core.h"
#include "core/sky_audio.h"
#include "sky_audio_hw.h"
#include "usb_skylander.h"
#include "sky_ble_bridge.h"
#include "sky_ble_link.h"

/* Each game has its own folder (dumps + session.txt):
*   .../spyros_adventure  .../giants  .../swap_force  .../trap_team  .../superchargers  .../imaginators */
#define SKY_ROOT_DIR EXT_PATH("apps_data/skybase")
#define SKY_DIR_SWAP SKY_ROOT_DIR "/swap_force"
#define SKY_DIR_GIANTS SKY_ROOT_DIR "/giants"
#define SKY_DIR_SPYRO SKY_ROOT_DIR "/spyros_adventure"
#define SKY_DIR_TRAP SKY_ROOT_DIR "/trap_team"
#define SKY_DIR_IMAG SKY_ROOT_DIR "/imaginators"
#define SKY_DIR_SUPER SKY_ROOT_DIR "/superchargers"
#define SKY_SETTINGS_FILE SKY_ROOT_DIR "/settings.txt" /* shared by all games */

/* Speaker volume levels: gain in 1/16 steps (see core/sky_audio.h). The Flipper speaker is
 * quiet and the game's portal audio is mixed low, so these are boosts. Adjust to taste. */
#define AUDIO_VOL_COUNT 3
static const uint8_t audio_gain_table[AUDIO_VOL_COUNT] = {32, 64, 128}; /* 2x, 4x, 8x */
static const char* const audio_vol_names[AUDIO_VOL_COUNT] = {"Low", "Medium", "High"};
#define SETTINGS_ROWS 5 /* 0 = Audio on/off, 1 = Volume, 2 = Lights on/off, 3 = Test tone, 4 = Save to dump (RW/RO) */
#define SETTINGS_VISIBLE 4

/* Games (label layout + folder only; the portal protocol is the same). Passed in as `p`. */
typedef enum { SkyGameSwapForce = 0, SkyGameTrapTeam = 1, SkyGameImaginators = 2, SkyGameSuperChargers = 3, SkyGameGiants = 4, SkyGameSpyrosAdventure = 5 } SkyGame;
#define PATH_LEN 256
#define TXT_LEN 4608
#define FAV_MAX 16

typedef enum { LoadOk, LoadDup, LoadBadFile, LoadFull, LoadLong } LoadResult;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* queue;
    FuriMutex* mutex;
    Storage* storage;
    DialogsApp* dialogs;
    NotificationApp* notif;
    SkyBase* base;
    uint8_t* io_buf; /* SKY_FIGURE_SIZE scratch */
    char* txt; /* session file scratch */
    char path[SKY_SLOT_COUNT][PATH_LEN];
    bool loaded[SKY_SLOT_COUNT];
    SkySlotInfo info[SKY_SLOT_COUNT];
    UsbSkyInfo usb;
    uint8_t cursor;
    bool save_enabled;
    bool show_info;
    bool usb_ok;
    SkyBleLink* ble_link;
    bool ble_enabled;
    SkyBleBridge* ble_bridge;
    SkyGame game; /* label layout only (set by the game picked in ToyBox) */
    const char* dir; /* this game's dump folder */
    char session_file[PATH_LEN];
    char fav_file[PATH_LEN];
    char debug_file[PATH_LEN];
    uint16_t log_lines;
    char (*favs)[PATH_LEN]; /* FAV_MAX favorite dump paths for this game */
    uint8_t fav_count;
    bool was_connected; /* host has been up at least once this session */
    SkyAudio audio; /* portal speaker audio (USB OUT endpoint -> speaker) */
    bool audio_on; /* setting: play the portal's audio on the Flipper speaker */
    uint8_t audio_vol; /* setting: index into audio_gain_table */
    bool audio_hw_on; /* speaker is currently running */
    bool audio_busy; /* last attempt to take the speaker failed */
    uint32_t audio_retry_at;
    bool show_settings;
    uint8_t set_row;
    bool lights_on; /* setting: show the game's light commands on the Flipper LED / backlight */
    bool lights_used; /* we changed the LED or backlight and must give them back */
    bool bl_changed; /* the game has changed the backlight brightness */
    uint32_t lights_seq; /* last SkyLights.seq we have shown */
    uint8_t led[3]; /* ring colour currently on the LED */
    uint8_t fade_from[3];
    uint8_t fade_to[3];
    uint32_t fade_start;
    uint32_t fade_ms;
    bool fading;
    char msg[28];
    uint32_t msg_until;
} App;

static int fav_index(const App* app, const char* path);

static void set_msg(App* app, const char* text) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    snprintf(app->msg, sizeof(app->msg), "%s", text);
    app->msg_until = furi_get_tick() + 3000;
    furi_mutex_release(app->mutex);
}

/* Labels: P1 = slots 1-4, P2 = slots 5-8 (the portal protocol itself has no player or
 * figure/item field; this is only the app's labeling). */
static uint8_t player_of(uint8_t slot) {
    return slot < SKY_SLOT_COUNT / 2 ? 1 : 2;
}

/* Per player, four rows (labels only; the protocol is identical for every game).
 * Swap Force:  Figure 1, Figure 2 (a Swap Force character uses both), Item 1, Item 2.
 * Trap Team:   Figure 1, Trap 1, Item 1, Item 2.
 * Imaginators: Figure 1, Crystal 1, Item 1, Item 2 (Item = magic items).
 * SuperChargers: Figure 1, Vehicle 1, Item 1, Item 2.
 * Giants and Spyro's Adventure: Figure 1, Figure 2, Item 1, Item 2. */
static void slot_label(SkyGame game, uint8_t slot, char* out, size_t len) {
    const uint8_t r = slot % (SKY_SLOT_COUNT / 2);
    const char* kind;
    uint8_t num;
    const bool plain = game == SkyGameSwapForce || game == SkyGameGiants || game == SkyGameSpyrosAdventure;
    if(r < 2) {
        const bool second_is_special = r == 1 && !plain;
        kind = !second_is_special ? "Figure" :
               game == SkyGameTrapTeam ? "Trap" :
               game == SkyGameSuperChargers ? "Vehicle" :
                                              "Crystal";
        num = plain ? r + 1 : 1;
    } else {
        kind = "Item";
        num = r - 1;
    }
    snprintf(out, len, "%s %d (P%d)", kind, num, player_of(slot));
}

static const char* game_tag(SkyGame game) {
    return game == SkyGameSpyrosAdventure ? "SA" :
           game == SkyGameGiants ? "GI" :
           game == SkyGameTrapTeam ? "TT" :
           game == SkyGameImaginators ? "IM" :
           game == SkyGameSuperChargers ? "SC" :
                                          "SF";
}

static const char* base_name(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static const char* usb_text(const App* app) {
    if(!app->usb_ok) return "USB error";
    switch(app->usb.state) {
    case UsbSkyEnumerated:
        return "Host found";
    case UsbSkyConnected:
        return "Host connected";
    case UsbSkyActive:
        return "Game talking";
    default:
        return app->was_connected ? "Disconnected" : "Waiting for host";
    }
}

static const char* ble_text(const App* app) {
    if(!app->ble_enabled) return "BLE: Off";
    if(!app->ble_link || !sky_ble_link_connected(app->ble_link)) return "BLE: waiting for PC";
    return "BLE: PC connected";
}

/* ---------------- drawing ---------------- */

static void draw_settings(Canvas* canvas, const App* app) {
    static const char* const labels[SETTINGS_ROWS] = {
        "Audio", "Volume", "Lights", "Test tone", "Save to dump"};
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Settings");
    canvas_set_font(canvas, FontSecondary);
    const uint8_t first_row = app->set_row >= SETTINGS_VISIBLE ? app->set_row - SETTINGS_VISIBLE + 1 : 0;
    for(uint8_t r = first_row; r < first_row + SETTINGS_VISIBLE; r++) {
        const uint8_t y = 20 + (r - first_row) * 10;
        const bool sel = app->set_row == r;
        if(sel) {
            canvas_draw_box(canvas, 0, y - 8, 128, 10);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 2, y, labels[r]);
        canvas_draw_str_aligned(
            canvas,
            126,
            y - 7,
            AlignRight,
            AlignTop,
            r == 0 ? (app->audio_on ? "On" : "Off") :
            r == 1 ? audio_vol_names[app->audio_vol] :
            r == 2 ? (app->lights_on ? "On" : "Off") :
            r == 4 ? (app->save_enabled ? "RW" : "RO") :
                     "OK");
        if(sel) canvas_set_color(canvas, ColorBlack);
    }
    canvas_draw_line(canvas, 0, 54, 127, 54);
    /* Diagnostics on one line: Spk = speaker state, P = OUT packets from the game,
     * Pk = loudest queued sample (0..127). */
    const char* st = !app->audio_on ? "Off" : app->audio_busy ? "Busy" : app->audio_hw_on ? "Play" : "Idle";
    char line[40];
    snprintf(
        line,
        sizeof(line),
        "Spk:%s P:%lu Pk:%u",
        st,
        (unsigned long)app->usb.out_count,
        (unsigned)app->audio.peak);
    canvas_draw_str(canvas, 2, 63, line);
}

static void draw_cb(Canvas* canvas, void* ctx) {
    App* app = ctx;
    if(furi_mutex_acquire(app->mutex, 25) != FuriStatusOk) return;

    canvas_clear(canvas);
    if(app->show_settings) {
        draw_settings(canvas, app);
        furi_mutex_release(app->mutex);
        return;
    }
    canvas_set_font(canvas, FontPrimary);
    if(app->msg[0] && furi_get_tick() < app->msg_until) {
        canvas_draw_str(canvas, 2, 10, app->msg);
    } else if(app->show_info) {
        char t[20];
        slot_label(app->game, app->cursor, t, sizeof(t));
        canvas_draw_str(canvas, 2, 10, t);
    } else {
        canvas_draw_str(canvas, 2, 10, "Skylanders Portal");
    }
    canvas_set_font(canvas, FontSecondary);
    char tag[12];
    snprintf(tag, sizeof(tag), "%s %s", game_tag(app->game), app->save_enabled ? "RW" : "RO");
    canvas_draw_str_aligned(canvas, 126, 2, AlignRight, AlignTop, tag);

    char line[48];
    if(app->show_info) {
        const SkySlotInfo* in = &app->info[app->cursor];
        if(app->loaded[app->cursor]) {
            snprintf(line, sizeof(line), "%.24s", base_name(app->path[app->cursor]));
            canvas_draw_str(canvas, 2, 22, line);
            snprintf(line, sizeof(line), "Serial  %08lX", (unsigned long)in->serial);
            canvas_draw_str(canvas, 2, 33, line);
            snprintf(
                line,
                sizeof(line),
                "Type %04X  Fav: %s",
                (unsigned)in->type_id,
                fav_index(app, app->path[app->cursor]) >= 0 ? "yes" : "no");
            canvas_draw_str(canvas, 2, 44, line);
            snprintf(
                line,
                sizeof(line),
                "Unsaved changes: %s",
                in->dirty ? (app->save_enabled ? "saving" : "yes (RO)") : "no");
            canvas_draw_str(canvas, 2, 55, line);
        } else if(in->present) {
            canvas_draw_str(canvas, 2, 22, "From PC (Bluetooth)");
            snprintf(line, sizeof(line), "Serial  %08lX", (unsigned long)in->serial);
            canvas_draw_str(canvas, 2, 33, line);
            snprintf(line, sizeof(line), "Type %04X", (unsigned)in->type_id);
            canvas_draw_str(canvas, 2, 44, line);
        } else {
            canvas_draw_str(canvas, 2, 22, "Empty slot");
            snprintf(line, sizeof(line), "Last cmd: len %u", (unsigned)app->usb.last_len);
            canvas_draw_str(canvas, 2, 33, line);
            const uint8_t* r = app->usb.last_raw;
            snprintf(line, sizeof(line), "%02X %02X %02X %02X", r[0], r[1], r[2], r[3]);
            canvas_draw_str(canvas, 2, 44, line);
            snprintf(line, sizeof(line), "%02X %02X %02X %02X", r[4], r[5], r[6], r[7]);
            canvas_draw_str(canvas, 2, 55 - 1, line);
        }
    } else {
        const uint8_t first = app->cursor > 3 ? app->cursor - 3 : 0;
        for(uint8_t r = 0; r < 4; r++) {
            const uint8_t slot = first + r;
            const uint8_t y = 21 + r * 10;
            const char* name = app->loaded[slot] ? base_name(app->path[slot]) :
                               app->info[slot].present ? "(PC)" : "-";
            char lbl[20];
            slot_label(app->game, slot, lbl, sizeof(lbl));
            const bool sel = slot == app->cursor;
            if(sel) {
                canvas_draw_box(canvas, 0, y - 9, 128, 11);
                canvas_set_color(canvas, ColorWhite);
            }
            if(app->loaded[slot] && app->info[slot].dirty) {
                size_t n = strlen(lbl);
                if(n < sizeof(lbl) - 1) {
                    lbl[n] = '*';
                    lbl[n + 1] = '\0';
                }
            }
            canvas_draw_str(canvas, 2, y, lbl);
            snprintf(line, sizeof(line), "%.8s", name);
            canvas_draw_str_aligned(canvas, 126, y - 8, AlignRight, AlignTop, line);
            if(sel) canvas_set_color(canvas, ColorBlack);
        }
    }

    canvas_draw_line(canvas, 0, 54, 127, 54);
    /* USB and Bluetooth state share the bottom-left line, alternating every 3 s. */
    canvas_draw_str(
        canvas, 2, 63, app->ble_enabled && (furi_get_tick() / 3000) % 2 == 0 ? ble_text(app) : usb_text(app));
    if(app->usb.cmd_count > 0) {
        char letters[8];
        uint8_t n = 0;
        for(uint8_t k = 0; k < 6; k++) {
            if(app->usb.last_cmds[k]) letters[n++] = app->usb.last_cmds[k];
        }
        letters[n] = '\0';
        snprintf(
            line,
            sizeof(line),
            "%lu %s",
            (unsigned long)app->usb.cmd_count,
            letters + (n > 5 ? n - 5 : 0));
        canvas_draw_str_aligned(canvas, 126, 56, AlignRight, AlignTop, line);
    }

    furi_mutex_release(app->mutex);
}

static void input_cb(InputEvent* event, void* ctx) {
    App* app = ctx;
    furi_message_queue_put(app->queue, event, 0);
}

/* ---------------- core access (shared with USB interrupt, so guarded) ---------------- */

static int core_load(App* app, const uint8_t* data, int target) {
    int slot;
    FURI_CRITICAL_ENTER();
    slot = target >= 0 ? sky_base_load_figure_at(app->base, (uint8_t)target, data) :
                         sky_base_load_figure(app->base, data);
    FURI_CRITICAL_EXIT();
    return slot;
}

static void core_remove(App* app, uint8_t slot) {
    FURI_CRITICAL_ENTER();
    sky_base_remove_figure(app->base, slot);
    FURI_CRITICAL_EXIT();
}

static bool core_take_dirty(App* app, uint8_t slot) {
    bool dirty;
    FURI_CRITICAL_ENTER();
    dirty = sky_base_take_dirty(app->base, slot, app->io_buf);
    FURI_CRITICAL_EXIT();
    return dirty;
}

static void core_mark_dirty(App* app, uint8_t slot) {
    FURI_CRITICAL_ENTER();
    sky_base_mark_dirty(app->base, slot);
    FURI_CRITICAL_EXIT();
}

/* Refresh the UI's copy of per-slot info and USB state. */
static void refresh(App* app) {
    SkySlotInfo tmp[SKY_SLOT_COUNT];
    FURI_CRITICAL_ENTER();
    for(uint8_t i = 0; i < SKY_SLOT_COUNT; i++) sky_base_slot_info(app->base, i, &tmp[i]);
    FURI_CRITICAL_EXIT();
    UsbSkyInfo usb = usb_skylander_get_info();
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    memcpy(app->info, tmp, sizeof(tmp));
    app->usb = usb;
    furi_mutex_release(app->mutex);
}

/* ---------------- favorites (per game folder) ---------------- */

static int fav_index(const App* app, const char* path) {
    for(uint8_t i = 0; i < app->fav_count; i++) {
        if(strcmp(app->favs[i], path) == 0) return i;
    }
    return -1;
}

static void fav_save(App* app) {
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, app->fav_file, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        for(uint8_t i = 0; i < app->fav_count; i++) {
            storage_file_write(f, app->favs[i], strlen(app->favs[i]));
            storage_file_write(f, "\n", 1);
        }
    }
    storage_file_close(f);
    storage_file_free(f);
}

static void fav_restore(App* app) {
    File* f = storage_file_alloc(app->storage);
    size_t len = 0;
    if(storage_file_open(f, app->fav_file, FSAM_READ, FSOM_OPEN_EXISTING)) {
        len = storage_file_read(f, app->txt, TXT_LEN - 1);
    }
    storage_file_close(f);
    storage_file_free(f);
    if(len == 0) return;
    app->txt[len] = '\0';
    char* p = app->txt;
    while(*p && app->fav_count < FAV_MAX) {
        char* nl = strchr(p, '\n');
        if(nl) *nl = '\0';
        size_t l = strlen(p);
        if(l && p[l - 1] == '\r') p[l - 1] = '\0';
        if(p[0] && strlen(p) < PATH_LEN && fav_index(app, p) < 0) {
            snprintf(app->favs[app->fav_count++], PATH_LEN, "%s", p);
        }
        if(!nl) break;
        p = nl + 1;
    }
}

/* Add or remove the figure in the current slot as a favorite. */
static void fav_toggle(App* app) {
    if(!app->loaded[app->cursor]) {
        set_msg(app, "Load a figure first");
        return;
    }
    const char* path = app->path[app->cursor];
    int idx = fav_index(app, path);
    if(idx >= 0) {
        for(uint8_t i = (uint8_t)idx; i + 1 < app->fav_count; i++) {
            memcpy(app->favs[i], app->favs[i + 1], PATH_LEN);
        }
        app->fav_count--;
        set_msg(app, "Removed from favorites");
    } else if(app->fav_count >= FAV_MAX) {
        set_msg(app, "Favorites full (16)");
        return;
    } else {
        snprintf(app->favs[app->fav_count++], PATH_LEN, "%s", path);
        set_msg(app, "Added to favorites");
    }
    fav_save(app);
}

/* ---------------- files ---------------- */

/* Write game changes back into the dump (in place, same size). */
static void save_slot(App* app, uint8_t slot) {
    if(!app->loaded[slot] || !app->save_enabled) return; /* RO: changes stay in RAM, marked * */
    if(!core_take_dirty(app, slot)) return;

    File* f = storage_file_alloc(app->storage);
    bool ok = false;
    if(storage_file_open(f, app->path[slot], FSAM_WRITE, FSOM_OPEN_EXISTING)) {
        ok = storage_file_write(f, app->io_buf, SKY_FIGURE_SIZE) == SKY_FIGURE_SIZE;
    }
    storage_file_close(f);
    storage_file_free(f);
    if(!ok) {
        core_mark_dirty(app, slot);
        set_msg(app, "Save failed");
        notification_message(app->notif, &sequence_error);
    }
}

static void session_save(App* app) {
    int n = snprintf(app->txt, TXT_LEN, "%s\n", app->save_enabled ? "RW" : "RO");
    for(uint8_t i = 0; i < SKY_SLOT_COUNT; i++) {
        if(app->loaded[i] && n < TXT_LEN - PATH_LEN - 2) {
            n += snprintf(app->txt + n, TXT_LEN - n, "%d|%s\n", i + 1, app->path[i]);
        }
    }
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, app->session_file, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, app->txt, (size_t)n);
    }
    storage_file_close(f);
    storage_file_free(f);
}

static LoadResult load_path(App* app, const char* path, int target) {
    if(strlen(path) >= PATH_LEN) return LoadLong;
    for(uint8_t i = 0; i < SKY_SLOT_COUNT; i++) {
        if(app->loaded[i] && strcmp(app->path[i], path) == 0) return LoadDup;
    }

    File* f = storage_file_alloc(app->storage);
    bool ok = storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
              storage_file_size(f) == SKY_FIGURE_SIZE &&
              storage_file_read(f, app->io_buf, SKY_FIGURE_SIZE) == SKY_FIGURE_SIZE;
    storage_file_close(f);
    storage_file_free(f);
    if(!ok) return LoadBadFile;

    int slot = core_load(app, app->io_buf, target);
    if(slot < 0) return LoadFull;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    snprintf(app->path[slot], PATH_LEN, "%s", path);
    app->loaded[slot] = true;
    app->cursor = (uint8_t)slot;
    furi_mutex_release(app->mutex);
    return LoadOk;
}

static void session_restore(App* app) {
    File* f = storage_file_alloc(app->storage);
    size_t len = 0;
    if(storage_file_open(f, app->session_file, FSAM_READ, FSOM_OPEN_EXISTING)) {
        len = storage_file_read(f, app->txt, TXT_LEN - 1);
    }
    storage_file_close(f);
    storage_file_free(f);
    if(len == 0) return;
    app->txt[len] = '\0';

    uint8_t restored = 0;
    bool first = true;
    char* p = app->txt;
    while(*p) {
        char* nl = strchr(p, '\n');
        if(nl) *nl = '\0';
        size_t l = strlen(p);
        if(l && p[l - 1] == '\r') p[l - 1] = '\0';
        if(first) {
            app->save_enabled = strcmp(p, "RO") != 0;
            first = false;
        } else if(p[0]) {
            int target = -1;
            const char* path = p;
            if(p[0] >= '1' && p[0] <= '8' && p[1] == '|') {
                target = p[0] - '1';
                path = p + 2;
            }
            if(load_path(app, path, target) == LoadOk) restored++;
        }
        if(!nl) break;
        p = nl + 1;
    }
    if(restored) {
        char m[28];
        snprintf(m, sizeof(m), "Restored %d figure%s", restored, restored == 1 ? "" : "s");
        set_msg(app, m);
    }
}

/* ---------------- Bluetooth link (PC portal app) ---------------- */

/* SkyBase is shared with the USB interrupt, so sky_ble guards it with a critical section.
 * FURI_CRITICAL_ENTER cannot be split across two functions, hence the raw calls. */
static __FuriCriticalInfo ble_crit_state;
static void ble_lock(void) {
    ble_crit_state = __furi_critical_enter();
}
static void ble_unlock(void) {
    __furi_critical_exit(ble_crit_state);
}

/* Data the PC never received: keep it as <game folder>/unsent_slotN.bin. */
static void ble_unsent_cb(void* ctx, uint8_t slot, const uint8_t data[SKY_FIGURE_SIZE]) {
    App* app = ctx;
    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s/unsent_slot%u.bin", app->dir, (unsigned)slot);
    File* f = storage_file_alloc(app->storage);
    bool ok = false;
    if(storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = storage_file_write(f, data, SKY_FIGURE_SIZE) == SKY_FIGURE_SIZE;
    }
    storage_file_close(f);
    storage_file_free(f);
    set_msg(app, ok ? "Unsent data kept" : "Unsent save failed");
}

/* After the PC changed the portal: a slot it overwrote or emptied is no longer the SD file
 * we remember, so the SD file must never receive its data. */
static void ble_sync_loaded(App* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    for(uint8_t i = 0; i < SKY_SLOT_COUNT; i++) {
        if(app->loaded[i] &&
           (sky_ble_bridge_slot_remote(app->ble_bridge, i) || !sky_base_slot_present(app->base, i))) {
            app->loaded[i] = false;
            app->path[i][0] = '\0';
        }
    }
    furi_mutex_release(app->mutex);
}

static void ble_service(App* app) {
    if(!app->ble_enabled || !app->ble_link || !app->ble_bridge) return;
    /* Write pending changes of SD-loaded figures first: a PC CLEAR may remove them. */
    for(uint8_t i = 0; i < SKY_SLOT_COUNT; i++) save_slot(app, i);
    sky_ble_bridge_set_usb_state(app->ble_bridge, (uint8_t)app->usb.state);
    sky_ble_link_pump(app->ble_link, app->ble_bridge);
    ble_sync_loaded(app);
    sky_ble_bridge_poll_saves(app->ble_bridge, app->io_buf, ble_unsent_cb, app);
}

static void ble_stop(App* app) {
    if(app->ble_bridge) {
        sky_ble_bridge_flush_unsent(app->ble_bridge, app->io_buf, ble_unsent_cb, app);
        sky_ble_bridge_free(app->ble_bridge);
        app->ble_bridge = NULL;
    }
    if(app->ble_link) {
        sky_ble_link_stop(app->ble_link);
        app->ble_link = NULL;
    }
    app->ble_enabled = false;
}

static void toggle_ble(App* app) {
    if(app->ble_enabled) {
        ble_stop(app);
        set_msg(app, "BLE off");
        return;
    }
    app->ble_link = sky_ble_link_start();
    app->ble_bridge = app->ble_link ? sky_ble_bridge_alloc(app->base) : NULL;
    if(!app->ble_bridge) {
        if(app->ble_link) sky_ble_link_stop(app->ble_link);
        app->ble_link = NULL;
        set_msg(app, "BLE start failed");
        return;
    }
    sky_ble_bridge_configure(app->ble_bridge, (uint8_t)app->game, ble_lock, ble_unlock);
    sky_ble_bridge_set_link(app->ble_bridge, app->ble_link);
    app->ble_enabled = true;
    set_msg(app, "BLE on");
}

/* A figure with no SD file behind it (sent by the PC, or left on the portal when Bluetooth
 * went off): send any pending game changes, then take it off. */
static void remove_unfiled(App* app, uint8_t slot) {
    if(app->ble_bridge) {
        sky_ble_bridge_poll_saves(app->ble_bridge, app->io_buf, ble_unsent_cb, app);
    }
    core_remove(app, slot);
    if(app->ble_bridge) sky_ble_bridge_forget_slot(app->ble_bridge, slot);
}

/* ---------------- actions ---------------- */

static void unload_slot(App* app, uint8_t slot) {
    save_slot(app, slot);
    core_remove(app, slot);
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->loaded[slot] = false;
    app->path[slot][0] = '\0';
    furi_mutex_release(app->mutex);
}

static void load_new(App* app) {
    FuriString* picked = furi_string_alloc();
    FuriString* start = furi_string_alloc_set_str(app->dir);
    DialogsFileBrowserOptions opts;
    dialog_file_browser_set_basic_options(&opts, "*", NULL);

    if(dialog_file_browser_show(app->dialogs, picked, start, &opts)) {
        switch(load_path(app, furi_string_get_cstr(picked), app->cursor)) {
        case LoadOk: {
            char m[28];
            char lbl[20];
            slot_label(app->game, app->cursor, lbl, sizeof(lbl));
            snprintf(m, sizeof(m), "Loaded %s", lbl);
            set_msg(app, m);
            notification_message(app->notif, &sequence_success);
            session_save(app);
            break;
        }
        case LoadDup:
            set_msg(app, "Already loaded");
            break;
        case LoadFull:
            set_msg(app, "Portal full");
            break;
        case LoadLong:
            set_msg(app, "Path too long");
            break;
        default:
            set_msg(app, "Need 1024-byte dump");
            notification_message(app->notif, &sequence_error);
            break;
        }
    }
    furi_string_free(picked);
    furi_string_free(start);
}

/* Quick switch: put favorite #idx into the current slot, replacing what is there. */
static void quick_switch(App* app, uint8_t idx) {
    const uint8_t slot = app->cursor;
    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s", app->favs[idx]);
    if(app->loaded[slot] && strcmp(app->path[slot], path) == 0) {
        set_msg(app, "Already in this slot");
        return;
    }
    for(uint8_t i = 0; i < SKY_SLOT_COUNT; i++) {
        if(i != slot && app->loaded[i] && strcmp(app->path[i], path) == 0) {
            set_msg(app, "Loaded in another slot");
            return;
        }
    }
    if(app->loaded[slot]) unload_slot(app, slot);
    char lbl[20];
    slot_label(app->game, slot, lbl, sizeof(lbl));
    if(load_path(app, path, slot) == LoadOk) {
        char m[28];
        snprintf(m, sizeof(m), "Loaded %s", lbl);
        set_msg(app, m);
        notification_message(app->notif, &sequence_success);
    } else {
        set_msg(app, "Favorite file missing");
        notification_message(app->notif, &sequence_error);
    }
    session_save(app);
}

/* Quick switch with Left/Right: step through the favorites in the current slot. */
static void fav_step(App* app, int dir) {
    const int n = app->fav_count;
    if(n == 0) {
        set_msg(app, "No favorites: hold OK");
        return;
    }
    const int cur = app->loaded[app->cursor] ? fav_index(app, app->path[app->cursor]) : -1;
    const int start = cur >= 0 ? cur : (dir > 0 ? -1 : n);
    for(int k = 1; k <= n; k++) {
        const int cand = ((start + dir * k) % n + n) % n;
        bool elsewhere = false;
        for(uint8_t i = 0; i < SKY_SLOT_COUNT; i++) {
            if(i != app->cursor && app->loaded[i] && strcmp(app->path[i], app->favs[cand]) == 0) {
                elsewhere = true;
            }
        }
        if(elsewhere) continue;
        quick_switch(app, (uint8_t)cand);
        return;
    }
    set_msg(app, "No other favorites");
}

/* ---------------- settings and audio ---------------- */

static void audio_apply(App* app) {
    sky_audio_set_gain(&app->audio, audio_gain_table[app->audio_vol]);
    app->audio.accept = app->audio_on;
}

static void settings_save(App* app) {
    int n = snprintf(
        app->txt,
        TXT_LEN,
        "audio=%d\nvolume=%d\nlights=%d\n",
        app->audio_on ? 1 : 0,
        (int)app->audio_vol,
        app->lights_on ? 1 : 0);
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, SKY_SETTINGS_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, app->txt, (size_t)n);
    }
    storage_file_close(f);
    storage_file_free(f);
}

static void settings_restore(App* app) {
    File* f = storage_file_alloc(app->storage);
    size_t len = 0;
    if(storage_file_open(f, SKY_SETTINGS_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        len = storage_file_read(f, app->txt, TXT_LEN - 1);
    }
    storage_file_close(f);
    storage_file_free(f);
    if(len > 0) {
        app->txt[len] = '\0';
        char* p = app->txt;
        while(*p) {
            char* nl = strchr(p, '\n');
            if(nl) *nl = '\0';
            if(strncmp(p, "audio=", 6) == 0) {
                app->audio_on = p[6] != '0';
            } else if(strncmp(p, "lights=", 7) == 0) {
                app->lights_on = p[7] != '0';
            } else if(strncmp(p, "volume=", 7) == 0) {
                const int v = p[7] - '0';
                if(v >= 0 && v < AUDIO_VOL_COUNT) app->audio_vol = (uint8_t)v;
            }
            if(!nl) break;
            p = nl + 1;
        }
    }
    audio_apply(app);
}

/* Called from the USB interrupt on every 'M' command: wake the main loop so the speaker
 * starts (or stops) without waiting for the next poll. */
static void audio_wake_cb(void* ctx) {
    App* app = ctx;
    InputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.key = InputKeyBack;
    ev.type = InputTypeMAX; /* not a real key press: ignored by the input handlers */
    furi_message_queue_put(app->queue, &ev, 0);
}

/* Start the speaker while the host has it on (and keep going until the queued audio has
 * played out); stop it otherwise. Runs in the main loop: taking the speaker can block. */
static void audio_service(App* app) {
    const bool host_up = app->usb_ok && app->usb.state != UsbSkyDetached;
    const bool want = (app->audio_on && host_up &&
                       (app->base->speaker_on || sky_audio_count(&app->audio) > 0)) ||
                      sky_audio_hw_tone_active();
    if(want && !app->audio_hw_on) {
        if(furi_get_tick() >= app->audio_retry_at) {
            if(sky_audio_hw_start(&app->audio)) {
                app->audio_hw_on = true;
                app->audio_busy = false;
            } else {
                app->audio_busy = true;
                app->audio_retry_at = furi_get_tick() + 2000;
                set_msg(app, "Speaker busy");
            }
        }
    } else if(!want && app->audio_hw_on) {
        sky_audio_hw_stop();
        app->audio_hw_on = false;
    }
    if(!app->audio_on) app->audio_busy = false;
    if(!app->audio_on && !app->audio_hw_on) sky_audio_flush(&app->audio);
}

/* ---------------- lights: show the game's colour / backlight commands ---------------- */

static void led_set(App* app, uint8_t r, uint8_t g, uint8_t b) {
    furi_hal_light_set(LightRed, r);
    furi_hal_light_set(LightGreen, g);
    furi_hal_light_set(LightBlue, b);
    app->led[0] = r;
    app->led[1] = g;
    app->led[2] = b;
    app->lights_used = true;
}

/* Give the LED and backlight back to the system. */
static void lights_release(App* app) {
    app->fading = false;
    if(!app->lights_used) return;
    notification_message(app->notif, &sequence_reset_rgb);
    notification_message(app->notif, &sequence_display_backlight_on);
    app->led[0] = app->led[1] = app->led[2] = 0;
    app->bl_changed = false;
    app->lights_used = false;
}

/* A key press brings the screen back if the game turned the backlight down. */
static void backlight_restore(App* app) {
    if(!app->bl_changed) return;
    notification_message(app->notif, &sequence_display_backlight_on);
    app->bl_changed = false;
}

static uint8_t lerp8(uint8_t from, uint8_t to, uint32_t elapsed, uint32_t total) {
    const int32_t span = (int32_t)to - (int32_t)from;
    return (uint8_t)((int32_t)from + (span * (int32_t)elapsed) / (int32_t)total);
}

/* Runs in the main loop: the LED driver must not be touched from the USB interrupt. */
static void lights_service(App* app) {
    SkyLights lights;
    FURI_CRITICAL_ENTER();
    lights = app->base->lights;
    FURI_CRITICAL_EXIT();

    const bool host_up = app->usb_ok && app->usb.state != UsbSkyDetached;
    if(!app->lights_on || !host_up) {
        lights_release(app);
        /* Lights off: forget what we saw, so turning them on shows the game's current state.
         * Host gone: do not bring back stale colours when it returns. */
        app->lights_seq = app->lights_on ? lights.seq : 0;
        return;
    }

    if(lights.seq != app->lights_seq) {
        app->lights_seq = lights.seq;
        if(lights.fade_ms > 0) {
            memcpy(app->fade_from, app->led, sizeof(app->fade_from));
            memcpy(app->fade_to, lights.ring, sizeof(app->fade_to));
            app->fade_start = furi_get_tick();
            app->fade_ms = lights.fade_ms;
            app->fading = true;
        } else {
            app->fading = false;
            led_set(app, lights.ring[0], lights.ring[1], lights.ring[2]);
        }
        if(lights.backlight_set) {
            furi_hal_light_set(LightBacklight, lights.backlight);
            app->bl_changed = true;
            app->lights_used = true;
        }
    }

    if(app->fading) {
        const uint32_t elapsed = furi_get_tick() - app->fade_start;
        if(elapsed >= app->fade_ms) {
            app->fading = false;
            led_set(app, app->fade_to[0], app->fade_to[1], app->fade_to[2]);
        } else {
            led_set(
                app,
                lerp8(app->fade_from[0], app->fade_to[0], elapsed, app->fade_ms),
                lerp8(app->fade_from[1], app->fade_to[1], elapsed, app->fade_ms),
                lerp8(app->fade_from[2], app->fade_to[2], elapsed, app->fade_ms));
        }
    }
}

/* Settings -> Test tone: a 1 s beep through the same speaker path, no game needed. */
static void audio_test_tone(App* app) {
    if(!app->audio_hw_on) {
        if(!sky_audio_hw_start(&app->audio)) {
            app->audio_busy = true;
            return;
        }
        app->audio_hw_on = true;
        app->audio_busy = false;
    }
    sky_audio_hw_tone(1000);
}

static void settings_input(App* app, const InputEvent* ev) {
    const bool press = ev->type == InputTypeShort || ev->type == InputTypeRepeat;
    switch(ev->key) {
    case InputKeyUp:
    case InputKeyDown:
        if(press) {
            app->set_row = ev->key == InputKeyDown ?
                               (app->set_row + 1) % SETTINGS_ROWS :
                               (app->set_row + SETTINGS_ROWS - 1) % SETTINGS_ROWS;
        }
        break;
    case InputKeyLeft:
    case InputKeyRight:
    case InputKeyOk:
        if(!press) break;
        if(app->set_row == 4) {
            app->save_enabled = !app->save_enabled;
            set_msg(app, app->save_enabled ? "Read-write" : "Read-only");
            session_save(app);
            break;
        }
        if(app->set_row == 3) {
            audio_test_tone(app);
            break;
        }
        if(app->set_row == 2) {
            app->lights_on = !app->lights_on;
            set_msg(app, app->lights_on ? "Lights on" : "Lights off");
            settings_save(app);
            break;
        }
        if(app->set_row == 0) {
            app->audio_on = !app->audio_on;
            set_msg(app, app->audio_on ? "Audio on" : "Audio off");
        } else {
            const uint8_t step = ev->key == InputKeyLeft ? AUDIO_VOL_COUNT - 1 : 1;
            app->audio_vol = (app->audio_vol + step) % AUDIO_VOL_COUNT;
        }
        audio_apply(app);
        settings_save(app);
        break;
    case InputKeyBack:
        if(ev->type == InputTypeShort) app->show_settings = false;
        break;
    default:
        break;
    }
}

/* ---------------- debug log ---------------- */

#define DEBUG_LOG_MAX 600 /* stop after this many lines so the SD card is not flooded */

/* Append the host/portal traffic to <game folder>/debug.txt (recreated at every launch).
 * Lines: <ms> C|R len=<n> <first 8 bytes>   (C = command from the game, R = our reply). */
static void debug_log_drain(App* app) {
    UsbSkyLogEntry e;
    File* f = NULL;
    while(app->log_lines < DEBUG_LOG_MAX && usb_skylander_log_pop(&e)) {
        if(!f) {
            f = storage_file_alloc(app->storage);
            if(!storage_file_open(f, app->debug_file, FSAM_WRITE, FSOM_OPEN_APPEND)) break;
        }
        char line[64];
        int n = snprintf(
            line,
            sizeof(line),
            "%lu %c len=%u %02X %02X %02X %02X %02X %02X %02X %02X\n",
            (unsigned long)e.tick,
            e.kind,
            (unsigned)e.len,
            e.raw[0],
            e.raw[1],
            e.raw[2],
            e.raw[3],
            e.raw[4],
            e.raw[5],
            e.raw[6],
            e.raw[7]);
        if(n > 0) storage_file_write(f, line, (size_t)n);
        app->log_lines++;
    }
    if(f) {
        storage_file_close(f);
        storage_file_free(f);
    }
}

/* ---------------- main ---------------- */

/* p: SkyGame value cast to a pointer (NULL = Swap Force). */
int32_t skybase_app(void* p) {

    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->base = malloc(sizeof(SkyBase));
    sky_base_init(app->base);
    app->io_buf = malloc(SKY_FIGURE_SIZE);
    app->txt = malloc(TXT_LEN);
    app->favs = malloc((size_t)FAV_MAX * PATH_LEN);
    app->save_enabled = true;
    app->audio_on = true;
    app->audio_vol = 1;
    app->lights_on = true;
    sky_audio_init(&app->audio);
    app->game = (SkyGame)(uintptr_t)p;
    /* Experimental: the original game may expect an older portal firmware version in the
     * reply to 'R' (0x18) than the later games (0x1B). Only applied to Spyro's Adventure. */
    if(app->game == SkyGameSpyrosAdventure) app->base->fw_version = 0x18;
    app->dir = app->game == SkyGameSpyrosAdventure ? SKY_DIR_SPYRO :
               app->game == SkyGameGiants ? SKY_DIR_GIANTS :
               app->game == SkyGameTrapTeam ? SKY_DIR_TRAP :
               app->game == SkyGameImaginators ? SKY_DIR_IMAG :
               app->game == SkyGameSuperChargers ? SKY_DIR_SUPER :
                                                   SKY_DIR_SWAP;
    snprintf(app->session_file, sizeof(app->session_file), "%s/session.txt", app->dir);
    snprintf(app->fav_file, sizeof(app->fav_file), "%s/favorites.txt", app->dir);
    snprintf(app->debug_file, sizeof(app->debug_file), "%s/debug.txt", app->dir);
    app->queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->notif = furi_record_open(RECORD_NOTIFICATION);

    storage_simply_mkdir(app->storage, EXT_PATH("apps_data"));
    storage_simply_mkdir(app->storage, SKY_ROOT_DIR);
    storage_simply_mkdir(app->storage, app->dir);
    storage_simply_remove(app->storage, app->debug_file); /* fresh log each launch */

    usb_skylander_attach_base(app->base);
    settings_restore(app);
    usb_skylander_attach_audio(&app->audio);
    usb_skylander_set_wake(audio_wake_cb, app);
    FuriHalUsbInterface* usb_prev = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    app->usb_ok = furi_hal_usb_set_config(&usb_skylander, NULL);

    app->ble_enabled = false;
    app->ble_link = NULL;
    app->ble_bridge = NULL;


    fav_restore(app);
    session_restore(app);
    if(!app->msg[0]) set_msg(app, "OK:load HoldOK:fav");

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_cb, app);
    view_port_input_callback_set(app->view_port, input_cb, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    UsbSkyState prev_state = UsbSkyDetached;
    bool running = true;
    InputEvent ev;
    while(running) {
        /* Poll faster while the speaker runs (so it stops soon after the audio ends) and while
         * a light fade is running (so it looks smooth). */
        uint32_t wait = app->fading ? 30 : app->audio_hw_on ? 50 : 250;
        if(app->ble_enabled && wait > 40) wait = 40; /* keep pumping the Bluetooth link */
        if(furi_message_queue_get(app->queue, &ev, wait) == FuriStatusOk) {
            if(ev.type == InputTypePress || ev.type == InputTypeShort) backlight_restore(app);
            const bool short_press = ev.type == InputTypeShort;
            const bool repeat = ev.type == InputTypeRepeat;
            if(app->show_settings) {
                settings_input(app, &ev);
            } else
            switch(ev.key) {
            case InputKeyUp:
                if(short_press || repeat)
                    app->cursor = (app->cursor + SKY_SLOT_COUNT - 1) % SKY_SLOT_COUNT;
                break;
            case InputKeyDown:
                if(short_press || repeat) app->cursor = (app->cursor + 1) % SKY_SLOT_COUNT;
                break;
            case InputKeyOk:
                if(short_press) {
                    if(app->loaded[app->cursor]) {
                        unload_slot(app, app->cursor);
                        set_msg(app, "Removed");
                        session_save(app);
                    } else if(app->info[app->cursor].present) {
                        remove_unfiled(app, app->cursor);
                        set_msg(app, "Removed");
                    } else {
                        load_new(app);
                    }
                } else if(ev.type == InputTypeLong) {
                    fav_toggle(app);
                }
                break;
            case InputKeyRight:
                if(short_press) {
                    fav_step(app, 1);
                } else if(ev.type == InputTypeLong) {
                    app->show_info = !app->show_info;
                }
                break;
            case InputKeyLeft:
                if(short_press) {
                    fav_step(app, -1);
                } else if(ev.type == InputTypeLong) {
                    toggle_ble(app);
                }
                break;
            case InputKeyBack:
                if(short_press) {
                    if(app->show_info) {
                        app->show_info = false;
                    } else {
                        running = false;
                    }
                } else if(ev.type == InputTypeLong) {
                    app->show_settings = true; /* hold Back: settings */
                }
                break;
            default:
                break;
            }
        }

        for(uint8_t i = 0; i < SKY_SLOT_COUNT; i++) save_slot(app, i);
        debug_log_drain(app);
        refresh(app);
        audio_service(app);
        lights_service(app);

        /* connection change feedback */
        if(app->usb_ok && app->usb.state != prev_state) {
            const bool was_up = prev_state >= UsbSkyConnected;
            const bool is_up = app->usb.state >= UsbSkyConnected;
            if(is_up) app->was_connected = true;
            if(is_up && !was_up) {
                set_msg(app, "Host connected");
                notification_message(app->notif, &sequence_success);
            } else if(!is_up && was_up) {
                set_msg(app, "Host disconnected");
                notification_message(app->notif, &sequence_error);
            } else if(app->usb.state == UsbSkyEnumerated) {
                set_msg(app, "Host found");
            }
            prev_state = app->usb.state;
        }

        ble_service(app);

        view_port_update(app->view_port);
    }

    for(uint8_t i = 0; i < SKY_SLOT_COUNT; i++) save_slot(app, i);
    session_save(app);
    lights_release(app); /* LED and backlight back to the system */

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);

    ble_stop(app); /* keeps any unsent changes, then gives Bluetooth back */



    if(app->usb_ok) {
        furi_hal_usb_set_config(usb_prev, NULL);
        /* The switch is carried out by the USB thread. Wait (max 2 s) so the USB interrupt
         * is no longer using the base before it is freed, and so the launcher sees the
         * restored mode. */
        for(uint8_t i = 0; i < 40 && furi_hal_usb_get_config() != usb_prev; i++) {
            furi_delay_ms(50);
        }
    }
    app->audio.accept = false;
    usb_skylander_set_wake(NULL, NULL);
    usb_skylander_attach_audio(NULL);
    sky_audio_hw_stop();
    usb_skylander_attach_base(NULL);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    furi_mutex_free(app->mutex);
    furi_message_queue_free(app->queue);
    free(app->txt);
    free(app->favs);
    free(app->io_buf);
    free(app->base);
    free(app);
    return 0;
}
