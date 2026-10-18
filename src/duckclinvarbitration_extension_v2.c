/* RClinVarbitration DuckDB extension: stable C API v2 host
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This translation unit targets DuckDB's stable C extension API v2
 * (`duckdb_extension_v2.h`, API version v2.0.0). DuckDB v2.0 and later emit
 * `C_STRUCT` metadata whose C API version is v2.0.0 and dispatch to the v2
 * entrypoint, so this host is what a modern engine loads.
 *
 * The XML scanners themselves are engine-agnostic and live in the shared
 * `.inc` cores, which the v1 host (duckclinvarbitration_extension.c) includes
 * too. Only the DuckDB glue differs between the two hosts.
 */
#define DUCKDB_EXTENSION_NAME duckclinvarbitration
#define DUCKDB_V2_API_ALLOW_UNSTABLE 0
#define DUCKDB_V2_API_ALLOW_DEPRECATED 0
#include "duckdb_extension_v2.h"

#include <libxml/xmlreader.h>
#include <zlib.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(DUCKDB_EXTENSION_HEADER_VERSION) || DUCKDB_EXTENSION_HEADER_VERSION != 2 || \
    !defined(DUCKDB_V2_API_VERSION_MAJOR) || DUCKDB_V2_API_VERSION_MAJOR != 2
#error "RClinVarbitration requires the DuckDB C API v2 SDK (duckdb_extension_v2.h)"
#endif

DUCKDB_EXTENSION_EXTERN

/* Engine-agnostic ClinVar/PubMed XML scanners shared by every DuckDB host. */
#include "rclinvarbitration_clinvar_core.inc"
#include "rclinvarbitration_pubmed_core.inc"

#define RCLINVAR_COLUMN_COUNT 10U
#define RPUBMED_COLUMN_COUNT 13U

/* ==================================================================== */
/* C API v2 helpers                                                      */
/* ==================================================================== */

static duckdb_v2_str rclinv2_view(const char *text) {
    duckdb_v2_str view = {text, text ? (idx_t)strlen(text) : 0};
    return view;
}

static void rclinv2_set_error(duckdb_v2_error_info_handle target, DUCKDB_V2_ERROR code,
                              const char *text) {
    (void)duckdb_v2_error_info_set_code(target, code);
    (void)duckdb_v2_error_info_set_text(target, rclinv2_view(text));
}

/* Callback error handles are borrowed. Each fallible API call writes into its
 * own owned slot, whose detail is copied into the callback handle. */
static void rclinv2_copy_error(duckdb_v2_error_info_handle target, DUCKDB_V2_ERROR code,
                               duckdb_v2_error_info_handle source) {
    duckdb_v2_str text = rclinv2_view("RClinVarbitration DuckDB C API v2 call failed");
    if (source) {
        (void)duckdb_v2_error_info_get_text(source, &text);
    }
    (void)duckdb_v2_error_info_set_code(target, code);
    (void)duckdb_v2_error_info_set_text(target, text);
}

#define RCLINV2_CALL(expression)                                                                    \
    do {                                                                                            \
        DUCKDB_V2_ERROR call_status = (expression);                                                 \
        if (call_status != DUCKDB_V2_ERROR_NONE) {                                                  \
            rclinv2_copy_error(*error, call_status, detail);                                        \
            goto cleanup;                                                                           \
        }                                                                                           \
    } while (0)

#define RCLINV2_INPUT_ERROR(text)                                                                   \
    do {                                                                                            \
        rclinv2_set_error(*error, DUCKDB_V2_ERROR_INPUT_INVALID, (text));                           \
        goto cleanup;                                                                               \
    } while (0)

static DUCKDB_V2_ERROR rclinv2_write_bytes(duckdb_v2_arena_handle arena, duckdb_v2_bytes *output,
                                           const char *text, size_t length,
                                           duckdb_v2_error_info_handle *detail) {
    memset(output, 0, sizeof(*output));
    if (length > UINT32_MAX) return DUCKDB_V2_ERROR_INPUT_INVALID;
    output->value.inlined.length = (uint32_t)length;
    if (length <= sizeof(output->value.inlined.inlined)) {
        if (length > 0) memcpy(output->value.inlined.inlined, text, length);
        return DUCKDB_V2_ERROR_NONE;
    }
    {
        uint8_t *target = NULL;
        DUCKDB_V2_ERROR status = duckdb_v2_arena_allocate(arena, (idx_t)length, &target, detail);
        if (status != DUCKDB_V2_ERROR_NONE) return status;
        memcpy(target, text, length);
        memcpy(output->value.pointer.prefix, text, sizeof(output->value.pointer.prefix));
        output->value.pointer.ptr = (char *)target;
        return DUCKDB_V2_ERROR_NONE;
    }
}

