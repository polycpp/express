#pragma once

/**
 * @file middleware/cors.hpp
 * @brief CORS (Cross-Origin Resource Sharing) middleware.
 *
 * C++ port of npm cors (https://github.com/expressjs/cors).
 * Enables cross-origin requests by setting the appropriate
 * Access-Control-* response headers.
 *
 * @par Example
 * @code{.cpp}
 *   #include <polycpp/express/express.hpp>
 *
 *   auto app = express();
 *
 *   // Enable CORS with defaults (reflect origin, all methods)
 *   app.use(express::cors());
 *
 *   // Enable CORS with specific origin
 *   app.use(express::cors({.origin = "https://example.com"}));
 *
 *   // Enable CORS with whitelist
 *   app.use(express::cors({
 *       .origin = polycpp::JsonArray{"https://a.com", "https://b.com"},
 *       .credentials = true
 *   }));
 * @endcode
 *
 * @see https://www.npmjs.com/package/cors
 * @see https://github.com/expressjs/cors
 * @since 0.1.0
 */

#include <optional>
#include <string>
#include <vector>

#include <polycpp/core/json.hpp>

#include <polycpp/express/types.hpp>
#include <polycpp/express/request.hpp>
#include <polycpp/express/response.hpp>

namespace polycpp {
namespace express {

// ══════════════════════════════════════════════════════════════════════
// CorsOptions
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Configuration options for the CORS middleware.
 *
 * Mirrors the npm `cors` package options object.
 *
 * | Option               | Type                       | Default                                              |
 * |----------------------|----------------------------|------------------------------------------------------|
 * | origin               | bool / string / JsonArray  | true (reflect request Origin)                        |
 * | methods              | vector<string>             | GET, HEAD, PUT, PATCH, POST, DELETE                  |
 * | allowedHeaders       | vector<string>             | (empty = reflect Access-Control-Request-Headers)     |
 * | exposedHeaders       | vector<string>             | (empty)                                              |
 * | credentials          | bool                       | false                                                |
 * | maxAge               | optional<int>              | (none)                                               |
 * | preflightContinue    | bool                       | false                                                |
 * | optionsSuccessStatus | int                        | 204                                                  |
 *
 * @see https://github.com/expressjs/cors#configuration-options
 * @since 0.1.0
 */
struct CorsOptions {
    /**
     * @brief Configures the Access-Control-Allow-Origin header.
     *
     * - `true` (bool) -- reflect the request Origin header (default).
     * - `false` (bool) -- disable CORS (no header set).
     * - `"https://example.com"` (string) -- use a specific origin.
     * - `"*"` (string) -- allow any origin (no Vary).
     * - `JsonArray{"https://a.com", "https://b.com"}` -- whitelist.
     *
     * @since 0.1.0
     */
    JsonValue origin = true;

    /**
     * @brief Configures the Access-Control-Allow-Methods header.
     * @since 0.1.0
     */
    std::vector<std::string> methods = {"GET", "HEAD", "PUT", "PATCH", "POST", "DELETE"};

    /**
     * @brief Configures the Access-Control-Allow-Headers header.
     *
     * When empty (default), the middleware reflects the request's
     * Access-Control-Request-Headers header.
     *
     * @since 0.1.0
     */
    std::vector<std::string> allowedHeaders;

    /**
     * @brief Configures the Access-Control-Expose-Headers header.
     * @since 0.1.0
     */
    std::vector<std::string> exposedHeaders;

    /**
     * @brief Whether to set Access-Control-Allow-Credentials to "true".
     * @since 0.1.0
     */
    bool credentials = false;

    /**
     * @brief Configures the Access-Control-Max-Age header (seconds).
     * @since 0.1.0
     */
    std::optional<int> maxAge;

    /**
     * @brief Pass the CORS preflight response to the next handler.
     *
     * When false (default), the middleware responds to OPTIONS requests
     * with a 204 and ends the response. When true, next() is called
     * so downstream handlers can add additional headers or body.
     *
     * @since 0.1.0
     */
    bool preflightContinue = false;

