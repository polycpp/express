#pragma once

/**
 * @file middleware/compression.hpp
 * @brief Compression middleware for Express responses.
 *
 * C++ port of npm compression (https://github.com/expressjs/compression).
 * Compresses response bodies using gzip, deflate, or brotli based on
 * the client's Accept-Encoding header.
 *
 * The middleware sets compression metadata on `res.locals` so that
 * `Response::send()` can compress the body before writing.
 *
 * @par Example
 * @code{.cpp}
 *   #include <polycpp/express/express.hpp>
 *
 *   auto app = express();
 *
 *   // Enable compression with defaults
 *   app.use(express::compress());
 *
 *   // Enable compression with custom options
 *   app.use(express::compress({.threshold = 512, .level = 6}));
 * @endcode
 *
 * @see https://www.npmjs.com/package/compression
 * @see https://github.com/expressjs/compression
 * @since 0.1.0
 */

#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <polycpp/buffer.hpp>
#include <polycpp/core/json.hpp>
#include <polycpp/zlib/zlib.hpp>

#include <polycpp/express/types.hpp>
#include <polycpp/express/request.hpp>
#include <polycpp/express/response.hpp>

namespace polycpp {
namespace express {

// ══════════════════════════════════════════════════════════════════════
// CompressionOptions
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Configuration options for the compression middleware.
 *
 * Mirrors the npm `compression` package options object.
 *
 * | Option    | Type                   | Default                               |
 * |-----------|------------------------|---------------------------------------|
 * | level     | int                    | -1 (zlib default)                     |
 * | threshold | int                    | 1024 (bytes)                          |
 * | filter    | vector<string>         | (empty = use default compressible types) |
 *
 * @see https://github.com/expressjs/compression#options
 * @since 0.1.0
 */
struct CompressionOptions {
    /**
     * @brief Compression level (-1 = default, 0 = none, 9 = best).
     *
     * Passed to zlib/brotli as the compression level.
     * -1 uses the default level for the chosen algorithm.
     *
     * @since 0.1.0
     */
    int level = -1;

    /**
     * @brief Minimum response body size in bytes to compress.
     *
     * Responses smaller than this threshold are sent uncompressed.
     * Default is 1024 bytes (matching npm compression's default).
     *
     * @since 0.1.0
     */
    int threshold = 1024;

    /**
     * @brief Content types to compress.
     *
     * When empty (default), the middleware compresses types that are
     * generally considered compressible: text/*, application/json,
     * application/javascript, application/xml, and
     * application/x-www-form-urlencoded.
     *
     * When non-empty, only content types matching one of the filter
     * entries are compressed. Entries can be exact types (e.g.,
     * "application/json") or wildcard prefixes (e.g., "text/").
     *
     * @since 0.1.0
     */
    std::vector<std::string> filter;
};

// ══════════════════════════════════════════════════════════════════════
// Internal helpers
// ══════════════════════════════════════════════════════════════════════

namespace detail {

/**
 * @brief Check whether a content type should be compressed.
 *
 * @param contentType The Content-Type header value.
 * @param filter User-provided filter list. If empty, uses defaults.
 * @return True if the content type is compressible.
 * @since 0.1.0
 */
inline bool shouldCompress(const std::string& contentType,
                           const std::vector<std::string>& filter) {
    if (contentType.empty()) return false;

    // Extract the MIME type (strip charset and parameters)
    std::string mimeType = contentType;
    auto semicolon = mimeType.find(';');
    if (semicolon != std::string::npos) {
        mimeType = mimeType.substr(0, semicolon);
    }
    // Trim whitespace
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

            // Wildcard prefix: "text/" matches "text/html", "text/plain", etc.
            if (!lowerEntry.empty() && lowerEntry.back() == '/') {
                if (lower.substr(0, lowerEntry.size()) == lowerEntry) {
                    return true;
                }
            }
            // Exact match or prefix match (e.g., "text/*")
            else if (lowerEntry.size() >= 2 &&
                     lowerEntry.substr(lowerEntry.size() - 2) == "/*") {
                auto prefix = lowerEntry.substr(0, lowerEntry.size() - 1);
                if (lower.substr(0, prefix.size()) == prefix) {
                    return true;
                }
            }
            // Exact match
            else if (lower == lowerEntry) {
                return true;
            }
        }
        return false;
    }

