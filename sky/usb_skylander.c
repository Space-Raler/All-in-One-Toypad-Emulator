#include "usb_skylander.h"
#include <furi.h>
#include <furi_hal.h>
#include <usb.h>
#include <string.h>

#ifndef USB_EP0_SIZE
#define USB_EP0_SIZE 8
#endif

#define SKY_EP_IN 0x81
#define SKY_EP_OUT 0x02
#define SKY_EP_SIZE 0x20

/* bInterval (ms) of the interrupt-IN endpoint = how often the host polls for packets.
 * At 1 ms the host is handed ~1000 status packets/s and a command reply ends up buried
 * between them. RPCS3's emulated portal paces IN transfers at ~22 ms. If a game still
 * misses replies, go slower: 1 ms lost nearly every reply, 8 ms lost ~10%. */
#ifndef SKY_IN_INTERVAL_MS
#define SKY_IN_INTERVAL_MS 22
#endif

#define REQ_GET_REPORT 0x01
#define REQ_GET_IDLE 0x02
#define REQ_GET_PROTOCOL 0x03
#define REQ_SET_REPORT 0x09
#define REQ_SET_IDLE 0x0A
#define REQ_SET_PROTOCOL 0x0B

#define CFG_HID_OFFSET 18 /* config(9) + interface(9) */
#define CFG_HID_LENGTH 9

static SkyBase* s_base = NULL;
static SkyAudio* s_audio = NULL;
static UsbSkyWakeCb s_wake = NULL;
static void* s_wake_ctx = NULL;
static volatile uint32_t s_out_count = 0;
static volatile uint8_t s_audio_logged = 0;
static volatile bool s_configured = false;
static volatile bool s_polling = false;
static volatile bool s_suspended = false;
static volatile bool s_cmd_seen = false;
static volatile uint32_t s_last_in_tick = 0;
static volatile uint32_t s_last_cmd_tick = 0;
static volatile uint32_t s_cmd_count = 0;
#define LOG_RING 128
static UsbSkyLogEntry s_log[LOG_RING];
static volatile uint8_t s_log_head = 0; /* next to read */
static volatile uint8_t s_log_count = 0;
static uint8_t s_last_s1 = 0xFF, s_last_s2 = 0xFF; /* last status bytes logged */
static volatile uint16_t s_last_len = 0;
static volatile uint8_t s_last_raw[8];
static volatile char s_last_cmds[7]; /* ring of the last 6 command letters + NUL */
static uint8_t s_ctl_buf[SKY_PACKET_SIZE] __attribute__((aligned(4)));

static const uint8_t sky_dev_desc[18] __attribute__((aligned(4))) = {
    0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, USB_EP0_SIZE, /* USB 2.0, class in interface */
    0x30, 0x14, /* VID 0x1430 */
    0x50, 0x01, /* PID 0x0150 */
    0x00, 0x01, /* bcdDevice 1.00 */
    0x00, 0x00, 0x00, /* no string descriptors */
    0x01, /* one configuration */
};

static const uint8_t sky_cfg_desc[41] __attribute__((aligned(4))) = {
    /* configuration */ 0x09, 0x02, 0x29, 0x00, 0x01, 0x01, 0x00, 0x80, 0xFA,
    /* interface: HID, 2 endpoints */ 0x09, 0x04, 0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,
    /* HID 1.11, report descriptor 29 bytes */ 0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x1D, 0x00,
    /* EP IN  0x81 interrupt 32B, SKY_IN_INTERVAL_MS */ 0x07, 0x05, 0x81, 0x03, 0x20, 0x00, SKY_IN_INTERVAL_MS,
    /* EP OUT 0x02 interrupt 32B 1ms */ 0x07, 0x05, 0x02, 0x03, 0x20, 0x00, 0x01,
};

