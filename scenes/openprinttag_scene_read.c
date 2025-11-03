#include "../openprinttag_i.h"
#include <nfc/protocols/nfc_device_base_i.h>

typedef enum {
    OpenPrintTagReadStateIdle,
    OpenPrintTagReadStateDetecting,
    OpenPrintTagReadStateReading,
} OpenPrintTagReadState;

static OpenPrintTagReadState read_state = OpenPrintTagReadStateIdle;

static NfcCommand openprinttag_scene_read_callback(NfcGenericEvent event, void* context) {
    furi_assert(context);
    OpenPrintTag* app = context;

    if(event == NfcGenericEventPollerSuccess) {
        read_state = OpenPrintTagReadStateReading;
        view_dispatcher_send_custom_event(app->view_dispatcher, 0);
    }

    return NfcCommandContinue;
}

void openprinttag_scene_read_on_enter(void* context) {
    OpenPrintTag* app = context;

    read_state = OpenPrintTagReadStateDetecting;

    // Show loading view
    loading_set_context(app->loading, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, OpenPrintTagViewLoading);

    // Allocate NFC device if not already allocated
    if(!app->nfc_device) {
        app->nfc_device = nfc_device_alloc();
    }

    // Start NFC listener for ISO15693
    nfc_config(app->nfc, NfcModePoller, NfcTechIso15693);
    nfc_set_fdt_poll_fc(app->nfc, 5000);
    nfc_set_mask_receive_time_fc(app->nfc, 5000);
    nfc_set_fdt_listen_fc(app->nfc, 5000);
    nfc_set_guard_time_us(app->nfc, 10000);

    nfc_start(app->nfc, openprinttag_scene_read_callback, app);
}

bool openprinttag_scene_read_on_event(void* context, SceneManagerEvent event) {
    OpenPrintTag* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(read_state == OpenPrintTagReadStateReading) {
            nfc_stop(app->nfc);

            // Get NFC data
            const NfcDeviceData* nfc_data = nfc_device_get_data(app->nfc_device, NfcProtocolIso15693_3a);

            if(nfc_data) {
                // Parse NDEF data
                const Iso15693_3aData* iso_data = nfc_data;

                // For now, we'll try to read the tag data directly
                // In a real implementation, you'd need to read the tag memory properly
                FURI_LOG_I(TAG, "NFC tag detected");

                // Try to parse NDEF - this is simplified
                // You'll need to implement proper ISO15693 memory reading
                bool parse_success = false;

                if(parse_success) {
                    scene_manager_next_scene(app->scene_manager, OpenPrintTagSceneReadSuccess);
                } else {
                    scene_manager_next_scene(app->scene_manager, OpenPrintTagSceneReadError);
                }
            } else {
                scene_manager_next_scene(app->scene_manager, OpenPrintTagSceneReadError);
            }
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        nfc_stop(app->nfc);
        consumed = scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, OpenPrintTagSceneStart);
    }

    return consumed;
}

void openprinttag_scene_read_on_exit(void* context) {
    OpenPrintTag* app = context;
    nfc_stop(app->nfc);
    read_state = OpenPrintTagReadStateIdle;
}
