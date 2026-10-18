
#define RCLINVAR_CHUNK_SIZE 1024U
#define RCLINVAR_ERROR_SIZE 512U

typedef struct rclinvar_field {
    char *name;
    char *value;
} rclinvar_field_t;

typedef struct rclinvar_entity {
    uint64_t record_ordinal;
    uint64_t entity_ordinal;
    char *vcv_accession;
    char *rcv_entity_id;
    char *scv_entity_id;
    char *entity_type;
    char *entity_id;
    char *parent_type;
    char *parent_id;
    char *fields_json;
} rclinvar_entity_t;

typedef struct rclinvar_stack_entry {
    char *name;
    char *semantic_type;
    char *semantic_id;
    char *parent_type;
    char *parent_id;
    char *type_attribute;
    char *id_attribute;
    char *contributes_attribute;
    char *text;
    size_t text_length;
    size_t text_capacity;
    rclinvar_field_t *fields;
    size_t field_count;
    size_t field_capacity;
} rclinvar_stack_entry_t;

typedef struct rclinvar_bind_state {
    char *path;
} rclinvar_bind_state_t;

typedef struct rclinvar_scan_state {
    xmlTextReaderPtr reader;
    rclinvar_entity_t *entities;
    size_t entity_count;
    size_t entity_capacity;
    size_t entity_pos;
    rclinvar_stack_entry_t *stack;
    size_t stack_count;
    size_t stack_capacity;
    uint64_t record_ordinal;
    uint64_t entity_ordinal;
    uint64_t assertion_ordinal;
    uint64_t allele_ordinal;
    uint64_t gene_ordinal;
    uint64_t location_ordinal;
    uint64_t condition_ordinal;
    uint64_t observation_ordinal;
    uint64_t citation_ordinal;
    uint64_t citation_identifier_ordinal;
    uint64_t xref_ordinal;
    uint64_t attribute_ordinal;
    uint64_t name_ordinal;
    uint64_t text_ordinal;
    char *vcv_accession;
    int active_record;
    int finished;
    char error[RCLINVAR_ERROR_SIZE];
} rclinvar_scan_state_t;

static int rclinvar_is_gzip_path(const char *path) {
    size_t length;
    if (!path) return 0;
    length = strlen(path);
    return length >= 3U && path[length - 3U] == '.' &&
           tolower((unsigned char)path[length - 2U]) == 'g' &&
           tolower((unsigned char)path[length - 1U]) == 'z';
}

static int rclinvar_gzip_read(void *context, char *buffer, int length) {
    int read_count;
    if (!context || !buffer || length < 0) return -1;
    read_count = gzread((gzFile)context, buffer, (unsigned int)length);
    return read_count < 0 ? -1 : read_count;
}

static int rclinvar_gzip_close(void *context) {
    if (!context) return 0;
    return gzclose((gzFile)context) == Z_OK ? 0 : -1;
}

static char *rclinvar_strdup(const char *text) {
    size_t n;
    char *out;
    if (!text) return NULL;
    n = strlen(text);
    out = (char *)malloc(n + 1U);
    if (!out) return NULL;
    memcpy(out, text, n + 1U);
    return out;
}

static char *rclinvar_attribute(xmlTextReaderPtr reader, const char *name) {
    xmlChar *value = xmlTextReaderGetAttribute(reader, BAD_CAST name);
    char *out;
    if (!value) return NULL;
    out = rclinvar_strdup((const char *)value);
    xmlFree(value);
    return out;
}

static char *rclinvar_snake(const char *name) {
    size_t i;
    size_t n;
    size_t out_n = 0;
    char *out;
    if (!name) return NULL;
    n = strlen(name);
    out = (char *)malloc(n * 2U + 1U);
    if (!out) return NULL;
    for (i = 0; i < n; i++) {
        unsigned char current = (unsigned char)name[i];
        unsigned char previous = i ? (unsigned char)name[i - 1U] : 0;
        unsigned char next = i + 1U < n ? (unsigned char)name[i + 1U] : 0;
        if (current == '-' || current == ' ' || current == ':') {
            if (out_n && out[out_n - 1U] != '_') out[out_n++] = '_';
        } else if (isupper(current)) {
            if (out_n && out[out_n - 1U] != '_' &&
                (islower(previous) || isdigit(previous) || (isupper(previous) && islower(next)))) {
                out[out_n++] = '_';
            }
            out[out_n++] = (char)tolower(current);
        } else {
            out[out_n++] = (char)tolower(current);
        }
    }
    while (out_n && out[out_n - 1U] == '_') out_n--;
    out[out_n] = '\0';
    return out;
}

static char *rclinvar_scoped_id(const char *parent_id, const char *kind, uint64_t ordinal) {
    int n;
    char *out;
    if (!parent_id || !kind) return NULL;
    n = snprintf(NULL, 0, "%s#%s/%llu", parent_id, kind, (unsigned long long)ordinal);
    if (n < 0) return NULL;
    out = (char *)malloc((size_t)n + 1U);
    if (!out) return NULL;
    snprintf(out, (size_t)n + 1U, "%s#%s/%llu", parent_id, kind, (unsigned long long)ordinal);
    return out;
}

static void rclinvar_set_error(rclinvar_scan_state_t *state, const char *message) {
    if (!state || state->error[0]) return;
    snprintf(state->error, sizeof(state->error), "%s", message ? message : "ClinVar XML parser failed");
}

