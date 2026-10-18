/* RClinVarbitration DuckDB extension
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * libxml2 performs one forward scan of a ClinVar VCV XML/XML.GZ release.
 * The table function emits one compact row per selected ClinVar entity. XML
 * parser coordinates and per-field EAV rows are deliberately not exposed.
 */
#include "duckdb_extension.h"

#include <libxml/xmlreader.h>
#include <zlib.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DUCKDB_EXTENSION_EXTERN

/* Engine-agnostic ClinVar/PubMed XML scanners shared by every DuckDB host. */
#include "rclinvarbitration_clinvar_core.inc"
#include "rclinvarbitration_pubmed_core.inc"

static void rclinvar_add_column(duckdb_bind_info info, const char *name, duckdb_type type_id) {
    duckdb_logical_type type = duckdb_create_logical_type(type_id);
    duckdb_bind_add_result_column(info, name, type);
    duckdb_destroy_logical_type(&type);
}

static void rclinvar_xml_bind(duckdb_bind_info info) {
    duckdb_value value = NULL;
    char *path = NULL;
    rclinvar_bind_state_t *state = NULL;
    if (duckdb_bind_get_parameter_count(info) != 1U) {
        duckdb_bind_set_error(info, "clinvar_xml_entities() requires exactly one XML or XML.GZ path");
        return;
    }
    value = duckdb_bind_get_parameter(info, 0);
    path = value ? duckdb_get_varchar(value) : NULL;
    if (value) duckdb_destroy_value(&value);
    if (!path || !path[0]) {
        if (path) duckdb_free(path);
        duckdb_bind_set_error(info, "clinvar_xml_entities() path must be a non-empty string");
        return;
    }
    state = (rclinvar_bind_state_t *)calloc(1, sizeof(*state));
    if (!state) {
        duckdb_free(path);
        duckdb_bind_set_error(info, "out of memory binding clinvar_xml_entities()");
        return;
    }
    state->path = rclinvar_strdup(path);
    duckdb_free(path);
    if (!state->path) {
        rclinvar_bind_destroy(state);
        duckdb_bind_set_error(info, "out of memory copying ClinVar XML path");
        return;
    }
    rclinvar_add_column(info, "record_ordinal", DUCKDB_TYPE_UBIGINT);
    rclinvar_add_column(info, "entity_ordinal", DUCKDB_TYPE_UBIGINT);
    rclinvar_add_column(info, "vcv_accession", DUCKDB_TYPE_VARCHAR);
    rclinvar_add_column(info, "rcv_entity_id", DUCKDB_TYPE_VARCHAR);
    rclinvar_add_column(info, "scv_entity_id", DUCKDB_TYPE_VARCHAR);
    rclinvar_add_column(info, "entity_type", DUCKDB_TYPE_VARCHAR);
    rclinvar_add_column(info, "entity_id", DUCKDB_TYPE_VARCHAR);
    rclinvar_add_column(info, "parent_type", DUCKDB_TYPE_VARCHAR);
    rclinvar_add_column(info, "parent_id", DUCKDB_TYPE_VARCHAR);
    rclinvar_add_column(info, "fields_json", DUCKDB_TYPE_VARCHAR);
    duckdb_bind_set_bind_data(info, state, rclinvar_bind_destroy);
}

static void rclinvar_xml_init(duckdb_init_info info) {
    const rclinvar_bind_state_t *bind = (const rclinvar_bind_state_t *)duckdb_init_get_bind_data(info);
    rclinvar_scan_state_t *state;
    if (!bind || !bind->path) {
        duckdb_init_set_error(info, "clinvar_xml_entities() bind state is missing");
        return;
    }
    state = (rclinvar_scan_state_t *)calloc(1, sizeof(*state));
    if (!state) {
        duckdb_init_set_error(info, "out of memory initializing ClinVar XML scan");
        return;
    }
    if (rclinvar_is_gzip_path(bind->path)) {
        gzFile input = gzopen(bind->path, "rb");
        if (input) {
            state->reader = xmlReaderForIO(
                rclinvar_gzip_read, rclinvar_gzip_close, input, bind->path, NULL,
                XML_PARSE_NONET | XML_PARSE_COMPACT | XML_PARSE_NOENT
            );
            if (!state->reader) gzclose(input);
        }
    } else {
        state->reader = xmlReaderForFile(bind->path, NULL, XML_PARSE_NONET | XML_PARSE_COMPACT | XML_PARSE_NOENT);
    }
    if (!state->reader) {
        rclinvar_scan_destroy(state);
        duckdb_init_set_error(info, "failed to open or parse ClinVar XML/XML.GZ input");
        return;
    }
    duckdb_init_set_max_threads(info, 1);
    duckdb_init_set_init_data(info, state, rclinvar_scan_destroy);
}

