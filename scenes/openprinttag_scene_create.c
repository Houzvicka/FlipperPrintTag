#include "../openprinttag_i.h"
#include "../material_types.h"

// Creates a new OpenPrintTag on a blank tag:
//   form -> hold a blank tag near the Flipper -> the tag image is built and written
//
// The form keeps its values while the number pad, the text input or the scan is shown, and a
// failed or cancelled write returns to it.

typedef enum {
    CreateItemBrand,
    CreateItemMaterial,
    CreateItemType,
    CreateItemWeight,
    CreateItemSpoolWeight,
    CreateItemNozzleMin,
    CreateItemNozzleMax,
    CreateItemBedMin,
    CreateItemBedMax,
    CreateItemWrite,
    CreateItemCount,
} CreateItem;

_Static_assert(CreateItemCount <= OPENPRINTTAG_CREATE_ITEMS_MAX, "too many form rows");

typedef enum {
    CreatePhaseForm, // The form is shown
    CreatePhaseInput, // The text input or the number pad is shown
    CreatePhaseScan, // Waiting for a tag to read
    CreatePhaseWrite, // Writing the tag, waiting for it if it is not in the field
} CreatePhase;

typedef enum {
    CreateEventTagDetected = 1,
    CreateEventTagRead = 2,
    CreateEventTextEntered = 3,
    CreateEventNumberEntered = 4,
    CreateEventWriteDone = OpenPrintTagEventWriteDone,
    CreateEventWriteFailed = OpenPrintTagEventWriteFailed,
    CreateEventResultDismissed = 6, // Message shown, go back to the main menu
    CreateEventFailureDismissed = 7, // Message shown, go back to the form
    CreateEventItemClicked = 100, // Plus the index of the form row
} CreateEvent;

#define CREATE_MAX_BLOCK_SIZE  (32U)
#define CREATE_MAX_BLOCK_COUNT (256U)

#define CREATE_TEXT_SHOWN (11U) // Characters of a name shown on the form

static void create_stop_scanner(OpenPrintTag* app) {
    if(app->nfc_scanner) {
        nfc_scanner_stop(app->nfc_scanner);
        nfc_scanner_free(app->nfc_scanner);
        app->nfc_scanner = NULL;
    }
}

static void create_stop_poller(OpenPrintTag* app) {
    if(app->nfc_poller) {
        nfc_poller_stop(app->nfc_poller);
        nfc_poller_free(app->nfc_poller);
        app->nfc_poller = NULL;
    }
}

static void create_free_write_data(OpenPrintTag* app) {
    if(app->write_data) {
        free(app->write_data);
        app->write_data = NULL;
    }
    app->write_in_progress = false;
}

// ---- Form -------------------------------------------------------------------------------------

static uint32_t* create_number_field(OpenPrintTag* app, uint8_t item) {
    switch(item) {
    case CreateItemWeight:
        return &app->create.weight;
    case CreateItemSpoolWeight:
        return &app->create.empty_weight;
    case CreateItemNozzleMin:
        return &app->create.nozzle_min;
    case CreateItemNozzleMax:
        return &app->create.nozzle_max;
    case CreateItemBedMin:
        return &app->create.bed_min;
    case CreateItemBedMax:
        return &app->create.bed_max;
    default:
        return NULL;
    }
}

static uint32_t create_number_max(uint8_t item) {
    switch(item) {
    case CreateItemWeight:
        return 20000;
    case CreateItemSpoolWeight:
        return 5000;
    default:
        return 500; // Temperatures
    }
}

static const char* create_number_header(uint8_t item) {
    switch(item) {
    case CreateItemWeight:
        return "Full weight (g)";
    case CreateItemSpoolWeight:
        return "Empty spool (g)";
    case CreateItemNozzleMin:
        return "Nozzle min (C)";
    case CreateItemNozzleMax:
        return "Nozzle max (C)";
    case CreateItemBedMin:
        return "Bed min (C)";
    default:
        return "Bed max (C)";
    }
}

