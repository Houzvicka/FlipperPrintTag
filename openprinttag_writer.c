#include "openprinttag_i.h"
#include "cbor_encoder.h"
#include "openprinttag_fields.h"
#include <furi.h>

size_t openprinttag_encode_auxiliary(
    OpenPrintTag* app,
    uint8_t* buffer,
    size_t buffer_size,
    uint32_t consumed_weight) {
    CborEncoder encoder;
    cbor_encoder_init(&encoder, buffer, buffer_size);

    // Count how many fields we'll encode
    size_t field_count = 0;
    if(consumed_weight > 0 || app->tag_data.aux.has_data) field_count++; // consumed_weight
    if(furi_string_size(app->tag_data.aux.workgroup) > 0) field_count++; // workgroup
    if(app->tag_data.aux.last_stir_time > 0) field_count++; // last_stir_time

    // If no fields, encode empty map
    if(field_count == 0) {
        cbor_encode_map(&encoder, 0);
        return cbor_encoder_get_size(&encoder);
    }

    // Encode map with field count
    if(!cbor_encode_map(&encoder, field_count)) {
        return 0;
    }

    // Encode consumed_weight (key 0)
    if(consumed_weight > 0 || app->tag_data.aux.has_data) {
        if(!cbor_encode_uint(&encoder, AUX_CONSUMED_WEIGHT)) return 0;
        if(!cbor_encode_uint(&encoder, consumed_weight)) return 0;
    }

    // Encode workgroup (key 1) if present
    if(furi_string_size(app->tag_data.aux.workgroup) > 0) {
        if(!cbor_encode_uint(&encoder, AUX_WORKGROUP)) return 0;
        const char* workgroup_str = furi_string_get_cstr(app->tag_data.aux.workgroup);
        size_t workgroup_len = furi_string_size(app->tag_data.aux.workgroup);
        if(!cbor_encode_text(&encoder, workgroup_str, workgroup_len)) return 0;
    }

    // Encode last_stir_time (key 3) if present
    if(app->tag_data.aux.last_stir_time > 0) {
        if(!cbor_encode_uint(&encoder, AUX_LAST_STIR_TIME)) return 0;
        if(!cbor_encode_uint(&encoder, app->tag_data.aux.last_stir_time)) return 0;
    }

    return cbor_encoder_get_size(&encoder);
}