static const uint8_t sky_report_desc[29] __attribute__((aligned(4))) = {
    0x06, 0x00, 0xFF, /* usage page: vendor 0xFF00 */
    0x09, 0x01, /* usage 1 */
    0xA1, 0x01, /* collection: application */
    0x19, 0x01, 0x29, 0x40, /* usage 1..64 */
    0x15, 0x00, 0x26, 0xFF, 0x00, /* logical 0..255 */
    0x75, 0x08, 0x95, 0x20, /* 32 x 8-bit */
    0x81, 0x00, /* input */
    0x19, 0x01, 0x29, 0x40, /* usage 1..64 */
    0x91, 0x00, /* output */
    0xC0,
};

void usb_skylander_attach_base(SkyBase* base) {
    s_base = base;
    if(base) {
        /* Fresh session: inside ToyBox this app can be opened more than once per process
         * (Skylanders -> Back -> Skylanders), so stale flags and counters must not carry over. */
        s_configured = false;
        s_polling = false;
        s_suspended = false;
        s_cmd_seen = false;
        s_last_in_tick = 0;
        s_last_cmd_tick = 0;
        s_cmd_count = 0;
        s_out_count = 0;
        s_audio_logged = 0;
        s_last_s1 = 0xFF;
        s_last_s2 = 0xFF;
        for(uint8_t i = 0; i < sizeof(s_last_cmds); i++) s_last_cmds[i] = 0;
        s_last_len = 0;
        s_log_head = 0;
        s_log_count = 0;
        for(uint8_t i = 0; i < sizeof(s_last_raw); i++) s_last_raw[i] = 0;
    }
}

void usb_skylander_attach_audio(SkyAudio* audio) {
    s_audio = audio;
}

void usb_skylander_set_wake(UsbSkyWakeCb cb, void* ctx) {
    s_wake_ctx = ctx;
    s_wake = cb;
}

UsbSkyInfo usb_skylander_get_info(void) {
    UsbSkyInfo i;
    const uint32_t now = furi_get_tick();
    i.cmd_count = s_cmd_count;
    i.out_count = s_out_count;
    for(uint8_t k = 0; k < 6; k++) i.last_cmds[k] = s_last_cmds[k];
    i.last_cmds[6] = '\0';
    i.last_cmds[7] = '\0';
    i.last_len = s_last_len;
    for(uint8_t k = 0; k < 8; k++) i.last_raw[k] = s_last_raw[k];
    if(!s_configured || s_suspended) {
        i.state = UsbSkyDetached; /* not configured, or bus suspended (cable out / host asleep) */
    } else if(!s_polling) {
        i.state = UsbSkyEnumerated;
    } else if(now - s_last_in_tick > 600) {
        i.state = UsbSkyDetached; /* polling stopped: cable pulled or host suspended */
    } else if(s_cmd_seen && now - s_last_cmd_tick < 3000) {
        i.state = UsbSkyActive;
    } else {
        i.state = UsbSkyConnected;
    }
    return i;
}

static bool sky_is_cmd_letter(uint8_t c) {
    return c == 'A' || c == 'C' || c == 'J' || c == 'L' || c == 'M' || c == 'Q' || c == 'R' ||
           c == 'S' || c == 'V' || c == 'W' || c == 'Z';
}

static void sky_log(char kind, const uint8_t* d, size_t len, size_t total) {
    if(s_log_count >= LOG_RING) return; /* app not draining: drop newest */
    UsbSkyLogEntry* e = &s_log[(s_log_head + s_log_count) % LOG_RING];
    e->kind = kind;
    e->len = (uint16_t)total;
    for(uint8_t k = 0; k < 8; k++) e->raw[k] = k < len ? d[k] : 0;
    e->tick = furi_get_tick();
    s_log_count++;
}

bool usb_skylander_log_pop(UsbSkyLogEntry* out) {
    bool ok = false;
    FURI_CRITICAL_ENTER();
    if(s_log_count > 0) {
        *out = s_log[s_log_head];
        s_log_head = (uint8_t)((s_log_head + 1) % LOG_RING);
        s_log_count--;
        ok = true;
    }
    FURI_CRITICAL_EXIT();
    return ok;
}

