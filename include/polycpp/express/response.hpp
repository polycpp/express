#pragma once

/**
 * @file response.hpp
 * @brief Response class -- wraps http::ServerResponse with Express features.
 *
 * Provides send, json, redirect, cookie, sendFile, and other Express.js
 * response methods.
 *
 * @see https://expressjs.com/en/api.html#res
 * @since 0.1.0
 */

#include <algorithm>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <polycpp/buffer.hpp>
#include <polycpp/core/json.hpp>
#include <polycpp/cookie/cookie.hpp>
#include <polycpp/fs.hpp>
#include <polycpp/http.hpp>
#include <polycpp/mime/mime.hpp>
#include <polycpp/path.hpp>
#include <polycpp/zlib/zlib.hpp>

#include <polycpp/express/types.hpp>
#include <polycpp/express/http_error.hpp>
#include <polycpp/express/detail/utils.hpp>

namespace polycpp {
namespace express {

/**
 * @brief Express response object wrapping http::ServerResponse.
 *
 * Adds Express.js-style methods on top of the raw HTTP server response:
 * send, json, redirect, cookie, type, links, sendFile, and more.
 *
 * All setters return a reference for method chaining.
 *
 * @see https://expressjs.com/en/api.html#res
 * @since 0.1.0
 */
class Response {
public:
    /**
     * @brief Construct a Response wrapping a ServerResponse.
     *
     * @param raw The raw HTTP server response.
     * @param app Pointer to the Express application.
     * @since 0.1.0
     */
    Response(http::ServerResponse& raw, Application* app)
        : raw_(raw), app_(app) {}

    // ── Per-response template variables ──────────────────────────────

    /**
     * @brief Local variables for template rendering.
     * @since 0.1.0
     */
    JsonValue locals = JsonObject{};

    // ── Status ───────────────────────────────────────────────────────

    /**
     * @brief Set the HTTP status code.
     *
     * @param code The status code.
     * @return Reference to this Response for chaining.
     *
     * @par Example
     * @code{.cpp}
     *   res.status(404).send("Not Found");
     * @endcode
     *
     * @since 0.1.0
     */
    Response& status(int code) {
        raw_.status(code);
        return *this;
    }

    /**
     * @brief Get the current status code.
     * @return The status code.
     * @since 0.1.0
     */
    int statusCode() const {
        return raw_.statusCode();
    }

    // ── Send Response ────────────────────────────────────────────────

    /**
     * @brief Send a string response.
     *
     * Sets Content-Type to text/html if not already set, generates ETag,
     * checks freshness, and sends the body.
     *
     * @param body The response body string.
     * @return Reference to this Response.
     *
     * @since 0.1.0
     */
    Response& send(const std::string& body) {
        // Set Content-Type if not already set
        if (!raw_.hasHeader("Content-Type")) {
            raw_.setHeader("Content-Type", "text/html; charset=utf-8");
        }

        // Generate ETag if setting is enabled (before compression)
        // Only for GET and HEAD — ETags are meaningless for POST/PUT/DELETE
        if (req_ && (req_->method() == "GET" || req_->method() == "HEAD")) {
            if (!raw_.hasHeader("ETag") && !body.empty()) {
                auto etag = detail::generateWeakETag(body);
                raw_.setHeader("ETag", etag);
            }
        }

        // Check freshness for GET/HEAD
        if (req_ && req_->fresh()) {
            raw_.status(304);
            raw_.end();
            return *this;
        }

        // Try compression if compression middleware is active
        auto compressed = applyCompression_(body);
        const std::string& output = compressed ? *compressed : body;

        // Set Content-Length (after compression, if applied)
        raw_.setHeader("Content-Length", std::to_string(output.size()));

        // HEAD requests get no body
        if (req_ && req_->method() == "HEAD") {
            raw_.end();
        } else {
            raw_.end(output);
        }
        return *this;
    }