static int rclinvar_is_whitespace(const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;
    if (!text) return 1;
    while (*cursor) {
        if (!isspace(*cursor)) return 0;
        cursor++;
    }
    return 1;
}

static void rclinvar_field_clear(rclinvar_field_t *field) {
    if (!field) return;
    free(field->name);
    free(field->value);
    memset(field, 0, sizeof(*field));
}

static int rclinvar_entry_set_field(rclinvar_scan_state_t *state, rclinvar_stack_entry_t *entry,
                                    const char *name, const char *value) {
    size_t i;
    size_t capacity;
    rclinvar_field_t *fields;
    if (!entry || !name || !value) return 0;
    for (i = 0; i < entry->field_count; i++) {
        if (strcmp(entry->fields[i].name, name) == 0) return 1;
    }
    if (entry->field_count == entry->field_capacity) {
        capacity = entry->field_capacity ? entry->field_capacity * 2U : 8U;
        fields = (rclinvar_field_t *)realloc(entry->fields, capacity * sizeof(*fields));
        if (!fields) {
            rclinvar_set_error(state, "out of memory allocating ClinVar entity fields");
            return 0;
        }
        memset(fields + entry->field_capacity, 0,
               (capacity - entry->field_capacity) * sizeof(*fields));
        entry->fields = fields;
        entry->field_capacity = capacity;
    }
    entry->fields[entry->field_count].name = rclinvar_strdup(name);
    entry->fields[entry->field_count].value = rclinvar_strdup(value);
    if (!entry->fields[entry->field_count].name || !entry->fields[entry->field_count].value) {
        rclinvar_field_clear(&entry->fields[entry->field_count]);
        rclinvar_set_error(state, "out of memory copying ClinVar entity field");
        return 0;
    }
    entry->field_count++;
    return 1;
}

static size_t rclinvar_json_escaped_size(const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    size_t n = 0;
    while (*p) {
        if (*p == '"' || *p == '\\' || *p == '\b' || *p == '\f' || *p == '\n' || *p == '\r' || *p == '\t') n += 2U;
        else if (*p < 0x20U) n += 6U;
        else n++;
        p++;
    }
    return n;
}

static char *rclinvar_json_escape_into(char *out, const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    static const char hex[] = "0123456789abcdef";
    while (*p) {
        switch (*p) {
        case '"': *out++ = '\\'; *out++ = '"'; break;
        case '\\': *out++ = '\\'; *out++ = '\\'; break;
        case '\b': *out++ = '\\'; *out++ = 'b'; break;
        case '\f': *out++ = '\\'; *out++ = 'f'; break;
        case '\n': *out++ = '\\'; *out++ = 'n'; break;
        case '\r': *out++ = '\\'; *out++ = 'r'; break;
        case '\t': *out++ = '\\'; *out++ = 't'; break;
        default:
            if (*p < 0x20U) {
                *out++ = '\\'; *out++ = 'u'; *out++ = '0'; *out++ = '0';
                *out++ = hex[*p >> 4U]; *out++ = hex[*p & 0x0fU];
            } else {
                *out++ = (char)*p;
            }
        }
        p++;
    }
    return out;
}

static char *rclinvar_fields_json(const rclinvar_stack_entry_t *entry) {
    size_t i;
    size_t size = 3U;
    char *json;
    char *out;
    for (i = 0; i < entry->field_count; i++) {
        size += rclinvar_json_escaped_size(entry->fields[i].name);
        size += rclinvar_json_escaped_size(entry->fields[i].value);
        size += 6U;
    }
    json = (char *)malloc(size);
    if (!json) return NULL;
    out = json;
    *out++ = '{';
    for (i = 0; i < entry->field_count; i++) {
        if (i) *out++ = ',';
        *out++ = '"';
        out = rclinvar_json_escape_into(out, entry->fields[i].name);
        *out++ = '"'; *out++ = ':'; *out++ = '"';
        out = rclinvar_json_escape_into(out, entry->fields[i].value);
        *out++ = '"';
    }
    *out++ = '}';
    *out = '\0';
    return json;
}

