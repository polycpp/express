#pragma once

/**
 * @file request.hpp
 * @brief Request class -- wraps http::IncomingMessage with Express features.
 *
 * Provides route parameters, body, query parsing, content negotiation,
 * IP resolution, and other Express.js request enhancements.
 *
 * @see https://expressjs.com/en/api.html#req
 * @since 0.1.0
 */

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <polycpp/core/json.hpp>
#include <polycpp/http.hpp>
#include <polycpp/negotiate/negotiate.hpp>
#include <polycpp/qs/qs.hpp>
#include <polycpp/url.hpp>

#include <polycpp/express/types.hpp>
#include <polycpp/express/http_error.hpp>
#include <polycpp/express/detail/utils.hpp>

namespace polycpp {
namespace express {

/**
 * @brief Express request object wrapping http::IncomingMessage.
 *
 * Adds Express.js-style properties and methods on top of the raw HTTP
 * incoming message: route parameters, parsed query string, body parsing,
 * content negotiation, IP resolution, and more.
 *
 * @see https://expressjs.com/en/api.html#req
 * @since 0.1.0
 */
class Request {
public:
    /**
     * @brief Construct a Request wrapping an IncomingMessage.
     *
     * @param raw The raw HTTP incoming message.
     * @param app Pointer to the Express application.
     * @since 0.1.0
     */
    Request(http::IncomingMessage& raw, Application* app)
        : raw_(raw), app_(app) {}

    // ── Route Parameters ─────────────────────────────────────────────

    /**
     * @brief Route parameters extracted from the URL pattern.
     *
     * For a route `/users/:id`, accessing `req.params["id"]` gives
     * the matched value.
     *
     * @since 0.1.0
     */
    JsonValue params = JsonObject{};

    // ── Body (set by body-parser middleware) ──────────────────────────

    /**
     * @brief Parsed request body (set by body-parser middleware).
     *
     * Populated by express::json(), express::urlencoded(), etc.
     *
     * @since 0.1.0
     */
    JsonValue body;

    // ── Header Access ────────────────────────────────────────────────

    /**
     * @brief Get a request header value (case-insensitive).
     *
     * @param name The header name.
     * @return The header value, or std::nullopt if not present.
     *
     * @par Example
     * @code{.cpp}
     *   auto ct = req.get("Content-Type");
     *   // check ct
     * @endcode
     *
     * @since 0.1.0
     */
    std::optional<std::string> get(const std::string& name) const {
        return raw_.headers().get(name);
    }

    /**
     * @brief Alias for get().
     * @since 0.1.0
     */
    std::optional<std::string> header(const std::string& name) const {
        return get(name);
    }

    // ── Content Negotiation ──────────────────────────────────────────

    /**
     * @brief Check if the request accepts the given types.
     *
     * @param types The content types to check.
     * @return The best matching type, or std::nullopt if none match.
     * @since 0.1.0
     */
    std::optional<std::string> accepts(const std::vector<std::string>& types) const {
        auto acceptHeader = get("accept").value_or("*/*");
        negotiate::Accepts acc(acceptHeader);
        return acc.type(types);
    }

    /**
     * @brief Check accepted encodings.
     * @since 0.1.0
     */
    std::optional<std::string> acceptsEncodings(const std::vector<std::string>& encodings) const {
        auto header = get("accept-encoding").value_or("");
        negotiate::Accepts acc("", header);
        return acc.encoding(encodings);
    }

    /**
     * @brief Check accepted charsets.
     * @since 0.1.0
     */
    std::optional<std::string> acceptsCharsets(const std::vector<std::string>& charsets) const {
        auto header = get("accept-charset").value_or("");
        negotiate::Accepts acc("", "", header);
        return acc.charset(charsets);
    }

    /**
     * @brief Check accepted languages.
     * @since 0.1.0
     */
    std::optional<std::string> acceptsLanguages(const std::vector<std::string>& langs) const {
        auto header = get("accept-language").value_or("");
        negotiate::Accepts acc("", "", "", header);
        return acc.language(langs);
    }

    // ── Content Type ─────────────────────────────────────────────────

    /**
     * @brief Check if the request Content-Type matches the given types.
     *
     * @param types The types to check (e.g., "json", "application/json").
     * @return The matching type, or std::nullopt if no match.
     * @since 0.1.0
     */
    std::optional<std::string> is(const std::vector<std::string>& types) const {
        auto ct = get("content-type").value_or("");
        if (ct.empty()) return std::nullopt;
        return negotiate::typeIs(ct, types);
    }

    // ── Range Parsing ────────────────────────────────────────────────

    /**
     * @brief Parse the Range header.
     *
     * @param size The total resource size.
     * @return Vector of range specifications, or empty if invalid.
     * @since 0.1.0
     */
    std::vector<detail::RangeSpec> range(size_t size) const {
        auto header = get("range").value_or("");
        if (header.empty()) return {};
        return detail::parseRange(size, header);
    }

    // ── Computed Properties ──────────────────────────────────────────

    /**
     * @brief Get the parsed query string as a JsonValue.
     *
     * Uses qs for extended parsing or querystring for simple parsing,
     * depending on app settings.
     *
     * @return The parsed query object.
     * @since 0.1.0
     */
    JsonValue query() const {
        auto rawUrl = raw_.url();
        auto qpos = rawUrl.find('?');
        if (qpos == std::string::npos) {
            return JsonObject{};
        }
        auto queryStr = rawUrl.substr(qpos + 1);
        // Remove fragment
        auto fragPos = queryStr.find('#');
        if (fragPos != std::string::npos) {
            queryStr = queryStr.substr(0, fragPos);
        }
        return qs::parse(queryStr);
    }