    /**
     * @brief Send a Buffer response.
     *
     * @param body The response body as a Buffer.
     * @return Reference to this Response.
     * @since 0.1.0
     */
    Response& send(const Buffer& body) {
        if (!raw_.hasHeader("Content-Type")) {
            raw_.setHeader("Content-Type", "application/octet-stream");
        }

        // Convert Buffer to raw string preserving exact bytes (no latin1 inflation)
        auto bodyStr = detail::bufferToRawString(body);

        // Try compression if compression middleware is active
        auto compressed = applyCompression_(bodyStr);
        const std::string& output = compressed ? *compressed : bodyStr;

        raw_.setHeader("Content-Length", std::to_string(output.size()));
        if (req_ && req_->method() == "HEAD") {
            raw_.end();
        } else {
            raw_.end(output);
        }
        return *this;
    }

    /**
     * @brief Send a JSON response (auto-converts JsonValue to JSON string).
     *
     * @param obj The object to serialize as JSON.
     * @return Reference to this Response.
     * @since 0.1.0
     */
    Response& json(const JsonValue& obj) {
        // Set Content-Type
        if (!raw_.hasHeader("Content-Type")) {
            raw_.setHeader("Content-Type", "application/json; charset=utf-8");
        }

        auto body = JSON::stringify(obj);

        // Delegate to send() for ETag generation and 304 freshness checks
        return send(body);
    }

    /**
     * @brief Send a JSONP response.
     *
     * @param obj The object to serialize.
     * @return Reference to this Response.
     * @since 0.1.0
     */
    Response& jsonp(const JsonValue& obj) {
        auto body = JSON::stringify(obj);

        // Get callback name from query
        std::string callback;
        if (req_) {
            auto q = req_->query();
            if (q.isObject()) {
                auto it = q.asObject().find("callback");
                if (it != q.asObject().end() && it->second.isString()) {
                    callback = it->second.asString();
                }
            }
        }

        if (!callback.empty()) {
            // Validate callback name against strict pattern to prevent XSS
            static const std::regex validCallback(R"(^[a-zA-Z_$][a-zA-Z0-9_$.[\]]*$)");
            if (!std::regex_match(callback, validCallback)) {
                // Invalid callback -- fall back to regular JSON
                callback.clear();
            }
        }

        if (!callback.empty()) {
            raw_.setHeader("Content-Type", "text/javascript; charset=utf-8");
            raw_.setHeader("X-Content-Type-Options", "nosniff");

            // Replace characters that are valid JSON but invalid JS
            // U+2028 LINE SEPARATOR, U+2029 PARAGRAPH SEPARATOR
            std::string safeBody;
            safeBody.reserve(body.size());
            for (size_t i = 0; i < body.size(); ++i) {
                // Check for UTF-8 encoded U+2028 (E2 80 A8) and U+2029 (E2 80 A9)
                if (i + 2 < body.size() &&
                    static_cast<unsigned char>(body[i]) == 0xE2 &&
                    static_cast<unsigned char>(body[i + 1]) == 0x80) {
                    if (static_cast<unsigned char>(body[i + 2]) == 0xA8) {
                        safeBody += "\\u2028";
                        i += 2;
                        continue;
                    }
                    if (static_cast<unsigned char>(body[i + 2]) == 0xA9) {
                        safeBody += "\\u2029";
                        i += 2;
                        continue;
                    }
                }
                // Replace '<' with \u003c to prevent </script> injection
                if (body[i] == '<') {
                    safeBody += "\\u003c";
                } else {
                    safeBody += body[i];
                }
            }
            body = "/**/ typeof " + callback + " === 'function' && " + callback + "(" + safeBody + ");";
        } else {
            raw_.setHeader("Content-Type", "application/json; charset=utf-8");
        }

        // Delegate to send() for ETag generation and 304 freshness checks
        return send(body);
    }