static void rclinvar_set_null(duckdb_vector vector, idx_t row) {
    uint64_t *validity;
    duckdb_vector_ensure_validity_writable(vector);
    validity = duckdb_vector_get_validity(vector);
    duckdb_validity_set_row_invalid(validity, row);
}

static void rclinvar_json_field_function(duckdb_function_info info, duckdb_data_chunk input,
                                          duckdb_vector output) {
    duckdb_vector fields_vector = duckdb_data_chunk_get_vector(input, 0U);
    duckdb_vector keys_vector = duckdb_data_chunk_get_vector(input, 1U);
    duckdb_string_t *fields = (duckdb_string_t *)duckdb_vector_get_data(fields_vector);
    duckdb_string_t *keys = (duckdb_string_t *)duckdb_vector_get_data(keys_vector);
    uint64_t *fields_validity = duckdb_vector_get_validity(fields_vector);
    uint64_t *keys_validity = duckdb_vector_get_validity(keys_vector);
    idx_t row;
    idx_t row_count = duckdb_data_chunk_get_size(input);
    (void)info;
    for (row = 0; row < row_count; row++) {
        char *value;
        size_t value_length = 0;
        const char *json;
        const char *key;
        if ((fields_validity && !duckdb_validity_row_is_valid(fields_validity, row)) ||
            (keys_validity && !duckdb_validity_row_is_valid(keys_validity, row))) {
            rclinvar_set_null(output, row);
            continue;
        }
        json = duckdb_string_t_data(&fields[row]);
        key = duckdb_string_t_data(&keys[row]);
        value = rclinvar_json_field_value(
            json, (size_t)duckdb_string_t_length(fields[row]),
            key, (size_t)duckdb_string_t_length(keys[row]), &value_length
        );
        if (!value) {
            rclinvar_set_null(output, row);
            continue;
        }
        duckdb_vector_assign_string_element_len(output, row, value, (idx_t)value_length);
        free(value);
    }
}

static void rclinvar_assign_entity(duckdb_data_chunk output, idx_t row, rclinvar_entity_t *entity) {
    uint64_t *record = (uint64_t *)duckdb_vector_get_data(duckdb_data_chunk_get_vector(output, 0));
    uint64_t *ordinal = (uint64_t *)duckdb_vector_get_data(duckdb_data_chunk_get_vector(output, 1));
    duckdb_vector vcv = duckdb_data_chunk_get_vector(output, 2);
    duckdb_vector rcv = duckdb_data_chunk_get_vector(output, 3);
    duckdb_vector scv = duckdb_data_chunk_get_vector(output, 4);
    duckdb_vector entity_type = duckdb_data_chunk_get_vector(output, 5);
    duckdb_vector entity_id = duckdb_data_chunk_get_vector(output, 6);
    duckdb_vector parent_type = duckdb_data_chunk_get_vector(output, 7);
    duckdb_vector parent_id = duckdb_data_chunk_get_vector(output, 8);
    duckdb_vector fields_json = duckdb_data_chunk_get_vector(output, 9);
    record[row] = entity->record_ordinal;
    ordinal[row] = entity->entity_ordinal;
    duckdb_vector_assign_string_element(vcv, row, entity->vcv_accession);
    if (entity->rcv_entity_id) duckdb_vector_assign_string_element(rcv, row, entity->rcv_entity_id);
    else rclinvar_set_null(rcv, row);
    if (entity->scv_entity_id) duckdb_vector_assign_string_element(scv, row, entity->scv_entity_id);
    else rclinvar_set_null(scv, row);
    duckdb_vector_assign_string_element(entity_type, row, entity->entity_type);
    duckdb_vector_assign_string_element(entity_id, row, entity->entity_id);
    if (entity->parent_type) duckdb_vector_assign_string_element(parent_type, row, entity->parent_type);
    else rclinvar_set_null(parent_type, row);
    if (entity->parent_id) duckdb_vector_assign_string_element(parent_id, row, entity->parent_id);
    else rclinvar_set_null(parent_id, row);
    duckdb_vector_assign_string_element(fields_json, row, entity->fields_json);
}

