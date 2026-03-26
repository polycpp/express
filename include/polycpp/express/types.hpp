#pragma once

/**
 * @file types.hpp
 * @brief Core type definitions for polycpp Express.
 *
 * Defines the callback types, option structs, and forward declarations
 * used throughout the Express library.
 *
 * @see https://expressjs.com/en/api.html
 * @since 0.1.0
 */

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <polycpp/core/json.hpp>
#include <polycpp/http.hpp>

namespace polycpp {
namespace express {

// ── Forward Declarations ─────────────────────────────────────────────

class HttpError;
class Request;
class Response;
class Router;
class Route;
class Layer;
class Application;

// ── Callback Types ───────────────────────────────────────────────────

/**
 * @brief Next function type -- called to pass control to the next middleware.
 *
 * Call with no argument (or std::nullopt) to continue to the next handler.
 * Call with an HttpError to trigger error handling.
 *
 * @since 0.1.0
 */
using NextFunction = std::function<void(std::optional<HttpError>)>;

/**
 * @brief Terminal route handler (req, res) -- no next, ends the chain.
 *
 * @par Example
 * @code{.cpp}
 *   app.get("/", [](auto& req, auto& res) {
 *       res.send("Hello World!");
 *   });
 * @endcode
 *
 * @since 0.1.0
 */
using RouteHandler = std::function<void(Request&, Response&)>;

/**
 * @brief Middleware handler (req, res, next) -- calls next() to continue.
 *
 * @par Example
 * @code{.cpp}
 *   app.use([](auto& req, auto& res, auto next) {
 *       std::println("{} {}", req.method(), req.url());
 *       next(std::nullopt);
 *   });
 * @endcode
 *
 * @since 0.1.0
 */
using MiddlewareHandler = std::function<void(Request&, Response&, NextFunction)>;

/**
 * @brief Error handler (err, req, res, next) -- handles errors in the chain.
 *
 * @par Example
 * @code{.cpp}
 *   app.use([](const HttpError& err, auto& req, auto& res, auto next) {
 *       res.status(err.statusCode()).json({{"error", err.what()}});
 *   });
 * @endcode
 *
 * @since 0.1.0
 */
using ErrorHandler = std::function<void(const HttpError&, Request&, Response&, NextFunction)>;

/**
 * @brief Parameter handler (req, res, next, paramValue).
 *
 * @par Example
 * @code{.cpp}
 *   app.param("id", [](auto& req, auto& res, auto next, const std::string& id) {
 *       // Validate id
 *       next(std::nullopt);
 *   });
 * @endcode
 *
 * @since 0.1.0
 */
using ParamHandler = std::function<void(Request&, Response&, NextFunction, const std::string&)>;

/**
 * @brief Template engine function.
 *
 * @param path The file path of the template.
 * @param options Template variables.
 * @param callback Called with (error_code, rendered_string).
 *
 * @since 0.1.0
 */
using EngineFunction = std::function<void(
    const std::string& path,
    const JsonValue& options,
    std::function<void(std::optional<std::string> error, std::string result)>)>;

/**
 * @brief A handler variant that can be any of the four handler types.
 * @since 0.1.0
 */
using AnyHandler = std::variant<RouteHandler, MiddlewareHandler, ErrorHandler>;

// ── Option Structs ───────────────────────────────────────────────────

/**
 * @brief Options for creating a Router.
 *
 * @see https://expressjs.com/en/api.html#express.router
 * @since 0.1.0
 */
struct RouterOptions {
    /** @brief Enable case-sensitive routing. Default: false. */
    bool caseSensitive = false;

    /** @brief Enable strict routing (trailing slash matters). Default: false. */
    bool strict = false;

    /** @brief Merge params from parent router. Default: false. */
    bool mergeParams = false;
};

/**
 * @brief Options for res.cookie().
 *
 * @see https://expressjs.com/en/api.html#res.cookie
 * @since 0.1.0
 */
struct CookieOptions {
    /** @brief Domain attribute. */
    std::optional<std::string> domain;

