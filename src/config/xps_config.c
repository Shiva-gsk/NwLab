#include "../xps.h"
#include "../lib/parson/parson.h"

/* Forward declarations of helper functions */
static xps_config_listener_t *parse_listener(JSON_Object *listener_obj);
static xps_config_route_t    *parse_route(JSON_Object *route_obj);
static xps_config_server_t   *parse_server(JSON_Object *server_obj);
static void                   parse_all_listeners(xps_config_t *config);


/* ─── Parse a single listener object from JSON ─────────────────────────── */

static xps_config_listener_t *parse_listener(JSON_Object *listener_obj) {
    assert(listener_obj != NULL);

    xps_config_listener_t *listener = malloc(sizeof(xps_config_listener_t));
    if (listener == NULL) {
        logger(LOG_ERROR, "parse_listener()", "malloc() failed");
        return NULL;
    }

    listener->host = json_object_get_string(listener_obj, "host");
    listener->port = (u_int)json_object_get_number(listener_obj, "port");

    return listener;
}


/* ─── Parse a single route object from JSON ────────────────────────────── */

static xps_config_route_t *parse_route(JSON_Object *route_obj) {
    assert(route_obj != NULL);

    xps_config_route_t *route = malloc(sizeof(xps_config_route_t));
    if (route == NULL) {
        logger(LOG_ERROR, "parse_route()", "malloc() failed");
        return NULL;
    }

    // Common fields
    route->req_path         = json_object_get_string(route_obj, "req_path");
    route->type             = json_object_get_string(route_obj, "type");
    route->dir_path         = NULL;
    route->redirect_url     = NULL;
    route->http_status_code = 0;
    route->keep_alive       = false;
    vec_init(&route->index);
    vec_init(&route->upstreams);

    if (strcmp(route->type, "file_serve") == 0) {
        route->dir_path = json_object_get_string(route_obj, "dir_path");

        // Parse index file list (e.g. ["index.html"])
        JSON_Array *index_arr = json_object_get_array(route_obj, "index");
        for (size_t i = 0; i < json_array_get_count(index_arr); i++) {
            const char *index_file = json_array_get_string(index_arr, i);
            vec_push(&route->index, (void *)index_file);
        }

    } else if (strcmp(route->type, "reverse_proxy") == 0) {
        // Parse upstreams list (e.g. ["localhost:3000"])
        JSON_Array *upstreams_arr = json_object_get_array(route_obj, "upstreams");
        for (size_t i = 0; i < json_array_get_count(upstreams_arr); i++) {
            const char *upstream = json_array_get_string(upstreams_arr, i);
            vec_push(&route->upstreams, (void *)upstream);
        }

    } else if (strcmp(route->type, "redirect") == 0) {
        route->http_status_code = (u_int)json_object_get_number(route_obj, "http_status_code");
        route->redirect_url     = json_object_get_string(route_obj, "redirect_url");
    }

    return route;
}


/* ─── Parse a single server object from JSON ────────────────────────────── */

static xps_config_server_t *parse_server(JSON_Object *server_obj) {
    assert(server_obj != NULL);

    xps_config_server_t *server = malloc(sizeof(xps_config_server_t));
    if (server == NULL) {
        logger(LOG_ERROR, "parse_server()", "malloc() failed");
        return NULL;
    }

    vec_init(&server->listeners);
    vec_init(&server->hostnames);
    vec_init(&server->routes);

    // Parse listeners
    JSON_Array *listeners_arr = json_object_get_array(server_obj, "listeners");
    for (size_t i = 0; i < json_array_get_count(listeners_arr); i++) {
        JSON_Object *listener_obj = json_array_get_object(listeners_arr, i);
        xps_config_listener_t *listener = parse_listener(listener_obj);
        if (listener != NULL)
            vec_push(&server->listeners, listener);
    }

    // Parse hostnames (optional field)
    JSON_Array *hostnames_arr = json_object_get_array(server_obj, "hostnames");
    for (size_t i = 0; i < json_array_get_count(hostnames_arr); i++) {
        const char *hostname = json_array_get_string(hostnames_arr, i);
        vec_push(&server->hostnames, (void *)hostname);
    }

    // Parse routes
    JSON_Array *routes_arr = json_object_get_array(server_obj, "routes");
    for (size_t i = 0; i < json_array_get_count(routes_arr); i++) {
        JSON_Object *route_obj = json_array_get_object(routes_arr, i);
        xps_config_route_t *route = parse_route(route_obj);
        if (route != NULL)
            vec_push(&server->routes, route);
    }

    return server;
}


/* ─── Collect all unique listeners across all servers ───────────────────── */