static int rclinvar_json_hex_digit(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

/* The parser owns fields_json and always writes a compact object of string
 * values. This deliberately small decoder is not a general JSON API: it
 * extracts one ASCII field name and decodes the string escapes emitted by
 * rclinvar_json_escape_into(). Keeping it here avoids requiring DuckDB's
 * downloadable JSON extension, which is unavailable in browser/webR runtimes.
 */
static char *rclinvar_json_field_value(const char *json, size_t json_length,
                                       const char *key, size_t key_length,
                                       size_t *value_length) {
    size_t position = 0;
    if (!json || !key || !value_length || json_length < 2U || json[position++] != '{') return NULL;
    while (position < json_length && json[position] != '}') {
        size_t field_start;
        size_t field_end;
        size_t encoded_value_start;
        if (json[position] != '"') return NULL;
        field_start = ++position;
        while (position < json_length) {
            if (json[position] == '\\') {
                position += 2U;
            } else if (position < json_length && json[position] == '"') {
                break;
            } else {
                position++;
            }
        }
        if (position >= json_length) return NULL;
        field_end = position++;
        if (position >= json_length || json[position++] != ':') return NULL;
        if (position >= json_length || json[position++] != '"') return NULL;
        encoded_value_start = position;
        if (field_end - field_start == key_length &&
            memcmp(json + field_start, key, key_length) == 0) {
            char *value = (char *)malloc(json_length - encoded_value_start + 1U);
            char *out;
            if (!value) return NULL;
            out = value;
            while (position < json_length) {
                char character = json[position++];
                if (character == '"') {
                    *out = '\0';
                    *value_length = (size_t)(out - value);
                    return value;
                }
                if (character != '\\') {
                    *out++ = character;
                    continue;
                }
                if (position >= json_length) break;
                character = json[position++];
                switch (character) {
                case '"': *out++ = '"'; break;
                case '\\': *out++ = '\\'; break;
                case '/': *out++ = '/'; break;
                case 'b': *out++ = '\b'; break;
                case 'f': *out++ = '\f'; break;
                case 'n': *out++ = '\n'; break;
                case 'r': *out++ = '\r'; break;
                case 't': *out++ = '\t'; break;
                case 'u': {
                    int first;
                    int second;
                    int third;
                    int fourth;
                    unsigned int codepoint;
                    if (position + 4U > json_length) {
                        free(value);
                        return NULL;
                    }
                    first = rclinvar_json_hex_digit(json[position]);
                    second = rclinvar_json_hex_digit(json[position + 1U]);
                    third = rclinvar_json_hex_digit(json[position + 2U]);
                    fourth = rclinvar_json_hex_digit(json[position + 3U]);
                    if (first < 0 || second < 0 || third < 0 || fourth < 0) {
                        free(value);
                        return NULL;
                    }
                    position += 4U;
                    codepoint = ((unsigned int)first << 12U) | ((unsigned int)second << 8U) |
                                ((unsigned int)third << 4U) | (unsigned int)fourth;
                    if (codepoint <= 0x7fU) {
                        *out++ = (char)codepoint;
                    } else if (codepoint <= 0x7ffU) {
                        *out++ = (char)(0xc0U | (codepoint >> 6U));
                        *out++ = (char)(0x80U | (codepoint & 0x3fU));
                    } else {
                        *out++ = (char)(0xe0U | (codepoint >> 12U));
                        *out++ = (char)(0x80U | ((codepoint >> 6U) & 0x3fU));
                        *out++ = (char)(0x80U | (codepoint & 0x3fU));
                    }
                    break;
                }
                default:
                    free(value);
                    return NULL;
                }
            }
            free(value);
            return NULL;
        }
        while (position < json_length) {
            if (json[position] == '\\') {
                position += 2U;
            } else if (json[position] == '"') {
                position++;
                break;
            } else {
                position++;
            }
        }
        if (position < json_length && json[position] == ',') position++;
    }
    return NULL;
}

static void rclinvar_entity_clear(rclinvar_entity_t *entity) {
    if (!entity) return;
    free(entity->vcv_accession);
    free(entity->rcv_entity_id);
    free(entity->scv_entity_id);
    free(entity->entity_type);
    free(entity->entity_id);
    free(entity->parent_type);
    free(entity->parent_id);
    free(entity->fields_json);
    memset(entity, 0, sizeof(*entity));
}

static void rclinvar_entities_reset(rclinvar_scan_state_t *state) {
    size_t i;
    if (!state) return;
    for (i = state->entity_pos; i < state->entity_count; i++) rclinvar_entity_clear(&state->entities[i]);
    state->entity_count = 0;
    state->entity_pos = 0;
}

static int rclinvar_entities_reserve(rclinvar_scan_state_t *state, size_t required) {
    size_t capacity;
    rclinvar_entity_t *entities;
    if (required <= state->entity_capacity) return 1;
    capacity = state->entity_capacity ? state->entity_capacity : 16U;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) {
            rclinvar_set_error(state, "ClinVar entity queue is too large");
            return 0;
        }
        capacity *= 2U;
    }
    entities = (rclinvar_entity_t *)realloc(state->entities, capacity * sizeof(*entities));
    if (!entities) {
        rclinvar_set_error(state, "out of memory allocating ClinVar entity queue");
        return 0;
    }
    memset(entities + state->entity_capacity, 0,
           (capacity - state->entity_capacity) * sizeof(*entities));
    state->entities = entities;
    state->entity_capacity = capacity;
    return 1;
}

static rclinvar_stack_entry_t *rclinvar_nearest_semantic(rclinvar_scan_state_t *state) {
    size_t i = state ? state->stack_count : 0;
    while (i) {
        rclinvar_stack_entry_t *entry = &state->stack[--i];
        if (entry->semantic_type && entry->semantic_id) return entry;
    }
    return NULL;
}

static rclinvar_stack_entry_t *rclinvar_nearest_type(rclinvar_scan_state_t *state,
                                                      const char *semantic_type) {
    size_t i = state ? state->stack_count : 0;
    while (i) {
        rclinvar_stack_entry_t *entry = &state->stack[--i];
        if (entry->semantic_type && strcmp(entry->semantic_type, semantic_type) == 0) return entry;
    }
    return NULL;
}

static rclinvar_stack_entry_t *rclinvar_nearest_named(rclinvar_scan_state_t *state, const char *name) {
    size_t i = state ? state->stack_count : 0;
    while (i) {
        rclinvar_stack_entry_t *entry = &state->stack[--i];
        if (entry->name && strcmp(entry->name, name) == 0) return entry;
    }
    return NULL;
}