static void create_update_item(OpenPrintTag* app, uint8_t item) {
    VariableItem* row = app->create_items[item];
    if(!row) return;

    char text[24];
    switch(item) {
    case CreateItemBrand:
    case CreateItemMaterial: {
        const char* value = item == CreateItemBrand ? app->create.brand : app->create.material;
        if(value[0] == '\0') {
            snprintf(text, sizeof(text), "-");
        } else if(strlen(value) > CREATE_TEXT_SHOWN) {
            snprintf(text, sizeof(text), "%.10s~", value); // CREATE_TEXT_SHOWN - 1 characters
        } else {
            snprintf(text, sizeof(text), "%.11s", value); // CREATE_TEXT_SHOWN characters
        }
        break;
    }
    case CreateItemType:
        snprintf(text, sizeof(text), "%.12s", material_types[app->create.type_index].abbreviation);
        break;
    default: {
        const uint32_t* field = create_number_field(app, item);
        if(field) {
            if(*field == 0) {
                snprintf(text, sizeof(text), "-");
            } else {
                snprintf(text, sizeof(text), "%lu", *field);
            }
        } else {
            text[0] = '\0';
        }
        break;
    }
    }

    variable_item_set_current_value_text(row, text);
}

static void create_type_change_callback(VariableItem* item) {
    OpenPrintTag* app = variable_item_get_context(item);
    app->create.type_index = variable_item_get_current_value_index(item);
    create_update_item(app, CreateItemType);
}

static void create_item_click_callback(void* context, uint32_t index) {
    OpenPrintTag* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, CreateEventItemClicked + index);
}

static void create_show_form(OpenPrintTag* app) {
    app->create_phase = CreatePhaseForm;
    view_dispatcher_switch_to_view(app->view_dispatcher, OpenPrintTagViewVariableItemList);
}

static void create_build_form(OpenPrintTag* app) {
    VariableItemList* list = app->variable_item_list;
    variable_item_list_reset(list);
    variable_item_list_set_enter_callback(list, create_item_click_callback, app);

    app->create_items[CreateItemBrand] = variable_item_list_add(list, "Brand", 1, NULL, app);
    app->create_items[CreateItemMaterial] = variable_item_list_add(list, "Material", 1, NULL, app);

    app->create_items[CreateItemType] = variable_item_list_add(
        list, "Type", MATERIAL_TYPES_COUNT, create_type_change_callback, app);
    variable_item_set_current_value_index(
        app->create_items[CreateItemType], app->create.type_index);

    app->create_items[CreateItemWeight] = variable_item_list_add(list, "Weight g", 1, NULL, app);
    app->create_items[CreateItemSpoolWeight] =
        variable_item_list_add(list, "Spool g", 1, NULL, app);
    app->create_items[CreateItemNozzleMin] =
        variable_item_list_add(list, "Nozzle min", 1, NULL, app);
    app->create_items[CreateItemNozzleMax] =
        variable_item_list_add(list, "Nozzle max", 1, NULL, app);
    app->create_items[CreateItemBedMin] = variable_item_list_add(list, "Bed min", 1, NULL, app);
    app->create_items[CreateItemBedMax] = variable_item_list_add(list, "Bed max", 1, NULL, app);
    app->create_items[CreateItemWrite] =
        variable_item_list_add(list, "Write to tag", 0, NULL, app);

    for(uint8_t item = 0; item < CreateItemWrite; item++) {
        create_update_item(app, item);
    }
}

// ---- Result messages --------------------------------------------------------------------------

static void create_popup_to_menu_callback(void* context) {
    OpenPrintTag* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, CreateEventResultDismissed);
}

static void create_popup_to_form_callback(void* context) {
    OpenPrintTag* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, CreateEventFailureDismissed);
}