static void rclinvar_xml_function(duckdb_function_info info, duckdb_data_chunk output) {
    rclinvar_scan_state_t *state = (rclinvar_scan_state_t *)duckdb_function_get_init_data(info);
    idx_t count = 0;
    if (!state) {
        duckdb_function_set_error(info, "clinvar_xml_entities() scan state is missing");
        duckdb_data_chunk_set_size(output, 0);
        return;
    }
    while (count < RCLINVAR_CHUNK_SIZE) {
        rclinvar_entity_t entity;
        int status;
        if (state->entity_pos >= state->entity_count) {
            status = rclinvar_scan_next(state);
            if (status < 0) {
                duckdb_function_set_error(info, state->error[0] ? state->error : "ClinVar XML scan failed");
                duckdb_data_chunk_set_size(output, 0);
                return;
            }
            if (status == 0) break;
        }
        entity = state->entities[state->entity_pos];
        memset(&state->entities[state->entity_pos], 0, sizeof(entity));
        state->entity_pos++;
        rclinvar_assign_entity(output, count, &entity);
        rclinvar_entity_clear(&entity);
        count++;
    }
    duckdb_data_chunk_set_size(output, count);
}

static bool rclinvar_register_json_field(duckdb_connection connection) {
    duckdb_scalar_function function = duckdb_create_scalar_function();
    duckdb_logical_type varchar_type;
    duckdb_state status;
    if (!function) return false;
    varchar_type = duckdb_create_logical_type(DUCKDB_TYPE_VARCHAR);
    if (!varchar_type) {
        duckdb_destroy_scalar_function(&function);
        return false;
    }
    duckdb_scalar_function_set_name(function, "rclinvar_json_field");
    duckdb_scalar_function_add_parameter(function, varchar_type);
    duckdb_scalar_function_add_parameter(function, varchar_type);
    duckdb_scalar_function_set_return_type(function, varchar_type);
    duckdb_scalar_function_set_function(function, rclinvar_json_field_function);
    status = duckdb_register_scalar_function(connection, function);
    duckdb_destroy_logical_type(&varchar_type);
    duckdb_destroy_scalar_function(&function);
    return status == DuckDBSuccess;
}

static bool rclinvar_register_xml_entities(duckdb_connection connection) {
    duckdb_table_function function = duckdb_create_table_function();
    duckdb_logical_type parameter_type;
    duckdb_state status;
    if (!function) return false;
    parameter_type = duckdb_create_logical_type(DUCKDB_TYPE_VARCHAR);
    if (!parameter_type) {
        duckdb_destroy_table_function(&function);
        return false;
    }
    duckdb_table_function_set_name(function, "clinvar_xml_entities");
    duckdb_table_function_add_parameter(function, parameter_type);
    duckdb_destroy_logical_type(&parameter_type);
    duckdb_table_function_set_bind(function, rclinvar_xml_bind);
    duckdb_table_function_set_init(function, rclinvar_xml_init);
    duckdb_table_function_set_function(function, rclinvar_xml_function);
    status = duckdb_register_table_function(connection, function);
    duckdb_destroy_table_function(&function);
    return status == DuckDBSuccess;
}