    /**
     * @brief Send a status code with the default message.
     *
     * @param code The status code.
     * @return Reference to this Response.
     *
     * @par Example
     * @code{.cpp}
     *   res.sendStatus(404);  // Sends "Not Found"
     * @endcode
     *
     * @since 0.1.0
     */
    Response& sendStatus(int code) {
        const auto& codes = http::STATUS_CODES();
        auto it = codes.find(code);
        std::string msg = (it != codes.end()) ? it->second : std::to_string(code);
        status(code);
        raw_.setHeader("Content-Type", "text/plain; charset=utf-8");
        raw_.setHeader("Content-Length", std::to_string(msg.size()));
        raw_.end(msg);
        return *this;
    }

    // ── Headers ──────────────────────────────────────────────────────

    /**
     * @brief Set a response header.
     *
     * @param field The header name.
     * @param value The header value.
     * @return Reference to this Response.
     * @since 0.1.0
     */
    Response& set(const std::string& field, const std::string& value) {
        detail::validateHeaderName(field);
        detail::validateHeaderValue(field, value);
        raw_.setHeader(field, value);
        return *this;
    }

    /**
     * @brief Set multiple response headers.
     * @param fields Map of header name-value pairs.
     * @return Reference to this Response.
     * @since 0.1.0
     */
    Response& set(const std::map<std::string, std::string>& fields) {
        for (const auto& [key, val] : fields) {
            detail::validateHeaderName(key);
            detail::validateHeaderValue(key, val);
            raw_.setHeader(key, val);
        }
        return *this;
    }

    /**
     * @brief Alias for set().
     * @since 0.1.0
     */
    Response& header(const std::string& field, const std::string& value) {
        return set(field, value);
    }

    /**
     * @brief Get a response header value.
     * @param field The header name.
     * @return The header value, or std::nullopt if not set.
     * @since 0.1.0
     */
    std::optional<std::string> get(const std::string& field) const {
        auto val = raw_.getHeader(field);
        if (val.empty()) return std::nullopt;
        return val;
    }

    /**
     * @brief Append a value to a response header.
     *
     * If the header already exists, the value is appended with a comma.
     *
     * @param field The header name.
     * @param value The value to append.
     * @return Reference to this Response.
     * @since 0.1.0
     */
    Response& append(const std::string& field, const std::string& value) {
        detail::validateHeaderName(field);
        detail::validateHeaderValue(field, value);
        auto existing = raw_.getHeader(field);
        if (existing.empty()) {
            raw_.setHeader(field, value);
        } else {
            raw_.setHeader(field, existing + ", " + value);
        }
        return *this;
    }

    /**
     * @brief Add a field to the Vary header.
     *
     * @param field The Vary field to add.
     * @return Reference to this Response.
     * @since 0.1.0
     */
    Response& vary(const std::string& field) {
        detail::varyAppend(raw_, field);
        return *this;
    }

    // ── Content Type ─────────────────────────────────────────────────

    /**
     * @brief Set the Content-Type header.
     *
     * If the type contains "/", it is used directly. Otherwise, it is
     * looked up in the MIME type database.
     *
     * @param typeStr The content type or file extension.
     * @return Reference to this Response.
     *
     * @par Example
     * @code{.cpp}
     *   res.type("json");         // "application/json"
     *   res.type("text/html");    // "text/html"
     *   res.type(".png");         // "image/png"
     * @endcode
     *
     * @since 0.1.0
     */
    Response& type(const std::string& typeStr) {
        std::string ct;
        if (typeStr.find('/') != std::string::npos) {
            ct = typeStr;
        } else {
            auto lookup = mime::contentType(typeStr);
            ct = lookup.value_or(typeStr);
        }
        raw_.setHeader("Content-Type", ct);
        return *this;
    }

    /**
     * @brief Alias for type().
     * @since 0.1.0
     */
    Response& contentType(const std::string& typeStr) {
        return type(typeStr);
    }

    // ── Links ────────────────────────────────────────────────────────