// Shows a message for a moment, then goes back to the main menu, or to the form (which still
// holds everything that was entered) if return_to_form is set
static void create_show_result(
    OpenPrintTag* app,
    const char* header,
    const char* text,
    bool return_to_form) {
    Popup* popup = app->popup;
    popup_reset(popup);
    popup_set_header(popup, header, 64, 10, AlignCenter, AlignTop);
    popup_set_text(popup, text, 64, 32, AlignCenter, AlignCenter);
    popup_set_context(popup, app);
    popup_set_callback(
        popup, return_to_form ? create_popup_to_form_callback : create_popup_to_menu_callback);
    popup_set_timeout(popup, 2500);
    popup_enable_timeout(popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, OpenPrintTagViewPopup);
}

static void create_show_waiting(
    OpenPrintTag* app,
    const char* header,
    const char* line_1,
    const char* line_2) {
    char text[48];
    snprintf(text, sizeof(text), "%s\n%s\n\nBACK = edit", line_1, line_2);

    Popup* popup = app->popup;
    popup_reset(popup);
    popup_set_header(popup, header, 64, 6, AlignCenter, AlignTop);
    popup_set_text(popup, text, 64, 22, AlignCenter, AlignTop);
    view_dispatcher_switch_to_view(app->view_dispatcher, OpenPrintTagViewPopup);
}

// ---- Text input and number pad ----------------------------------------------------------------

static void create_text_done_callback(void* context) {
    OpenPrintTag* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, CreateEventTextEntered);
}

static void create_number_done_callback(void* context, uint32_t value) {
    OpenPrintTag* app = context;

    uint32_t* field = create_number_field(app, app->create_editing);
    if(field) *field = value;

    view_dispatcher_send_custom_event(app->view_dispatcher, CreateEventNumberEntered);
}

static void create_open_text_input(OpenPrintTag* app, uint8_t item) {
    const char* current = item == CreateItemBrand ? app->create.brand : app->create.material;
    snprintf(app->text_buffer, sizeof(app->text_buffer), "%s", current);

    text_input_reset(app->text_input);
    text_input_set_header_text(
        app->text_input, item == CreateItemBrand ? "Brand" : "Material name");
    text_input_set_result_callback(
        app->text_input,
        create_text_done_callback,
        app,
        app->text_buffer,
        sizeof(app->text_buffer),
        false);

    app->create_editing = item;
    app->create_phase = CreatePhaseInput;
    view_dispatcher_switch_to_view(app->view_dispatcher, OpenPrintTagViewTextInput);
}

static void create_open_number_pad(OpenPrintTag* app, uint8_t item) {
    const uint32_t* field = create_number_field(app, item);
    if(!field) return;

    numpad_setup(
        app->numpad,
        create_number_header(item),
        *field,
        create_number_max(item),
        create_number_done_callback,
        app);

    app->create_editing = item;
    app->create_phase = CreatePhaseInput;
    view_dispatcher_switch_to_view(app->view_dispatcher, OpenPrintTagViewNumberInput);
}

// ---- Scanning and writing ---------------------------------------------------------------------

static void create_scanner_callback(NfcScannerEvent event, void* context) {
    OpenPrintTag* app = context;

    if(event.type == NfcScannerEventTypeDetected) {
        for(size_t i = 0; i < event.data.protocol_num; i++) {
            // SLIX/SLIX2 chips are reported as their own protocol, a child of ISO15693-3
            if(event.data.protocols[i] == NfcProtocolIso15693_3 ||
               event.data.protocols[i] == NfcProtocolSlix) {
                view_dispatcher_send_custom_event(app->view_dispatcher, CreateEventTagDetected);
                return;
            }
        }
    }
}

static NfcCommand create_poller_callback(NfcGenericEvent event, void* context) {
    OpenPrintTag* app = context;

    if(event.protocol == NfcProtocolIso15693_3) {
        view_dispatcher_send_custom_event(app->view_dispatcher, CreateEventTagRead);
        return NfcCommandStop;
    }

    return NfcCommandContinue;
}

