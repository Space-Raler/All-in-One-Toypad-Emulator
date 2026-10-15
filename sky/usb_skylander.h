#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <furi.h>
#include <furi_hal_usb.h>
#include "core/sky_core.h"
#include "core/sky_audio.h"

/* USB HID device 1430:0150 (Skylanders Portal of Power). */
extern FuriHalUsbInterface usb_skylander;

/* Set before furi_hal_usb_set_config(); pass NULL after switching USB away.
 * The base is touched from the USB interrupt, so app-side access must be
 * inside FURI_CRITICAL_ENTER()/EXIT(). */
void usb_skylander_attach_base(SkyBase* base);

/* Audio sink for the OUT endpoint (NULL = discard). Samples are queued only while
 * audio->accept is true and the host has switched the speaker on ('M'). */
void usb_skylander_attach_audio(SkyAudio* audio);

/* Called from the USB interrupt whenever the host sends an 'M' command, so the app can start
 * or stop the speaker without polling. Must be interrupt-safe (e.g. post to a queue). */
typedef void (*UsbSkyWakeCb)(void* ctx);
void usb_skylander_set_wake(UsbSkyWakeCb cb, void* ctx);

typedef enum {
    UsbSkyDetached, /* not configured, or host stopped polling */
    UsbSkyEnumerated, /* host configured the device but is not reading yet */
    UsbSkyConnected, /* host is polling the portal */
    UsbSkyActive, /* host sent a command in the last 3 s (a game is talking) */
} UsbSkyState;

typedef struct {
    UsbSkyState state;
    uint32_t cmd_count; /* commands received since app start */
    char last_cmds[8]; /* first byte of the most recent commands, newest last (diagnostics) */
    uint16_t last_len; /* wLength of the last SET_REPORT */
    uint8_t last_raw[8]; /* its first 8 raw bytes (diagnostics) */
    uint32_t out_count; /* packets received on the OUT endpoint (audio) */
} UsbSkyInfo;

UsbSkyInfo usb_skylander_get_info(void);

/* Traffic log for debugging: every command from the host ('C') and every queued reply we
 * send back ('R'), oldest first. The USB interrupt fills a ring, the app drains it. */
typedef struct {
    char kind; /* 'C' = command from host, 'R' = reply to host */
    uint16_t len; /* length of the command (for 'R': always 32) */
    uint8_t raw[8]; /* first 8 bytes */
    uint32_t tick; /* ms */
} UsbSkyLogEntry;
bool usb_skylander_log_pop(UsbSkyLogEntry* out);