static void parse_all_listeners(xps_config_t *config) {
    assert(config != NULL);

    for (int i = 0; i < config->servers.length; i++) {
        xps_config_server_t *server = config->servers.data[i];

        for (int j = 0; j < server->listeners.length; j++) {
            xps_config_listener_t *listener = server->listeners.data[j];

            // Check for duplicates before adding
            bool already_exists = false;
            for (int k = 0; k < config->_all_listeners.length; k++) {
                xps_config_listener_t *existing = config->_all_listeners.data[k];
                if (existing->port == listener->port &&
                    strcmp(existing->host, listener->host) == 0) {
                    already_exists = true;
                    break;
                }
            }

            if (!already_exists)
                vec_push(&config->_all_listeners, listener);
        }
    }
}


/* ─── Create config from JSON file ──────────────────────────────────────── */

xps_config_t *xps_config_create(const char *config_path) {
    assert(config_path != NULL);

    xps_config_t *config = malloc(sizeof(xps_config_t));
    if (config == NULL) {
        logger(LOG_ERROR, "xps_config_create()", "malloc() failed");
        return NULL;
    }

    // Parse the JSON file
    JSON_Value *config_json = json_parse_file(config_path);
    if (config_json == NULL) {
        logger(LOG_ERROR, "xps_config_create()", "json_parse_file() failed for path: %s", config_path);
        free(config);
        return NULL;
    }

    JSON_Object *root = json_value_get_object(config_json);

    // Initialize config fields
    config->config_path  = config_path;
    config->server_name  = json_object_get_string(root, "server_name");
    config->_config_json = config_json;
    vec_init(&config->servers);
    vec_init(&config->_all_listeners);

    // Parse each server block
    JSON_Array *servers_arr = json_object_get_array(root, "servers");
    for (size_t i = 0; i < json_array_get_count(servers_arr); i++) {
        JSON_Object *server_obj = json_array_get_object(servers_arr, i);
        xps_config_server_t *server = parse_server(server_obj);
        if (server != NULL)
            vec_push(&config->servers, server);
    }

    // Collect all unique listeners
    parse_all_listeners(config);

    return config;
}


/* ─── Destroy config and free all memory ───────────────────────────────── */

void xps_config_destroy(xps_config_t *config) {
    assert(config != NULL);

    for (int i = 0; i < config->servers.length; i++) {
        xps_config_server_t *server = config->servers.data[i];

        // Free listeners
        for (int j = 0; j < server->listeners.length; j++)
            free(server->listeners.data[j]);
        vec_deinit(&server->listeners);

        // Free routes
        for (int j = 0; j < server->routes.length; j++) {
            xps_config_route_t *route = server->routes.data[j];
            vec_deinit(&route->index);
            vec_deinit(&route->upstreams);
            free(route);
        }
        vec_deinit(&server->routes);
        vec_deinit(&server->hostnames);

        free(server);
    }

    vec_deinit(&config->servers);
    vec_deinit(&config->_all_listeners);  // pointers already freed above, just deinit

    json_value_free(config->_config_json);
    free(config);
}


/* ─── Lookup: find the right server + route for an incoming request ─────── */