static int rclinvar_stack_contains(rclinvar_scan_state_t *state, const char *name) {
    return rclinvar_nearest_named(state, name) != NULL;
}

static int rclinvar_parent_is(rclinvar_scan_state_t *state, const char *name) {
    if (!state || state->stack_count < 2U) return 0;
    return state->stack[state->stack_count - 2U].name &&
           strcmp(state->stack[state->stack_count - 2U].name, name) == 0;
}

static int rclinvar_queue_entry(rclinvar_scan_state_t *state, const rclinvar_stack_entry_t *entry) {
    rclinvar_entity_t *entity;
    rclinvar_stack_entry_t *rcv;
    rclinvar_stack_entry_t *scv;
    if (!entry || !entry->semantic_type || !entry->semantic_id) return 1;
    if (!rclinvar_entities_reserve(state, state->entity_count + 1U)) return 0;
    entity = &state->entities[state->entity_count];
    entity->record_ordinal = state->record_ordinal;
    entity->entity_ordinal = ++state->entity_ordinal;
    rcv = rclinvar_nearest_type(state, "rcv_assertion");
    scv = rclinvar_nearest_type(state, "scv_assertion");
    entity->vcv_accession = rclinvar_strdup(state->vcv_accession ? state->vcv_accession : "");
    entity->rcv_entity_id = strcmp(entry->semantic_type, "rcv_assertion") == 0 ?
                            rclinvar_strdup(entry->semantic_id) :
                            (rcv ? rclinvar_strdup(rcv->semantic_id) : NULL);
    entity->scv_entity_id = strcmp(entry->semantic_type, "scv_assertion") == 0 ?
                            rclinvar_strdup(entry->semantic_id) :
                            (scv ? rclinvar_strdup(scv->semantic_id) : NULL);
    entity->entity_type = rclinvar_strdup(entry->semantic_type);
    entity->entity_id = rclinvar_strdup(entry->semantic_id);
    entity->parent_type = entry->parent_type ? rclinvar_strdup(entry->parent_type) : NULL;
    entity->parent_id = entry->parent_id ? rclinvar_strdup(entry->parent_id) : NULL;
    entity->fields_json = rclinvar_fields_json(entry);
    if (!entity->vcv_accession || !entity->entity_type || !entity->entity_id || !entity->fields_json ||
        (entry->parent_type && !entity->parent_type) || (entry->parent_id && !entity->parent_id) ||
        ((rcv || strcmp(entry->semantic_type, "rcv_assertion") == 0) && !entity->rcv_entity_id) ||
        ((scv || strcmp(entry->semantic_type, "scv_assertion") == 0) && !entity->scv_entity_id)) {
        rclinvar_entity_clear(entity);
        rclinvar_set_error(state, "out of memory queuing ClinVar entity");
        return 0;
    }
    state->entity_count++;
    return 1;
}

static void rclinvar_stack_entry_clear(rclinvar_stack_entry_t *entry) {
    size_t i;
    if (!entry) return;
    free(entry->name);
    free(entry->semantic_type);
    free(entry->semantic_id);
    free(entry->parent_type);
    free(entry->parent_id);
    free(entry->type_attribute);
    free(entry->id_attribute);
    free(entry->contributes_attribute);
    free(entry->text);
    for (i = 0; i < entry->field_count; i++) rclinvar_field_clear(&entry->fields[i]);
    free(entry->fields);
    memset(entry, 0, sizeof(*entry));
}

static int rclinvar_stack_reserve(rclinvar_scan_state_t *state, size_t required) {
    size_t capacity;
    rclinvar_stack_entry_t *stack;
    if (required <= state->stack_capacity) return 1;
    capacity = state->stack_capacity ? state->stack_capacity : 32U;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) {
            rclinvar_set_error(state, "ClinVar XML nesting is too deep");
            return 0;
        }
        capacity *= 2U;
    }
    stack = (rclinvar_stack_entry_t *)realloc(state->stack, capacity * sizeof(*stack));
    if (!stack) {
        rclinvar_set_error(state, "out of memory allocating ClinVar XML stack");
        return 0;
    }
    memset(stack + state->stack_capacity, 0,
           (capacity - state->stack_capacity) * sizeof(*stack));
    state->stack = stack;
    state->stack_capacity = capacity;
    return 1;
}

static int rclinvar_append_text(rclinvar_scan_state_t *state, const char *value) {
    rclinvar_stack_entry_t *entry;
    size_t n;
    size_t required;
    size_t capacity;
    char *text;
    if (!state || !state->stack_count || !value) return 1;
    entry = &state->stack[state->stack_count - 1U];
    n = strlen(value);
    if (!n) return 1;
    required = entry->text_length + n + 1U;
    if (required > entry->text_capacity) {
        capacity = entry->text_capacity ? entry->text_capacity : 64U;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2U) {
                rclinvar_set_error(state, "ClinVar XML text is too large");
                return 0;
            }
            capacity *= 2U;
        }
        text = (char *)realloc(entry->text, capacity);
        if (!text) {
            rclinvar_set_error(state, "out of memory buffering ClinVar XML text");
            return 0;
        }
        entry->text = text;
        entry->text_capacity = capacity;
    }
    memcpy(entry->text + entry->text_length, value, n);
    entry->text_length += n;
    entry->text[entry->text_length] = '\0';
    return 1;
}