static void create_start_scan(OpenPrintTag* app) {
    if(app->create.material[0] == '\0') {
        create_show_result(app, "Missing data", "Enter a material\nname first", true);
        return;
    }

    create_show_waiting(app, "Create tag", "Hold a blank tag", "near Flipper");

    app->read_retries = 0;
    app->create_phase = CreatePhaseScan;
    app->nfc_scanner = nfc_scanner_alloc(app->nfc);
    nfc_scanner_start(app->nfc_scanner, create_scanner_callback, app);
}

// A tag can be written when it has no NDEF message: all zero, or a capability container that is
// followed by nothing, an empty NDEF TLV or the terminator
static bool create_tag_is_blank(const uint8_t* memory, size_t size) {
    if(size < 8) return false;

    size_t offset = 0;
    if(memory[0] == 0xE1) {
        offset = 4;
    } else if(memory[0] == 0xE2) {
        offset = 8;
    }

    if(offset == 0) {
        for(size_t i = 0; i < size; i++) {
            if(memory[i] != 0) return false;
        }
        return true;
    }

    const uint8_t tlv = memory[offset];
    return tlv == 0x00 || tlv == 0xFE || (tlv == 0x03 && memory[offset + 1] == 0x00);
}

// The tag was read: check it is blank, build the image and start writing it
static void create_handle_tag_read(OpenPrintTag* app) {
    // A tag that was reported without blocks is read again with a new poller
    const OpenPrintTagReadCheck check = openprinttag_check_read(app, create_poller_callback);
    if(check == OpenPrintTagReadRetried) return;
    if(check == OpenPrintTagReadFailed) {
        create_stop_poller(app);
        create_show_result(app, "Error", "Failed to read\nthe tag", true);
        return;
    }

    const Iso15693_3Data* iso_data = nfc_poller_get_data(app->nfc_poller);
    const uint16_t block_count = iso15693_3_get_block_count(iso_data);
    const uint8_t block_size = iso15693_3_get_block_size(iso_data);

    if(block_size == 0 || block_size > CREATE_MAX_BLOCK_SIZE || block_count == 0 ||
       block_count > CREATE_MAX_BLOCK_COUNT) {
        create_stop_poller(app);
        create_show_result(app, "Error", "Unsupported tag", true);
        return;
    }

    const size_t capacity = (size_t)block_count * block_size;
    uint8_t* memory = malloc(capacity);
    for(uint16_t i = 0; i < block_count; i++) {
        const uint8_t* block = iso15693_3_get_block_data(iso_data, i);
        memcpy(memory + (size_t)i * block_size, block, block_size);
    }
    const bool blank = create_tag_is_blank(memory, capacity);
    free(memory);

    if(!blank) {
        create_stop_poller(app);
        create_show_result(app, "Tag not empty", "Use a blank tag", true);
        return;
    }

    // Build the whole image, then pad it to full blocks for writing
    uint8_t* image = malloc(capacity);
    const size_t used = openprinttag_build_tag_image(&app->create, capacity, image);
    if(used == 0) {
        free(image);
        create_stop_poller(app);
        create_show_result(app, "Error", "Data does not\nfit on this tag", true);
        return;
    }

    const uint16_t blocks = (used + block_size - 1) / block_size;
    uint8_t* write_data = malloc((size_t)blocks * block_size);
    memcpy(write_data, image, (size_t)blocks * block_size); // The image is zero padded
    free(image);

    create_free_write_data(app);
    app->write_data = write_data;
    app->write_data_size = (size_t)blocks * block_size;
    app->write_start_block = 0;
    app->write_block_count = blocks;
    app->write_current_block = 0;
    app->write_attempts = 0;
    app->write_in_progress = true;

    // Address the write to this tag. The stored UID is reversed relative to the sent order.
    for(size_t i = 0; i < ISO15693_3_UID_SIZE; i++) {
        app->write_uid[i] = iso_data->uid[ISO15693_3_UID_SIZE - 1 - i];
    }

    FURI_LOG_I(
        TAG,
        "Creating tag: %zu bytes in %d blocks of %d (tag capacity %zu)",
        used,
        blocks,
        block_size,
        capacity);

    // The read poller has finished, replace it with the one that writes
    create_stop_poller(app);
    app->create_phase = CreatePhaseWrite;
    create_show_waiting(app, "Writing tag", "Keep the tag", "near Flipper");
    app->nfc_poller = nfc_poller_alloc(app->nfc, NfcProtocolIso15693_3);
    nfc_poller_start_ex(app->nfc_poller, openprinttag_tag_write_callback, app);
}

