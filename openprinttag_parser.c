#include "openprinttag_i.h"
#include "cbor_parser.h"
#include <furi.h>

// OpenPrintTag field keys (from specification)
#define KEY_AUX_REGION_OFFSET 1
#define KEY_AUX_REGION_SIZE 2

#define KEY_BRAND 1
#define KEY_MATERIAL_NAME 2
#define KEY_MATERIAL_CLASS 3
#define KEY_MATERIAL_TYPE 4
#define KEY_GTIN 10

#define KEY_REMAINING_LENGTH 1
#define KEY_USED_LENGTH 2
#define KEY_TIMESTAMP 10

static bool parse_meta_section(CborParser* parser, OpenPrintTagMeta* meta) {
    size_t count;
    if(!cbor_parse_map(parser, &count)) return false;

    meta->aux_region_offset = 0;
    meta->aux_region_size = 0;

    for(size_t i = 0; i < count; i++) {
        CborValue key, value;

        if(!cbor_parse_value(parser, &key)) return false;
        if(key.type != CborValueTypeUnsigned) {
            cbor_skip_value(parser);
            continue;
        }

        uint32_t key_id = (uint32_t)key.value.u64;

        if(!cbor_parse_value(parser, &value)) return false;

        switch(key_id) {
        case KEY_AUX_REGION_OFFSET:
            if(value.type == CborValueTypeUnsigned) {
                meta->aux_region_offset = (uint32_t)value.value.u64;
            }
            break;
        case KEY_AUX_REGION_SIZE:
            if(value.type == CborValueTypeUnsigned) {
                meta->aux_region_size = (uint32_t)value.value.u64;
            }
            break;
        default:
            break;
        }
    }

    return true;
}

static bool parse_main_section(CborParser* parser, OpenPrintTagMain* main) {
    size_t count;
    if(!cbor_parse_map(parser, &count)) return false;

    main->has_data = false;
    main->material_class = 0;
    main->gtin = 0;

    for(size_t i = 0; i < count; i++) {
        CborValue key, value;

        if(!cbor_parse_value(parser, &key)) return false;
        if(key.type != CborValueTypeUnsigned) {
            cbor_skip_value(parser);
            continue;
        }

        uint32_t key_id = (uint32_t)key.value.u64;

        if(!cbor_parse_value(parser, &value)) return false;

        switch(key_id) {
        case KEY_BRAND:
            if(value.type == CborValueTypeText) {
                furi_string_set_strn(
                    main->brand, (const char*)value.value.text.data, value.value.text.size);
                main->has_data = true;
            }
            break;
        case KEY_MATERIAL_NAME:
            if(value.type == CborValueTypeText) {
                furi_string_set_strn(
                    main->material_name,
                    (const char*)value.value.text.data,
                    value.value.text.size);
                main->has_data = true;
            }
            break;
        case KEY_MATERIAL_TYPE:
            if(value.type == CborValueTypeText) {
                furi_string_set_strn(
                    main->material_type,
                    (const char*)value.value.text.data,
                    value.value.text.size);
                main->has_data = true;
                main->has_material_type_enum = false;
            } else if(value.type == CborValueTypeUnsigned) {
                main->material_type_enum = (uint32_t)value.value.u64;
                main->has_material_type_enum = true;
                main->has_data = true;
            }
            break;
        case KEY_MATERIAL_CLASS:
            if(value.type == CborValueTypeUnsigned) {
                main->material_class = (uint32_t)value.value.u64;
            }
            break;
        case KEY_GTIN:
            if(value.type == CborValueTypeUnsigned) {
                main->gtin = value.value.u64;
            }
            break;
        default:
            break;
        }
    }

    return main->has_data;
}

static bool parse_aux_section(CborParser* parser, OpenPrintTagAux* aux) {
    size_t count;
    if(!cbor_parse_map(parser, &count)) return false;

    aux->has_data = false;
    aux->remaining_length = 0;
    aux->used_length = 0;
    aux->timestamp = 0;

    for(size_t i = 0; i < count; i++) {
        CborValue key, value;

        if(!cbor_parse_value(parser, &key)) return false;
        if(key.type != CborValueTypeUnsigned) {
            cbor_skip_value(parser);
            continue;
        }

        uint32_t key_id = (uint32_t)key.value.u64;

        if(!cbor_parse_value(parser, &value)) return false;

        switch(key_id) {
        case KEY_REMAINING_LENGTH:
            if(value.type == CborValueTypeUnsigned) {
                aux->remaining_length = (uint32_t)value.value.u64;
                aux->has_data = true;
            }
            break;
        case KEY_USED_LENGTH:
            if(value.type == CborValueTypeUnsigned) {
                aux->used_length = (uint32_t)value.value.u64;
                aux->has_data = true;
            }
            break;
        case KEY_TIMESTAMP:
            if(value.type == CborValueTypeUnsigned) {
                aux->timestamp = value.value.u64;
                aux->has_data = true;
            }
            break;
        default:
            break;
        }
    }

    return true;
}

bool openprinttag_parse_cbor(OpenPrintTag* app, const uint8_t* payload, size_t size) {
    CborParser parser;
    cbor_parser_init(&parser, payload, size);

    // Parse meta section
    if(!parse_meta_section(&parser, &app->tag_data.meta)) {
        FURI_LOG_E(TAG, "Failed to parse meta section");
        return false;
    }

    // Parse main section
    if(!parse_main_section(&parser, &app->tag_data.main)) {
        FURI_LOG_E(TAG, "Failed to parse main section");
        return false;
    }

    // Parse auxiliary section if present
    if(app->tag_data.meta.aux_region_offset > 0) {
        cbor_parser_init(
            &parser, payload + app->tag_data.meta.aux_region_offset, size - app->tag_data.meta.aux_region_offset);
        if(!parse_aux_section(&parser, &app->tag_data.aux)) {
            FURI_LOG_W(TAG, "Failed to parse aux section");
        }
    }

    FURI_LOG_I(TAG, "Successfully parsed OpenPrintTag data");
    return true;
}
