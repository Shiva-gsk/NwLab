#include "../xps.h"
static void parse_server(JSON_Object *server_object, xps_config_server_t *server);
static void parse_listener(JSON_Object *listener_object, xps_config_listener_t *listener);
static void parse_all_listener(xps_config_t *config);
static void parse_route(JSON_Object *route_object, xps_config_route_t *route);

xps_config_t *xps_config_create(const char *config_path)
{
    /*assert*/
    assert(config_path != NULL);
    /*allocate mem for config*/
    xps_config_t *config = malloc(sizeof(xps_config_t));
    config->config_path = malloc(strlen(config_path) + 1);
    strcpy((char *)config->config_path, config_path);
    /*get config_json using json_parse_file*/
    config->_config_json = json_parse_file(config_path);
    if (config->_config_json == NULL)
    {
        logger(LOG_ERROR, "xps_config_create()", "Failed to parse JSON file");
        free((void *)config->config_path);
        free(config);
        return NULL;
    }
    /*initialize fields of config object*/
    JSON_Object *root_object = json_value_get_object(config->_config_json);
    /*initialize server_name,servers fields - hint: use json_object_get_string
    ,json_object_get_number,json_object_get_array*/
    config->server_name = json_object_get_string(root_object, "server_name");
    JSON_Array *servers = json_object_get_array(root_object, "servers");

    vec_init(&(config->servers));
    vec_init(&(config->_all_listeners));

    for (size_t i = 0; i < json_array_get_count(servers); i++)
    {
        JSON_Object *server_object = json_array_get_object(servers, i);
        xps_config_server_t *server = malloc(sizeof(xps_config_server_t));

        // 1. Actually parse the server object
        parse_server(server_object, server);

        // 2. Push it to the config's servers list
        vec_push(&config->servers, server);
    }

    // 3. Populate the master listeners list so main.c can read it
    parse_all_listener(config);

    return config;
}

void xps_config_destroy(xps_config_t *config)
{
    assert(config != NULL);
    if (config->config_path)
        free((void *)config->config_path);
    if (config->_config_json)
        json_value_free(config->_config_json);
    for (int i = 0; i < config->servers.length; i++)
    {
        xps_config_server_t *server = config->servers.data[i];
        if (server)
        {
            for (int j = 0; j < server->hostnames.length; j++)
                free(server->hostnames.data[j]);
            for (int j = 0; j < server->listeners.length; j++)
                free(server->listeners.data[j]);
            for (int j = 0; j < server->routes.length; j++)
                free(server->routes.data[j]);
            vec_deinit(&server->hostnames);
            vec_deinit(&server->listeners);
            vec_deinit(&server->routes);
            free(server);
        }
    }
    vec_deinit(&config->_all_listeners);
    free(config);
}

