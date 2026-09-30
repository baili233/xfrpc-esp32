// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: json-c compatibility layer over ESP-IDF's cJSON.
 *
 * Ownership model (matches how msg.c uses json-c):
 *   - json_object_new_* / json_tokener_parse return an OWNED wrapper;
 *     json_object_put() frees the wrapper + cJSON tree.
 *   - object_add / array_add STEAL the val reference: the val wrapper is
 *     freed and its cJSON node re-parented (val == NULL deletes the key).
 *   - object_get_ex / array_get_idx return BORROWED wrappers, tracked on
 *     the owning root and freed with it.
 *   - json_object_to_json_string returns a cached malloc'd string owned by
 *     the wrapper, valid until the next call on the same wrapper / free.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include "json-c/json.h"
#include "json-c/json_tokener.h"

struct json_object {
    cJSON *node;
    int    owned;      /* 1 = json_object_put() frees this wrapper */
    char  *str;        /* cached to_json_string() result */
    char  *conv;       /* cached get_string() conversion for non-strings */

    /* borrowed-wrapper bookkeeping (root wrapper only) */
    struct json_object *borrowed_head;
    /* link fields when this wrapper is borrowed */
    struct json_object *root;
    struct json_object *next_borrowed;
};

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static struct json_object *wrap_new(cJSON *node, int owned)
{
    struct json_object *jo = calloc(1, sizeof(*jo));
    if (!jo) {
        cJSON_Delete(node);
        return NULL;
    }
    jo->node = node;
    jo->owned = owned;
    return jo;
}

static struct json_object *root_of(struct json_object *jo)
{
    return jo->root ? jo->root : jo;
}

/* create (or reuse) a borrowed wrapper for a child node */
static struct json_object *borrow_child(struct json_object *parent, cJSON *node)
{
    if (!node)
        return NULL;
    struct json_object *root = root_of(parent);
    struct json_object *jo = calloc(1, sizeof(*jo));
    if (!jo)
        return NULL;
    jo->node = node;
    jo->owned = 0;
    jo->root = root;
    jo->next_borrowed = root->borrowed_head;
    root->borrowed_head = jo;
    return jo;
}

static void set_cached_str(char **slot, const char *s)
{
    char *dup = strdup(s);
    if (!dup)
        return;
    free(*slot);
    *slot = dup;
}

/* ------------------------------------------------------------------ */
/* creation                                                            */
/* ------------------------------------------------------------------ */

struct json_object *json_object_new_object(void)
{
    return wrap_new(cJSON_CreateObject(), 1);
}

struct json_object *json_object_new_array(void)
{
    return wrap_new(cJSON_CreateArray(), 1);
}

struct json_object *json_object_new_string(const char *s)
{
    if (!s)
        return NULL;
    return wrap_new(cJSON_CreateString(s), 1);
}

struct json_object *json_object_new_int(int i)
{
    return wrap_new(cJSON_CreateNumber((double)i), 1);
}

/* Note: stored as a double (cJSON has no int64 type); values beyond 2^53
 * lose precision — frp's int64 fields (timestamps, ports) are far below. */
struct json_object *json_object_new_int64(int64_t i)
{
    return wrap_new(cJSON_CreateNumber((double)i), 1);
}

struct json_object *json_object_new_boolean(int b)
{
    return wrap_new(cJSON_CreateBool(b ? 1 : 0), 1);
}

struct json_object *json_tokener_parse(const char *str)
{
    if (!str)
        return NULL;
    cJSON *node = cJSON_Parse(str);
    if (!node)
        return NULL;
    return wrap_new(node, 1);
}

/* ------------------------------------------------------------------ */
/* mutation                                                            */
/* ------------------------------------------------------------------ */

void json_object_object_add(struct json_object *obj, const char *key,
                            struct json_object *val)
{
    if (!obj || !key || !cJSON_IsObject(obj->node))
        goto drop_val;
    if (!val) {
        /* NULL val deletes the key (json-c semantics) */
        cJSON_DeleteItemFromObjectCaseSensitive(obj->node, key);
        return;
    }
    /* json-c replaces an existing key */
    cJSON_DeleteItemFromObjectCaseSensitive(obj->node, key);
    if (!cJSON_AddItemToObject(obj->node, key, val->node))
        goto drop_val;
    /* steal the reference: node re-parented, wrapper freed */
    free(val->str);
    free(val->conv);
    free(val);
    return;

drop_val:
    /* adoption failed: consume the reference as best we can */
    if (val) {
        cJSON_Delete(val->node);
        val->node = NULL;
        json_object_put(val);
    }
}

