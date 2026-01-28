/*
 * rest_api.h
 *
 *  Created on: Dec 15, 2025
 *      Author: Daruin Solano
 */

#ifndef RESTAPI_REST_API_H_
#define RESTAPI_REST_API_H_

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "http_server.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rest_api_s rest_api_t;

typedef int (*rest_handler_fn)(http_srv_t *hs,
                               const http_srv_request_t *req,
                               cJSON *body_in,          /* JSON object or form object */
                               cJSON **json_out,        /* response JSON object */
                               uint32_t *http_status);

typedef bool (*rest_auth_fn)(const http_srv_request_t *req);

typedef struct {
    const char       *method;   /* "GET","POST","PUT","DELETE" */
    const char       *path;     /* exact match */
    rest_handler_fn   fn;
    bool              parse_body; /* parse JSON or form body */
} rest_route_t;

struct rest_api_s {
    const rest_route_t *routes;
    size_t              route_count;
    rest_auth_fn        auth;
    bool                pretty_json;
};

void rest_api_init(rest_api_t *api, const rest_route_t *routes, size_t route_count);
void rest_api_set_auth(rest_api_t *api, rest_auth_fn auth_cb);
void rest_api_set_pretty(rest_api_t *api, bool pretty);

int  rest_api_dispatch(rest_api_t *api, http_srv_t *hs, const http_srv_request_t *req);

int  rest_send_json(http_srv_t *hs, uint32_t status, cJSON *obj, bool pretty);
int  rest_send_error(http_srv_t *hs, uint32_t status, const char *code, const char *message);

const char *rest_query_get(const http_srv_request_t *req, const char *key,
                           char *out, size_t out_len);

#ifdef __cplusplus
}
#endif


#endif /* RESTAPI_REST_API_H_ */
