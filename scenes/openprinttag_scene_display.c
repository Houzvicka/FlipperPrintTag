#include "../openprinttag_i.h"

void openprinttag_scene_display_on_enter(void* context) {
    OpenPrintTag* app = context;
    Widget* widget = app->widget;

    FuriString* temp_str = furi_string_alloc();

    // Display tag information
    widget_add_text_scroll_element(widget, 0, 0, 128, 64, furi_string_get_cstr(temp_str));

    // Build display string
    furi_string_cat_printf(temp_str, "OpenPrintTag Data\n\n");

    if(app->tag_data.main.has_data) {
        if(!furi_string_empty(app->tag_data.main.brand)) {
            furi_string_cat_printf(
                temp_str, "Brand: %s\n", furi_string_get_cstr(app->tag_data.main.brand));
        }

        if(!furi_string_empty(app->tag_data.main.material_name)) {
            furi_string_cat_printf(
                temp_str, "Material: %s\n", furi_string_get_cstr(app->tag_data.main.material_name));
        }

        if(!furi_string_empty(app->tag_data.main.material_type)) {
            furi_string_cat_printf(
                temp_str, "Type: %s\n", furi_string_get_cstr(app->tag_data.main.material_type));
        }

        if(app->tag_data.main.gtin > 0) {
            furi_string_cat_printf(temp_str, "GTIN: %llu\n", app->tag_data.main.gtin);
        }

        if(app->tag_data.main.material_class > 0) {
            furi_string_cat_printf(temp_str, "Class: %lu\n", app->tag_data.main.material_class);
        }
    } else {
        furi_string_cat_printf(temp_str, "No main data found\n");
    }

    if(app->tag_data.aux.has_data) {
        furi_string_cat_printf(temp_str, "\nAuxiliary Data:\n");

        if(app->tag_data.aux.remaining_length > 0) {
            furi_string_cat_printf(
                temp_str, "Remaining: %lu mm\n", app->tag_data.aux.remaining_length);
        }

        if(app->tag_data.aux.used_length > 0) {
            furi_string_cat_printf(temp_str, "Used: %lu mm\n", app->tag_data.aux.used_length);
        }

        if(app->tag_data.aux.timestamp > 0) {
            furi_string_cat_printf(temp_str, "Updated: %llu\n", app->tag_data.aux.timestamp);
        }
    }

    // Update widget with the built string
    widget_reset(widget);
    widget_add_text_scroll_element(widget, 0, 0, 128, 64, furi_string_get_cstr(temp_str));

    furi_string_free(temp_str);

    view_dispatcher_switch_to_view(app->view_dispatcher, OpenPrintTagViewWidget);
}

bool openprinttag_scene_display_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void openprinttag_scene_display_on_exit(void* context) {
    OpenPrintTag* app = context;
    widget_reset(app->widget);
}