static void rpubmed_xml_bind(duckdb_bind_info info) {
    duckdb_value value = NULL;
    char *path = NULL;
    rclinvar_bind_state_t *state = NULL;
    if (duckdb_bind_get_parameter_count(info) != 1U) {
        duckdb_bind_set_error(info, "rclinvarbitration_pubmed_xml_rows() requires exactly one XML or XML.GZ path");
        return;
    }
    value = duckdb_bind_get_parameter(info, 0);
    path = value ? duckdb_get_varchar(value) : NULL;
    if (value) duckdb_destroy_value(&value);
    if (!path || !path[0]) {
        if (path) duckdb_free(path);
        duckdb_bind_set_error(info, "rclinvarbitration_pubmed_xml_rows() path must be a non-empty string");
        return;
    }
    state = (rclinvar_bind_state_t *)calloc(1, sizeof(*state));
    if (!state) {
        duckdb_free(path);
        duckdb_bind_set_error(info, "out of memory binding rclinvarbitration_pubmed_xml_rows()");
        return;
    }
    state->path = rclinvar_strdup(path);
    duckdb_free(path);
    if (!state->path) {
        rclinvar_bind_destroy(state);
        duckdb_bind_set_error(info, "out of memory copying PubMed XML path");
        return;
    }
    rclinvar_add_column(info, "record_ordinal", DUCKDB_TYPE_UBIGINT);
    rclinvar_add_column(info, "entity_ordinal", DUCKDB_TYPE_UBIGINT);
    rclinvar_add_column(info, "pmid", DUCKDB_TYPE_VARCHAR);
    rclinvar_add_column(info, "entity_type", DUCKDB_TYPE_VARCHAR);
    rclinvar_add_column(info, "section", DUCKDB_TYPE_VARCHAR);
    rclinvar_add_column(info, "text_value", DUCKDB_TYPE_VARCHAR);
    rclinvar_add_column(info, "identifier_source", DUCKDB_TYPE_VARCHAR);
    rclinvar_add_column(info, "identifier", DUCKDB_TYPE_VARCHAR);
    rclinvar_add_column(info, "citation_ordinal", DUCKDB_TYPE_UBIGINT);
    rclinvar_add_column(info, "article_title", DUCKDB_TYPE_VARCHAR);
    rclinvar_add_column(info, "publication_date", DUCKDB_TYPE_VARCHAR);
    rclinvar_add_column(info, "source_date", DUCKDB_TYPE_VARCHAR);
    rclinvar_add_column(info, "is_deleted", DUCKDB_TYPE_BOOLEAN);
    duckdb_bind_set_bind_data(info, state, rclinvar_bind_destroy);
}

static void rpubmed_xml_init(duckdb_init_info info) {
    const rclinvar_bind_state_t *bind =
        (const rclinvar_bind_state_t *)duckdb_init_get_bind_data(info);
    rpubmed_scan_state_t *state;
    if (!bind || !bind->path) {
        duckdb_init_set_error(info, "rclinvarbitration_pubmed_xml_rows() bind state is missing");
        return;
    }
    state = (rpubmed_scan_state_t *)calloc(1, sizeof(*state));
    if (!state) {
        duckdb_init_set_error(info, "out of memory initializing PubMed XML scan");
        return;
    }
    if (rclinvar_is_gzip_path(bind->path)) {
        gzFile input = gzopen(bind->path, "rb");
        if (input) {
            state->reader = xmlReaderForIO(
                rclinvar_gzip_read, rclinvar_gzip_close, input, bind->path, NULL,
                XML_PARSE_NONET | XML_PARSE_COMPACT | XML_PARSE_NOENT
            );
            if (!state->reader) gzclose(input);
        }
    } else {
        state->reader = xmlReaderForFile(
            bind->path, NULL, XML_PARSE_NONET | XML_PARSE_COMPACT | XML_PARSE_NOENT
        );
    }
    if (!state->reader) {
        rpubmed_scan_destroy(state);
        duckdb_init_set_error(info, "failed to open or parse PubMed XML/XML.GZ input");
        return;
    }
    duckdb_init_set_max_threads(info, 1);
    duckdb_init_set_init_data(info, state, rpubmed_scan_destroy);
}

static void rpubmed_assign_string(duckdb_vector vector, idx_t row, const char *value) {
    if (value) duckdb_vector_assign_string_element(vector, row, value);
    else rclinvar_set_null(vector, row);
}

