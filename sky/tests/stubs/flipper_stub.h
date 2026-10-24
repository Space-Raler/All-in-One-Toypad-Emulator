#pragma once
/* Minimal stand-ins for the Flipper SDK, only to syntax-check the app on a host.
 * Signatures are from memory of the official/Momentum SDK, NOT the real headers. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define UNUSED(x) (void)(x)
#define EXT_PATH(p) "/ext/" p
#define FURI_CRITICAL_ENTER() uint32_t furi_crit_ = 0; (void)furi_crit_
#define FURI_CRITICAL_EXIT()
typedef uint32_t __FuriCriticalInfo;
__FuriCriticalInfo __furi_critical_enter(void); void __furi_critical_exit(__FuriCriticalInfo state);
#define RECORD_GUI "gui"
#define RECORD_STORAGE "storage"
#define RECORD_DIALOGS "dialogs"
#define RECORD_NOTIFICATION "notification"
#define FuriWaitForever 0xFFFFFFFFU
typedef enum { FuriStatusOk = 0, FuriStatusError = -1 } FuriStatus;
typedef enum { FuriMutexTypeNormal } FuriMutexType;
typedef struct FuriMutex FuriMutex; typedef struct FuriMessageQueue FuriMessageQueue; typedef struct FuriString FuriString;
FuriMutex* furi_mutex_alloc(FuriMutexType t); void furi_mutex_free(FuriMutex* m);
FuriStatus furi_mutex_acquire(FuriMutex* m, uint32_t t); FuriStatus furi_mutex_release(FuriMutex* m);
FuriMessageQueue* furi_message_queue_alloc(uint32_t n, uint32_t sz); void furi_message_queue_free(FuriMessageQueue* q);
FuriStatus furi_message_queue_put(FuriMessageQueue* q, const void* m, uint32_t t);
FuriStatus furi_message_queue_get(FuriMessageQueue* q, void* m, uint32_t t);
uint32_t furi_get_tick(void); void* furi_record_open(const char* n); void furi_record_close(const char* n); void furi_delay_ms(uint32_t ms);
FuriString* furi_string_alloc(void); FuriString* furi_string_alloc_set_str(const char* s);
const char* furi_string_get_cstr(const FuriString* s); void furi_string_free(FuriString* s);
/* gui */
typedef struct Canvas Canvas; typedef struct Gui Gui; typedef struct ViewPort ViewPort;
typedef enum { FontPrimary, FontSecondary } Font; typedef enum { ColorWhite, ColorBlack } Color;
typedef enum { AlignLeft, AlignRight, AlignTop, AlignBottom, AlignCenter } Align;
typedef enum { GuiLayerFullscreen } GuiLayer;
void canvas_clear(Canvas*); void canvas_set_font(Canvas*, Font); void canvas_set_color(Canvas*, Color);
void canvas_draw_str(Canvas*, int, int, const char*);
void canvas_draw_str_aligned(Canvas*, int, int, Align, Align, const char*);
void canvas_draw_line(Canvas*, int, int, int, int);
void canvas_draw_box(Canvas*, int, int, size_t, size_t);
/* input */
typedef enum { InputKeyUp, InputKeyDown, InputKeyRight, InputKeyLeft, InputKeyOk, InputKeyBack } InputKey;
typedef enum { InputTypePress, InputTypeRelease, InputTypeShort, InputTypeLong, InputTypeRepeat, InputTypeMAX } InputType;
typedef struct { uint32_t sequence; InputKey key; InputType type; } InputEvent;
ViewPort* view_port_alloc(void); void view_port_free(ViewPort*);
void view_port_draw_callback_set(ViewPort*, void (*)(Canvas*, void*), void*);
void view_port_input_callback_set(ViewPort*, void (*)(InputEvent*, void*), void*);
void view_port_update(ViewPort*);
void gui_add_view_port(Gui*, ViewPort*, GuiLayer); void gui_remove_view_port(Gui*, ViewPort*);
/* dialogs */
typedef struct DialogsApp DialogsApp; typedef struct { const char* extension; } DialogsFileBrowserOptions;
void dialog_file_browser_set_basic_options(DialogsFileBrowserOptions*, const char*, const void*);
bool dialog_file_browser_show(DialogsApp*, FuriString*, FuriString*, const DialogsFileBrowserOptions*);
/* storage */
typedef struct Storage Storage; typedef struct File File; typedef struct { uint8_t flags; } FileInfo;
#define FSF_DIRECTORY 1
typedef enum { FSAM_READ, FSAM_WRITE } FS_AccessMode; typedef enum { FSOM_OPEN_EXISTING, FSOM_OPEN_ALWAYS, FSOM_OPEN_APPEND, FSOM_CREATE_NEW, FSOM_CREATE_ALWAYS } FS_OpenMode;
File* storage_file_alloc(Storage*); void storage_file_free(File*);
bool storage_file_open(File*, const char*, FS_AccessMode, FS_OpenMode); bool storage_file_close(File*);
uint64_t storage_file_size(File*); size_t storage_file_read(File*, void*, size_t); size_t storage_file_write(File*, const void*, size_t);
bool storage_dir_open(File*, const char*); bool storage_dir_read(File*, FileInfo*, char*, uint16_t); bool storage_dir_close(File*);
bool storage_simply_mkdir(Storage*, const char*);
bool storage_simply_remove(Storage*, const char*);
/* usb */
typedef struct usbd_device usbd_device;
typedef enum { usbd_fail, usbd_ack, usbd_nak } usbd_respond;
enum { usbd_evt_eptx = 2, usbd_evt_eprx = 3 };
typedef struct { void* data_ptr; uint16_t data_count; } usbd_status;
struct usbd_device { usbd_status status; };
typedef struct { uint8_t bmRequestType, bRequest; uint16_t wValue, wIndex, wLength; uint8_t data[]; } usbd_ctlreq;
typedef usbd_respond (*usbd_rqc_callback)(usbd_device*, usbd_ctlreq*);
#define USB_EPTYPE_INTERRUPT 3
void usbd_reg_config(usbd_device*, usbd_respond (*)(usbd_device*, uint8_t));
void usbd_reg_control(usbd_device*, usbd_respond (*)(usbd_device*, usbd_ctlreq*, usbd_rqc_callback*));
void usbd_reg_endpoint(usbd_device*, uint8_t, void (*)(usbd_device*, uint8_t, uint8_t));
bool usbd_ep_config(usbd_device*, uint8_t, uint8_t, uint16_t); void usbd_ep_deconfig(usbd_device*, uint8_t);
int usbd_ep_write(usbd_device*, uint8_t, const void*, uint16_t); int usbd_ep_read(usbd_device*, uint8_t, void*, uint16_t);
void usbd_connect(usbd_device*, bool);
typedef struct FuriHalUsbInterface FuriHalUsbInterface;
struct FuriHalUsbInterface {
    void (*init)(usbd_device*, FuriHalUsbInterface*, void*); void (*deinit)(usbd_device*);
    void (*wakeup)(usbd_device*); void (*suspend)(usbd_device*);
    struct usb_device_descriptor* dev_descr; void* str_manuf_descr; void* str_prod_descr; void* str_serial_descr; void* cfg_descr;
};
FuriHalUsbInterface* furi_hal_usb_get_config(void); bool furi_hal_usb_set_config(FuriHalUsbInterface*, void*); void furi_hal_usb_unlock(void);
typedef struct NotificationApp NotificationApp; typedef struct NotificationSequence NotificationSequence;
extern const NotificationSequence sequence_success; extern const NotificationSequence sequence_error;
extern const NotificationSequence sequence_reset_rgb; extern const NotificationSequence sequence_display_backlight_on;
void notification_message(NotificationApp*, const NotificationSequence*);

/* LED and backlight (signatures from memory, for syntax checking only) */
typedef enum { LightRed = (1 << 0), LightGreen = (1 << 1), LightBlue = (1 << 2), LightBacklight = (1 << 3) } Light;
void furi_hal_light_set(Light light, uint8_t value);

/* speaker / interrupt / LL (signatures from memory, for syntax checking only) */
bool furi_hal_speaker_acquire(uint32_t timeout); void furi_hal_speaker_release(void);
typedef enum { FuriHalInterruptIdDma1Ch1 } FuriHalInterruptId;
void furi_hal_interrupt_set_isr(FuriHalInterruptId, void (*)(void*), void*);
extern uint32_t SystemCoreClock;
