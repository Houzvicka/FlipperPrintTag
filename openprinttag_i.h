#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/widget.h>
#include <gui/modules/popup.h>
#include <gui/modules/loading.h>
#include <gui/modules/variable_item_list.h>
#include "numpad.h"
#include <nfc/nfc.h>
#include <nfc/nfc_device.h>
#include <nfc/nfc_scanner.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/iso15693_3/iso15693_3.h>

#define TAG                    "OpenPrintTag"
#define OPENPRINTTAG_MIME_TYPE "application/vnd.openprinttag"

// OpenPrintTag data structures
typedef struct {
    uint32_t main_region_offset;
    uint32_t main_region_size;
    uint32_t aux_region_offset;
    uint32_t aux_region_size;
} OpenPrintTagMeta;

typedef struct {
    // Brand and material identification
    FuriString* brand_name;
    FuriString* material_name;
    FuriString* material_type_str;
    FuriString* material_abbreviation;
    uint32_t material_type_enum;
    uint32_t material_class;
    uint64_t gtin;

    // Weights (in grams)
    uint32_t nominal_netto_full_weight;
    uint32_t actual_netto_full_weight;
    uint32_t empty_container_weight;

    // Timestamps
    uint64_t manufactured_date;
    uint64_t expiration_date;

    // FFF-specific
    float filament_diameter;
    float nominal_full_length;
    float actual_full_length;
    int32_t min_print_temperature;
    int32_t max_print_temperature;
    int32_t min_bed_temperature;
    int32_t max_bed_temperature;

    // Material properties
    float density;
    uint32_t* tags;
    size_t tags_count;

    bool has_data;
    bool has_material_type_enum;
} OpenPrintTagMain;

typedef struct {
    uint32_t consumed_weight; // in grams
    FuriString* workgroup;
    uint64_t last_stir_time; // timestamp
    bool has_data;
} OpenPrintTagAux;

typedef struct {
    OpenPrintTagMeta meta;
    OpenPrintTagMain main;
    OpenPrintTagAux aux;
    uint8_t* raw_data;
    size_t raw_data_size;
    uint32_t ndef_payload_offset; // byte offset of the OpenPrintTag payload in tag memory
} OpenPrintTagData;

// App scenes
typedef enum {
    OpenPrintTagSceneStart,
    OpenPrintTagSceneRead,
    OpenPrintTagSceneReadSuccess,
    OpenPrintTagSceneReadError,
    OpenPrintTagSceneDisplay,
    OpenPrintTagSceneWrite,
    OpenPrintTagSceneCreate,
    OpenPrintTagSceneNum,
} OpenPrintTagScene;

// App views
typedef enum {
    OpenPrintTagViewSubmenu,
    OpenPrintTagViewWidget,
    OpenPrintTagViewPopup,
    OpenPrintTagViewLoading,
    OpenPrintTagViewVariableItemList,
    OpenPrintTagViewNumberInput,
    OpenPrintTagViewTextInput,
} OpenPrintTagView;

// Custom events sent by openprinttag_tag_write_callback() to the scene that started it
#define OpenPrintTagEventWriteDone   (0x100U)
#define OpenPrintTagEventWriteFailed (0x101U)

// Longest brand / material name that can be entered
#define OPENPRINTTAG_TEXT_MAX (32U)

// Data entered for a new tag
typedef struct {
    char brand[OPENPRINTTAG_TEXT_MAX + 1];
    char material[OPENPRINTTAG_TEXT_MAX + 1];
    uint32_t type_index; // Index into material_types[] (see material_types.h)
    uint32_t weight; // Weight of a full spool, in g (stored as nominal and actual weight)
    uint32_t empty_weight; // Weight of the empty spool, in g, 0 = not stored
    uint32_t nozzle_min; // Print temperatures in degrees C, 0 = not stored
    uint32_t nozzle_max;
    uint32_t bed_min;
    uint32_t bed_max;
} OpenPrintTagCreateData;

#define OPENPRINTTAG_CREATE_ITEMS_MAX (12U)