/* Runs in USB interrupt context. */
static void sky_next_packet(uint8_t* out) {
    if(s_base) {
        if(sky_base_next_packet(s_base, out)) {
            sky_log('R', out, 8, SKY_PACKET_SIZE);
        } else {
            /* status packet: log only when the figure bits change */
            if(out[1] != s_last_s1 || out[2] != s_last_s2) {
                s_last_s1 = out[1];
                s_last_s2 = out[2];
                sky_log('S', out, 8, SKY_PACKET_SIZE);
            }
        }
    } else {
        memset(out, 0, SKY_PACKET_SIZE);
    }
}

static void sky_send_next(usbd_device* dev) {
    uint8_t pkt[SKY_PACKET_SIZE] __attribute__((aligned(4)));
    sky_next_packet(pkt);
    usbd_ep_write(dev, SKY_EP_IN, pkt, SKY_PACKET_SIZE);
}

static void sky_ep_in(usbd_device* dev, uint8_t event, uint8_t ep) {
    UNUSED(ep);
    if(event == usbd_evt_eptx) {
        s_polling = true;
        s_last_in_tick = furi_get_tick();
        sky_send_next(dev);
    }
}

static void sky_ep_out(usbd_device* dev, uint8_t event, uint8_t ep) {
    if(event == usbd_evt_eprx) {
        uint8_t pkt[SKY_PACKET_SIZE] __attribute__((aligned(4)));
        const int n = usbd_ep_read(dev, ep, pkt, sizeof(pkt));
        if(n <= 0) return;
        s_out_count++;
        if(s_out_count <= 4) sky_log('O', pkt, n < 8 ? (size_t)n : 8, (size_t)n); /* path check */
        /* Speaker audio: 8 kHz, 16-bit signed PCM. Only queued while the host has the speaker on. */
        SkyAudio* audio = s_audio;
        if(audio && audio->accept && s_base && s_base->speaker_on) {
            if(s_audio_logged < 8) { /* what the audio really looks like: see debug.txt, 'A' lines */
                s_audio_logged++;
                sky_log('A', pkt, n < 8 ? (size_t)n : 8, (size_t)n);
            }
            sky_audio_push_pcm16(audio, pkt, (size_t)n);
        }
    }
}

static usbd_respond sky_ep_config(usbd_device* dev, uint8_t cfg) {
    switch(cfg) {
    case 0:
        s_configured = false;
        s_polling = false;
        usbd_ep_deconfig(dev, SKY_EP_IN);
        usbd_ep_deconfig(dev, SKY_EP_OUT);
        usbd_reg_endpoint(dev, SKY_EP_IN, NULL);
        usbd_reg_endpoint(dev, SKY_EP_OUT, NULL);
        return usbd_ack;
    case 1:
        s_polling = false;
        s_suspended = false;
        s_cmd_seen = false;
        if(s_base) sky_base_clear_replies(s_base); /* drop stale replies from a previous session */
        s_configured = true;
        usbd_ep_config(dev, SKY_EP_IN, USB_EPTYPE_INTERRUPT, SKY_EP_SIZE);
        usbd_ep_config(dev, SKY_EP_OUT, USB_EPTYPE_INTERRUPT, SKY_EP_SIZE);
        usbd_reg_endpoint(dev, SKY_EP_IN, sky_ep_in);
        usbd_reg_endpoint(dev, SKY_EP_OUT, sky_ep_out);
        sky_send_next(dev); /* prime; each TX-complete queues the next packet */
        return usbd_ack;
    default:
        return usbd_fail;
    }
}