    /**
     * @brief Set the Link header.
     *
     * @param links Map of rel -> href pairs.
     * @return Reference to this Response.
     *
     * @par Example
     * @code{.cpp}
     *   res.links({{"next", "/users?page=2"}, {"last", "/users?page=5"}});
     * @endcode
     *
     * @since 0.1.0
     */
    Response& links(const std::map<std::string, std::string>& linksMap) {
        std::string header;
        for (const auto& [rel, href] : linksMap) {
            if (!header.empty()) header += ", ";
            header += "<" + href + ">; rel=\"" + rel + "\"";
        }
        auto existing = raw_.getHeader("Link");
        if (!existing.empty()) {
            header = existing + ", " + header;
        }
        raw_.setHeader("Link", header);
        return *this;
    }

    // ── Cookies ──────────────────────────────────────────────────────

    /**
     * @brief Set a cookie on the response.
     *
     * @param name Cookie name.
     * @param value Cookie value.
     * @param opts Cookie options.
     * @return Reference to this Response.
     *
     * @par Example
     * @code{.cpp}
     *   res.cookie("session", "abc123", {.httpOnly = true, .secure = true});
     * @endcode
     *
     * @since 0.1.0
     */
    Response& cookie(const std::string& name, const std::string& value,
                     const CookieOptions& opts = {}) {
        // Validate cookie name/value against CRLF injection
        detail::validateHeaderValue("Set-Cookie", name);
        detail::validateHeaderValue("Set-Cookie", value);

        cookie::SetCookie sc;
        sc.name = name;
        sc.value = value;
        sc.path = opts.path;
        sc.httpOnly = opts.httpOnly;
        sc.secure = opts.secure;
        if (opts.domain) sc.domain = *opts.domain;
        if (opts.maxAge) {
            sc.maxAge = static_cast<int>(
                std::chrono::duration_cast<std::chrono::seconds>(*opts.maxAge).count()
            );
        }
        if (opts.expires) sc.expires = *opts.expires;
        if (!opts.sameSite.empty()) sc.sameSite = opts.sameSite;

        auto headerVal = cookie::stringifySetCookie(sc);
        // Append to existing Set-Cookie headers
        raw_.appendHeader("Set-Cookie", headerVal);
        return *this;
    }

    /**
     * @brief Clear a cookie.
     *
     * @param name Cookie name.
     * @param opts Cookie options (path and domain must match the set cookie).
     * @return Reference to this Response.
     * @since 0.1.0
     */
    Response& clearCookie(const std::string& name,
                          const CookieOptions& opts = {}) {
        CookieOptions clearOpts = opts;
        clearOpts.maxAge = std::chrono::milliseconds(0);
        // Set expiry to epoch
        clearOpts.expires = std::chrono::system_clock::from_time_t(0);
        return cookie(name, "", clearOpts);
    }

    // ── Redirect ─────────────────────────────────────────────────────

    /**
     * @brief Redirect to a URL with status 302.
     *
     * @param url The redirect URL.
     * @since 0.1.0
     */
    void redirect(const std::string& url) {
        redirect(302, url);
    }

    /**
     * @brief Redirect to a URL with a specific status code.
     *
     * @param statusCode The redirect status code (301, 302, 303, 307, 308).
     * @param url The redirect URL.
     * @since 0.1.0
     */
    void redirect(int statusCode, const std::string& url) {
        auto loc = url;

        // Handle "back" redirect
        if (loc == "back") {
            if (req_) {
                auto referer = req_->get("referrer");
                if (!referer) referer = req_->get("referer");
                loc = referer.value_or("/");
            } else {
                loc = "/";
            }
        }

        // Set Location header
        raw_.setHeader("Location", detail::encodeUrl(loc));
        raw_.status(statusCode);

        // Send a simple HTML body for non-HEAD requests
        auto body = "<p>" + std::to_string(statusCode) + " Redirecting to <a href=\""
                    + detail::escapeHtml(loc) + "\">" + detail::escapeHtml(loc) + "</a></p>";
        raw_.setHeader("Content-Type", "text/html; charset=utf-8");
        raw_.setHeader("Content-Length", std::to_string(body.size()));

        if (req_ && req_->method() == "HEAD") {
            raw_.end();
        } else {
            raw_.end(body);
        }
    }

    // ── Location ─────────────────────────────────────────────────────