xps_config_lookup_t *xps_config_lookup(xps_config_t *config, xps_http_req_t *http_req,
                                        xps_connection_t *client, int *error) {
    assert(config != NULL);
    assert(http_req != NULL);
    assert(client != NULL);
    assert(error != NULL);

    *error = E_FAIL;

    // Extract useful fields from the request
    const char *host        = xps_http_get_header(&http_req->headers, "Host");
    const char *connection  = xps_http_get_header(&http_req->headers, "Connection");
    const char *pathname    = http_req->pathname;
    bool keep_alive         = (connection != NULL && strcasecmp(connection, "keep-alive") == 0);

    // ── Step 1: Find a matching server block ────────────────────────────
    int target_server_index = -1;

    for (int i = 0; i < config->servers.length; i++) {
        xps_config_server_t *server = config->servers.data[i];

        // Check if the client's listener port matches any of this server's listeners
        bool has_matching_listener = false;
        for (int j = 0; j < server->listeners.length; j++) {
            xps_config_listener_t *listener = server->listeners.data[j];
            if (listener->port == client->listener->port) {
                has_matching_listener = true;
                break;
            }
        }

        if (!has_matching_listener)
            continue;

        // Check if the Host header matches any declared hostname.
        // If no hostnames are declared, treat it as a wildcard match.
        bool has_matching_hostname = (server->hostnames.length == 0);
        for (int j = 0; j < server->hostnames.length; j++) {
            const char *hostname = server->hostnames.data[j];
            if (host != NULL && strcasecmp(host, hostname) == 0) {
                has_matching_hostname = true;
                break;
            }
        }

        if (has_matching_hostname) {
            target_server_index = i;
            break;
        }
    }

    if (target_server_index == -1) {
        *error = E_NOTFOUND;
        return NULL;
    }

    xps_config_server_t *server = config->servers.data[target_server_index];

    // ── Step 2: Find the best (longest prefix) matching route ───────────
    xps_config_route_t *route = NULL;
    size_t best_match_len = 0;

    for (int i = 0; i < server->routes.length; i++) {
        xps_config_route_t *current_route = server->routes.data[i];
        size_t route_path_len = strlen(current_route->req_path);

        if (str_starts_with(pathname, current_route->req_path)) {
            if (route_path_len > best_match_len) {
                best_match_len = route_path_len;
                route = current_route;
            }
        }
    }

    if (route == NULL) {
        *error = E_NOTFOUND;
        return NULL;
    }

    // ── Step 3: Build the lookup result ─────────────────────────────────
    xps_config_lookup_t *lookup = malloc(sizeof(xps_config_lookup_t));
    if (lookup == NULL) {
        logger(LOG_ERROR, "xps_config_lookup()", "malloc() failed");
        return NULL;
    }

    // Initialize all fields to safe defaults
    lookup->file_path        = NULL;
    lookup->dir_path         = NULL;
    lookup->file_start       = -1;
    lookup->file_end         = -1;
    lookup->upstream         = NULL;
    lookup->http_status_code = 0;
    lookup->redirect_url     = NULL;
    lookup->keep_alive       = keep_alive;
    vec_init(&lookup->ip_whitelist);
    vec_init(&lookup->ip_blacklist);

    // Map route type string to enum
    if (strcmp(route->type, "file_serve") == 0)
        lookup->type = REQ_FILE_SERVE;
    else if (strcmp(route->type, "reverse_proxy") == 0)
        lookup->type = REQ_REVERSE_PROXY;
    else if (strcmp(route->type, "redirect") == 0)
        lookup->type = REQ_REDIRECT;
    else if (strcmp(route->type, "metrics") == 0)
        lookup->type = REQ_METRICS;
    else
        lookup->type = REQ_INVALID;

    // ── File serve: resolve the actual file or index path ───────────────
    if (lookup->type == REQ_FILE_SERVE) {
        // Join the route's dir_path with the request pathname
        char *resource_path = path_join(route->dir_path, pathname);

        // If the dir_path was relative, resolve it to absolute
        if (!is_abs_path(resource_path)) {
            char *abs_path = realpath(resource_path, NULL);
            free(resource_path);
            resource_path = abs_path;
        }

        if (resource_path == NULL) {
            *error = E_NOTFOUND;
            free(lookup);
            return NULL;
        }

        if (is_file(resource_path)) {
            // Direct file hit
            lookup->file_path = resource_path;

        } else if (is_dir(resource_path)) {
            // Directory: try each index file in order (e.g. "index.html")
            bool index_found = false;
            for (int i = 0; i < route->index.length; i++) {
                char *index_path = path_join(resource_path, route->index.data[i]);
                if (is_file(index_path)) {
                    lookup->file_path = index_path;
                    index_found = true;
                    break;
                }
                free(index_path);
            }

            free(resource_path);

            if (!index_found) {
                // No index file found in directory → 404
                *error = E_NOTFOUND;
                free(lookup);
                return NULL;
            }

        } else {
            // Path doesn't exist or is not a file/directory
            free(resource_path);
            *error = E_NOTFOUND;
            free(lookup);
            return NULL;
        }

        lookup->dir_path = (char *)route->dir_path;
        *error = OK;
        return lookup;
    }

    // ── Reverse proxy: pick first upstream ──────────────────────────────
    if (lookup->type == REQ_REVERSE_PROXY) {
        if (route->upstreams.length == 0) {
            logger(LOG_ERROR, "xps_config_lookup()", "no upstreams configured");
            free(lookup);
            *error = E_FAIL;
            return NULL;
        }
        lookup->upstream = route->upstreams.data[0];
        *error = OK;
        return lookup;
    }

    // ── Redirect ─────────────────────────────────────────────────────────
    if (lookup->type == REQ_REDIRECT) {
        lookup->http_status_code = route->http_status_code;
        lookup->redirect_url     = route->redirect_url;
        *error = OK;
        return lookup;
    }

    // Unknown type
    *error = E_FAIL;
    free(lookup);
    return NULL;
}


/* ─── Destroy a lookup result ───────────────────────────────────────────── */

void xps_config_lookup_destroy(xps_config_lookup_t *lookup, xps_core_t *core) {
    assert(lookup != NULL);

    // file_path is heap-allocated in the file_serve branch
    if (lookup->file_path != NULL)
        free(lookup->file_path);

    vec_deinit(&lookup->ip_whitelist);
    vec_deinit(&lookup->ip_blacklist);

    free(lookup);
}