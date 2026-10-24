#pragma once
#include <furi.h>
#include <furi_hal.h>
#include <pthread.h>

#include "frame.h"
#include "../ToyPadEmu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HID_EP_SZ  0x20 // 32 bytes packet size
#define HID_EP_IN  0x81
#define HID_EP_OUT 0x01

enum ConnectedStatus {
    ConnectedStatusDisconnected = 0,
    ConnectedStatusConnected = 1,
    ConnectedStatusReconnecting = 2,
    ConnectedStatusCleanupWanted = 3
};

extern FuriHalUsbInterface usb_hid_ldtoypad;

usbd_device* get_usb_device();

uint8_t get_connected_status();

/* Bit n is set when the game wrote to the token with index n (set from the USB interrupt, cleared
 * by the Bluetooth link). Lets the web page keep vehicle upgrades. Use the atomic builtins. */
extern volatile uint8_t ld_token_dirty_mask;
void set_connected_status(enum ConnectedStatus status);

#ifdef __cplusplus
}
#endif