// ---- Scene ------------------------------------------------------------------------------------

void openprinttag_scene_create_on_enter(void* context) {
    OpenPrintTag* app = context;

    create_build_form(app);
    create_show_form(app);
}

bool openprinttag_scene_create_on_event(void* context, SceneManagerEvent event) {
    OpenPrintTag* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;

        if(event.event >= CreateEventItemClicked &&
           event.event < CreateEventItemClicked + CreateItemCount) {
            const uint8_t item = event.event - CreateEventItemClicked;

            if(item == CreateItemBrand || item == CreateItemMaterial) {
                create_open_text_input(app, item);
            } else if(item == CreateItemWrite) {
                create_start_scan(app);
            } else if(item != CreateItemType) {
                create_open_number_pad(app, item);
            }
        } else if(event.event == CreateEventTextEntered) {
            char* field = app->create_editing == CreateItemBrand ? app->create.brand :
                                                                   app->create.material;
            snprintf(field, OPENPRINTTAG_TEXT_MAX + 1, "%s", app->text_buffer);
            create_update_item(app, app->create_editing);
            create_show_form(app);
        } else if(event.event == CreateEventNumberEntered) {
            create_update_item(app, app->create_editing);
            create_show_form(app);
        } else if(event.event == CreateEventTagDetected) {
            create_stop_scanner(app);
            app->nfc_poller = nfc_poller_alloc(app->nfc, NfcProtocolIso15693_3);
            nfc_poller_start(app->nfc_poller, create_poller_callback, app);
        } else if(event.event == CreateEventTagRead) {
            create_handle_tag_read(app);
        } else if(event.event == CreateEventWriteDone) {
            create_stop_poller(app);
            create_free_write_data(app);
            create_show_result(app, "Tag created", "OpenPrintTag is\nready to use", false);
        } else if(event.event == CreateEventWriteFailed) {
            create_stop_poller(app);
            create_free_write_data(app);
            create_show_result(app, "Write failed", "Tag is locked or\nrefuses the data", true);
        } else if(event.event == CreateEventResultDismissed) {
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, OpenPrintTagSceneStart);
        } else if(event.event == CreateEventFailureDismissed) {
            create_show_form(app);
        } else {
            consumed = false;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        if(app->create_phase == CreatePhaseInput) {
            // Leave the text input or number pad without changing the value
            create_show_form(app);
            consumed = true;
        } else if(app->create_phase == CreatePhaseScan || app->create_phase == CreatePhaseWrite) {
            // Cancel scanning or writing, the form still holds everything that was entered
            create_stop_scanner(app);
            create_stop_poller(app);
            create_free_write_data(app);
            create_show_form(app);
            consumed = true;
        }
    }

    return consumed;
}

void openprinttag_scene_create_on_exit(void* context) {
    OpenPrintTag* app = context;

    create_stop_scanner(app);
    create_stop_poller(app);
    create_free_write_data(app);

    variable_item_list_reset(app->variable_item_list);
    memset(app->create_items, 0, sizeof(app->create_items));
    app->create_phase = CreatePhaseForm;
    popup_reset(app->popup);
}