/* Output columns of a table-function chunk, bound once per exec callback. */
typedef struct rclinv2_columns {
    duckdb_v2_vector_handle columns[RPUBMED_COLUMN_COUNT];
    void *data[RPUBMED_COLUMN_COUNT];
    duckdb_v2_arena_handle arena[RPUBMED_COLUMN_COUNT];
} rclinv2_columns_t;

static DUCKDB_V2_ERROR rclinv2_bind_vectors(duckdb_v2_data_chunk_handle chunk, idx_t column_count,
                                            rclinv2_columns_t *out,
                                            duckdb_v2_error_info_handle *detail) {
    idx_t index;
    memset(out, 0, sizeof(*out));
    for (index = 0; index < column_count; index++) {
        DUCKDB_V2_ERROR status =
            duckdb_v2_data_chunk_get_vector(chunk, index, &out->columns[index], detail);
        if (status != DUCKDB_V2_ERROR_NONE) return status;
    }
    return DUCKDB_V2_ERROR_NONE;
}

/* Every output vector is sized before use so vector_set_null accepts each row;
 * the engine reads the batch cardinality from the first vector afterwards. */
static DUCKDB_V2_ERROR rclinv2_size_columns(rclinv2_columns_t *out, idx_t column_count, idx_t size,
                                            duckdb_v2_error_info_handle *detail) {
    idx_t index;
    for (index = 0; index < column_count; index++) {
        DUCKDB_V2_ERROR status = duckdb_v2_vector_set_size(out->columns[index], size, detail);
        if (status != DUCKDB_V2_ERROR_NONE) return status;
    }
    return DUCKDB_V2_ERROR_NONE;
}

static DUCKDB_V2_ERROR rclinv2_prepare_columns(rclinv2_columns_t *out, idx_t column_count,
                                               const char *string_column,
                                               duckdb_v2_error_info_handle *detail) {
    idx_t index;
    for (index = 0; index < column_count; index++) {
        DUCKDB_V2_ERROR status =
            duckdb_v2_vector_get_data_mutable(out->columns[index], &out->data[index], detail);
        if (status != DUCKDB_V2_ERROR_NONE) return status;
        if (string_column[index]) {
            status = duckdb_v2_vector_get_arena(out->columns[index], &out->arena[index], detail);
            if (status != DUCKDB_V2_ERROR_NONE) return status;
        }
    }
    return DUCKDB_V2_ERROR_NONE;
}

/* Writes one VARCHAR cell, marking the row NULL when `value` is NULL. */
static DUCKDB_V2_ERROR rclinv2_write_string(rclinv2_columns_t *out, idx_t column, idx_t row,
                                            const char *value,
                                            duckdb_v2_error_info_handle *detail) {
    if (!value) return duckdb_v2_vector_set_null(out->columns[column], row, detail);
    return rclinv2_write_bytes(out->arena[column],
                               &((duckdb_v2_bytes *)out->data[column])[row], value, strlen(value),
                               detail);
}

/* Reads a cell of a VARCHAR input view. */
static idx_t rclinv2_physical_row(const duckdb_v2_vector_view *view, idx_t row) {
    return view->sel ? view->sel[row] : row;
}

static bool rclinv2_row_valid(const duckdb_v2_vector_view *view, idx_t row) {
    idx_t position = rclinv2_physical_row(view, row);
    return !view->validity ||
           (view->validity[position >> 6] & (UINT64_C(1) << (position & 63))) != 0;
}

static const char *rclinv2_cell_data(const duckdb_v2_vector_view *view, idx_t row) {
    const duckdb_v2_bytes *slot =
        &((const duckdb_v2_bytes *)view->data)[rclinv2_physical_row(view, row)];
    if (slot->value.inlined.length <= sizeof(slot->value.inlined.inlined)) {
        return slot->value.inlined.inlined;
    }
    return slot->value.pointer.ptr;
}

static size_t rclinv2_cell_length(const duckdb_v2_vector_view *view, idx_t row) {
    const duckdb_v2_bytes *slot =
        &((const duckdb_v2_bytes *)view->data)[rclinv2_physical_row(view, row)];
    return (size_t)slot->value.inlined.length;
}

/* ==================================================================== */
/* ClinVar host                                                          */
/* ==================================================================== */

static const char rclinv2_clinvar_string_column[RCLINVAR_COLUMN_COUNT] = {
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1};

static DUCKDB_V2_ERROR rclinv2_add_column(duckdb_v2_table_function_bind_info_handle info,
                                          duckdb_v2_context_handle context, const char *name,
                                          const char *type_name,
                                          duckdb_v2_error_info_handle *detail) {
    duckdb_v2_logical_type_handle type = NULL;
    DUCKDB_V2_ERROR status =
        duckdb_v2_context_create_type_from_text(context, rclinv2_view(type_name), &type, detail);
    if (status == DUCKDB_V2_ERROR_NONE) {
        status =
            duckdb_v2_table_function_bind_add_result_column(info, rclinv2_view(name), type, detail);
    }
    (void)duckdb_v2_logical_type_destroy(&type);
    return status;
}