    /**
     * @brief Set the Location header.
     *
     * @param url The URL for the Location header.
     * @return Reference to this Response.
     * @since 0.1.0
     */
    Response& location(const std::string& url) {
        raw_.setHeader("Location", detail::encodeUrl(url));
        return *this;
    }

    // ── File Serving ─────────────────────────────────────────────────

    /**
     * @brief Send a file as the response.
     *
     * Supports Range requests (HTTP 206 Partial Content), conditional GET
     * (304 Not Modified via ETag and Last-Modified), Accept-Ranges, and
     * proper Content-Type detection.
     *
     * @param filePath The path to the file.
     * @param opts Options for file serving.
     * @param callback Optional callback for errors.
     *
     * @par Example
     * @code{.cpp}
     *   res.sendFile("/path/to/photo.jpg");
     *   res.sendFile("photo.jpg", {.root = "/uploads"});
     * @endcode
     *
     * @see https://expressjs.com/en/api.html#res.sendFile
     * @since 0.1.0
     */
    void sendFile(const std::string& filePath, const SendFileOptions& opts = {},
                  std::function<void(std::optional<std::string>)> callback = nullptr) {
        // Security: reject null bytes in path (prevents C-level path truncation)
        if (filePath.find('\0') != std::string::npos) {
            if (callback) {
                callback("Bad Request");
            } else {
                status(400);
                raw_.setHeader("Content-Type", "text/plain; charset=utf-8");
                raw_.end("Bad Request");
            }
            return;
        }

        // 1. Resolve the path
        std::string resolvedPath;
        if (!opts.root.empty()) {
            resolvedPath = path::resolve(opts.root, filePath);
        } else {
            resolvedPath = filePath;
        }

        // Security: canonicalize and verify path stays within root
        if (!opts.root.empty()) {
            auto resolvedRoot = path::resolve(opts.root);
            auto canonical = path::normalize(resolvedPath);
            if (canonical.find(resolvedRoot) != 0) {
                if (callback) {
                    callback("Forbidden");
                } else {
                    status(403);
                    raw_.setHeader("Content-Type", "text/plain; charset=utf-8");
                    raw_.end("Forbidden");
                }
                return;
            }
        }

        // 2. Security: check dotfiles (all path components)
        if (detail::hasDotfileComponent(resolvedPath)) {
            if (opts.dotfiles == "deny") {
                status(403);
                raw_.setHeader("Content-Type", "text/plain; charset=utf-8");
                raw_.end("Forbidden");
                if (callback) callback("Forbidden");
                return;
            } else if (opts.dotfiles == "ignore") {
                status(404);
                raw_.setHeader("Content-Type", "text/plain; charset=utf-8");
                raw_.end("Not Found");
                if (callback) callback("Not Found");
                return;
            }
        }

        try {
            // 3. Stat the file to get size and modification time
            auto stats = fs::statSync(resolvedPath);
            auto fileSize = stats.size;
            auto mtimeMs = stats.mtimeMs;

            // 4. Set Content-Type from extension
            auto ext = path::extname(resolvedPath);
            if (!ext.empty() && !raw_.hasHeader("Content-Type")) {
                auto ct = mime::contentType(ext);
                if (ct) {
                    raw_.setHeader("Content-Type", *ct);
                }
            }

            // 5. Set Last-Modified header from mtime
            if (opts.lastModified && !raw_.hasHeader("Last-Modified")) {
                raw_.setHeader("Last-Modified", detail::httpDate(mtimeMs));
            }

            // 6. Set ETag from stats (size + mtime)
            if (opts.etag && !raw_.hasHeader("ETag")) {
                raw_.setHeader("ETag", detail::statEtag(fileSize, mtimeMs));
            }

            // 7. Set Accept-Ranges
            if (!raw_.hasHeader("Accept-Ranges")) {
                raw_.setHeader("Accept-Ranges", "bytes");
            }

            // 8. Set custom headers
            for (const auto& [key, val] : opts.headers) {
                raw_.setHeader(key, val);
            }

            // 9. Set cache headers
            if (opts.maxAge) {
                auto maxAgeSec = std::chrono::duration_cast<std::chrono::seconds>(*opts.maxAge).count();
                raw_.setHeader("Cache-Control", "public, max-age=" + std::to_string(maxAgeSec));
            }

            // 10. Check conditional GET (If-None-Match + If-Modified-Since)
            if (req_ && req_->fresh()) {
                raw_.status(304);
                raw_.end();
                if (callback) callback(std::nullopt);
                return;
            }

            // 11. Handle Range requests
            if (req_ && fileSize > 0) {
                auto rangeHeader = req_->get("range");
                if (rangeHeader) {
                    auto ranges = detail::parseRange(fileSize, *rangeHeader);
                    if (!ranges.empty()) {
                        // Single range: 206 Partial Content
                        auto& range = ranges[0];
                        auto contentLength = range.end - range.start + 1;
                        raw_.status(206);
                        raw_.setHeader("Content-Range",
                            "bytes " + std::to_string(range.start) + "-" +
                            std::to_string(range.end) + "/" + std::to_string(fileSize));
                        raw_.setHeader("Content-Length", std::to_string(contentLength));

                        if (req_->method() == "HEAD") {
                            raw_.end();
                        } else {
                            // Read only the requested range
                            int fd = fs::openSync(resolvedPath, "r");
                            auto buf = Buffer::alloc(contentLength);
                            fs::readSync(fd, buf, 0, static_cast<ssize_t>(contentLength),
                                         static_cast<ssize_t>(range.start));
                            fs::closeSync(fd);
                            raw_.end(detail::bufferToRawString(buf));
                        }
                        if (callback) callback(std::nullopt);
                        return;
                    } else {
                        // Invalid or unsatisfiable range -> 416
                        raw_.status(416);
                        raw_.setHeader("Content-Range",
                            "bytes */" + std::to_string(fileSize));
                        raw_.setHeader("Content-Type", "text/plain; charset=utf-8");
                        raw_.end("Range Not Satisfiable");
                        if (callback) callback("Range Not Satisfiable");
                        return;
                    }
                }
            }

            // 12. Full file: 200 OK
            raw_.setHeader("Content-Length", std::to_string(fileSize));
            if (req_ && req_->method() == "HEAD") {
                raw_.end();
            } else {
                auto contentStr = fs::readFileSync(resolvedPath);
                raw_.end(contentStr);
            }
            if (callback) callback(std::nullopt);
        } catch (const std::exception& e) {
            if (callback) {
                callback(e.what());
            } else {
                throw;
            }
        }
    }