int json_object_array_add(struct json_object *arr, struct json_object *val)
{
    if (!arr || !val || !cJSON_IsArray(arr->node)) {
        if (val)
            json_object_put(val);
        return -1;
    }
    if (!cJSON_AddItemToArray(arr->node, val->node)) {
        json_object_put(val);
        return -1;
    }
    free(val->str);
    free(val->conv);
    free(val);
    return 0;
}

/* ------------------------------------------------------------------ */
/* access                                                              */
/* ------------------------------------------------------------------ */

json_bool json_object_object_get_ex(const struct json_object *obj,
                                    const char *key,
                                    struct json_object **value)
{
    if (value)
        *value = NULL;
    if (!obj || !key || !cJSON_IsObject(obj->node))
        return 0;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj->node, key);
    if (!item)
        return 0;
    if (value)
        *value = borrow_child((struct json_object *)obj, item);
    return 1;
}

size_t json_object_array_length(const struct json_object *arr)
{
    if (!arr || !cJSON_IsArray(arr->node))
        return 0;
    return cJSON_GetArraySize(arr->node);
}

struct json_object *json_object_array_get_idx(const struct json_object *arr,
                                              size_t idx)
{
    if (!arr || !cJSON_IsArray(arr->node))
        return NULL;
    cJSON *item = cJSON_GetArrayItem(arr->node, (int)idx);
    if (!item)
        return NULL;
    return borrow_child((struct json_object *)arr, item);
}

const char *json_object_get_string(struct json_object *obj)
{
    if (!obj)
        return NULL;
    cJSON *node = obj->node;
    if (cJSON_IsString(node))
        return cJSON_GetStringValue(node);
    /* json-c converts other types to their string representation */
    if (cJSON_IsNumber(node) || cJSON_IsBool(node)) {
        char buf[64];
        if (cJSON_IsTrue(node))
            snprintf(buf, sizeof(buf), "true");
        else if (cJSON_IsFalse(node))
            snprintf(buf, sizeof(buf), "false");
        else
            snprintf(buf, sizeof(buf), "%g", node->valuedouble);
        set_cached_str(&obj->conv, buf);
        return obj->conv;
    }
    return NULL;
}

int json_object_get_int(struct json_object *obj)
{
    if (!obj)
        return 0;
    return (int)cJSON_GetNumberValue(obj->node);
}

int json_object_get_boolean(struct json_object *obj)
{
    if (!obj)
        return 0;
    return cJSON_IsTrue(obj->node) ? 1 : 0;
}

double json_object_get_double(struct json_object *obj)
{
    if (!obj)
        return 0.0;
    return cJSON_GetNumberValue(obj->node);
}

/* ------------------------------------------------------------------ */
/* serialize                                                           */
/* ------------------------------------------------------------------ */

const char *json_object_to_json_string_ext(struct json_object *obj, int flags)
{
    (void)flags; /* xfrpc always wants compact output */
    if (!obj)
        return NULL;
    char *s = cJSON_PrintUnformatted(obj->node);
    if (!s)
        return NULL;
    set_cached_str(&obj->str, s);
    cJSON_free(s);
    return obj->str;
}

const char *json_object_to_json_string(struct json_object *obj)
{
    return json_object_to_json_string_ext(obj, JSON_C_TO_STRING_PLAIN);
}

/* ------------------------------------------------------------------ */
/* release                                                             */
/* ------------------------------------------------------------------ */

int json_object_put(struct json_object *obj)
{
    if (!obj)
        return 0;
    if (!obj->owned) {
        /* borrowed reference: nothing to do, freed with the root */
        return 0;
    }
    /* free borrowed wrappers hanging off this root */
    struct json_object *b = obj->borrowed_head;
    while (b) {
        struct json_object *next = b->next_borrowed;
        free(b->str);
        free(b->conv);
        free(b);
        b = next;
    }
    free(obj->str);
    free(obj->conv);
    cJSON_Delete(obj->node);
    free(obj);
    return 1;
}