    /**
     * @brief Status code for successful OPTIONS preflight requests.
     * @since 0.1.0
     */
    int optionsSuccessStatus = 204;
};

// ══════════════════════════════════════════════════════════════════════
// Internal helpers
// ══════════════════════════════════════════════════════════════════════

namespace detail {

/**
 * @brief Join a vector of strings with a separator.
 *
 * @param parts The strings to join.
 * @param sep The separator.
 * @return The joined string.
 * @since 0.1.0
 */
inline std::string corsJoin(const std::vector<std::string>& parts,
                            const std::string& sep) {
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += sep;
        result += parts[i];
    }
    return result;
}

} // namespace detail

// ══════════════════════════════════════════════════════════════════════
// cors() factory
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Create a CORS middleware handler.
 *
 * Returns a MiddlewareHandler that sets the appropriate
 * Access-Control-* headers on every response.
 *
 * @param opts CORS configuration options.
 * @return A MiddlewareHandler suitable for app.use().
 *
 * @par Example
 * @code{.cpp}
 *   // Reflect origin (default)
 *   app.use(cors());
 *
 *   // Specific origin
 *   app.use(cors({.origin = "https://example.com"}));
 *
 *   // Whitelist
 *   app.use(cors({
 *       .origin = JsonArray{"https://a.com", "https://b.com"},
 *       .credentials = true,
 *       .maxAge = 86400
 *   }));
 * @endcode
 *
 * @see https://github.com/expressjs/cors
 * @since 0.1.0
 */
inline MiddlewareHandler corsMiddleware(const CorsOptions& opts = {}) {
    return [opts](Request& req, Response& res, NextFunction next) {
        auto origin = req.get("origin").value_or("");

        // ── 1. Determine Access-Control-Allow-Origin ────────────────
        std::string allowOrigin;
        bool originSet = false;

        if (opts.origin.isBool()) {
            if (opts.origin.asBool()) {
                // Reflect the request Origin
                allowOrigin = origin;
                res.vary("Origin");
                originSet = true;
            }
            // false -> don't set any CORS headers at all
        } else if (opts.origin.isString()) {
            allowOrigin = opts.origin.asString();
            if (allowOrigin != "*") {
                res.vary("Origin");
            }
            originSet = true;
        } else if (opts.origin.isArray()) {
            // Whitelist: check if request origin is in the array
            for (const auto& o : opts.origin.asArray()) {
                if (o.isString() && o.asString() == origin) {
                    allowOrigin = origin;
                    res.vary("Origin");
                    originSet = true;
                    break;
                }
            }
            if (!originSet) {
                // Origin not in whitelist -- add Vary but no Allow-Origin
                res.vary("Origin");
                next({});
                return;
            }
        }

        // If origin is bool(false), skip all CORS headers
        if (opts.origin.isBool() && !opts.origin.asBool()) {
            next({});
            return;
        }

        // Set the Allow-Origin header (may be empty string for
        // reflect-mode when no Origin header was sent)
        if (!allowOrigin.empty()) {
            res.set("Access-Control-Allow-Origin", allowOrigin);
        }

        // ── 2. Credentials ──────────────────────────────────────────
        if (opts.credentials) {
            res.set("Access-Control-Allow-Credentials", "true");
        }

        // ── 3. Exposed headers (sent on every request, not just preflight)
        if (!opts.exposedHeaders.empty()) {
            res.set("Access-Control-Expose-Headers",
                    detail::corsJoin(opts.exposedHeaders, ","));
        }

        // ── 4. Preflight handling (OPTIONS method) ──────────────────
        if (req.method() == "OPTIONS") {
            // Methods
            res.set("Access-Control-Allow-Methods",
                    detail::corsJoin(opts.methods, ","));

            // Allowed headers
            if (!opts.allowedHeaders.empty()) {
                res.set("Access-Control-Allow-Headers",
                        detail::corsJoin(opts.allowedHeaders, ","));
            } else {
                // Reflect the request's Access-Control-Request-Headers
                auto reqHeaders = req.get("access-control-request-headers");
                if (reqHeaders) {
                    res.set("Access-Control-Allow-Headers", *reqHeaders);
                    res.vary("Access-Control-Request-Headers");
                }
            }

            // Max-Age
            if (opts.maxAge) {
                res.set("Access-Control-Max-Age",
                        std::to_string(*opts.maxAge));
            }

            // End the preflight or pass to next handler
            if (!opts.preflightContinue) {
                res.status(opts.optionsSuccessStatus);
                res.set("Content-Length", "0");
                res.raw().end("");
                return;
            }
        }

        next({});
    };
}

} // namespace express
} // namespace polycpp