static int rclinvar_emit_entry(rclinvar_scan_state_t *state, rclinvar_stack_entry_t *entry,
                               const char *field, const char *value) {
    return rclinvar_entry_set_field(state, entry, field, value);
}

static int rclinvar_emit_reader_attributes(rclinvar_scan_state_t *state, xmlTextReaderPtr reader,
                                           rclinvar_stack_entry_t *entry) {
    int status = xmlTextReaderMoveToFirstAttribute(reader);
    while (status == 1) {
        const xmlChar *name = xmlTextReaderConstLocalName(reader);
        const xmlChar *value = xmlTextReaderConstValue(reader);
        char *field = rclinvar_snake((const char *)name);
        if (!field || !rclinvar_emit_entry(state, entry, field, value ? (const char *)value : "")) {
            free(field);
            return 0;
        }
        free(field);
        status = xmlTextReaderMoveToNextAttribute(reader);
    }
    xmlTextReaderMoveToElement(reader);
    if (status < 0) {
        rclinvar_set_error(state, "failed reading ClinVar XML attributes");
        return 0;
    }
    return 1;
}

static int rclinvar_emit_attribute_if_present(rclinvar_scan_state_t *state, xmlTextReaderPtr reader,
                                              rclinvar_stack_entry_t *entry,
                                              const char *xml_name, const char *field) {
    char *value = rclinvar_attribute(reader, xml_name);
    int ok = 1;
    if (value) ok = rclinvar_emit_entry(state, entry, field, value);
    free(value);
    return ok;
}

static int rclinvar_transfer_attributes(rclinvar_scan_state_t *state, xmlTextReaderPtr reader,
                                        const char *local_name, rclinvar_stack_entry_t *context) {
    if (!context) return 1;
    if (strcmp(local_name, "ClinVarAccession") == 0 && strcmp(context->semantic_type, "scv_assertion") == 0) {
        return rclinvar_emit_attribute_if_present(state, reader, context, "Accession", "scv_accession") &&
               rclinvar_emit_attribute_if_present(state, reader, context, "Version", "scv_version") &&
               rclinvar_emit_attribute_if_present(state, reader, context, "DateCreated", "accession_date_created") &&
               rclinvar_emit_attribute_if_present(state, reader, context, "DateUpdated", "accession_date_updated") &&
               rclinvar_emit_attribute_if_present(state, reader, context, "SubmitterName", "submitter_name") &&
               rclinvar_emit_attribute_if_present(state, reader, context, "OrgID", "submitter_id") &&
               rclinvar_emit_attribute_if_present(state, reader, context, "OrganizationCategory", "organization_category") &&
               rclinvar_emit_attribute_if_present(state, reader, context, "OrgAbbreviation", "organization_abbreviation");
    }
    if (strcmp(local_name, "ClinVarSubmissionID") == 0 && strcmp(context->semantic_type, "scv_assertion") == 0) {
        return rclinvar_emit_attribute_if_present(state, reader, context, "localKey", "local_key") &&
               rclinvar_emit_attribute_if_present(state, reader, context, "submittedAssembly", "submitted_assembly") &&
               rclinvar_emit_attribute_if_present(state, reader, context, "title", "submission_title");
    }
    if (strcmp(local_name, "Classification") == 0 && strcmp(context->semantic_type, "scv_assertion") == 0) {
        return rclinvar_emit_attribute_if_present(state, reader, context, "DateLastEvaluated", "date_last_evaluated");
    }
    if (strcmp(local_name, "GermlineClassification") == 0 ||
        strcmp(local_name, "SomaticClinicalImpact") == 0 ||
        strcmp(local_name, "OncogenicityClassification") == 0) {
        return rclinvar_emit_attribute_if_present(state, reader, context, "DateLastEvaluated", "date_last_evaluated") &&
               rclinvar_emit_attribute_if_present(state, reader, context, "NumberOfSubmissions", "number_of_submissions") &&
               rclinvar_emit_attribute_if_present(state, reader, context, "NumberOfSubmitters", "number_of_submitters") &&
               rclinvar_emit_attribute_if_present(state, reader, context, "DateCreated", "classification_date_created") &&
               rclinvar_emit_attribute_if_present(state, reader, context, "MostRecentSubmission", "most_recent_submission");
    }
    if (strcmp(local_name, "Description") == 0 && rclinvar_parent_is(state, "GermlineClassification")) {
        return rclinvar_emit_attribute_if_present(state, reader, context, "DateLastEvaluated", "date_last_evaluated") &&
               rclinvar_emit_attribute_if_present(state, reader, context, "SubmissionCount", "submission_count");
    }
    return 1;
}

