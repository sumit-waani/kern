/*
 * kern_router.c - Radix tree URL router
 *
 * Implements a compressed trie (radix tree) for efficient URL routing.
 * Supports static segments, parameterized segments (:name), and
 * wildcard segments (*name). One tree per HTTP method for simplicity.
 */

#include "kern.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Maximum number of children per node */
#define KERN_ROUTER_MAX_CHILDREN 256

/* Maximum number of methods (GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS) */
#define KERN_ROUTER_NUM_METHODS 7

/* Maximum number of middleware registrations */
#define KERN_ROUTER_MAX_MIDDLEWARE 64

/* Node types */
typedef enum {
    NODE_STATIC,
    NODE_PARAM,
    NODE_WILDCARD
} kern_node_type_t;

/* A single node in the radix tree */
typedef struct kern_route_node {
    char *segment;                   /* The path segment (without leading /) */
    kern_node_type_t type;           /* static, param, or wildcard */
    char *param_name;                /* For param/wildcard: the capture name */
    kern_handler_fn handler;         /* Handler if this is a leaf */
    struct kern_route_node **children;
    int child_count;
    int child_cap;
} kern_route_node_t;

/* Middleware entry */
typedef struct {
    char *prefix;
    kern_middleware_fn fn;
} kern_middleware_entry_t;

/* The router structure - one tree per method */
struct kern_router {
    kern_route_node_t *trees[KERN_ROUTER_NUM_METHODS];
    kern_middleware_entry_t middleware[KERN_ROUTER_MAX_MIDDLEWARE];
    int middleware_count;
};

/* Map method enum to tree index */
static int method_index(kern_method_t method) {
    switch (method) {
        case KERN_METHOD_GET:     return 0;
        case KERN_METHOD_POST:    return 1;
        case KERN_METHOD_PUT:     return 2;
        case KERN_METHOD_PATCH:   return 3;
        case KERN_METHOD_DELETE:  return 4;
        case KERN_METHOD_HEAD:    return 5;
        case KERN_METHOD_OPTIONS: return 6;
        default: return -1;
    }
}

/* Parse method string to enum */
static kern_method_t method_from_str(const char *method) {
    if (!method) return KERN_METHOD_GET;
    if (strcmp(method, "GET") == 0)     return KERN_METHOD_GET;
    if (strcmp(method, "POST") == 0)    return KERN_METHOD_POST;
    if (strcmp(method, "PUT") == 0)     return KERN_METHOD_PUT;
    if (strcmp(method, "PATCH") == 0)   return KERN_METHOD_PATCH;
    if (strcmp(method, "DELETE") == 0)  return KERN_METHOD_DELETE;
    if (strcmp(method, "HEAD") == 0)    return KERN_METHOD_HEAD;
    if (strcmp(method, "OPTIONS") == 0) return KERN_METHOD_OPTIONS;
    return KERN_METHOD_GET;
}

/* Create a new node */
static kern_route_node_t *node_new(const char *segment, kern_node_type_t type, const char *param_name) {
    kern_route_node_t *node = calloc(1, sizeof(kern_route_node_t));
    if (!node) return NULL;

    if (segment) {
        node->segment = strdup(segment);
        if (!node->segment) {
            free(node);
            return NULL;
        }
    } else {
        node->segment = strdup("");
    }

    node->type = type;
    if (param_name) {
        node->param_name = strdup(param_name);
    }
    node->handler = NULL;
    node->child_count = 0;
    node->child_cap = 0;
    node->children = NULL;
    return node;
}

/* Free a node and all its children recursively */
static void node_free(kern_route_node_t *node) {
    if (!node) return;
    for (int i = 0; i < node->child_count; i++) {
        node_free(node->children[i]);
    }
    free(node->children);
    free(node->segment);
    free(node->param_name);
    free(node);
}

/* Find a child node matching the given segment and type */
static kern_route_node_t *find_child(kern_route_node_t *node, const char *segment, kern_node_type_t type) {
    for (int i = 0; i < node->child_count; i++) {
        kern_route_node_t *child = node->children[i];
        if (child->type == type && strcmp(child->segment, segment) == 0) {
            return child;
        }
    }
    return NULL;
}