    /**
     * @brief Send a file as a download attachment.
     *
     * @param filePath The path to the file.
     * @param filename The download filename (defaults to basename of filePath).
     * @param opts Options for file serving.
     * @param callback Optional callback for errors.
     * @since 0.1.0
     */
    void download(const std::string& filePath,
                  const std::string& filename = "",
                  const SendFileOptions& opts = {},
                  std::function<void(std::optional<std::string>)> callback = nullptr) {
        auto name = filename.empty() ? path::basename(filePath) : filename;
        auto cd = detail::contentDisposition("attachment", name);
        raw_.setHeader("Content-Disposition", cd);
        sendFile(filePath, opts, std::move(callback));
    }

    /**
     * @brief Set the Content-Disposition header for attachment.
     *
     * @param filename Optional filename for the attachment.
     * @return Reference to this Response.
     * @since 0.1.0
     */
    Response& attachment(const std::string& filename = "") {
        if (!filename.empty()) {
            // Set Content-Type based on extension
            auto ext = path::extname(filename);
            if (!ext.empty()) {
                auto ct = mime::contentType(ext);
                if (ct) {
                    raw_.setHeader("Content-Type", *ct);
                }
            }
        }
        raw_.setHeader("Content-Disposition",
                       detail::contentDisposition("attachment", filename));
        return *this;
    }

    // ── Content Negotiation ──────────────────────────────────────────

