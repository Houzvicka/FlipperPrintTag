#include "../openprinttag_i.h"

typedef enum {
    WriteItemConsumedWeight,
    WriteItemSave,
} WriteItem;

static void openprinttag_write_scanner_callback(NfcScannerEvent event, void* context) {
    OpenPrintTag* app = context;

    if(event.type == NfcScannerEventTypeDetected) {
        // Check if ISO15693 was detected
        for(size_t i = 0; i < event.data.protocol_num; i++) {
            if(event.data.protocols[i] == NfcProtocolIso15693_3) {
                app->detected_protocol = NfcProtocolIso15693_3;
                view_dispatcher_send_custom_event(app->view_dispatcher, 1);
                return;
            }
        }
    }
}

static NfcCommand openprinttag_write_poller_callback(NfcGenericEvent event, void* context) {
    OpenPrintTag* app = context;

    if(event.protocol == NfcProtocolIso15693_3) {
        view_dispatcher_send_custom_event(app->view_dispatcher, 2);
        return NfcCommandStop;
    }

    return NfcCommandContinue;
}

static void consumed_weight_change_callback(VariableItem* item) {
    OpenPrintTag* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    // Each step is 20 grams, max 5000g (250 steps)
    app->temp_consumed_weight = index * 20;

    char text[16];
    snprintf(text, sizeof(text), "%lu g", app->temp_consumed_weight);
    variable_item_set_current_value_text(item, text);
}

static void write_item_click_callback(void* context, uint32_t index) {
    OpenPrintTag* app = context;

    if(index == WriteItemSave) {
        // Save clicked - trigger save event
        view_dispatcher_send_custom_event(app->view_dispatcher, 3);
    }
}

void openprinttag_scene_write_on_enter(void* context) {
    OpenPrintTag* app = context;

    // Show loading view
    view_dispatcher_switch_to_view(app->view_dispatcher, OpenPrintTagViewLoading);

    // Allocate NFC device if not already allocated
    if(!app->nfc_device) {
        app->nfc_device = nfc_device_alloc();
    }

    // Start scanner
    app->nfc_scanner = nfc_scanner_alloc(app->nfc);
    nfc_scanner_start(app->nfc_scanner, openprinttag_write_scanner_callback, app);
}