/* Add a child node, maintaining priority ordering: static > param > wildcard */
static int add_child(kern_route_node_t *parent, kern_route_node_t *child) {
    if (parent->child_count >= parent->child_cap) {
        int new_cap = parent->child_cap == 0 ? 8 : parent->child_cap * 2;
        struct kern_route_node **new_children = realloc(parent->children,
            (size_t)new_cap * sizeof(struct kern_route_node *));
        if (!new_children) return -1;
        parent->children = new_children;
        parent->child_cap = new_cap;
    }

    /* Find insertion point to maintain priority order */
    int pos = parent->child_count;
    for (int i = 0; i < parent->child_count; i++) {
        if (parent->children[i]->type > child->type) {
            pos = i;
            break;
        }
    }

    /* Shift children to make room */
    for (int i = parent->child_count; i > pos; i--) {
        parent->children[i] = parent->children[i - 1];
    }
    parent->children[pos] = child;
    parent->child_count++;
    return 0;
}

/* Parse a path into segments. Returns allocated array of segment strings. */
typedef struct {
    char *text;
    kern_node_type_t type;
    char *param_name;  /* NULL for static segments */
} path_segment_t;

static int parse_path(const char *path, path_segment_t *segments, int max_segments) {
    if (!path || path[0] != '/') return -1;

    int count = 0;
    const char *p = path + 1; /* skip leading / */

    while (*p && count < max_segments) {
        const char *end = strchr(p, '/');
        size_t len = end ? (size_t)(end - p) : strlen(p);

        if (len == 0) {
            p = end + 1;
            continue;
        }

        if (p[0] == ':') {
            /* Parameterized segment */
            segments[count].type = NODE_PARAM;
            segments[count].text = strndup(p, len);
            segments[count].param_name = strndup(p + 1, len - 1);
        } else if (p[0] == '*') {
            /* Wildcard segment */
            segments[count].type = NODE_WILDCARD;
            segments[count].text = strndup(p, len);
            segments[count].param_name = strndup(p + 1, len - 1);
        } else {
            /* Static segment */
            segments[count].type = NODE_STATIC;
            segments[count].text = strndup(p, len);
            segments[count].param_name = NULL;
        }
        count++;

        if (!end) break;
        p = end + 1;
    }

    return count;
}

static void free_segments(path_segment_t *segments, int count) {
    for (int i = 0; i < count; i++) {
        free(segments[i].text);
        free(segments[i].param_name);
    }
}

/* ============================================================
 * Public API
 * ============================================================ */

kern_router_t *kern_router_new(void) {
    kern_router_t *router = calloc(1, sizeof(kern_router_t));
    if (!router) return NULL;

    /* Create root nodes for each method tree */
    for (int i = 0; i < KERN_ROUTER_NUM_METHODS; i++) {
        router->trees[i] = node_new("", NODE_STATIC, NULL);
        if (!router->trees[i]) {
            for (int j = 0; j < i; j++) {
                node_free(router->trees[j]);
            }
            free(router);
            return NULL;
        }
    }

    router->middleware_count = 0;
    return router;
}

void kern_router_free(kern_router_t *router) {
    if (!router) return;
    for (int i = 0; i < KERN_ROUTER_NUM_METHODS; i++) {
        node_free(router->trees[i]);
    }
    for (int i = 0; i < router->middleware_count; i++) {
        free(router->middleware[i].prefix);
    }
    free(router);
}

int kern_router_add(kern_router_t *router, const char *method, const char *path, kern_handler_fn handler) {
    if (!router || !method || !path || !handler) return -1;

    kern_method_t m = method_from_str(method);
    int idx = method_index(m);
    if (idx < 0) return -1;

    /* Handle root path "/" */
    if (strcmp(path, "/") == 0) {
        router->trees[idx]->handler = handler;
        return 0;
    }

    path_segment_t segments[64];
    int seg_count = parse_path(path, segments, 64);
    if (seg_count < 0) return -1;

    kern_route_node_t *current = router->trees[idx];

    for (int i = 0; i < seg_count; i++) {
        kern_route_node_t *child = NULL;

        if (segments[i].type == NODE_STATIC) {
            child = find_child(current, segments[i].text, NODE_STATIC);
        } else if (segments[i].type == NODE_PARAM) {
            /* Look for existing param node at this level */
            for (int j = 0; j < current->child_count; j++) {
                if (current->children[j]->type == NODE_PARAM) {
                    child = current->children[j];
                    break;
                }
            }
        } else if (segments[i].type == NODE_WILDCARD) {
            /* Look for existing wildcard node at this level */
            for (int j = 0; j < current->child_count; j++) {
                if (current->children[j]->type == NODE_WILDCARD) {
                    child = current->children[j];
                    break;
                }
            }
        }

        if (!child) {
            child = node_new(segments[i].text, segments[i].type, segments[i].param_name);
            if (!child) {
                free_segments(segments, seg_count);
                return -1;
            }
            if (add_child(current, child) != 0) {
                node_free(child);
                free_segments(segments, seg_count);
                return -1;
            }
        }

        current = child;
    }

    current->handler = handler;
    free_segments(segments, seg_count);
    return 0;
}

