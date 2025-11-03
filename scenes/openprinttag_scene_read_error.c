#include "../openprinttag_i.h"

static void openprinttag_scene_read_error_popup_callback(void* context) {
    OpenPrintTag* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, 0);
}

void openprinttag_scene_read_error_on_enter(void* context) {
    OpenPrintTag* app = context;
    Popup* popup = app->popup;

    popup_set_icon(popup, 32, 5, &I_DolphinNice_96x59);
    popup_set_header(popup, "Error!", 13, 22, AlignLeft, AlignBottom);
    popup_set_text(
        popup, "Failed to read tag\nor not an OpenPrintTag", 13, 32, AlignLeft, AlignTop);
    popup_set_timeout(popup, 2500);
    popup_set_context(popup, app);
    popup_set_callback(popup, openprinttag_scene_read_error_popup_callback);
    popup_enable_timeout(popup);

    view_dispatcher_switch_to_view(app->view_dispatcher, OpenPrintTagViewPopup);
}

bool openprinttag_scene_read_error_on_event(void* context, SceneManagerEvent event) {
    OpenPrintTag* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, OpenPrintTagSceneStart);
        consumed = true;
    }

    return consumed;
}

void openprinttag_scene_read_error_on_exit(void* context) {
    OpenPrintTag* app = context;
    popup_reset(app->popup);
}