bool openprinttag_scene_write_on_event(void* context, SceneManagerEvent event) {
    OpenPrintTag* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == 1) {
            // Tag detected, stop scanner and start poller
            nfc_scanner_stop(app->nfc_scanner);
            nfc_scanner_free(app->nfc_scanner);
            app->nfc_scanner = NULL;

            app->nfc_poller = nfc_poller_alloc(app->nfc, app->detected_protocol);
            nfc_poller_start(app->nfc_poller, openprinttag_write_poller_callback, app);
            consumed = true;
        } else if(event.event == 2) {
            // Reading complete
            nfc_poller_stop(app->nfc_poller);

            // Get the ISO15693 data
            const Iso15693_3Data* iso_data =
                nfc_device_get_data(app->nfc_device, NfcProtocolIso15693_3);

            if(iso_data) {
                // Read all blocks
                uint16_t block_count = iso15693_3_get_block_count(iso_data);
                uint8_t block_size = iso15693_3_get_block_size(iso_data);

                size_t total_size = block_count * block_size;
                uint8_t* tag_memory = malloc(total_size);

                for(uint16_t i = 0; i < block_count; i++) {
                    const uint8_t* block = iso15693_3_get_block_data(iso_data, i);
                    if(block) {
                        memcpy(tag_memory + (i * block_size), block, block_size);
                    }
                }

                // Parse NDEF
                bool parse_success = openprinttag_parse_ndef(app, tag_memory, total_size);
                free(tag_memory);

                if(parse_success && app->tag_data.main.has_data) {
                    // Initialize temp value with current consumed weight
                    app->temp_consumed_weight = app->tag_data.aux.consumed_weight;

                    // Setup variable item list
                    VariableItemList* vil = app->variable_item_list;
                    variable_item_list_reset(vil);
                    variable_item_list_set_enter_callback(vil, write_item_click_callback, app);

                    // Calculate remaining for display
                    uint32_t total_weight = app->tag_data.main.actual_netto_full_weight;
                    if(total_weight == 0) {
                        total_weight = app->tag_data.main.nominal_netto_full_weight;
                    }
                    uint32_t remaining = 0;
                    if(total_weight > app->temp_consumed_weight) {
                        remaining = total_weight - app->temp_consumed_weight;
                    }

                    // Add info item showing material and remaining
                    char info_text[32];
                    snprintf(info_text, sizeof(info_text), "%lu g", remaining);
                    VariableItem* info_item =
                        variable_item_list_add(vil, "Remaining:", 1, NULL, NULL);
                    variable_item_set_current_value_text(info_item, info_text);

                    // Add consumed weight item (0-5000g in 20g steps = 251 values)
                    VariableItem* item = variable_item_list_add(
                        vil, "Consumed:", 251, consumed_weight_change_callback, app);

                    // Set current value (0-5000g in 20g steps)
                    uint8_t current_index = app->temp_consumed_weight / 20;
                    if(current_index > 250) current_index = 250;
                    variable_item_set_current_value_index(item, current_index);

                    char text[16];
                    snprintf(text, sizeof(text), "%lu g", app->temp_consumed_weight);
                    variable_item_set_current_value_text(item, text);

                    // Add save button
                    variable_item_list_add(vil, "Save to Tag", 0, NULL, NULL);

                    view_dispatcher_switch_to_view(
                        app->view_dispatcher, OpenPrintTagViewVariableItemList);
                } else {
                    // Parsing failed, go back
                    Popup* popup = app->popup;
                    popup_set_header(popup, "Error", 64, 10, AlignCenter, AlignTop);
                    popup_set_text(
                        popup,
                        "Failed to read\nOpenPrintTag data",
                        64,
                        32,
                        AlignCenter,
                        AlignCenter);
                    popup_set_timeout(popup, 2500);
                    popup_set_callback(popup, NULL);
                    popup_enable_timeout(popup);
                    view_dispatcher_switch_to_view(app->view_dispatcher, OpenPrintTagViewPopup);
                    scene_manager_search_and_switch_to_previous_scene(
                        app->scene_manager, OpenPrintTagSceneStart);
                }
            } else {
                // Failed to read tag
                scene_manager_search_and_switch_to_previous_scene(
                    app->scene_manager, OpenPrintTagSceneStart);
            }
            consumed = true;
        } else if(event.event == 3) {
            // Save clicked - encode and write auxiliary section
            if(!app->tag_data.main.has_data) {
                // No tag data, can't write
                Popup* popup = app->popup;
                popup_set_header(popup, "Error", 64, 10, AlignCenter, AlignTop);
                popup_set_text(popup, "No tag data\nto update", 64, 32, AlignCenter, AlignCenter);
                popup_set_timeout(popup, 2500);
                popup_set_callback(popup, NULL);
                popup_enable_timeout(popup);
                view_dispatcher_switch_to_view(app->view_dispatcher, OpenPrintTagViewPopup);
                scene_manager_search_and_switch_to_previous_scene(
                    app->scene_manager, OpenPrintTagSceneStart);
                consumed = true;
            } else {
                // Encode new auxiliary section
                uint8_t aux_buffer[512];
                size_t aux_size = openprinttag_encode_auxiliary(
                    app, aux_buffer, sizeof(aux_buffer), app->temp_consumed_weight);

                if(aux_size == 0 || aux_size > app->tag_data.meta.aux_region_size) {
                    // Encoding failed or too large
                    Popup* popup = app->popup;
                    popup_set_header(popup, "Error", 64, 10, AlignCenter, AlignTop);
                    popup_set_text(
                        popup, "Failed to encode\nupdate data", 64, 32, AlignCenter, AlignCenter);
                    popup_set_timeout(popup, 2500);
                    popup_set_callback(popup, NULL);
                    popup_enable_timeout(popup);
                    view_dispatcher_switch_to_view(app->view_dispatcher, OpenPrintTagViewPopup);
                    scene_manager_search_and_switch_to_previous_scene(
                        app->scene_manager, OpenPrintTagSceneStart);
                    consumed = true;
                } else {
                    // Get ISO15693 data
                    const Iso15693_3Data* iso_data =
                        nfc_device_get_data(app->nfc_device, NfcProtocolIso15693_3);
                    if(!iso_data) {
                        Popup* popup = app->popup;
                        popup_set_header(popup, "Error", 64, 10, AlignCenter, AlignTop);
                        popup_set_text(popup, "Tag data lost", 64, 32, AlignCenter, AlignCenter);
                        popup_set_timeout(popup, 2500);
                        popup_set_callback(popup, NULL);
                        popup_enable_timeout(popup);
                        view_dispatcher_switch_to_view(
                            app->view_dispatcher, OpenPrintTagViewPopup);
                        scene_manager_search_and_switch_to_previous_scene(
                            app->scene_manager, OpenPrintTagSceneStart);
                        consumed = true;
                    } else {
                        uint8_t block_size = iso15693_3_get_block_size(iso_data);
                        uint16_t block_count = iso15693_3_get_block_count(iso_data);

                        // Calculate auxiliary region location in tag memory
                        // NDEF structure: [TLV][Record header][MIME type][Length][Payload...]
                        // We need to find where auxiliary section starts within the payload
                        uint32_t ndef_header_size = 3; // TLV: Type(1) + Length field(~1-3)
                        uint32_t record_header_size =
                            1 + // Record flags
                            1 + // MIME type length
                            strlen("application/vnd.openprinttag") + // MIME type
                            1; // Payload length (assuming <256)

                        uint32_t aux_absolute_offset = ndef_header_size + record_header_size +
                                                       app->tag_data.meta.aux_region_offset;

                        uint16_t start_block = aux_absolute_offset / block_size;
                        uint8_t start_offset = aux_absolute_offset % block_size;

                        // Calculate how many blocks we need to update
                        uint16_t blocks_needed = (aux_size + block_size - 1) / block_size;

                        // Validate we have enough space
                        if(start_block + blocks_needed > block_count) {
                            Popup* popup = app->popup;
                            popup_set_header(popup, "Error", 64, 10, AlignCenter, AlignTop);
                            popup_set_text(
                                popup, "Not enough\ntag memory", 64, 32, AlignCenter, AlignCenter);
                            popup_set_timeout(popup, 2500);
                            popup_set_callback(popup, NULL);
                            popup_enable_timeout(popup);
                            view_dispatcher_switch_to_view(
                                app->view_dispatcher, OpenPrintTagViewPopup);
                            scene_manager_search_and_switch_to_previous_scene(
                                app->scene_manager, OpenPrintTagSceneStart);
                            consumed = true;
                        } else if(start_offset != 0) {
                            // Auxiliary must be block-aligned for simple writing
                            Popup* popup = app->popup;
                            popup_set_header(popup, "Limitation", 64, 10, AlignCenter, AlignTop);
                            popup_set_text(
                                popup,
                                "Aux region not\nblock-aligned",
                                64,
                                32,
                                AlignCenter,
                                AlignCenter);
                            popup_set_timeout(popup, 2500);
                            popup_set_callback(popup, NULL);
                            popup_enable_timeout(popup);
                            view_dispatcher_switch_to_view(
                                app->view_dispatcher, OpenPrintTagViewPopup);
                            scene_manager_search_and_switch_to_previous_scene(
                                app->scene_manager, OpenPrintTagSceneStart);
                            consumed = true;
                        } else {
                            // Store write parameters
                            app->write_start_block = start_block;
                            app->write_block_count = blocks_needed;
                            app->write_current_block = 0;

                            // Prepare write data (pad to block boundary)
                            size_t padded_size = blocks_needed * block_size;
                            app->write_data = malloc(padded_size);
                            if(!app->write_data) {
                                Popup* popup = app->popup;
                                popup_set_header(popup, "Error", 64, 10, AlignCenter, AlignTop);
                                popup_set_text(
                                    popup, "Out of memory", 64, 32, AlignCenter, AlignCenter);
                                popup_set_timeout(popup, 2500);
                                popup_set_callback(popup, NULL);
                                popup_enable_timeout(popup);
                                view_dispatcher_switch_to_view(
                                    app->view_dispatcher, OpenPrintTagViewPopup);
                                scene_manager_search_and_switch_to_previous_scene(
                                    app->scene_manager, OpenPrintTagSceneStart);
                                consumed = true;
                            } else {
                                // Copy encoded data and pad with zeros
                                memcpy(app->write_data, aux_buffer, aux_size);
                                if(aux_size < padded_size) {
                                    memset(app->write_data + aux_size, 0, padded_size - aux_size);
                                }
                                app->write_data_size = padded_size;

                                // Update local aux data immediately
                                app->tag_data.aux.consumed_weight = app->temp_consumed_weight;
                                app->tag_data.aux.has_data = true;

                                // NOTE: Actual ISO15693 WRITE_BLOCK commands require platform HAL access
                                // External apps have limited NFC API access
                                // A full implementation would:
                                // 1. Use nfc_poller_start_ex with custom callback
                                // 2. Send ISO15693_3_CMD_WRITE_BLOCK for each block
                                // 3. Handle write responses and errors
                                // 4. Verify writes with read-back

                                FURI_LOG_I(
                                    TAG,
                                    "Write prepared: %d blocks starting at block %d",
                                    blocks_needed,
                                    start_block);
                                FURI_LOG_I(
                                    TAG, "Auxiliary section CBOR size: %zu bytes", aux_size);
                                FURI_LOG_I(
                                    TAG,
                                    "Consumed weight updated: %lu -> %lu grams",
                                    (app->tag_data.aux.has_data ?
                                         app->tag_data.aux.consumed_weight :
                                         0),
                                    app->temp_consumed_weight);

                                // Log write info (actual data logging omitted for clarity)
                                FURI_LOG_I(
                                    TAG,
                                    "Encoded auxiliary data ready for blocks %d-%d",
                                    start_block,
                                    start_block + blocks_needed - 1);

                                // Show completion message
                                Popup* popup = app->popup;
                                popup_set_header(popup, "Ready!", 64, 10, AlignCenter, AlignTop);
                                popup_set_text(
                                    popup,
                                    "Data encoded\nand validated!\n\nISO15693 write\nAPI not exposed",
                                    64,
                                    32,
                                    AlignCenter,
                                    AlignCenter);
                                popup_set_timeout(popup, 3000);
                                popup_set_callback(popup, NULL);
                                popup_enable_timeout(popup);
                                view_dispatcher_switch_to_view(
                                    app->view_dispatcher, OpenPrintTagViewPopup);

                                // Free write data
                                free(app->write_data);
                                app->write_data = NULL;

                                scene_manager_search_and_switch_to_previous_scene(
                                    app->scene_manager, OpenPrintTagSceneStart);
                                consumed = true;
                            }
                        }
                    }
                }
            }
        }
    }

    return consumed;
}

void openprinttag_scene_write_on_exit(void* context) {
    OpenPrintTag* app = context;

    if(app->nfc_scanner) {
        nfc_scanner_stop(app->nfc_scanner);
        nfc_scanner_free(app->nfc_scanner);
        app->nfc_scanner = NULL;
    }

    if(app->nfc_poller) {
        nfc_poller_stop(app->nfc_poller);
        nfc_poller_free(app->nfc_poller);
        app->nfc_poller = NULL;
    }

    // Clean up write data if any
    if(app->write_data) {
        free(app->write_data);
        app->write_data = NULL;
    }
    app->write_in_progress = false;

    variable_item_list_reset(app->variable_item_list);
    popup_reset(app->popup);
}