static DUCKDB_V2_ERROR rclinv2_add_clinvar_columns(duckdb_v2_table_function_bind_info_handle info,
                                                   duckdb_v2_context_handle context,
                                                   duckdb_v2_error_info_handle *detail) {
    static const char *names[RCLINVAR_COLUMN_COUNT] = {
        "record_ordinal", "entity_ordinal", "vcv_accession", "rcv_entity_id", "scv_entity_id",
        "entity_type",    "entity_id",      "parent_type",   "parent_id",     "fields_json"};
    idx_t index;
    for (index = 0; index < RCLINVAR_COLUMN_COUNT; index++) {
        const char *type_name = rclinv2_clinvar_string_column[index] ? "VARCHAR" : "UBIGINT";
        DUCKDB_V2_ERROR status = rclinv2_add_column(info, context, names[index], type_name, detail);
        if (status != DUCKDB_V2_ERROR_NONE) return status;
    }
    return DUCKDB_V2_ERROR_NONE;
}

static DUCKDB_V2_ERROR rclinv2_add_pubmed_columns(duckdb_v2_table_function_bind_info_handle info,
                                                  duckdb_v2_context_handle context,
                                                  duckdb_v2_error_info_handle *detail) {
    static const char *names[RPUBMED_COLUMN_COUNT] = {
        "record_ordinal", "entity_ordinal",   "pmid",           "entity_type",       "section",
        "text_value",     "identifier_source", "identifier",    "citation_ordinal",  "article_title",
        "publication_date", "source_date",    "is_deleted"};
    static const char *types[RPUBMED_COLUMN_COUNT] = {
        "UBIGINT", "UBIGINT", "VARCHAR", "VARCHAR", "VARCHAR", "VARCHAR", "VARCHAR",
        "VARCHAR", "UBIGINT", "VARCHAR", "VARCHAR", "VARCHAR", "BOOLEAN"};
    idx_t index;
    for (index = 0; index < RPUBMED_COLUMN_COUNT; index++) {
        DUCKDB_V2_ERROR status = rclinv2_add_column(info, context, names[index], types[index], detail);
        if (status != DUCKDB_V2_ERROR_NONE) return status;
    }
    return DUCKDB_V2_ERROR_NONE;
}

static void rclinv2_xml_bind(duckdb_v2_table_function_bind_info_handle info,
                             duckdb_v2_context_handle context,
                             duckdb_v2_error_info_handle *error) {
    duckdb_v2_error_info_handle detail = NULL;
    duckdb_v2_value_handle value = NULL;
    duckdb_v2_str raw = {NULL, 0};
    char *path = NULL;
    rclinvar_bind_state_t *state = NULL;
    idx_t count = 0;
    bool bound = false;
    RCLINV2_CALL(duckdb_v2_table_function_bind_get_arg_count(info, &count, &detail));
    if (count != 1U) {
        RCLINV2_INPUT_ERROR("clinvar_xml_entities() requires exactly one XML or XML.GZ path");
    }
    RCLINV2_CALL(duckdb_v2_table_function_bind_get_arg_value(info, 0, &value, &detail));
    if (value) RCLINV2_CALL(duckdb_v2_value_get_varchar(value, &raw, &detail));
    if (!value || !raw.ptr || raw.len == 0) {
        RCLINV2_INPUT_ERROR("clinvar_xml_entities() path must be a non-empty string");
    }
    /* raw.ptr borrows the value handle, so copy it before releasing the value. */
    path = (char *)malloc((size_t)raw.len + 1U);
    if (!path) RCLINV2_INPUT_ERROR("out of memory copying ClinVar XML path");
    memcpy(path, raw.ptr, (size_t)raw.len);
    path[raw.len] = '\0';
    RCLINV2_CALL(duckdb_v2_value_destroy(&value));
    state = (rclinvar_bind_state_t *)calloc(1, sizeof(*state));
    if (!state) RCLINV2_INPUT_ERROR("out of memory binding clinvar_xml_entities()");
    state->path = path;
    path = NULL;
    RCLINV2_CALL(rclinv2_add_clinvar_columns(info, context, &detail));
    {
        duckdb_v2_opaque opaque = {state, rclinvar_bind_destroy, NULL};
        RCLINV2_CALL(duckdb_v2_table_function_bind_set_bind_data(info, &opaque, &detail));
    }
    bound = true;
cleanup:
    if (!bound && state) rclinvar_bind_destroy(state);
    free(path);
    (void)duckdb_v2_value_destroy(&value);
    (void)duckdb_v2_error_info_destroy(&detail);
}