static void rpubmed_assign_row(duckdb_data_chunk output, idx_t row,
                               const rpubmed_row_t *source) {
    uint64_t *record = (uint64_t *)duckdb_vector_get_data(duckdb_data_chunk_get_vector(output, 0));
    uint64_t *ordinal = (uint64_t *)duckdb_vector_get_data(duckdb_data_chunk_get_vector(output, 1));
    uint64_t *citation = (uint64_t *)duckdb_vector_get_data(duckdb_data_chunk_get_vector(output, 8));
    bool *deleted = (bool *)duckdb_vector_get_data(duckdb_data_chunk_get_vector(output, 12));
    record[row] = source->record_ordinal;
    ordinal[row] = source->entity_ordinal;
    citation[row] = source->citation_ordinal;
    rpubmed_assign_string(duckdb_data_chunk_get_vector(output, 2), row, source->pmid);
    rpubmed_assign_string(duckdb_data_chunk_get_vector(output, 3), row, source->entity_type);
    rpubmed_assign_string(duckdb_data_chunk_get_vector(output, 4), row, source->section);
    rpubmed_assign_string(duckdb_data_chunk_get_vector(output, 5), row, source->text_value);
    rpubmed_assign_string(duckdb_data_chunk_get_vector(output, 6), row, source->identifier_source);
    rpubmed_assign_string(duckdb_data_chunk_get_vector(output, 7), row, source->identifier);
    rpubmed_assign_string(duckdb_data_chunk_get_vector(output, 9), row, source->article_title);
    rpubmed_assign_string(duckdb_data_chunk_get_vector(output, 10), row, source->publication_date);
    rpubmed_assign_string(duckdb_data_chunk_get_vector(output, 11), row, source->source_date);
    deleted[row] = source->is_deleted;
}

static void rpubmed_xml_function(duckdb_function_info info, duckdb_data_chunk output) {
    rpubmed_scan_state_t *state =
        (rpubmed_scan_state_t *)duckdb_function_get_init_data(info);
    idx_t count = 0;
    if (!state) {
        duckdb_function_set_error(info, "rclinvarbitration_pubmed_xml_rows() scan state is missing");
        duckdb_data_chunk_set_size(output, 0);
        return;
    }
    while (count < RCLINVAR_CHUNK_SIZE) {
        rpubmed_row_t row;
        int status;
        if (state->row_pos >= state->row_count) {
            status = rpubmed_scan_next(state);
            if (status < 0) {
                duckdb_function_set_error(info, state->error[0] ? state->error : "PubMed XML scan failed");
                duckdb_data_chunk_set_size(output, 0);
                return;
            }
            if (status == 0) break;
        }
        row = state->rows[state->row_pos];
        memset(&state->rows[state->row_pos], 0, sizeof(row));
        state->row_pos++;
        rpubmed_assign_row(output, count, &row);
        rpubmed_row_clear(&row);
        count++;
    }
    duckdb_data_chunk_set_size(output, count);
}

static bool rpubmed_register_xml_rows(duckdb_connection connection) {
    duckdb_table_function function = duckdb_create_table_function();
    duckdb_logical_type parameter_type;
    duckdb_state status;
    if (!function) return false;
    parameter_type = duckdb_create_logical_type(DUCKDB_TYPE_VARCHAR);
    if (!parameter_type) {
        duckdb_destroy_table_function(&function);
        return false;
    }
    duckdb_table_function_set_name(function, "rclinvarbitration_pubmed_xml_rows");
    duckdb_table_function_add_parameter(function, parameter_type);
    duckdb_destroy_logical_type(&parameter_type);
    duckdb_table_function_set_bind(function, rpubmed_xml_bind);
    duckdb_table_function_set_init(function, rpubmed_xml_init);
    duckdb_table_function_set_function(function, rpubmed_xml_function);
    status = duckdb_register_table_function(connection, function);
    duckdb_destroy_table_function(&function);
    return status == DuckDBSuccess;
}

DUCKDB_EXTENSION_ENTRYPOINT(duckdb_connection connection,
                            duckdb_extension_info info,
                            struct duckdb_extension_access *access) {
    xmlInitParser();
    if (!rclinvar_register_json_field(connection)) {
        access->set_error(info, "failed to register rclinvar_json_field()");
        return false;
    }
    if (!rclinvar_register_xml_entities(connection)) {
        access->set_error(info, "failed to register clinvar_xml_entities()");
        return false;
    }
    if (!rpubmed_register_xml_rows(connection)) {
        access->set_error(info, "failed to register rclinvarbitration_pubmed_xml_rows()");
        return false;
    }
    return true;
}
