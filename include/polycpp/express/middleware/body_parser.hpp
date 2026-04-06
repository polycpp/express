#pragma once

/**
 * @file middleware/body_parser.hpp
 * @brief Body parsing middleware -- json(), urlencoded(), raw(), text().
 *
 * C++ port of npm body-parser. Reads the request body, parses it
 * according to content type, and populates req.body.
 *
 * @see https://www.npmjs.com/package/body-parser
 * @since 0.1.0
 */

#include <string>

#include <polycpp/core/json.hpp>
#include <polycpp/event_loop.hpp>
#include <polycpp/negotiate/negotiate.hpp>
#include <polycpp/qs/qs.hpp>

#include <polycpp/express/types.hpp>
#include <polycpp/express/http_error.hpp>
#include <polycpp/express/detail/utils.hpp>

namespace polycpp {
namespace express {

/**
 * @brief Read the raw request body as a string.
 *
 * Reads the body from the IncomingMessage, respecting size limits.
 *
 * @param req The request.
 * @param limit Maximum body size in bytes.
 * @since 0.1.0
 */
inline std::string readBody(Request& req, int64_t limit) {
    auto& raw = req.raw();
    std::string body;

    auto& ctx = polycpp::event_loop::EventLoop::instance().context();
    while (true) {
        auto buf = raw.read();
        if (buf.length() > 0) {
            body.append(reinterpret_cast<const char*>(buf.data()), buf.length());
            if (limit > 0 && static_cast<int64_t>(body.size()) > limit) {
                throw HttpError::payloadTooLarge("Request body too large");
            }
        }
        if (raw.readableEnded()) {
            break;
        }
        ctx.pollOnce();
    }

    return body;
}

/**
 * @brief Create a JSON body parser middleware.
 *
 * Parses incoming requests with JSON payloads and populates req.body.
 *
 * @param opts Parser options.
 * @return A middleware handler.
 *
 * @par Example
 * @code{.cpp}
 *   app.use(express::json());
 *   app.post("/api", [](auto& req, auto& res) {
 *       auto name = req.body["name"].asString();
 *       res.json({{"greeting", "Hello " + name}});
 *   });
 * @endcode
 *
 * @since 0.1.0
 */
inline MiddlewareHandler jsonParser(const JsonParserOptions& opts = {}) {
    auto limit = detail::parseBytes(opts.limit);
    auto type = opts.type;
    auto strict = opts.strict;

    return [limit, type, strict](Request& req, Response& res, NextFunction next) {
        // Check Content-Type
        auto ct = req.get("content-type");
        if (!ct) {
            next(std::nullopt);
            return;
        }

        auto match = negotiate::typeIs(*ct, {type});
        if (!match) {
            next(std::nullopt);
            return;
        }

        try {
            auto bodyStr = readBody(req, limit);
            if (bodyStr.empty()) {
                next(std::nullopt);
                return;
            }

            auto parsed = JSON::parse(bodyStr);

            // Strict mode: only accept objects and arrays
            if (strict && !parsed.isObject() && !parsed.isArray()) {
                next(HttpError::badRequest("Invalid JSON: only objects and arrays allowed in strict mode"));
                return;
            }

            req.body = std::move(parsed);
            next(std::nullopt);
        } catch (const HttpError& e) {
            next(e);
        } catch (const std::exception& e) {
            next(HttpError::badRequest(std::string("Invalid JSON: ") + e.what()));
        }
    };
}

/**
 * @brief Create a URL-encoded body parser middleware.
 *
 * Parses incoming requests with urlencoded payloads and populates req.body.
 *
 * @param opts Parser options.
 * @return A middleware handler.
 *
 * @par Example
 * @code{.cpp}
 *   app.use(express::urlencoded());
 *   app.post("/login", [](auto& req, auto& res) {
 *       auto user = req.body["username"].asString();
 *   });
 * @endcode
 *
 * @since 0.1.0
 */
inline MiddlewareHandler urlencodedParser(const UrlencodedOptions& opts = {}) {
    auto limit = detail::parseBytes(opts.limit);
    auto type = opts.type;
    auto extended = opts.extended;
    auto parameterLimit = opts.parameterLimit;

    return [limit, type, extended, parameterLimit](Request& req, Response& res, NextFunction next) {
        // Check Content-Type
        auto ct = req.get("content-type");
        if (!ct) {
            next(std::nullopt);
            return;
        }

        auto match = negotiate::typeIs(*ct, {type});
        if (!match) {
            next(std::nullopt);
            return;
        }

        try {
            auto bodyStr = readBody(req, limit);
            if (bodyStr.empty()) {
                next(std::nullopt);
                return;
            }

            if (extended) {
                // Use qs for extended parsing (supports nested objects)
                qs::ParseOptions qsOpts;
                qsOpts.parameterLimit = parameterLimit;
                req.body = qs::parse(bodyStr, qsOpts);
            } else {
                // Use simple parsing (no nesting)
                qs::ParseOptions qsOpts;
                qsOpts.depth = 0;
                qsOpts.parameterLimit = parameterLimit;
                req.body = qs::parse(bodyStr, qsOpts);
            }

            next(std::nullopt);
        } catch (const HttpError& e) {
            next(e);
        } catch (const std::exception& e) {
            next(HttpError::badRequest(std::string("Invalid urlencoded data: ") + e.what()));
        }
    };
}

/**
 * @brief Create a raw body parser middleware.
 *
 * Reads the request body as a raw Buffer and stores it in req.body.
 *
 * @param opts Parser options.
 * @return A middleware handler.
 * @since 0.1.0
 */
inline MiddlewareHandler rawParser(const RawParserOptions& opts = {}) {
    auto limit = detail::parseBytes(opts.limit);
    auto type = opts.type;

    return [limit, type](Request& req, Response& res, NextFunction next) {
        auto ct = req.get("content-type");
        if (!ct) {
            next(std::nullopt);
            return;
        }

        auto match = negotiate::typeIs(*ct, {type});
        if (!match) {
            next(std::nullopt);
            return;
        }

        try {
            auto bodyStr = readBody(req, limit);
            req.body = bodyStr;
            next(std::nullopt);
        } catch (const HttpError& e) {
            next(e);
        } catch (const std::exception& e) {
            next(HttpError::badRequest(e.what()));
        }
    };
}

/**
 * @brief Create a text body parser middleware.
 *
 * Reads the request body as text and stores it as a string in req.body.
 *
 * @param opts Parser options.
 * @return A middleware handler.
 * @since 0.1.0
 */
inline MiddlewareHandler textParser(const TextParserOptions& opts = {}) {
    auto limit = detail::parseBytes(opts.limit);
    auto type = opts.type;

    return [limit, type](Request& req, Response& res, NextFunction next) {
        auto ct = req.get("content-type");
        if (!ct) {
            next(std::nullopt);
            return;
        }

        auto match = negotiate::typeIs(*ct, {type});
        if (!match) {
            next(std::nullopt);
            return;
        }

        try {
            auto bodyStr = readBody(req, limit);
            req.body = bodyStr;
            next(std::nullopt);
        } catch (const HttpError& e) {
            next(e);
        } catch (const std::exception& e) {
            next(HttpError::badRequest(e.what()));
        }
    };
}

} // namespace express
} // namespace polycpp