static void rclinv2_xml_init(duckdb_v2_table_function_init_global_info_handle info,
                             duckdb_v2_context_handle context,
                             duckdb_v2_error_info_handle *error) {
    duckdb_v2_error_info_handle detail = NULL;
    rclinvar_bind_state_t *bind = NULL;
    rclinvar_scan_state_t *state = NULL;
    bool owned = false;
    (void)context;
    RCLINV2_CALL(duckdb_v2_table_function_init_global_get_bind_data(info, (void **)&bind, &detail));
    if (!bind || !bind->path) {
        RCLINV2_INPUT_ERROR("clinvar_xml_entities() bind state is missing");
    }
    state = (rclinvar_scan_state_t *)calloc(1, sizeof(*state));
    if (!state) RCLINV2_INPUT_ERROR("out of memory initializing ClinVar XML scan");
    if (rclinvar_is_gzip_path(bind->path)) {
        gzFile input = gzopen(bind->path, "rb");
        if (input) {
            state->reader = xmlReaderForIO(
                rclinvar_gzip_read, rclinvar_gzip_close, input, bind->path, NULL,
                XML_PARSE_NONET | XML_PARSE_COMPACT | XML_PARSE_NOENT);
            if (!state->reader) gzclose(input);
        }
    } else {
        state->reader =
            xmlReaderForFile(bind->path, NULL, XML_PARSE_NONET | XML_PARSE_COMPACT | XML_PARSE_NOENT);
    }
    if (!state->reader) RCLINV2_INPUT_ERROR("failed to open or parse ClinVar XML/XML.GZ input");
    RCLINV2_CALL(duckdb_v2_table_function_init_global_set_max_threads(info, 1, &detail));
    {
        duckdb_v2_opaque opaque = {state, rclinvar_scan_destroy, NULL};
        RCLINV2_CALL(duckdb_v2_table_function_init_global_set_global_state(info, &opaque, &detail));
    }
    owned = true;
cleanup:
    if (!owned && state) rclinvar_scan_destroy(state);
    (void)duckdb_v2_error_info_destroy(&detail);
}

static DUCKDB_V2_ERROR rclinv2_assign_entity(rclinv2_columns_t *out, idx_t row,
                                             rclinvar_entity_t *entity,
                                             duckdb_v2_error_info_handle *detail) {
    DUCKDB_V2_ERROR status;
    ((uint64_t *)out->data[0])[row] = entity->record_ordinal;
    ((uint64_t *)out->data[1])[row] = entity->entity_ordinal;
    status = rclinv2_write_string(out, 2, row, entity->vcv_accession, detail);
    if (status != DUCKDB_V2_ERROR_NONE) return status;
    status = rclinv2_write_string(out, 3, row, entity->rcv_entity_id, detail);
    if (status != DUCKDB_V2_ERROR_NONE) return status;
    status = rclinv2_write_string(out, 4, row, entity->scv_entity_id, detail);
    if (status != DUCKDB_V2_ERROR_NONE) return status;
    status = rclinv2_write_string(out, 5, row, entity->entity_type, detail);
    if (status != DUCKDB_V2_ERROR_NONE) return status;
    status = rclinv2_write_string(out, 6, row, entity->entity_id, detail);
    if (status != DUCKDB_V2_ERROR_NONE) return status;
    status = rclinv2_write_string(out, 7, row, entity->parent_type, detail);
    if (status != DUCKDB_V2_ERROR_NONE) return status;
    status = rclinv2_write_string(out, 8, row, entity->parent_id, detail);
    if (status != DUCKDB_V2_ERROR_NONE) return status;
    return rclinv2_write_string(out, 9, row, entity->fields_json, detail);
}