static int rclinvar_semantic_start(rclinvar_scan_state_t *state, xmlTextReaderPtr reader,
                                   const char *local_name, rclinvar_stack_entry_t *entry,
                                   rclinvar_stack_entry_t *parent) {
    char *identifier = NULL;
    const char *scope = parent ? parent->semantic_id : state->vcv_accession;
    uint64_t ordinal = 0;
    int within_scv = rclinvar_nearest_type(state, "scv_assertion") != NULL;

    if (strcmp(local_name, "VariationArchive") == 0) {
        entry->semantic_type = rclinvar_strdup("variation");
        entry->semantic_id = rclinvar_strdup(state->vcv_accession);
    } else if ((strcmp(local_name, "SimpleAllele") == 0 || strcmp(local_name, "Haplotype") == 0 ||
                strcmp(local_name, "Genotype") == 0) && parent &&
               (strcmp(parent->semantic_type, "variation") == 0 || strcmp(parent->semantic_type, "allele") == 0) &&
               !rclinvar_stack_contains(state, "ClinicalAssertion")) {
        entry->semantic_type = rclinvar_strdup("allele");
        identifier = rclinvar_attribute(reader, "AlleleID");
        ordinal = ++state->allele_ordinal;
        entry->semantic_id = rclinvar_scoped_id(scope, identifier ? identifier : "allele", ordinal);
    } else if (strcmp(local_name, "Gene") == 0 && parent && strcmp(parent->semantic_type, "allele") == 0) {
        entry->semantic_type = rclinvar_strdup("gene");
        identifier = rclinvar_attribute(reader, "GeneID");
        ordinal = ++state->gene_ordinal;
        entry->semantic_id = rclinvar_scoped_id(scope, identifier ? identifier : "gene", ordinal);
    } else if (strcmp(local_name, "SequenceLocation") == 0 && parent && strcmp(parent->semantic_type, "allele") == 0) {
        entry->semantic_type = rclinvar_strdup("location");
        ordinal = ++state->location_ordinal;
        entry->semantic_id = rclinvar_scoped_id(scope, "location", ordinal);
    } else if (strcmp(local_name, "RCVAccession") == 0) {
        entry->semantic_type = rclinvar_strdup("rcv_assertion");
        entry->semantic_id = rclinvar_attribute(reader, "Accession");
    } else if (strcmp(local_name, "ClinicalAssertion") == 0) {
        entry->semantic_type = rclinvar_strdup("scv_assertion");
        identifier = rclinvar_attribute(reader, "ID");
        ordinal = identifier ? strtoull(identifier, NULL, 10) : ++state->assertion_ordinal;
        entry->semantic_id = rclinvar_scoped_id(state->vcv_accession, "assertion", ordinal);
    } else if ((strcmp(local_name, "ClassifiedCondition") == 0 && parent && strcmp(parent->semantic_type, "rcv_assertion") == 0) ||
               (strcmp(local_name, "Trait") == 0 && parent && strcmp(parent->semantic_type, "scv_assertion") == 0)) {
        entry->semantic_type = rclinvar_strdup("condition");
        ordinal = ++state->condition_ordinal;
        entry->semantic_id = rclinvar_scoped_id(scope, "condition", ordinal);
    } else if (strcmp(local_name, "ObservedIn") == 0 && parent && strcmp(parent->semantic_type, "scv_assertion") == 0) {
        entry->semantic_type = rclinvar_strdup("observation");
        ordinal = ++state->observation_ordinal;
        entry->semantic_id = rclinvar_scoped_id(scope, "observation", ordinal);
    } else if (strcmp(local_name, "Citation") == 0 && parent && within_scv) {
        entry->semantic_type = rclinvar_strdup("citation");
        ordinal = ++state->citation_ordinal;
        entry->semantic_id = rclinvar_scoped_id(scope, "citation", ordinal);
    } else if (strcmp(local_name, "ID") == 0 && parent && strcmp(parent->semantic_type, "citation") == 0) {
        entry->semantic_type = rclinvar_strdup("citation_identifier");
        ordinal = ++state->citation_identifier_ordinal;
        entry->semantic_id = rclinvar_scoped_id(scope, "identifier", ordinal);
    } else if (strcmp(local_name, "XRef") == 0 && parent &&
               (strcmp(parent->semantic_type, "allele") == 0 ||
                (within_scv && strcmp(parent->semantic_type, "condition") == 0))) {
        entry->semantic_type = rclinvar_strdup("xref");
        ordinal = ++state->xref_ordinal;
        entry->semantic_id = rclinvar_scoped_id(scope, "xref", ordinal);
    } else if (strcmp(local_name, "Attribute") == 0 && parent && within_scv) {
        entry->semantic_type = rclinvar_strdup("attribute");
        ordinal = ++state->attribute_ordinal;
        entry->semantic_id = rclinvar_scoped_id(scope, "attribute", ordinal);
    } else if (strcmp(local_name, "ElementValue") == 0 && parent &&
               strcmp(parent->semantic_type, "condition") == 0 && rclinvar_stack_contains(state, "Name")) {
        entry->semantic_type = rclinvar_strdup("condition_name");
        ordinal = ++state->name_ordinal;
        entry->semantic_id = rclinvar_scoped_id(scope, "name", ordinal);
    } else if ((strcmp(local_name, "Comment") == 0 || strcmp(local_name, "CitationText") == 0) && parent && within_scv) {
        entry->semantic_type = rclinvar_strdup("text");
        ordinal = ++state->text_ordinal;
        entry->semantic_id = rclinvar_scoped_id(scope, "text", ordinal);
    }
    free(identifier);

    if ((entry->semantic_type && !entry->semantic_id) || (!entry->semantic_type && entry->semantic_id)) {
        rclinvar_set_error(state, "out of memory naming ClinVar semantic entity");
        return 0;
    }
    if (entry->semantic_type && parent) {
        entry->parent_type = rclinvar_strdup(parent->semantic_type);
        entry->parent_id = rclinvar_strdup(parent->semantic_id);
        if (!entry->parent_type || !entry->parent_id) {
            rclinvar_set_error(state, "out of memory naming ClinVar semantic relation");
            return 0;
        }
    }
    if (entry->semantic_type) {
        if (!rclinvar_emit_reader_attributes(state, reader, entry)) return 0;
        if (strcmp(entry->semantic_type, "text") == 0) {
            const char *section = strcmp(local_name, "Comment") == 0 ? "comment" : "citation_text";
            if (!rclinvar_emit_entry(state, entry, "section", section)) return 0;
        }
        if (strcmp(entry->semantic_type, "condition") == 0) {
            rclinvar_stack_entry_t *trait_set = rclinvar_nearest_named(state, "TraitSet");
            rclinvar_stack_entry_t *condition_list = rclinvar_nearest_named(state, "ClassifiedConditionList");
            rclinvar_stack_entry_t *source = trait_set ? trait_set : condition_list;
            if (source) {
                if (source->id_attribute && !rclinvar_emit_entry(state, entry, "trait_set_id", source->id_attribute)) return 0;
                if (source->type_attribute && !rclinvar_emit_entry(state, entry, "trait_set_type", source->type_attribute)) return 0;
                if (source->contributes_attribute &&
                    !rclinvar_emit_entry(state, entry, "contributes_to_aggregate_classification", source->contributes_attribute)) return 0;
            }
        }
    }
    return 1;
}

