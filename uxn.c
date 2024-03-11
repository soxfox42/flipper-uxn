#include <core/base.h>
#include <core/kernel.h>
#include <core/message_queue.h>
#include <core/mutex.h>
#include <core/string.h>
#include <core/timer.h>
#include <dialogs/dialogs.h>
#include <furi.h>
#include <gui/canvas.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include "uxn/uxn.h"
#include "uxn/devices/screen.h"

#include "uxn_icons.h"

#define TAG "uxn"

typedef struct {
    FuriMutex *mutex;
    Uxn uxn;
    bool running;
    bool redraw;
} UxnState;

uint8_t button_bit(InputKey key) {
    switch (key) {
        case InputKeyOk: return 0x01;
        case InputKeyBack: return 0x02;
        case InputKeyUp: return 0x10;
        case InputKeyDown: return 0x20;
        case InputKeyLeft: return 0x40;
        case InputKeyRight: return 0x80;
        default: return 0;
    }
}

void input_callback(const void* value, void* context) {
    const InputEvent *event = value;
    UxnState *state = context;
    furi_mutex_acquire(state->mutex, FuriWaitForever);

    uint16_t controller_vector = state->uxn.dev[0x80] << 8 | state->uxn.dev[0x81];
    if (event->type == InputTypePress) {
        state->uxn.dev[0x82] |= button_bit(event->key);
        uxn_eval(&state->uxn, controller_vector);
    } else if (event->type == InputTypeRelease) {
        state->uxn.dev[0x82] &= ~button_bit(event->key);
        uxn_eval(&state->uxn, controller_vector);
    }

    if (event->key == InputKeyBack && event->type == InputTypeLong) state->running = false;

    furi_mutex_release(state->mutex);
}

void timer_callback(void *context) {
    UxnState *state = context;
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    uint16_t screen_vector = state->uxn.dev[0x20] << 8 | state->uxn.dev[0x21];
    uxn_eval(&state->uxn, screen_vector);
    state->redraw = true;
    furi_mutex_release(state->mutex);
}

Uint8 emu_dei(Uxn *uxn, Uint8 addr) {
    Uint8 port = addr & 0x0f, dev = addr >> 4;
    UNUSED(port);
	switch(dev) {
        case 0x2: return screen_dei(uxn, addr);
	}
    return uxn->dev[addr];
}

void emu_deo(Uxn *uxn, Uint8 addr, Uint8 value) {
    uxn->dev[addr] = value;
    Uint8 port = addr & 0x0f, dev = addr >> 4;
	switch(dev) {
        case 0x2: screen_deo(uxn->ram, &uxn->dev[0x20], port); break;
	}
}

// Required for stock screen device
int emu_resize(int width, int height) {
    UNUSED(width);
    UNUSED(height);
    return 0;
}

int32_t uxn_app() {
    UxnState state;
    state.mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    memset(&state.uxn, 0, sizeof(Uxn));
    state.running = false;
    state.redraw = false;

    Gui *gui = furi_record_open(RECORD_GUI);
    Storage *storage = furi_record_open(RECORD_STORAGE);
    NotificationApp *notification = furi_record_open(RECORD_NOTIFICATION);
    DialogsApp *dialogs = furi_record_open(RECORD_DIALOGS);
    FuriPubSub *input_events = furi_record_open(RECORD_INPUT_EVENTS);
    FuriPubSubSubscription *subscription = NULL;
    FuriTimer *timer = NULL;

    notification_message(notification, &sequence_display_backlight_enforce_on);

    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, ".rom", &I_uxn);
    browser_options.base_path = ANY_PATH("uxn");
    FuriString *path = furi_string_alloc();
    furi_string_set(path, ANY_PATH("uxn"));
    bool success = dialog_file_browser_show(dialogs, path, path, &browser_options);

    uint8_t *ram = NULL;
    if (success) {
        ram = malloc(0x10000);
        uint8_t dev[0x100] = {0};
        state.uxn.ram = ram;
        state.uxn.dev = dev;
        screen_resize(128, 64);

        File *file = storage_file_alloc(storage);
        storage_file_open(file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING);
        storage_file_read(file, ram + 0x100, 0x10000 - 0x100);
        storage_file_close(file);
        storage_file_free(file);

        uxn_eval(&state.uxn, 0x100);

        subscription = furi_pubsub_subscribe(input_events, input_callback, &state);
        timer = furi_timer_alloc(timer_callback, FuriTimerTypePeriodic, &state);
        furi_timer_start(timer, furi_kernel_get_tick_frequency() / 60);
        state.running = true;
    }

    furi_string_free(path);

    Canvas *canvas = gui_direct_draw_acquire(gui);
    while (state.running) {
        furi_mutex_acquire(state.mutex, FuriWaitForever);

        if (state.redraw) {
            canvas_clear(canvas);
            for (int y = 0; y < 64; y++) {
                for (int x = 0; x < 128; x++) {
                    uint8_t fg = uxn_screen.fg[y * 128 + x];
                    uint8_t bg = uxn_screen.bg[y * 128 + x];
                    bool on = fg & 1;
                    if (fg == 0) on = bg & 1;
                    if (on) canvas_draw_dot(canvas, x, y);
                }
            }
            canvas_commit(canvas);
            state.redraw = false;
        }

        furi_mutex_release(state.mutex);

        furi_delay_tick(2);
    }

    if (ram) free(ram);
    if (uxn_screen.fg) free(uxn_screen.fg);
    if (uxn_screen.bg) free(uxn_screen.bg);

    notification_message(notification, &sequence_display_backlight_enforce_auto);

    if (timer != NULL) {
        furi_timer_stop(timer);
        furi_timer_free(timer);
    }
    if (subscription != NULL) {
        furi_pubsub_unsubscribe(input_events, subscription);
    }
    furi_record_close(RECORD_INPUT_EVENTS);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_NOTIFICATION);
    gui_direct_draw_release(gui);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    furi_mutex_free(state.mutex);

    return 0;
}