static void rclinv2_xml_exec(duckdb_v2_table_function_exec_info_handle info,
                             duckdb_v2_context_handle context,
                             duckdb_v2_error_info_handle *error) {
    duckdb_v2_error_info_handle detail = NULL;
    rclinvar_scan_state_t *state = NULL;
    duckdb_v2_data_chunk_handle chunk = NULL;
    rclinv2_columns_t columns;
    rclinvar_entity_t *batch = NULL;
    idx_t count = 0;
    idx_t index;
    (void)context;
    RCLINV2_CALL(duckdb_v2_table_function_exec_get_global_state(info, (void **)&state, &detail));
    RCLINV2_CALL(duckdb_v2_table_function_exec_get_output_chunk(info, &chunk, &detail));
    if (!state) RCLINV2_INPUT_ERROR("clinvar_xml_entities() scan state is missing");
    batch = (rclinvar_entity_t *)malloc(sizeof(rclinvar_entity_t) * RCLINVAR_CHUNK_SIZE);
    if (!batch) RCLINV2_INPUT_ERROR("out of memory assembling a ClinVar batch");
    RCLINV2_CALL(rclinv2_bind_vectors(chunk, RCLINVAR_COLUMN_COUNT, &columns, &detail));
    /* The scanner recycles its queue once drained, so move entities into a batch
     * buffer before the output vectors are sized and filled. */
    while (count < RCLINVAR_CHUNK_SIZE) {
        int status;
        if (state->entity_pos >= state->entity_count) {
            status = rclinvar_scan_next(state);
            if (status < 0) {
                RCLINV2_INPUT_ERROR(state->error[0] ? state->error : "ClinVar XML scan failed");
            }
            if (status == 0) break;
        }
        batch[count] = state->entities[state->entity_pos];
        memset(&state->entities[state->entity_pos], 0, sizeof(rclinvar_entity_t));
        state->entity_pos++;
        count++;
    }
    RCLINV2_CALL(rclinv2_size_columns(&columns, RCLINVAR_COLUMN_COUNT, count, &detail));
    RCLINV2_CALL(
        rclinv2_prepare_columns(&columns, RCLINVAR_COLUMN_COUNT, rclinv2_clinvar_string_column, &detail));
    for (index = 0; index < count; index++) {
        RCLINV2_CALL(rclinv2_assign_entity(&columns, index, &batch[index], &detail));
    }
cleanup:
    if (batch) {
        for (index = 0; index < count; index++) rclinvar_entity_clear(&batch[index]);
        free(batch);
    }
    (void)duckdb_v2_error_info_destroy(&detail);
}

static void rclinv2_json_field_exec(duckdb_v2_scalar_function_exec_info_handle info,
                                    duckdb_v2_context_handle context,
                                    duckdb_v2_error_info_handle *error) {
    duckdb_v2_error_info_handle detail = NULL;
    duckdb_v2_vector_handle fields = NULL;
    duckdb_v2_vector_handle keys = NULL;
    duckdb_v2_vector_handle output = NULL;
    duckdb_v2_vector_view fields_view;
    duckdb_v2_vector_view keys_view;
    duckdb_v2_arena_handle arena = NULL;
    void *output_data = NULL;
    idx_t row_count = 0;
    idx_t row;
    (void)context;
    RCLINV2_CALL(duckdb_v2_scalar_function_exec_get_row_count(info, &row_count, &detail));
    RCLINV2_CALL(duckdb_v2_scalar_function_exec_get_arg(info, 0, &fields, &detail));
    RCLINV2_CALL(duckdb_v2_scalar_function_exec_get_arg(info, 1, &keys, &detail));
    RCLINV2_CALL(duckdb_v2_vector_flatten(fields, &detail));
    RCLINV2_CALL(duckdb_v2_vector_flatten(keys, &detail));
    RCLINV2_CALL(duckdb_v2_vector_get_view(fields, &fields_view, &detail));
    RCLINV2_CALL(duckdb_v2_vector_get_view(keys, &keys_view, &detail));
    RCLINV2_CALL(duckdb_v2_scalar_function_exec_get_result(info, &output, &detail));
    RCLINV2_CALL(duckdb_v2_vector_set_size(output, row_count, &detail));
    RCLINV2_CALL(duckdb_v2_vector_get_data_mutable(output, &output_data, &detail));
    RCLINV2_CALL(duckdb_v2_vector_get_arena(output, &arena, &detail));
    for (row = 0; row < row_count; row++) {
        char *value;
        size_t value_length = 0;
        DUCKDB_V2_ERROR status;
        if (!rclinv2_row_valid(&fields_view, row) || !rclinv2_row_valid(&keys_view, row)) {
            RCLINV2_CALL(duckdb_v2_vector_set_null(output, row, &detail));
            continue;
        }
        value = rclinvar_json_field_value(
            rclinv2_cell_data(&fields_view, row), rclinv2_cell_length(&fields_view, row),
            rclinv2_cell_data(&keys_view, row), rclinv2_cell_length(&keys_view, row), &value_length);
        if (!value) {
            RCLINV2_CALL(duckdb_v2_vector_set_null(output, row, &detail));
            continue;
        }
        status =
            rclinv2_write_bytes(arena, &((duckdb_v2_bytes *)output_data)[row], value, value_length, &detail);
        free(value);
        if (status != DUCKDB_V2_ERROR_NONE) {
            rclinv2_copy_error(*error, status, detail);
            goto cleanup;
        }
    }
cleanup:
    (void)duckdb_v2_error_info_destroy(&detail);
}