    /**
     * @brief Get the request path (without query string).
     * @since 0.1.0
     */
    std::string path() const {
        if (!overridePath_.empty()) return overridePath_;
        auto rawUrl = raw_.url();
        auto qpos = rawUrl.find('?');
        if (qpos != std::string::npos) {
            return rawUrl.substr(0, qpos);
        }
        return rawUrl;
    }

    /**
     * @brief Get the protocol ("http" or "https").
     *
     * When the "trust proxy" setting trusts the socket address, the
     * "X-Forwarded-Proto" header field will be trusted and used if present.
     *
     * @return "http" or "https".
     * @since 0.1.0
     */
    std::string protocol() const;

    /**
     * @brief Whether the connection is secure (HTTPS).
     * @since 0.1.0
     */
    bool secure() const {
        return protocol() == "https";
    }

    /**
     * @brief Get the remote IP address.
     *
     * When "trust proxy" is set, walks the X-Forwarded-For chain using
     * the trust function to determine the actual client IP.
     * When "trust proxy" is not set, returns the socket address.
     *
     * @return The client IP address.
     * @since 0.1.0
     */
    std::string ip() const;

    /**
     * @brief Get the list of proxy IP addresses.
     *
     * When "trust proxy" is set, returns trusted proxy addresses + client,
     * ordered from farthest to closest. When "trust proxy" is not set,
     * returns an empty vector.
     *
     * @return Vector of trusted proxy addresses.
     * @since 0.1.0
     */
    std::vector<std::string> ips() const;

    /**
     * @brief Get the hostname from the Host header.
     *
     * When the "trust proxy" setting trusts the socket address, the
     * "X-Forwarded-Host" header field will be trusted and used if present.
     *
     * @return The hostname, or std::nullopt if not available.
     * @since 0.1.0
     */
    std::optional<std::string> hostname() const;

    /**
     * @brief Get subdomains.
     *
     * For hostname "tobi.ferrets.example.com" with subdomain offset 2,
     * returns ["ferrets", "tobi"].
     *
     * @since 0.1.0
     */
    std::vector<std::string> subdomains() const {
        auto host = hostname();
        if (!host) return {};

        std::vector<std::string> parts;
        std::istringstream stream(*host);
        std::string part;
        while (std::getline(stream, part, '.')) {
            parts.push_back(part);
        }

        // Remove the last N parts (subdomain offset, default 2)
        int offset = 2; // TODO: get from app settings
        if (static_cast<int>(parts.size()) <= offset) {
            return {};
        }

        std::vector<std::string> result;
        for (int i = static_cast<int>(parts.size()) - offset - 1; i >= 0; --i) {
            result.push_back(parts[i]);
        }
        return result;
    }

    /**
     * @brief Check if the cache is fresh (for conditional GET).
     *
     * Requires the Response to be linked (setRes called).
     * Implementation is in express.hpp after Response is defined.
     *
     * @since 0.1.0
     */
    bool fresh() const;

    /**
     * @brief Check if the cache is stale (opposite of fresh).
     * @since 0.1.0
     */
    bool stale() const { return !fresh(); }

    /**
     * @brief Check if the request was made with XMLHttpRequest.
     * @since 0.1.0
     */
    bool xhr() const {
        auto header = get("x-requested-with");
        return header && detail::toLower(*header) == "xmlhttprequest";
    }

    // ── Raw HTTP Access ──────────────────────────────────────────────

    /** @brief Get the raw HTTP incoming message. @since 0.1.0 */
    http::IncomingMessage& raw() { return raw_; }

    /** @brief Get the raw HTTP incoming message (const). @since 0.1.0 */
    const http::IncomingMessage& raw() const { return raw_; }

    // ── Standard HTTP Properties ─────────────────────────────────────

    /** @brief Get the HTTP method. @since 0.1.0 */
    const std::string& method() const { return raw_.method(); }

    /** @brief Get the request URL. @since 0.1.0 */
    const std::string& url() const { return raw_.url(); }

    /** @brief Get the HTTP version. @since 0.1.0 */
    const std::string& httpVersion() const { return raw_.httpVersion(); }

    /** @brief Get all headers. @since 0.1.0 */
    const polycpp::http::Headers& headers() const { return raw_.headers(); }

    // ── Context ──────────────────────────────────────────────────────

    /** @brief Get the application. @since 0.1.0 */
    Application* app() const { return app_; }

    /** @brief Get the associated response. @since 0.1.0 */
    Response* res() const { return res_; }

    /** @brief Set the associated response (internal use). @since 0.1.0 */
    void setRes(Response* res) { res_ = res; }

    /** @brief Get the base URL for this request (for sub-routers). @since 0.1.0 */
    const std::string& baseUrl() const { return baseUrl_; }

    /** @brief Set the base URL (internal use). @since 0.1.0 */
    void setBaseUrl(const std::string& url) { baseUrl_ = url; }

    /** @brief Set an override path (internal use for sub-routers). @since 0.1.0 */
    void setPath(const std::string& path) { overridePath_ = path; }

    /**
     * @brief Get the socket's remote address.
     *
     * Returns the raw socket remote address, or empty string if unavailable.
     *
     * @return The socket remote address.
     * @since 0.1.0
     */
    std::string socketAddr() const {
        auto sock = raw_.socket();
        if (sock) {
            return sock->remoteAddress().value_or("");
        }
        return "";
    }

private:
    http::IncomingMessage& raw_;
    Application* app_ = nullptr;
    Response* res_ = nullptr;
    std::string baseUrl_;
    std::string overridePath_;
};

} // namespace express
} // namespace polycpp