// Main app structure
typedef struct OpenPrintTag {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Submenu* submenu;
    Widget* widget;
    Popup* popup;
    Loading* loading;
    VariableItemList* variable_item_list;
    NumPad* numpad;
    TextInput* text_input;
    char text_buffer[OPENPRINTTAG_TEXT_MAX + 1]; // Text being edited in the text input view

    Nfc* nfc;
    NfcDevice* nfc_device;
    NfcScanner* nfc_scanner;
    NfcPoller* nfc_poller;
    NfcProtocol detected_protocol;

    OpenPrintTagData tag_data;
    uint32_t temp_consumed_weight; // Temporary value for editing

    // Write state
    bool write_in_progress;
    uint8_t* write_data;
    size_t write_data_size;
    uint16_t write_start_block;
    uint16_t write_block_count;
    uint16_t write_current_block;
    uint8_t write_uid[ISO15693_3_UID_SIZE]; // UID of the tag that was read, in wire order
    uint8_t write_attempts; // Rounds in which the tag answered but the write did not stick

    // Items of the edit screen, kept so their texts can follow the entered value
    VariableItem* write_remaining_item;
    VariableItem* write_consumed_item;
    bool write_number_input_active; // The number keyboard is the visible view
    bool write_number_input_additive; // The entered number is added to the total, not set

    // Creating a new tag
    OpenPrintTagCreateData create;
    VariableItem* create_items[OPENPRINTTAG_CREATE_ITEMS_MAX];
    uint8_t create_editing; // Row being edited in the text input / number pad
    uint8_t create_phase; // See the create scene
} OpenPrintTag;

// Scene handlers
void openprinttag_scene_start_on_enter(void* context);
bool openprinttag_scene_start_on_event(void* context, SceneManagerEvent event);
void openprinttag_scene_start_on_exit(void* context);

void openprinttag_scene_read_on_enter(void* context);
bool openprinttag_scene_read_on_event(void* context, SceneManagerEvent event);
void openprinttag_scene_read_on_exit(void* context);

void openprinttag_scene_read_success_on_enter(void* context);
bool openprinttag_scene_read_success_on_event(void* context, SceneManagerEvent event);
void openprinttag_scene_read_success_on_exit(void* context);

void openprinttag_scene_read_error_on_enter(void* context);
bool openprinttag_scene_read_error_on_event(void* context, SceneManagerEvent event);
void openprinttag_scene_read_error_on_exit(void* context);

void openprinttag_scene_display_on_enter(void* context);
bool openprinttag_scene_display_on_event(void* context, SceneManagerEvent event);
void openprinttag_scene_display_on_exit(void* context);

void openprinttag_scene_write_on_enter(void* context);
bool openprinttag_scene_write_on_event(void* context, SceneManagerEvent event);
void openprinttag_scene_write_on_exit(void* context);

void openprinttag_scene_create_on_enter(void* context);
bool openprinttag_scene_create_on_event(void* context, SceneManagerEvent event);
void openprinttag_scene_create_on_exit(void* context);

// Helper functions
bool openprinttag_parse_ndef(OpenPrintTag* app, const uint8_t* data, size_t size);
bool openprinttag_parse_cbor(OpenPrintTag* app, const uint8_t* payload, size_t size);
void openprinttag_free_data(OpenPrintTagData* data);

// Writes app->write_data (blocks starting at app->write_start_block) to the tag whose UID is in
// app->write_uid. Start it with nfc_poller_start_ex() on an ISO15693-3 poller. It waits until the
// tag is in the field, skips blocks that already hold the data and verifies every block by reading
// it back. It sends OpenPrintTagEventWriteDone or OpenPrintTagEventWriteFailed when finished.
NfcCommand openprinttag_tag_write_callback(NfcGenericEventEx event, void* context);

// Builds the complete memory image of a new tag (capability container, NDEF message with the
// OpenPrintTag record, terminator). Returns the number of bytes used in out, which must hold
// capacity bytes, or 0 if the data does not fit or the tag is not supported.
size_t
    openprinttag_build_tag_image(const OpenPrintTagCreateData* data, size_t capacity, uint8_t* out);

// Encode auxiliary section to CBOR
size_t openprinttag_encode_auxiliary(
    OpenPrintTag* app,
    uint8_t* buffer,
    size_t buffer_size,
    uint32_t consumed_weight);