static int rclinvar_start_element(rclinvar_scan_state_t *state, xmlTextReaderPtr reader,
                                  const char *local_name) {
    rclinvar_stack_entry_t *entry;
    rclinvar_stack_entry_t *parent;
    int empty;
    if (!rclinvar_stack_reserve(state, state->stack_count + 1U)) return 0;
    parent = rclinvar_nearest_semantic(state);
    entry = &state->stack[state->stack_count];
    memset(entry, 0, sizeof(*entry));
    entry->name = rclinvar_strdup(local_name);
    entry->type_attribute = rclinvar_attribute(reader, "Type");
    entry->id_attribute = rclinvar_attribute(reader, "ID");
    entry->contributes_attribute = rclinvar_attribute(reader, "ContributesToAggregateClassification");
    if (!entry->name) {
        rclinvar_set_error(state, "out of memory copying ClinVar XML element name");
        return 0;
    }
    if (!rclinvar_semantic_start(state, reader, local_name, entry, parent)) {
        rclinvar_stack_entry_clear(entry);
        return 0;
    }
    state->stack_count++;
    if (!entry->semantic_type && !rclinvar_transfer_attributes(state, reader, local_name, parent)) return 0;
    empty = xmlTextReaderIsEmptyElement(reader);
    if (empty) {
        if (entry->semantic_type && !rclinvar_queue_entry(state, entry)) return 0;
        rclinvar_stack_entry_clear(entry);
        state->stack_count--;
    }
    return 1;
}

static int rclinvar_emit_element_text(rclinvar_scan_state_t *state, rclinvar_stack_entry_t *entry) {
    rclinvar_stack_entry_t *context;
    const char *field = NULL;
    if (!entry->text || rclinvar_is_whitespace(entry->text)) return 1;
    if (entry->semantic_type) {
        if (strcmp(entry->semantic_type, "text") == 0 || strcmp(entry->semantic_type, "attribute") == 0 ||
            strcmp(entry->semantic_type, "condition_name") == 0) field = "value";
        else if (strcmp(entry->semantic_type, "citation_identifier") == 0) field = "identifier";
        else if (strcmp(entry->semantic_type, "condition") == 0 && strcmp(entry->name, "ClassifiedCondition") == 0)
            field = "preferred_name";
        if (field) return rclinvar_emit_entry(state, entry, field, entry->text);
    }
    context = rclinvar_nearest_semantic(state);
    if (!context) return 1;
    if (strcmp(entry->name, "RecordStatus") == 0) field = "record_status";
    else if (strcmp(entry->name, "Species") == 0) field = "species";
    else if (strcmp(entry->name, "CanonicalSPDI") == 0) field = "canonical_spdi";
    else if (strcmp(entry->name, "VariantType") == 0) field = "variant_type";
    else if (strcmp(entry->name, "ReviewStatus") == 0) field = "review_status";
    else if (strcmp(entry->name, "Assertion") == 0) field = "assertion_type";
    else if (strcmp(entry->name, "Origin") == 0) field = "origin";
    else if (strcmp(entry->name, "AffectedStatus") == 0) field = "affected_status";
    else if (strcmp(entry->name, "NumberTested") == 0) field = "number_tested";
    else if (strcmp(entry->name, "MethodType") == 0) field = "method_type";
    else if (strcmp(entry->name, "URL") == 0 && strcmp(context->semantic_type, "citation") == 0) field = "url";
    else if (strcmp(entry->name, "GermlineClassification") == 0 ||
             strcmp(entry->name, "SomaticClinicalImpact") == 0 ||
             strcmp(entry->name, "OncogenicityClassification") == 0 ||
             (strcmp(entry->name, "Description") == 0 && rclinvar_parent_is(state, "GermlineClassification")))
        field = "classification";
    else if (strcmp(entry->name, "Name") == 0 && strcmp(context->semantic_type, "allele") == 0)
        field = "name";
    if (!field) return 1;
    return rclinvar_emit_entry(state, context, field, entry->text);
}