    /** @brief Max-Age in milliseconds. */
    std::optional<std::chrono::milliseconds> maxAge;

    /** @brief Expires attribute. */
    std::optional<std::chrono::system_clock::time_point> expires;

    /** @brief HttpOnly flag. Default: false. */
    bool httpOnly = false;

    /** @brief Path attribute. Default: "/". */
    std::string path = "/";

    /** @brief Secure flag. Default: false. */
    bool secure = false;

    /** @brief Whether cookie should be signed. Default: false. */
    bool signed_ = false;

    /** @brief SameSite attribute: "strict", "lax", "none", or empty. */
    std::string sameSite;
};

/**
 * @brief Options for res.sendFile().
 *
 * @see https://expressjs.com/en/api.html#res.sendFile
 * @since 0.1.0
 */
struct SendFileOptions {
    /** @brief Max-Age for caching in milliseconds. */
    std::optional<std::chrono::milliseconds> maxAge;

    /** @brief Root directory for relative paths. */
    std::string root;

    /** @brief Custom headers to set on the response. */
    std::map<std::string, std::string> headers;

    /** @brief How to handle dotfiles: "allow", "deny", "ignore". Default: "ignore". */
    std::string dotfiles = "ignore";

    /** @brief Whether to generate ETags. Default: true. */
    bool etag = true;

    /** @brief Whether to set Last-Modified header. Default: true. */
    bool lastModified = true;
};

/**
 * @brief Options for express.static() middleware.
 *
 * @see https://expressjs.com/en/api.html#express.static
 * @since 0.1.0
 */
struct StaticOptions : SendFileOptions {
    /** @brief Index file name. Default: "index.html". */
    std::string index = "index.html";

    /** @brief Redirect directories to trailing slash. Default: true. */
    bool redirect = true;

    /** @brief Whether to fall through to next handler on 404. Default: true. */
    bool fallthrough = true;
};

/**
 * @brief Options for JSON body parser middleware.
 *
 * @see https://expressjs.com/en/api.html#express.json
 * @since 0.1.0
 */
struct JsonParserOptions {
    /** @brief Maximum request body size. Default: "100kb". */
    std::string limit = "100kb";

    /** @brief Content-Type to match. Default: "application/json". */
    std::string type = "application/json";

    /** @brief Strict mode: only accept arrays and objects. Default: true. */
    bool strict = true;
};

/**
 * @brief Options for URL-encoded body parser middleware.
 *
 * @see https://expressjs.com/en/api.html#express.urlencoded
 * @since 0.1.0
 */
struct UrlencodedOptions {
    /** @brief Maximum request body size. Default: "100kb". */
    std::string limit = "100kb";

    /** @brief Content-Type to match. Default: "application/x-www-form-urlencoded". */
    std::string type = "application/x-www-form-urlencoded";

    /** @brief Use extended parser (qs) vs simple (querystring). Default: true. */
    bool extended = true;

    /** @brief Maximum parameter count. Default: 1000. */
    int parameterLimit = 1000;
};

/**
 * @brief Options for raw body parser middleware.
 *
 * @see https://expressjs.com/en/api.html#express.raw
 * @since 0.1.0
 */
struct RawParserOptions {
    /** @brief Maximum request body size. Default: "100kb". */
    std::string limit = "100kb";

    /** @brief Content-Type to match. Default: "application/octet-stream". */
    std::string type = "application/octet-stream";
};

/**
 * @brief Options for text body parser middleware.
 *
 * @see https://expressjs.com/en/api.html#express.text
 * @since 0.1.0
 */
struct TextParserOptions {
    /** @brief Maximum request body size. Default: "100kb". */
    std::string limit = "100kb";

    /** @brief Content-Type to match. Default: "text/plain". */
    std::string type = "text/plain";

    /** @brief Default charset. Default: "utf-8". */
    std::string defaultCharset = "utf-8";
};

} // namespace express
} // namespace polycpp
