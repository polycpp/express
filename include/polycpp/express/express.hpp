#pragma once

/**
 * @file express.hpp
 * @brief Main entry point for polycpp Express.
 *
 * C++ port of npm express (https://github.com/expressjs/express).
 * Provides a Node.js Express-style web framework for C++20.
 *
 * @par Example
 * @code{.cpp}
 *   #include <polycpp/express/express.hpp>
 *
 *   int main() {
 *       using namespace polycpp::express;
 *
 *       auto app = express();
 *
 *       app.get("/", [](auto& req, auto& res) {
 *           res.send("Hello World!");
 *       });
 *
 *       app.get("/json", [](auto& req, auto& res) {
 *           res.json({{"message", "Hello"}});
 *       });
 *
 *       app.listen(3000, []() {
 *           // Server is listening
 *       });
 *
 *       polycpp::EventLoop::run();
 *       return 0;
 *   }
 * @endcode
 *
 * @see https://expressjs.com/
 * @see https://www.npmjs.com/package/express
 * @since 0.1.0
 */

// Core types
#include <polycpp/express/types.hpp>
#include <polycpp/express/http_error.hpp>

// Request/Response
#include <polycpp/express/request.hpp>
#include <polycpp/express/response.hpp>

// Routing
#include <polycpp/express/layer.hpp>
#include <polycpp/express/route.hpp>
#include <polycpp/express/router.hpp>

// View
#include <polycpp/express/view.hpp>

// Application
#include <polycpp/express/application.hpp>

// Built-in middleware
#include <polycpp/express/middleware/body_parser.hpp>
#include <polycpp/express/middleware/compression.hpp>
#include <polycpp/express/middleware/cors.hpp>
#include <polycpp/express/middleware/serve_static.hpp>