xps_config_lookup_t *xps_config_lookup(xps_config_t *config, xps_http_req_t *http_req, xps_connection_t *client, int *error)
{
    assert(config != NULL);
    assert(http_req != NULL);
    assert(client != NULL);

    *error = E_FAIL;
    /*get host,keep_alive(connection),accept encoding,pathname from http_req*/
    const char *host = xps_http_get_header(&http_req->headers, "host");
    const char *connection = xps_http_get_header(&http_req->headers, "Connection");
    const char *pathname = http_req->path;

    bool keep_alive = false;
    if (connection != NULL && strcasecmp(connection, "keep-alive") == 0)
    {
        keep_alive = true;
    }

    // Step 1: Find matching server block
    int target_server_index = -1;
 
    for (int i = 0; i < config->servers.length; i++)
    {
        xps_config_server_t *server = config->servers.data[i];

        // Check if client listener port is present in server config
        bool has_matching_listener = false;

        for (int j = 0; j < server->listeners.length; j++)
        {
            xps_config_listener_t *conf_listener = server->listeners.data[j];

            if (client->listener->port == conf_listener->port)
            {
                has_matching_listener = true;
                break;
            }
        }

        if (!has_matching_listener)
        {
            continue;
        }

        /* Check if host header matches any hostname */
        // NOTE: if no hostnames provided, it is considered a match
        bool has_matching_hostname = false;
        if (server->hostnames.length == 0)
        {
            has_matching_hostname = true;
        }
        else
        {
            for (int j = 0; j < server->hostnames.length; j++)
            {
                if (host == NULL || strcmp(host, (char *)server->hostnames.data[j]) == 0)
                {
                    has_matching_hostname = true;
                    break;
                }
            }
        }

        if (has_matching_hostname)
        {
            target_server_index = i;
            break;
        }
    }

    if (target_server_index == -1)
    {
        *error = E_NOTFOUND;
        return NULL;
    }

    xps_config_server_t *server = config->servers.data[target_server_index];

    /*Find matching route block*/
    // Route matching uses prefix matching with longest-match-first strategy.
    // This is important because:
    // - For file serving routes (e.g., "/"), we need to match any path under it
    //   (e.g., "/index.html", "/css/style.css" should all match route "/")
    // - For specific routes (e.g., "/api"), we want them to take precedence over "/"
    //
    // Example: If we have routes "/" and "/api"
    // - Request "/index.html" matches "/" only → serves file from "/"
    // - Request "/api/users" matches both "/" and "/api" → use "/api" (longest match)

    xps_config_route_t *route = NULL;
    size_t best_match_len = 0; // Track the longest matching route path

    for (int i = 0; i < server->routes.length; i++)
    {
        xps_config_route_t *current_route = server->routes.data[i];
        size_t route_path_len = strlen(current_route->req_path);

        // Check if this route's path is a prefix of the request path
        if (str_starts_with(pathname, current_route->req_path))
        {
            // If this is a longer match than we've found so far, use it
            if (route_path_len > best_match_len)
            {
                best_match_len = route_path_len;
                route = current_route;
            }
        }
    }

    if (route == NULL)
    {
        *error = E_NOTFOUND; // No matching route found - 404
        return NULL;
    }
    /* Init values of lookup*/
    // File serve
    /* Init values of lookup */
    xps_config_lookup_t *lookup = malloc(sizeof(xps_config_lookup_t));
    memset(lookup, 0, sizeof(xps_config_lookup_t));
    lookup->keep_alive = keep_alive;

    // 1. Translate the JSON string into the C enum!
    if (strcmp(route->type, "file_serve") == 0)
        lookup->type = REQ_FILE_SERVE;
    else if (strcmp(route->type, "reverse_proxy") == 0)
        lookup->type = REQ_REVERSE_PROXY;
    else if (strcmp(route->type, "redirect") == 0)
        lookup->type = REQ_REDIRECT;
    else
        lookup->type = REQ_INVALID;

    // 2. Handle File Serve
    if (lookup->type == REQ_FILE_SERVE)
    {
        char *resource_path = path_join(route->dir_path, pathname);
        if (!is_abs_path(resource_path))
        {
            char *resolved_path = realpath(resource_path, NULL);
            free(resource_path);

            if (resolved_path == NULL)
            {
                free(lookup);
                *error = E_NOTFOUND;
                return NULL;
            }
            resource_path = resolved_path;
        }

        if (is_file(resource_path))
        {
            lookup->file_path = resource_path;
        }
        else if (is_dir(resource_path))
        {
            lookup->dir_path = resource_path;
        }
        else
        {
            free(resource_path);
            free(lookup);
            *error = E_NOTFOUND;
            return NULL;
        }
    }
    // 3. Handle Reverse Proxy (Copy the upstream IP)
    else if (lookup->type == REQ_REVERSE_PROXY)
    {
        if (route->upstreams.length > 0)
        {
            lookup->upstream = str_create((char *)route->upstreams.data[0]);
        }
    }
    // 4. Handle Redirect (Copy the URL and status code)
    else if (lookup->type == REQ_REDIRECT)
    {
        lookup->http_status_code = route->http_status_code;
        lookup->redirect_url = str_create(route->redirect_url);
    }

    *error = OK;
    return lookup;
}

void xps_config_lookup_destroy(xps_config_lookup_t *lookup, xps_core_t *core)
{
    if (lookup == NULL)
        return;

    if (lookup->file_path)
        free(lookup->file_path);
    if (lookup->dir_path)
        free(lookup->dir_path);

    if (lookup->upstream)
        free((void *)lookup->upstream);
    if (lookup->redirect_url)
        free((void *)lookup->redirect_url);

    free(lookup);
}