int kern_router_add_method(kern_router_t *router, kern_method_t method, const char *path, kern_handler_fn handler) {
    if (!router || !path || !handler) return -1;

    const char *method_str = NULL;
    switch (method) {
        case KERN_METHOD_GET:     method_str = "GET"; break;
        case KERN_METHOD_POST:    method_str = "POST"; break;
        case KERN_METHOD_PUT:     method_str = "PUT"; break;
        case KERN_METHOD_PATCH:   method_str = "PATCH"; break;
        case KERN_METHOD_DELETE:  method_str = "DELETE"; break;
        case KERN_METHOD_HEAD:    method_str = "HEAD"; break;
        case KERN_METHOD_OPTIONS: method_str = "OPTIONS"; break;
    }

    return kern_router_add(router, method_str, path, handler);
}

/* Internal: try to match path against a specific method tree */
static kern_handler_fn match_tree(kern_route_node_t *root, const char *path, kern_dict_t *params) {
    if (!root || !path) return NULL;

    /* Handle root path */
    if (strcmp(path, "/") == 0 || path[0] == '\0') {
        return root->handler;
    }

    /* Parse path segments from the request URL */
    const char *p = path;
    if (*p == '/') p++;

    /* Recursive matching */
    kern_route_node_t *current = root;
    const char *remaining = p;

    while (1) {
        /* Get next segment from remaining path */
        if (*remaining == '\0') {
            /* We've consumed the entire path, check if current node has a handler */
            if (current->handler) {
                return current->handler;
            }
            /* Backtrack not implemented in simple version - just fail */
            return NULL;
        }

        const char *seg_end = strchr(remaining, '/');
        size_t seg_len = seg_end ? (size_t)(seg_end - remaining) : strlen(remaining);
        const char *next_remaining = seg_end ? seg_end + 1 : remaining + seg_len;

        kern_handler_fn result = NULL;

        /* Try children in priority order: static > param > wildcard */
        for (int i = 0; i < current->child_count; i++) {
            kern_route_node_t *child = current->children[i];

            if (child->type == NODE_STATIC) {
                if (strlen(child->segment) == seg_len &&
                    strncmp(child->segment, remaining, seg_len) == 0) {
                    /* Static match */
                    if (*next_remaining == '\0') {
                        if (child->handler) {
                            return child->handler;
                        }
                        /* Check if child has a handler deeper (shouldn't happen for exact match) */
                    } else {
                        /* Recurse into this child */
                        result = match_tree(child, next_remaining - 1, params);
                        if (result) return result;

                        /* Construct a path with leading / for recursion */
                        size_t rem_len = strlen(next_remaining);
                        char *sub_path = malloc(rem_len + 2);
                        if (sub_path) {
                            sub_path[0] = '/';
                            memcpy(sub_path + 1, next_remaining, rem_len + 1);
                            result = match_tree(child, sub_path, params);
                            free(sub_path);
                            if (result) return result;
                        }
                    }
                }
            } else if (child->type == NODE_PARAM) {
                /* Param matches any single segment */
                char *value = strndup(remaining, seg_len);
                if (!value) continue;

                if (*next_remaining == '\0') {
                    if (child->handler) {
                        if (params && child->param_name) {
                            kern_dict_set(params, child->param_name, value);
                        } else {
                            free(value);
                        }
                        return child->handler;
                    }
                    free(value);
                } else {
                    /* Try recursing */
                    size_t rem_len = strlen(next_remaining);
                    char *sub_path = malloc(rem_len + 2);
                    if (sub_path) {
                        sub_path[0] = '/';
                        memcpy(sub_path + 1, next_remaining, rem_len + 1);
                        result = match_tree(child, sub_path, params);
                        free(sub_path);
                        if (result) {
                            if (params && child->param_name) {
                                kern_dict_set(params, child->param_name, value);
                            } else {
                                free(value);
                            }
                            return result;
                        }
                    }
                    free(value);
                }
            } else if (child->type == NODE_WILDCARD) {
                /* Wildcard matches rest of path */
                char *value = strdup(remaining);
                if (!value) continue;

                if (child->handler) {
                    if (params && child->param_name) {
                        kern_dict_set(params, child->param_name, value);
                    } else {
                        free(value);
                    }
                    return child->handler;
                }
                free(value);
            }
        }

        /* No match found */
        return NULL;
    }
}