static bool rclinv2_register_json_field(duckdb_v2_extension_handle extension,
                                        duckdb_v2_context_handle context,
                                        duckdb_v2_error_info_handle *error) {
    duckdb_v2_error_info_handle detail = NULL;
    duckdb_v2_scalar_function_handle function = NULL;
    duckdb_v2_function_signature_handle signature = NULL;
    duckdb_v2_logical_type_handle varchar_type = NULL;
    duckdb_v2_str name = rclinv2_view("rclinvar_json_field");
    bool success = false;
    RCLINV2_CALL(duckdb_v2_scalar_function_create_with_extension(extension, &function, &detail));
    RCLINV2_CALL(duckdb_v2_scalar_function_set_name(function, &name, &detail));
    RCLINV2_CALL(duckdb_v2_scalar_function_get_signature(function, &signature, &detail));
    RCLINV2_CALL(
        duckdb_v2_context_create_type_from_text(context, rclinv2_view("VARCHAR"), &varchar_type, &detail));
    RCLINV2_CALL(duckdb_v2_function_signature_add_parameter(signature, rclinv2_view("json"),
                                                            varchar_type, NULL, &detail));
    RCLINV2_CALL(duckdb_v2_function_signature_add_parameter(signature, rclinv2_view("key"),
                                                            varchar_type, NULL, &detail));
    RCLINV2_CALL(duckdb_v2_function_signature_set_return_type(signature, varchar_type, &detail));
    RCLINV2_CALL(duckdb_v2_scalar_function_set_exec_callback(function, rclinv2_json_field_exec, &detail));
    RCLINV2_CALL(duckdb_v2_scalar_function_register(function, &detail));
    success = true;
cleanup:
    (void)duckdb_v2_logical_type_destroy(&varchar_type);
    (void)duckdb_v2_scalar_function_destroy(&function);
    (void)duckdb_v2_error_info_destroy(&detail);
    return success;
}

static bool rclinv2_register_xml_entities(duckdb_v2_extension_handle extension,
                                          duckdb_v2_context_handle context,
                                          duckdb_v2_error_info_handle *error) {
    duckdb_v2_error_info_handle detail = NULL;
    duckdb_v2_table_function_handle function = NULL;
    duckdb_v2_function_signature_handle signature = NULL;
    duckdb_v2_logical_type_handle varchar_type = NULL;
    duckdb_v2_str name = rclinv2_view("clinvar_xml_entities");
    bool success = false;
    RCLINV2_CALL(duckdb_v2_table_function_create_with_extension(extension, &function, &detail));
    RCLINV2_CALL(duckdb_v2_table_function_set_name(function, &name, &detail));
    RCLINV2_CALL(duckdb_v2_table_function_get_signature(function, &signature, &detail));
    RCLINV2_CALL(
        duckdb_v2_context_create_type_from_text(context, rclinv2_view("VARCHAR"), &varchar_type, &detail));
    RCLINV2_CALL(duckdb_v2_function_signature_add_parameter(signature, rclinv2_view("path"),
                                                            varchar_type, NULL, &detail));
    RCLINV2_CALL(duckdb_v2_table_function_set_bind_callback(function, rclinv2_xml_bind, &detail));
    RCLINV2_CALL(duckdb_v2_table_function_set_init_global_callback(function, rclinv2_xml_init, &detail));
    RCLINV2_CALL(duckdb_v2_table_function_set_exec_callback(function, rclinv2_xml_exec, &detail));
    RCLINV2_CALL(duckdb_v2_table_function_register(function, &detail));
    success = true;
cleanup:
    (void)duckdb_v2_logical_type_destroy(&varchar_type);
    (void)duckdb_v2_table_function_destroy(&function);
    (void)duckdb_v2_error_info_destroy(&detail);
    return success;
}

/* ==================================================================== */
/* PubMed host                                                           */
/* ==================================================================== */

static const char rclinv2_pubmed_string_column[RPUBMED_COLUMN_COUNT] = {
    0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0};

static void rclinv2_pubmed_bind(duckdb_v2_table_function_bind_info_handle info,
                                duckdb_v2_context_handle context,
                                duckdb_v2_error_info_handle *error) {
    duckdb_v2_error_info_handle detail = NULL;
    duckdb_v2_value_handle value = NULL;
    duckdb_v2_str raw = {NULL, 0};
    char *path = NULL;
    rclinvar_bind_state_t *state = NULL;
    idx_t count = 0;
    bool bound = false;
    RCLINV2_CALL(duckdb_v2_table_function_bind_get_arg_count(info, &count, &detail));
    if (count != 1U) {
        RCLINV2_INPUT_ERROR(
            "rclinvarbitration_pubmed_xml_rows() requires exactly one XML or XML.GZ path");
    }
    RCLINV2_CALL(duckdb_v2_table_function_bind_get_arg_value(info, 0, &value, &detail));
    if (value) RCLINV2_CALL(duckdb_v2_value_get_varchar(value, &raw, &detail));
    if (!value || !raw.ptr || raw.len == 0) {
        RCLINV2_INPUT_ERROR("rclinvarbitration_pubmed_xml_rows() path must be a non-empty string");
    }
    path = (char *)malloc((size_t)raw.len + 1U);
    if (!path) RCLINV2_INPUT_ERROR("out of memory copying PubMed XML path");
    memcpy(path, raw.ptr, (size_t)raw.len);
    path[raw.len] = '\0';
    RCLINV2_CALL(duckdb_v2_value_destroy(&value));
    state = (rclinvar_bind_state_t *)calloc(1, sizeof(*state));
    if (!state) RCLINV2_INPUT_ERROR("out of memory binding rclinvarbitration_pubmed_xml_rows()");
    state->path = path;
    path = NULL;
    RCLINV2_CALL(rclinv2_add_pubmed_columns(info, context, &detail));
    {
        duckdb_v2_opaque opaque = {state, rclinvar_bind_destroy, NULL};
        RCLINV2_CALL(duckdb_v2_table_function_bind_set_bind_data(info, &opaque, &detail));
    }
    bound = true;
cleanup:
    if (!bound && state) rclinvar_bind_destroy(state);
    free(path);
    (void)duckdb_v2_value_destroy(&value);
    (void)duckdb_v2_error_info_destroy(&detail);
}