    /**
     * @brief Respond based on accepted content type.
     *
     * @param obj Map of content-type to handler function.
     * @return Reference to this Response.
     *
     * @par Example
     * @code{.cpp}
     *   res.format({
     *       {"text/html", [&]() { res.send("<p>Hello</p>"); }},
     *       {"application/json", [&]() { res.json({{"msg", "Hello"}}); }},
     *       {"default", [&]() { res.sendStatus(406); }}
     *   });
     * @endcode
     *
     * @since 0.1.0
     */
    Response& format(const std::map<std::string, std::function<void()>>& obj) {
        if (!req_) {
            // No request context, try default
            auto defIt = obj.find("default");
            if (defIt != obj.end()) {
                defIt->second();
            } else {
                sendStatus(406);
            }
            return *this;
        }

        // Build list of types
        std::vector<std::string> types;
        for (const auto& [key, _] : obj) {
            if (key != "default") {
                types.push_back(key);
            }
        }

        auto accepted = req_->accepts(types);
        if (accepted) {
            vary("Accept");
            auto it = obj.find(*accepted);
            if (it != obj.end()) {
                it->second();
            }
        } else {
            auto defIt = obj.find("default");
            if (defIt != obj.end()) {
                defIt->second();
            } else {
                sendStatus(406);
            }
        }
        return *this;
    }

    // ── Template Rendering ───────────────────────────────────────────

    /**
     * @brief Render a view template.
     *
     * @param view The view name.
     * @param options Template variables.
     * @param callback Optional callback (error, rendered).
     * @since 0.1.0
     */
    void render(const std::string& view, const JsonValue& options = JsonObject{},
                std::function<void(std::optional<std::string>, std::string)> callback = nullptr);

    // ── Raw HTTP Access ──────────────────────────────────────────────

    /** @brief Get the raw HTTP server response. @since 0.1.0 */
    http::ServerResponse& raw() { return raw_; }

    /** @brief Get the raw HTTP server response (const). @since 0.1.0 */
    const http::ServerResponse& raw() const { return raw_; }

    // ── Context ──────────────────────────────────────────────────────

    /** @brief Get the application. @since 0.1.0 */
    Application* app() const { return app_; }

    /** @brief Get the associated request. @since 0.1.0 */
    Request* req() const { return req_; }

