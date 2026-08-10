/**
 * kern-shards.js - Client-side runtime for kern shards
 *
 * Provides HTMX-style server-rendered interactivity by scanning
 * for [data-shard] elements and fetching HTML fragments from the server.
 *
 * Vanilla ES2020, no dependencies, < 12KB.
 */
(function() {
    "use strict";

    var SHARD_ATTR = "data-shard";
    var TARGET_ATTR = "data-shard-target";
    var SWAP_ATTR = "data-shard-swap";
    var TRIGGER_ATTR = "data-shard-trigger";
    var METHOD_ATTR = "data-shard-method";
    var CONFIRM_ATTR = "data-shard-confirm";
    var INDICATOR_ATTR = "data-shard-indicator";
    var HEADERS_ATTR = "data-shard-headers";

    var LOADING_CLASS = "kern-loading";

    function getDefaultTrigger(el) {
        var tag = el.tagName.toLowerCase();
        if (tag === "form") return "submit";
        if (tag === "input" || tag === "textarea" || tag === "select") return "input";
        return "click";
    }

    function getDefaultMethod(el) {
        var tag = el.tagName.toLowerCase();
        if (tag === "form") return "POST";
        return "GET";
    }

    function getTarget(el) {
        var selector = el.getAttribute(TARGET_ATTR);
        if (!selector) return el;
        return document.querySelector(selector) || el;
    }

    function getSwapMode(el) {
        return el.getAttribute(SWAP_ATTR) || "inner";
    }

    function dispatch(target, name, detail) {
        var event = new CustomEvent(name, {
            bubbles: true,
            cancelable: true,
            detail: detail || {}
        });
        return target.dispatchEvent(event);
    }

    function showIndicator(el) {
        var selector = el.getAttribute(INDICATOR_ATTR);
        if (!selector) return null;
        var indicator = document.querySelector(selector);
        if (indicator) indicator.classList.add(LOADING_CLASS);
        return indicator;
    }

    function hideIndicator(indicator) {
        if (indicator) indicator.classList.remove(LOADING_CLASS);
    }

    function swap(target, mode, html) {
        switch (mode) {
            case "outer":
                target.outerHTML = html;
                break;
            case "before":
                target.insertAdjacentHTML("beforebegin", html);
                break;
            case "after":
                target.insertAdjacentHTML("afterend", html);
                break;
            case "append":
                target.insertAdjacentHTML("beforeend", html);
                break;
            case "prepend":
                target.insertAdjacentHTML("afterbegin", html);
                break;
            case "inner":
            default:
                target.innerHTML = html;
                break;
        }
    }

    function serializeForm(form) {
        var formData = new FormData(form);
        var params = new URLSearchParams();
        formData.forEach(function(value, key) {
            params.append(key, value);
        });
        return params.toString();
    }

    function getExtraHeaders(el) {
        var raw = el.getAttribute(HEADERS_ATTR);
        if (!raw) return {};
        try {
            return JSON.parse(raw);
        } catch (e) {
            return {};
        }
    }

    function doFetch(el) {
        var url = el.getAttribute(SHARD_ATTR);
        if (!url) return;

        var confirmMsg = el.getAttribute(CONFIRM_ATTR);
        if (confirmMsg && !window.confirm(confirmMsg)) return;

        var target = getTarget(el);
        var mode = getSwapMode(el);
        var method = (el.getAttribute(METHOD_ATTR) || getDefaultMethod(el)).toUpperCase();
        var extraHeaders = getExtraHeaders(el);

        if (!dispatch(target, "kern:before-request", { url: url, method: method })) return;

        var indicator = showIndicator(el);

        var headers = { "X-Kern-Shard": "true" };
        Object.keys(extraHeaders).forEach(function(k) {
            headers[k] = extraHeaders[k];
        });

        var options = { method: method, headers: headers };

        if (method !== "GET" && method !== "HEAD") {
            var tag = el.tagName.toLowerCase();
            if (tag === "form") {
                headers["Content-Type"] = "application/x-www-form-urlencoded";
                options.body = serializeForm(el);
            }
        }

        fetch(url, options)
            .then(function(response) {
                return response.text();
            })
            .then(function(html) {
                dispatch(target, "kern:after-request", { url: url, html: html });
                swap(target, mode, html);
                dispatch(target, "kern:swap", { url: url, mode: mode });
                scanElement(mode === "outer" ? document.body : target);
            })
            .catch(function(err) {
                dispatch(target, "kern:after-request", { url: url, error: err });
            })
            .finally(function() {
                hideIndicator(indicator);
            });
    }

    function attachTrigger(el) {
        if (el._kernShardBound) return;
        el._kernShardBound = true;

        var trigger = el.getAttribute(TRIGGER_ATTR) || getDefaultTrigger(el);

        if (trigger === "load") {
            doFetch(el);
            return;
        }

        if (trigger === "revealed") {
            var observer = new IntersectionObserver(function(entries, obs) {
                entries.forEach(function(entry) {
                    if (entry.isIntersecting) {
                        obs.unobserve(entry.target);
                        doFetch(entry.target);
                    }
                });
            }, { threshold: 0 });
            observer.observe(el);
            return;
        }

        el.addEventListener(trigger, function(e) {
            if (trigger === "submit") e.preventDefault();
            doFetch(el);
        });
    }

    function scanElement(root) {
        if (!root) return;
        var elements = root.querySelectorAll("[" + SHARD_ATTR + "]");
        elements.forEach(function(el) {
            attachTrigger(el);
        });
        if (root.hasAttribute && root.hasAttribute(SHARD_ATTR)) {
            attachTrigger(root);
        }
    }

    function init() {
        scanElement(document.body);

        var observer = new MutationObserver(function(mutations) {
            mutations.forEach(function(mutation) {
                mutation.addedNodes.forEach(function(node) {
                    if (node.nodeType === 1) {
                        scanElement(node);
                    }
                });
            });
        });

        observer.observe(document.body, { childList: true, subtree: true });
    }

    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", init);
    } else {
        init();
    }
})();