static int rclinvar_end_element(rclinvar_scan_state_t *state, const char *local_name) {
    rclinvar_stack_entry_t *entry;
    int ending_record;
    if (!state->stack_count) return 1;
    entry = &state->stack[state->stack_count - 1U];
    ending_record = strcmp(local_name, "VariationArchive") == 0;
    if (!rclinvar_emit_element_text(state, entry)) return 0;
    if (entry->semantic_type && !rclinvar_queue_entry(state, entry)) return 0;
    rclinvar_stack_entry_clear(entry);
    state->stack_count--;
    if (ending_record) {
        state->active_record = 0;
        free(state->vcv_accession);
        state->vcv_accession = NULL;
    }
    return 1;
}

static int rclinvar_start_record(rclinvar_scan_state_t *state, xmlTextReaderPtr reader) {
    state->record_ordinal++;
    state->entity_ordinal = 0;
    state->assertion_ordinal = 0;
    state->allele_ordinal = 0;
    state->gene_ordinal = 0;
    state->location_ordinal = 0;
    state->condition_ordinal = 0;
    state->observation_ordinal = 0;
    state->citation_ordinal = 0;
    state->citation_identifier_ordinal = 0;
    state->xref_ordinal = 0;
    state->attribute_ordinal = 0;
    state->name_ordinal = 0;
    state->text_ordinal = 0;
    free(state->vcv_accession);
    state->vcv_accession = rclinvar_attribute(reader, "Accession");
    if (!state->vcv_accession || !state->vcv_accession[0]) {
        rclinvar_set_error(state, "VariationArchive is missing its ClinVar VCV Accession");
        return 0;
    }
    state->active_record = 1;
    return rclinvar_start_element(state, reader, "VariationArchive");
}

/* Reads until at least one entity is queued, EOF, or an error. */
static int rclinvar_scan_next(rclinvar_scan_state_t *state) {
    int rc;
    while (!state->finished && state->entity_pos == state->entity_count) {
        int node_type;
        const xmlChar *local;
        const xmlChar *value;
        rclinvar_entities_reset(state);
        rc = xmlTextReaderRead(state->reader);
        if (rc == 0) {
            state->finished = 1;
            return 0;
        }
        if (rc < 0) {
            const xmlError *error = xmlGetLastError();
            snprintf(state->error, sizeof(state->error), "ClinVar XML parse error%s%s",
                     error && error->message ? ": " : "", error && error->message ? error->message : "");
            return -1;
        }
        node_type = xmlTextReaderNodeType(state->reader);
        local = xmlTextReaderConstLocalName(state->reader);
        if (node_type == XML_READER_TYPE_ELEMENT) {
            if (!state->active_record) {
                if (local && xmlStrEqual(local, BAD_CAST "VariationArchive")) {
                    if (!rclinvar_start_record(state, state->reader)) return -1;
                }
            } else if (!rclinvar_start_element(state, state->reader, (const char *)local)) {
                return -1;
            }
        } else if (state->active_record && node_type == XML_READER_TYPE_END_ELEMENT) {
            if (!rclinvar_end_element(state, (const char *)local)) return -1;
        } else if (state->active_record &&
                   (node_type == XML_READER_TYPE_TEXT || node_type == XML_READER_TYPE_CDATA)) {
            value = xmlTextReaderConstValue(state->reader);
            if (value && !rclinvar_append_text(state, (const char *)value)) return -1;
        } else if (state->active_record && node_type == XML_READER_TYPE_COMMENT) {
            rclinvar_stack_entry_t *context = rclinvar_nearest_semantic(state);
            value = xmlTextReaderConstValue(state->reader);
            if (context && value && !rclinvar_is_whitespace((const char *)value) &&
                rclinvar_nearest_type(state, "scv_assertion")) {
                rclinvar_stack_entry_t standalone;
                memset(&standalone, 0, sizeof(standalone));
                standalone.semantic_type = rclinvar_strdup("text");
                standalone.semantic_id = rclinvar_scoped_id(context->semantic_id, "text", ++state->text_ordinal);
                standalone.parent_type = rclinvar_strdup(context->semantic_type);
                standalone.parent_id = rclinvar_strdup(context->semantic_id);
                if (!standalone.semantic_type || !standalone.semantic_id || !standalone.parent_type || !standalone.parent_id ||
                    !rclinvar_emit_entry(state, &standalone, "section", "xml_comment") ||
                    !rclinvar_emit_entry(state, &standalone, "value", (const char *)value) ||
                    !rclinvar_queue_entry(state, &standalone)) {
                    rclinvar_stack_entry_clear(&standalone);
                    return -1;
                }
                rclinvar_stack_entry_clear(&standalone);
            }
        }
    }
    return state->entity_pos < state->entity_count ? 1 : 0;
}

static void rclinvar_bind_destroy(void *pointer) {
    rclinvar_bind_state_t *state = (rclinvar_bind_state_t *)pointer;
    if (!state) return;
    free(state->path);
    free(state);
}

static void rclinvar_scan_destroy(void *pointer) {
    size_t i;
    rclinvar_scan_state_t *state = (rclinvar_scan_state_t *)pointer;
    if (!state) return;
    if (state->reader) xmlFreeTextReader(state->reader);
    for (i = 0; i < state->entity_count; i++) rclinvar_entity_clear(&state->entities[i]);
    free(state->entities);
    for (i = 0; i < state->stack_count; i++) rclinvar_stack_entry_clear(&state->stack[i]);
    free(state->stack);
    free(state->vcv_accession);
    free(state);
}