static usbd_respond sky_control(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* callback) {
    UNUSED(callback);
    const uint8_t kind = req->bmRequestType & 0x7F; /* ignore direction bit */

    if(kind == 0x21 && req->wIndex == 0) { /* class request to interface */
        switch(req->bRequest) {
        case REQ_SET_REPORT:
            /* The USB core has already received the OUT data stage into req->data. */
            s_last_cmd_tick = furi_get_tick();
            s_cmd_seen = true;
            s_cmd_count++;
            {
                const uint8_t* d = req->data;
                size_t n = req->wLength;
                /* Some hosts prefix the payload with a HID report ID of 0: skip it. */
                if(n > 1 && d[0] == 0x00 && sky_is_cmd_letter(d[1])) {
                    d++;
                    n--;
                }
                sky_log('C', req->data, req->wLength < 8 ? req->wLength : 8, req->wLength);
                s_last_len = (uint16_t)req->wLength;
                for(uint8_t k = 0; k < 8; k++) s_last_raw[k] = k < req->wLength ? req->data[k] : 0;
                if(n > 0) {
                    for(uint8_t k = 0; k < 5; k++) s_last_cmds[k] = s_last_cmds[k + 1];
                    s_last_cmds[5] = (d[0] >= 0x20 && d[0] < 0x7F) ? (char)d[0] : '?';
                    if(s_base) sky_base_handle_command(s_base, d, n);
                    if(d[0] == 'M' && s_wake) s_wake(s_wake_ctx);
                }
            }
            return usbd_ack;
        case REQ_GET_REPORT:
            sky_next_packet(s_ctl_buf);
            dev->status.data_ptr = s_ctl_buf;
            dev->status.data_count = req->wLength < SKY_PACKET_SIZE ? req->wLength : SKY_PACKET_SIZE;
            return usbd_ack;
        case REQ_GET_IDLE:
        case REQ_GET_PROTOCOL:
            s_ctl_buf[0] = (req->bRequest == REQ_GET_PROTOCOL) ? 1 : 0;
            dev->status.data_ptr = s_ctl_buf;
            dev->status.data_count = 1;
            return usbd_ack;
        case REQ_SET_IDLE:
        case REQ_SET_PROTOCOL:
            return usbd_ack;
        default:
            return usbd_fail;
        }
    }

    if(kind == 0x01 && req->bRequest == 0x06 /* GET_DESCRIPTOR */ && req->wIndex == 0) {
        switch(req->wValue >> 8) {
        case 0x21: /* HID descriptor */
            dev->status.data_ptr = (void*)(sky_cfg_desc + CFG_HID_OFFSET);
            dev->status.data_count = CFG_HID_LENGTH;
            return usbd_ack;
        case 0x22: /* report descriptor */
            dev->status.data_ptr = (void*)sky_report_desc;
            dev->status.data_count = sizeof(sky_report_desc);
            return usbd_ack;
        default:
            return usbd_fail;
        }
    }
    return usbd_fail;
}

static void sky_init(usbd_device* dev, FuriHalUsbInterface* intf, void* ctx) {
    UNUSED(intf);
    UNUSED(ctx);
    usbd_reg_config(dev, sky_ep_config);
    usbd_reg_control(dev, sky_control);
    usbd_connect(dev, true);
}

static void sky_deinit(usbd_device* dev) {
    s_configured = false;
    s_polling = false;
    usbd_reg_config(dev, NULL);
    usbd_reg_control(dev, NULL);
}

static void sky_wakeup(usbd_device* dev) {
    UNUSED(dev);
    s_suspended = false;
}

static void sky_suspend(usbd_device* dev) {
    UNUSED(dev);
    s_suspended = true;
    s_polling = false;
}

FuriHalUsbInterface usb_skylander = {
    .init = sky_init,
    .deinit = sky_deinit,
    .wakeup = sky_wakeup,
    .suspend = sky_suspend,
    .dev_descr = (void*)sky_dev_desc,
    .str_manuf_descr = NULL,
    .str_prod_descr = NULL,
    .str_serial_descr = NULL,
    .cfg_descr = (void*)sky_cfg_desc,
};