    // Default compressible types
    // Compress: text/*, application/json, application/javascript,
    //           application/xml, application/x-www-form-urlencoded,
    //           application/xhtml+xml, application/rss+xml,
    //           application/atom+xml, image/svg+xml
    // Don't compress: image/* (except svg+xml), audio/*, video/*,
    //                 application/octet-stream, application/zip,
    //                 application/pdf, application/gzip

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

/**
 * @brief Negotiate the best encoding from Accept-Encoding header.
 *
 * Parses the Accept-Encoding header and returns the preferred encoding
 * that we support, in order of preference: br, gzip, deflate.
 *
 * @param acceptEncoding The Accept-Encoding header value.
 * @return The chosen encoding ("br", "gzip", "deflate"), or empty string
 *         if no supported encoding is accepted.
 * @since 0.1.0
 */
inline std::string negotiateEncoding(const std::string& acceptEncoding) {
    if (acceptEncoding.empty()) return "";

    // Parse encoding tokens properly: split by comma, extract name and quality
    struct EncodingEntry {
        std::string name;
        double quality = 1.0;
    };

    std::vector<EncodingEntry> entries;
    std::istringstream stream(acceptEncoding);
    std::string token;
    while (std::getline(stream, token, ',')) {
        // Trim whitespace
        auto start = token.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        auto end = token.find_last_not_of(" \t");
        token = token.substr(start, end - start + 1);

        EncodingEntry entry;
        // Check for quality parameter (;q=...)
        auto semiPos = token.find(';');
        if (semiPos != std::string::npos) {
            entry.name = token.substr(0, semiPos);
            // Trim the name
            auto nameEnd = entry.name.find_last_not_of(" \t");
            if (nameEnd != std::string::npos) {
                entry.name = entry.name.substr(0, nameEnd + 1);
            }
            // Parse q value
            auto qPart = token.substr(semiPos + 1);
            auto qPos = qPart.find("q=");
            if (qPos == std::string::npos) qPos = qPart.find("Q=");
            if (qPos != std::string::npos) {
                try {
                    entry.quality = std::stod(qPart.substr(qPos + 2));
                } catch (...) {
                    entry.quality = 1.0;
                }
            }
        } else {
            entry.name = token;
        }

        // Lowercase the name
        std::transform(entry.name.begin(), entry.name.end(), entry.name.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        // Skip encodings with q=0
        if (entry.quality > 0) {
            entries.push_back(std::move(entry));
        }
    }

    // Find the best supported encoding by quality, preferring br > gzip > deflate
    struct Candidate {
        std::string name;
        double quality;
        int priority; // lower = higher priority (br=0, gzip=1, deflate=2)
    };

    std::vector<Candidate> candidates;
    for (const auto& e : entries) {
        if (e.name == "br") {
            candidates.push_back({e.name, e.quality, 0});
        } else if (e.name == "gzip") {
            candidates.push_back({e.name, e.quality, 1});
        } else if (e.name == "deflate") {
            candidates.push_back({e.name, e.quality, 2});
        } else if (e.name == "*") {
            // Wildcard matches all supported encodings not explicitly listed
            candidates.push_back({"gzip", e.quality, 1});
        }
    }

    if (candidates.empty()) return "";

    // Sort by quality (descending), then by priority (ascending)
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.quality != b.quality) return a.quality > b.quality;
        return a.priority < b.priority;
    });

    return candidates[0].name;
}

/**
 * @brief Compress a body string using the specified encoding.
 *
 * @param body The uncompressed body.
 * @param encoding The encoding to use ("gzip", "deflate", or "br").
 * @param level The compression level (-1 for default).
 * @return The compressed body as a string, or std::nullopt on failure.
 * @since 0.1.0
 */
inline std::optional<std::string> compressBody(const std::string& body,
                                               const std::string& encoding,
                                               int level) {
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

        return compressed.toString("latin1");
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace detail

// ══════════════════════════════════════════════════════════════════════
// compress() factory
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Create a compression middleware handler.
 *
 * Returns a MiddlewareHandler that negotiates content encoding and stores
 * compression parameters in `res.locals` for `Response::send()` to apply.
 *
 * The middleware:
 * 1. Checks the Accept-Encoding request header.
 * 2. Negotiates the best encoding (br > gzip > deflate).
 * 3. Adds `Vary: Accept-Encoding` to the response.
 * 4. Stores encoding info in `res.locals` for send() to use.
 *
 * @param opts Compression configuration options.
 * @return A MiddlewareHandler suitable for app.use().
 *
 * @par Example
 * @code{.cpp}
 *   // Default compression
 *   app.use(compress());
 *
 *   // Custom threshold
 *   app.use(compress({.threshold = 0}));  // Compress everything
 *
 *   // Custom level
 *   app.use(compress({.level = 6}));
 * @endcode
 *
 * @see https://github.com/expressjs/compression
 * @since 0.1.0
 */
inline MiddlewareHandler compressionMiddleware(const CompressionOptions& opts = {}) {
    return [opts](Request& req, Response& res, NextFunction next) {
        // 1. Check Accept-Encoding header
        auto acceptEncoding = req.get("accept-encoding").value_or("");

        // 2. Negotiate encoding
        auto encoding = detail::negotiateEncoding(acceptEncoding);

        // 3. Always add Vary: Accept-Encoding (even if not compressing)
        res.vary("Accept-Encoding");

        if (encoding.empty()) {
            // Client doesn't accept any supported encoding
            next({});
            return;
        }

        // 4. Store compression parameters in res.locals for send() to use
        res.locals.asObject()["_compression_encoding"] = encoding;
        res.locals.asObject()["_compression_threshold"] = static_cast<double>(opts.threshold);
        res.locals.asObject()["_compression_level"] = static_cast<double>(opts.level);

        // Store the filter as a JSON array for send() to reference
        JsonArray filterArr;
        for (const auto& f : opts.filter) {
            filterArr.push_back(f);
        }
        res.locals.asObject()["_compression_filter"] = filterArr;

        next({});
    };
}

} // namespace express
} // namespace polycpp