    /** @brief Set the associated request (internal use). @since 0.1.0 */
    void setReq(Request* req) { req_ = req; }

private:
    /**
     * @brief Apply compression to a response body if compression middleware is active.
     *
     * Checks `locals` for compression parameters set by the compression middleware.
     * If compression is appropriate (body exceeds threshold, content type is
     * compressible), compresses the body and sets Content-Encoding header.
     *
     * @param body The uncompressed response body.
     * @return The compressed body, or std::nullopt if compression was not applied.
     * @since 0.1.0
     */
    std::optional<std::string> applyCompression_(const std::string& body) {
        // Check if compression middleware stored encoding info
        if (!locals.isObject()) return std::nullopt;

        auto& obj = locals.asObject();
        auto encodingIt = obj.find("_compression_encoding");
        if (encodingIt == obj.end() || !encodingIt->second.isString()) {
            return std::nullopt;
        }

        auto encoding = encodingIt->second.asString();
        if (encoding.empty()) return std::nullopt;

        // Get threshold
        int threshold = 1024;
        auto threshIt = obj.find("_compression_threshold");
        if (threshIt != obj.end() && threshIt->second.isNumber()) {
            threshold = static_cast<int>(threshIt->second.asNumber());
        }

        // Check body size against threshold
        if (body.size() < static_cast<size_t>(threshold)) {
            return std::nullopt;
        }

        // Get Content-Type and check if it's compressible
        auto contentType = raw_.getHeader("Content-Type");

        // Build filter vector from locals
        std::vector<std::string> filter;
        auto filterIt = obj.find("_compression_filter");
        if (filterIt != obj.end() && filterIt->second.isArray()) {
            for (const auto& f : filterIt->second.asArray()) {
                if (f.isString()) {
                    filter.push_back(f.asString());
                }
            }
        }

        // Check if content type is compressible
        if (!isCompressible_(contentType, filter)) {
            return std::nullopt;
        }

        // Don't compress if Content-Encoding is already set
        auto existingEncoding = raw_.getHeader("Content-Encoding");
        if (!existingEncoding.empty()) {
            return std::nullopt;
        }

        // Get compression level
        int level = -1;
        auto levelIt = obj.find("_compression_level");
        if (levelIt != obj.end() && levelIt->second.isNumber()) {
            level = static_cast<int>(levelIt->second.asNumber());
        }

        // Perform compression
        try {
            Buffer input = Buffer::from(body);
            Buffer compressed;

            if (encoding == "gzip") {
                zlib::Options opts;
                if (level >= 0) opts.level = level;
                compressed = zlib::gzipSync(input, opts);
            } else if (encoding == "deflate") {
                zlib::Options opts;
                if (level >= 0) opts.level = level;
                compressed = zlib::deflateSync(input, opts);
            } else if (encoding == "br") {
                zlib::BrotliOptions opts;
                if (level >= 0) {
                    opts.params[BROTLI_PARAM_QUALITY] = level;
                }
                compressed = zlib::brotliCompressSync(input, opts);
            } else {
                return std::nullopt;
            }

            // Set Content-Encoding header
            raw_.setHeader("Content-Encoding", encoding);

            // Remove ETag since body has changed
            // (Express's compression module does this)
            raw_.removeHeader("ETag");

            return detail::bufferToRawString(compressed);
        } catch (...) {
            // If compression fails, send uncompressed
            return std::nullopt;
        }
    }

    /**
     * @brief Check whether a content type is compressible.
     *
     * @param contentType The Content-Type header value.
     * @param filter User-provided filter list.
     * @return True if the content type should be compressed.
     * @since 0.1.0
     */
    static bool isCompressible_(const std::string& contentType,
                                const std::vector<std::string>& filter) {
        if (contentType.empty()) return false;

        // Extract the MIME type (strip charset and parameters)
        std::string mimeType = contentType;
        auto semicolon = mimeType.find(';');
        if (semicolon != std::string::npos) {
            mimeType = mimeType.substr(0, semicolon);
        }
        while (!mimeType.empty() && mimeType.back() == ' ') mimeType.pop_back();

        // Lowercase for comparison
        std::string lower = mimeType;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (!filter.empty()) {
            for (const auto& entry : filter) {
                std::string lowerEntry = entry;
                std::transform(lowerEntry.begin(), lowerEntry.end(),
                               lowerEntry.begin(),
                               [](unsigned char c) { return std::tolower(c); });

                if (!lowerEntry.empty() && lowerEntry.back() == '/') {
                    if (lower.substr(0, lowerEntry.size()) == lowerEntry) {
                        return true;
                    }
                } else if (lowerEntry.size() >= 2 &&
                           lowerEntry.substr(lowerEntry.size() - 2) == "/*") {
                    auto prefix = lowerEntry.substr(0, lowerEntry.size() - 1);
                    if (lower.substr(0, prefix.size()) == prefix) {
                        return true;
                    }
                } else if (lower == lowerEntry) {
                    return true;
                }
            }
            return false;
        }

        // Default compressible types
        if (lower.substr(0, 5) == "text/") return true;
        if (lower == "application/json") return true;
        if (lower == "application/javascript") return true;
        if (lower == "application/x-javascript") return true;
        if (lower == "application/xml") return true;
        if (lower == "application/xhtml+xml") return true;
        if (lower == "application/rss+xml") return true;
        if (lower == "application/atom+xml") return true;
        if (lower == "application/x-www-form-urlencoded") return true;
        if (lower == "image/svg+xml") return true;

        return false;
    }

    http::ServerResponse& raw_;
    Application* app_ = nullptr;
    Request* req_ = nullptr;
};

} // namespace express
} // namespace polycpp