/* Check if any method tree has a node for this path (for 405 detection) */
static bool path_exists_any_method(kern_router_t *router, const char *path) {
    kern_dict_t *tmp = kern_dict_new();
    if (!tmp) return false;

    for (int i = 0; i < KERN_ROUTER_NUM_METHODS; i++) {
        kern_handler_fn h = match_tree(router->trees[i], path, tmp);
        /* Clean up any params that were set */
        kern_dict_free(tmp);
        tmp = kern_dict_new();
        if (!tmp) return false;
        if (h) {
            kern_dict_free(tmp);
            return true;
        }
    }
    kern_dict_free(tmp);
    return false;
}

kern_route_result_t kern_router_match(kern_router_t *router, kern_method_t method,
                                      const char *path, kern_dict_t *params) {
    kern_route_result_t result = {NULL, KERN_ROUTE_NOT_FOUND};
    if (!router || !path) return result;

    int idx = method_index(method);
    if (idx < 0) return result;

    kern_handler_fn handler = match_tree(router->trees[idx], path, params);
    if (handler) {
        result.handler = handler;
        result.status = KERN_ROUTE_OK;
        return result;
    }

    /* Check if the path exists under a different method */
    if (path_exists_any_method(router, path)) {
        result.status = KERN_ROUTE_METHOD_NOT_ALLOWED;
    }

    return result;
}

/* ============================================================
 * Middleware API
 * ============================================================ */

int kern_router_use(kern_router_t *router, const char *prefix, kern_middleware_fn fn) {
    if (!router || !fn) return -1;
    if (router->middleware_count >= KERN_ROUTER_MAX_MIDDLEWARE) return -1;

    kern_middleware_entry_t *entry = &router->middleware[router->middleware_count];
    entry->prefix = prefix ? strdup(prefix) : strdup("/");
    entry->fn = fn;
    router->middleware_count++;
    return 0;
}

/* Build the middleware chain for a given path */
typedef struct {
    kern_middleware_fn *fns;
    int count;
    int current;
    kern_handler_fn final_handler;
} middleware_chain_t;

/* Thread-local chain for the "next" callback mechanism */
static __thread middleware_chain_t *tl_chain = NULL;

static kern_response_t *chain_next(kern_req_t *req) {
    if (!tl_chain) return NULL;

    middleware_chain_t *chain = tl_chain;
    if (chain->current >= chain->count) {
        /* All middleware done, call the final handler */
        return chain->final_handler(req);
    }

    kern_middleware_fn fn = chain->fns[chain->current];
    chain->current++;

    return fn(req, chain_next);
}

kern_response_t *kern_router_dispatch(kern_router_t *router, kern_req_t *req, kern_handler_fn handler) {
    if (!router || !req || !handler) {
        return kern_500_response("Invalid dispatch arguments");
    }

    const char *path = kern_req_path(req);

    /* Collect applicable middleware */
    kern_middleware_fn applicable[KERN_ROUTER_MAX_MIDDLEWARE];
    int applicable_count = 0;

    for (int i = 0; i < router->middleware_count; i++) {
        const char *prefix = router->middleware[i].prefix;
        size_t prefix_len = strlen(prefix);

        /* Check if path starts with prefix */
        if (strncmp(path, prefix, prefix_len) == 0) {
            /* Prefix must end at a segment boundary */
            if (prefix_len == 1 || /* "/" matches everything */
                path[prefix_len] == '\0' ||
                path[prefix_len] == '/' ||
                prefix[prefix_len - 1] == '/') {
                applicable[applicable_count++] = router->middleware[i].fn;
            }
        }
    }

    if (applicable_count == 0) {
        /* No middleware, call handler directly */
        return handler(req);
    }

    /* Set up middleware chain */
    middleware_chain_t chain;
    chain.fns = applicable;
    chain.count = applicable_count;
    chain.current = 0;
    chain.final_handler = handler;

    middleware_chain_t *prev_chain = tl_chain;
    tl_chain = &chain;

    kern_response_t *res = chain_next(req);

    tl_chain = prev_chain;
    return res;
}