static void rclinv2_pubmed_init(duckdb_v2_table_function_init_global_info_handle info,
                                duckdb_v2_context_handle context,
                                duckdb_v2_error_info_handle *error) {
    duckdb_v2_error_info_handle detail = NULL;
    rclinvar_bind_state_t *bind = NULL;
    rpubmed_scan_state_t *state = NULL;
    bool owned = false;
    (void)context;
    RCLINV2_CALL(duckdb_v2_table_function_init_global_get_bind_data(info, (void **)&bind, &detail));
    if (!bind || !bind->path) {
        RCLINV2_INPUT_ERROR("rclinvarbitration_pubmed_xml_rows() bind state is missing");
    }
    state = (rpubmed_scan_state_t *)calloc(1, sizeof(*state));
    if (!state) RCLINV2_INPUT_ERROR("out of memory initializing PubMed XML scan");
    if (rclinvar_is_gzip_path(bind->path)) {
        gzFile input = gzopen(bind->path, "rb");
        if (input) {
            state->reader = xmlReaderForIO(
                rclinvar_gzip_read, rclinvar_gzip_close, input, bind->path, NULL,
                XML_PARSE_NONET | XML_PARSE_COMPACT | XML_PARSE_NOENT);
            if (!state->reader) gzclose(input);
        }
    } else {
        state->reader =
            xmlReaderForFile(bind->path, NULL, XML_PARSE_NONET | XML_PARSE_COMPACT | XML_PARSE_NOENT);
    }
    if (!state->reader) {
        RCLINV2_INPUT_ERROR("failed to open or parse PubMed XML/XML.GZ input");
    }
    RCLINV2_CALL(duckdb_v2_table_function_init_global_set_max_threads(info, 1, &detail));
    {
        duckdb_v2_opaque opaque = {state, rpubmed_scan_destroy, NULL};
        RCLINV2_CALL(duckdb_v2_table_function_init_global_set_global_state(info, &opaque, &detail));
    }
    owned = true;
cleanup:
    if (!owned && state) rpubmed_scan_destroy(state);
    (void)duckdb_v2_error_info_destroy(&detail);
}

static DUCKDB_V2_ERROR rclinv2_assign_pubmed_row(rclinv2_columns_t *out, idx_t row,
                                                 const rpubmed_row_t *source,
                                                 duckdb_v2_error_info_handle *detail) {
    DUCKDB_V2_ERROR status;
    ((uint64_t *)out->data[0])[row] = source->record_ordinal;
    ((uint64_t *)out->data[1])[row] = source->entity_ordinal;
    ((uint64_t *)out->data[8])[row] = source->citation_ordinal;
    ((bool *)out->data[12])[row] = source->is_deleted;
    status = rclinv2_write_string(out, 2, row, source->pmid, detail);
    if (status != DUCKDB_V2_ERROR_NONE) return status;
    status = rclinv2_write_string(out, 3, row, source->entity_type, detail);
    if (status != DUCKDB_V2_ERROR_NONE) return status;
    status = rclinv2_write_string(out, 4, row, source->section, detail);
    if (status != DUCKDB_V2_ERROR_NONE) return status;
    status = rclinv2_write_string(out, 5, row, source->text_value, detail);
    if (status != DUCKDB_V2_ERROR_NONE) return status;
    status = rclinv2_write_string(out, 6, row, source->identifier_source, detail);
    if (status != DUCKDB_V2_ERROR_NONE) return status;
    status = rclinv2_write_string(out, 7, row, source->identifier, detail);
    if (status != DUCKDB_V2_ERROR_NONE) return status;
    status = rclinv2_write_string(out, 9, row, source->article_title, detail);
    if (status != DUCKDB_V2_ERROR_NONE) return status;
    status = rclinv2_write_string(out, 10, row, source->publication_date, detail);
    if (status != DUCKDB_V2_ERROR_NONE) return status;
    return rclinv2_write_string(out, 11, row, source->source_date, detail);
}

