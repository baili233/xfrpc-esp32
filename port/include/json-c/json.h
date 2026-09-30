// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: json-c compatibility API over ESP-IDF's cJSON.
 *
 * Implements the subset xfrpc uses, with json-c ownership semantics:
 *   - json_object_new_*  : caller owns the returned reference
 *   - object_add/array_add: steal the child reference (NULL val deletes key)
 *   - object_get_ex / array_get_idx / get_string : borrowed references,
 *     valid until the owning root is json_object_put()
 *   - json_object_put    : frees the object tree
 *
 * See port/json_compat.c.
 */

#ifndef MINI_JSON_C_JSON_H
#define MINI_JSON_C_JSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct json_object;
/* json-c code often uses the bare typedef (e.g. wire_v2.c) */
typedef struct json_object json_object;

/* flags for json_object_to_json_string_ext */
#define JSON_C_TO_STRING_PLAIN  0
#define JSON_C_TO_STRING_SPACED (1 << 0)
#define JSON_C_TO_STRING_PRETTY (1 << 1)
#define JSON_C_TO_STRING_NOZERO (1 << 2)

typedef int json_bool;

/* ---- creation (owned references) ---- */
struct json_object *json_object_new_object(void);
struct json_object *json_object_new_array(void);
struct json_object *json_object_new_string(const char *s);
struct json_object *json_object_new_int(int i);
struct json_object *json_object_new_int64(int64_t i);
struct json_object *json_object_new_boolean(int b);

/* ---- parse (owned reference) ---- */
struct json_object *json_tokener_parse(const char *str);

/* ---- mutation (steals val; val == NULL deletes the key) ---- */
void json_object_object_add(struct json_object *obj, const char *key,
                            struct json_object *val);
int  json_object_array_add(struct json_object *arr, struct json_object *val);

/* ---- access (borrowed references) ---- */
json_bool json_object_object_get_ex(const struct json_object *obj,
                                    const char *key,
                                    struct json_object **value);
size_t    json_object_array_length(const struct json_object *arr);
struct json_object *json_object_array_get_idx(const struct json_object *arr,
                                              size_t idx);

const char *json_object_get_string(struct json_object *obj);
int         json_object_get_int(struct json_object *obj);
int         json_object_get_boolean(struct json_object *obj);
double      json_object_get_double(struct json_object *obj);

/* ---- serialize (valid until next serialize / object destruction) ---- */
const char *json_object_to_json_string(struct json_object *obj);
const char *json_object_to_json_string_ext(struct json_object *obj, int flags);

/* ---- release owned reference ---- */
int json_object_put(struct json_object *obj);

#ifdef __cplusplus
}
#endif

#endif /* MINI_JSON_C_JSON_H */