static void parse_server(JSON_Object *server_object, xps_config_server_t *server)
{
    vec_init(&server->listeners);
    vec_init(&server->hostnames);
    vec_init(&server->routes);

    /* Parse Listeners */
    JSON_Array *listeners_arr = json_object_get_array(server_object, "listeners");
    if (listeners_arr)
    {
        for (size_t i = 0; i < json_array_get_count(listeners_arr); i++)
        {
            JSON_Object *listener_obj = json_array_get_object(listeners_arr, i);
            xps_config_listener_t *listener = malloc(sizeof(xps_config_listener_t));

            parse_listener(listener_obj, listener);
            vec_push(&server->listeners, listener);
        }
    }

    /* Parse Routes */
    JSON_Array *routes_arr = json_object_get_array(server_object, "routes");
    if (routes_arr)
    {
        for (size_t i = 0; i < json_array_get_count(routes_arr); i++)
        {
            JSON_Object *route_obj = json_array_get_object(routes_arr, i);
            xps_config_route_t *route = malloc(sizeof(xps_config_route_t));

            parse_route(route_obj, route); // You must implement this helper too
            vec_push(&server->routes, route);
        }
    }

    /* Parse Hostnames (if they exist in the config) */
    JSON_Array *hostnames_arr = json_object_get_array(server_object, "hostnames");
    if (hostnames_arr)
    {
        for (size_t i = 0; i < json_array_get_count(hostnames_arr); i++)
        {
            const char *hostname = json_array_get_string(hostnames_arr, i);
            vec_push(&server->hostnames, (void *)str_create(hostname));
        }
    }
}

static void parse_listener(JSON_Object *listener_object, xps_config_listener_t *listener)
{
    /* Extract host string and duplicate it */
    const char *host = json_object_get_string(listener_object, "host");
    if (host != NULL)
    {
        listener->host = str_create(host);
    }
    else
    {
        listener->host = NULL;
    }

    /* Extract port number */
    listener->port = (u_int)json_object_get_number(listener_object, "port");
}

static void parse_all_listener(xps_config_t *config)
{
    /* Iterate through all parsed servers */
    for (int i = 0; i < config->servers.length; i++)
    {
        xps_config_server_t *server = config->servers.data[i];

        /* Iterate through all listeners attached to the current server */
        for (int j = 0; j < server->listeners.length; j++)
        {
            xps_config_listener_t *listener = server->listeners.data[j];

            bool is_duplicate = false;

            /* Check if this listener host/port combo is already in _all_listeners */
            for (int k = 0; k < config->_all_listeners.length; k++)
            {
                xps_config_listener_t *existing = config->_all_listeners.data[k];

                // Safely handle if host is NULL for either listener
                bool host_match = false;
                if (existing->host == NULL && listener->host == NULL)
                {
                    host_match = true;
                }
                else if (existing->host != NULL && listener->host != NULL)
                {
                    host_match = (strcmp((char *)existing->host, (char *)listener->host) == 0);
                }

                if (existing->port == listener->port && host_match)
                {
                    is_duplicate = true;
                    break;
                }
            }

            /* If it's not a duplicate, push it to the master list */
            if (!is_duplicate)
            {
                vec_push(&config->_all_listeners, listener);
            }
        }
    }
}

static void parse_route(JSON_Object *route_object, xps_config_route_t *route)
{
    /* 1. Initialize the arrays (vectors) inside the route struct */
    vec_init(&route->index);
    vec_init(&route->upstreams);

    /* 2. Extract mandatory base fields */
    const char *req_path = json_object_get_string(route_object, "req_path");
    if (req_path)
        route->req_path = str_create(req_path);

    const char *type = json_object_get_string(route_object, "type");
    if (type)
        route->type = str_create(type);

    /* 3. Extract optional fields (depends on route type) */

    // For file serving
    const char *dir_path = json_object_get_string(route_object, "dir_path");
    if (dir_path)
        route->dir_path = str_create(dir_path);
    else
        route->dir_path = NULL;

    // For redirects
    const char *redirect_url = json_object_get_string(route_object, "redirect_url");
    if (redirect_url)
        route->redirect_url = str_create(redirect_url);
    else
        route->redirect_url = NULL;

    if (json_object_has_value(route_object, "http_status_code"))
    {
        route->http_status_code = (u_int)json_object_get_number(route_object, "http_status_code");
    }
    else
    {
        route->http_status_code = 0;
    }

    /* 4. Extract arrays (index files and upstreams) */

    // Parse 'index' array for file serving
    JSON_Array *index_arr = json_object_get_array(route_object, "index");
    if (index_arr)
    {
        for (size_t i = 0; i < json_array_get_count(index_arr); i++)
        {
            const char *index_file = json_array_get_string(index_arr, i);
            vec_push(&route->index, (void *)str_create(index_file));
        }
    }

    // Parse 'upstreams' array for reverse proxy
    JSON_Array *upstreams_arr = json_object_get_array(route_object, "upstreams");
    if (upstreams_arr)
    {
        for (size_t i = 0; i < json_array_get_count(upstreams_arr); i++)
        {
            const char *upstream = json_array_get_string(upstreams_arr, i);
            vec_push(&route->upstreams, (void *)str_create(upstream));
        }
    }
}