static void rclinv2_pubmed_exec(duckdb_v2_table_function_exec_info_handle info,
                                duckdb_v2_context_handle context,
                                duckdb_v2_error_info_handle *error) {
    duckdb_v2_error_info_handle detail = NULL;
    rpubmed_scan_state_t *state = NULL;
    duckdb_v2_data_chunk_handle chunk = NULL;
    rclinv2_columns_t columns;
    rpubmed_row_t *batch = NULL;
    idx_t count = 0;
    idx_t index;
    (void)context;
    RCLINV2_CALL(duckdb_v2_table_function_exec_get_global_state(info, (void **)&state, &detail));
    RCLINV2_CALL(duckdb_v2_table_function_exec_get_output_chunk(info, &chunk, &detail));
    if (!state) {
        RCLINV2_INPUT_ERROR("rclinvarbitration_pubmed_xml_rows() scan state is missing");
    }
    batch = (rpubmed_row_t *)malloc(sizeof(rpubmed_row_t) * RCLINVAR_CHUNK_SIZE);
    if (!batch) RCLINV2_INPUT_ERROR("out of memory assembling a PubMed batch");
    RCLINV2_CALL(rclinv2_bind_vectors(chunk, RPUBMED_COLUMN_COUNT, &columns, &detail));
    while (count < RCLINVAR_CHUNK_SIZE) {
        int status;
        if (state->row_pos >= state->row_count) {
            status = rpubmed_scan_next(state);
            if (status < 0) {
                RCLINV2_INPUT_ERROR(state->error[0] ? state->error : "PubMed XML scan failed");
            }
            if (status == 0) break;
        }
        batch[count] = state->rows[state->row_pos];
        memset(&state->rows[state->row_pos], 0, sizeof(rpubmed_row_t));
        state->row_pos++;
        count++;
    }
    RCLINV2_CALL(rclinv2_size_columns(&columns, RPUBMED_COLUMN_COUNT, count, &detail));
    RCLINV2_CALL(
        rclinv2_prepare_columns(&columns, RPUBMED_COLUMN_COUNT, rclinv2_pubmed_string_column, &detail));
    for (index = 0; index < count; index++) {
        RCLINV2_CALL(rclinv2_assign_pubmed_row(&columns, index, &batch[index], &detail));
    }
cleanup:
    if (batch) {
        for (index = 0; index < count; index++) rpubmed_row_clear(&batch[index]);
        free(batch);
    }
    (void)duckdb_v2_error_info_destroy(&detail);
}

static bool rclinv2_register_pubmed_xml_rows(duckdb_v2_extension_handle extension,
                                             duckdb_v2_context_handle context,
                                             duckdb_v2_error_info_handle *error) {
    duckdb_v2_error_info_handle detail = NULL;
    duckdb_v2_table_function_handle function = NULL;
    duckdb_v2_function_signature_handle signature = NULL;
    duckdb_v2_logical_type_handle varchar_type = NULL;
    duckdb_v2_str name = rclinv2_view("rclinvarbitration_pubmed_xml_rows");
    bool success = false;
    RCLINV2_CALL(duckdb_v2_table_function_create_with_extension(extension, &function, &detail));
    RCLINV2_CALL(duckdb_v2_table_function_set_name(function, &name, &detail));
    RCLINV2_CALL(duckdb_v2_table_function_get_signature(function, &signature, &detail));
    RCLINV2_CALL(
        duckdb_v2_context_create_type_from_text(context, rclinv2_view("VARCHAR"), &varchar_type, &detail));
    RCLINV2_CALL(duckdb_v2_function_signature_add_parameter(signature, rclinv2_view("path"),
                                                            varchar_type, NULL, &detail));
    RCLINV2_CALL(duckdb_v2_table_function_set_bind_callback(function, rclinv2_pubmed_bind, &detail));
    RCLINV2_CALL(
        duckdb_v2_table_function_set_init_global_callback(function, rclinv2_pubmed_init, &detail));
    RCLINV2_CALL(duckdb_v2_table_function_set_exec_callback(function, rclinv2_pubmed_exec, &detail));
    RCLINV2_CALL(duckdb_v2_table_function_register(function, &detail));
    success = true;
cleanup:
    (void)duckdb_v2_logical_type_destroy(&varchar_type);
    (void)duckdb_v2_table_function_destroy(&function);
    (void)duckdb_v2_error_info_destroy(&detail);
    return success;
}

DUCKDB_EXTENSION_ENTRYPOINT(duckdb_v2_extension_handle extension,
                            duckdb_v2_context_handle context,
                            duckdb_v2_error_info_handle *error) {
    xmlInitParser();
    if (!rclinv2_register_json_field(extension, context, error)) return;
    if (!rclinv2_register_xml_entities(extension, context, error)) return;
    if (!rclinv2_register_pubmed_xml_rows(extension, context, error)) return;
}