namespace polycpp {
namespace express {

// ── Deferred implementations (require both Request and Application) ──

inline std::string Request::protocol() const {
    // Default protocol from socket
    std::string proto = "http"; // TODO: check socket.encrypted for TLS

    auto trust = app_ ? app_->trustFunction() : detail::TrustFunction{};
    if (trust && trust(socketAddr(), 0)) {
        auto xfp = get("x-forwarded-proto");
        if (xfp) {
            auto header = *xfp;
            auto commaPos = header.find(',');
            return commaPos != std::string::npos
                ? detail::trim(header.substr(0, commaPos))
                : detail::trim(header);
        }
    }
    return proto;
}

inline std::string Request::ip() const {
    auto trust = app_ ? app_->trustFunction() : detail::TrustFunction{};
    if (!trust) {
        return socketAddr();
    }
    auto xff = get("x-forwarded-for").value_or("");
    return detail::proxyAddr(socketAddr(), xff, trust);
}

inline std::vector<std::string> Request::ips() const {
    auto trust = app_ ? app_->trustFunction() : detail::TrustFunction{};
    if (!trust) {
        return {};
    }
    auto xff = get("x-forwarded-for").value_or("");
    return detail::allAddrs(socketAddr(), xff, trust);
}

inline std::optional<std::string> Request::hostname() const {
    auto trust = app_ ? app_->trustFunction() : detail::TrustFunction{};

    std::string host;

    // Check X-Forwarded-Host if trust proxy trusts the socket address
    if (trust && trust(socketAddr(), 0)) {
        auto xfh = get("x-forwarded-host");
        if (xfh) {
            host = *xfh;
            // Take first value if comma-separated
            auto commaPos = host.find(',');
            if (commaPos != std::string::npos) {
                host = detail::trim(host.substr(0, commaPos));
            }
        }
    }

    // Fall back to Host header
    if (host.empty()) {
        auto hostHeader = get("host");
        if (!hostHeader) return std::nullopt;
        host = *hostHeader;
    }

    if (host.empty()) return std::nullopt;

    // Strip port — handle IPv6 literal addresses
    size_t offset = 0;
    if (!host.empty() && host[0] == '[') {
        auto bracketEnd = host.find(']');
        if (bracketEnd != std::string::npos) {
            offset = bracketEnd + 1;
        }
    }
    auto colonPos = host.find(':', offset);
    if (colonPos != std::string::npos) {
        host = host.substr(0, colonPos);
    }

    return host;
}

// ── Deferred implementations (require both Request and Response) ─────

inline bool Request::fresh() const {
    auto m = method();
    if (m != "GET" && m != "HEAD") return false;

    std::map<std::string, std::string> reqHeaders;
    auto inm = get("if-none-match");
    if (inm) reqHeaders["if-none-match"] = *inm;
    auto ims = get("if-modified-since");
    if (ims) reqHeaders["if-modified-since"] = *ims;

    if (res_) {
        // Only 2xx and 304 responses can be "fresh"
        auto sc = res_->statusCode();
        if (sc < 200 || (sc >= 300 && sc != 304)) return false;

        std::map<std::string, std::string> resHeaders;
        auto etag = res_->raw().getHeader("etag");
        if (!etag.empty()) resHeaders["etag"] = etag.front();
        auto lm = res_->raw().getHeader("last-modified");
        if (!lm.empty()) resHeaders["last-modified"] = lm.front();

        return detail::isFresh(reqHeaders, resHeaders);
    }
    return false;
}

/**
 * @brief Create a new Express application.
 *
 * Factory function matching the JavaScript `express()` pattern.
 *
 * @return A new Application instance.
 *
 * @par Example
 * @code{.cpp}
 *   auto app = express();
 *   app.get("/", [](auto& req, auto& res) {
 *       res.send("Hello!");
 *   });
 * @endcode
 *
 * @since 0.1.0
 */
inline Application express() {
    return Application();
}

// ── Built-in Middleware Convenience Functions ─────────────────────────

/**
 * @brief Create a JSON body parser middleware.
 *
 * @param opts Parser options.
 * @return A middleware handler.
 *
 * @par Example
 * @code{.cpp}
 *   app.use(express::json());
 * @endcode
 *
 * @since 0.1.0
 */
inline MiddlewareHandler json(const JsonParserOptions& opts = {}) {
    return jsonParser(opts);
}

/**
 * @brief Create a URL-encoded body parser middleware.
 *
 * @param opts Parser options.
 * @return A middleware handler.
 *
 * @par Example
 * @code{.cpp}
 *   app.use(express::urlencoded());
 * @endcode
 *
 * @since 0.1.0
 */
inline MiddlewareHandler urlencoded(const UrlencodedOptions& opts = {}) {
    return urlencodedParser(opts);
}

/**
 * @brief Create a raw body parser middleware.
 *
 * @param opts Parser options.
 * @return A middleware handler.
 * @since 0.1.0
 */
inline MiddlewareHandler raw(const RawParserOptions& opts = {}) {
    return rawParser(opts);
}

/**
 * @brief Create a text body parser middleware.
 *
 * @param opts Parser options.
 * @return A middleware handler.
 * @since 0.1.0
 */
inline MiddlewareHandler text(const TextParserOptions& opts = {}) {
    return textParser(opts);
}

/**
 * @brief Create a compression middleware.
 *
 * @param opts Compression options.
 * @return A middleware handler.
 *
 * @par Example
 * @code{.cpp}
 *   app.use(express::compress());
 *   app.use(express::compress({.threshold = 0, .level = 6}));
 * @endcode
 *
 * @see https://github.com/expressjs/compression
 * @since 0.1.0
 */
inline MiddlewareHandler compress(const CompressionOptions& opts = {}) {
    return compressionMiddleware(opts);
}

/**
 * @brief Create a CORS middleware.
 *
 * @param opts CORS options.
 * @return A middleware handler.
 *
 * @par Example
 * @code{.cpp}
 *   app.use(express::cors());
 *   app.use(express::cors({.origin = "https://example.com"}));
 * @endcode
 *
 * @see https://github.com/expressjs/cors
 * @since 0.1.0
 */
inline MiddlewareHandler cors(const CorsOptions& opts = {}) {
    return corsMiddleware(opts);
}

/**
 * @brief Create a static file serving middleware.
 *
 * @param root The root directory.
 * @param opts Serving options.
 * @return A middleware handler.
 *
 * @par Example
 * @code{.cpp}
 *   app.use(express::static_("public"));
 * @endcode
 *
 * @since 0.1.0
 */
inline MiddlewareHandler static_(const std::string& root,
                                  const StaticOptions& opts = {}) {
    return serveStatic(root, opts);
}

/**
 * @brief Create a new Router.
 *
 * @param opts Router options.
 * @return A Router instance.
 *
 * @par Example
 * @code{.cpp}
 *   auto router = express::Router();
 *   router.get("/", [](auto& req, auto& res) {
 *       res.send("Router root");
 *   });
 *   app.use("/api", router);
 * @endcode
 *
 * @since 0.1.0
 */
// Note: Router() is already the constructor, so this is just documentation.
// Users create routers with: Router router;

} // namespace express
} // namespace polycpp
