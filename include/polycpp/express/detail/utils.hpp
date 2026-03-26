#pragma once

/**
 * @file detail/utils.hpp
 * @brief Small inline utility functions for Express internals.
 *
 * Ports of: escape-html, encodeurl, cookie-signature, vary, etag, fresh,
 * range-parser, bytes, ms, content-disposition, on-finished, final-handler,
 * proxy-addr, forwarded.
 *
 * @since 0.1.0
 */

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <polycpp/buffer.hpp>
#include <polycpp/core/json.hpp>
#include <polycpp/core/number.hpp>
#include <polycpp/crypto.hpp>
#include <polycpp/http.hpp>
#include <polycpp/path.hpp>
#include <polycpp/url.hpp>

namespace polycpp {
namespace express {
namespace detail {

// ══════════════════════════════════════════════════════════════════════
// escape-html: Escape special HTML characters
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Escape special HTML characters in a string.
 *
 * Replaces &, <, >, ", ' with their HTML entity equivalents.
 *
 * @param str The input string.
 * @return The escaped string.
 * @since 0.1.0
 */
inline std::string escapeHtml(const std::string& str) {
    std::string result;
    result.reserve(str.size() + str.size() / 8);
    for (char c : str) {
        switch (c) {
            case '&':  result += "&amp;"; break;
            case '<':  result += "&lt;"; break;
            case '>':  result += "&gt;"; break;
            case '"':  result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default:   result += c; break;
        }
    }
    return result;
}

// ══════════════════════════════════════════════════════════════════════
// encodeurl: Encode a URL, escaping characters not allowed in URLs
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Encode a URL by percent-encoding characters that are not URL-safe.
 *
 * Unlike full percent encoding, this preserves characters that are valid
 * in URLs: A-Z, a-z, 0-9, and !#$&'()*+,-./:;=?@_~
 *
 * @param url The input URL string.
 * @return The encoded URL.
 * @since 0.1.0
 */
inline std::string encodeUrl(const std::string& url) {
    static const char* safe = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!#$&'()*+,-./:;=?@_~";
    static bool safeTable[256] = {};
    static bool initialized = false;
    if (!initialized) {
        for (const char* p = safe; *p; ++p) {
            safeTable[static_cast<unsigned char>(*p)] = true;
        }
        // Also keep percent for already-encoded sequences
        safeTable[static_cast<unsigned char>('%')] = true;
        initialized = true;
    }

    std::string result;
    result.reserve(url.size());
    static const char hex[] = "0123456789ABCDEF";
    for (unsigned char c : url) {
        if (safeTable[c]) {
            result += static_cast<char>(c);
        } else {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 0x0F];
        }
    }
    return result;
}

// ══════════════════════════════════════════════════════════════════════
// cookie-signature: Sign and unsign cookie values using HMAC-SHA256
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Sign a cookie value with HMAC-SHA256.
 *
 * @param val The value to sign.
 * @param secret The secret key.
 * @return The signed value in format "val.signature".
 * @since 0.1.0
 */
inline std::string cookieSign(const std::string& val, const std::string& secret) {
    auto hmac = crypto::createHmac("sha256", secret);
    hmac.update(val);
    auto sig = hmac.digest("base64");
    // Remove base64 trailing '=' padding
    while (!sig.empty() && sig.back() == '=') {
        sig.pop_back();
    }
    return val + "." + sig;
}

/**
 * @brief Unsign a signed cookie value.
 *
 * @param signedVal The signed value.
 * @param secret The secret key.
 * @return The original value if signature is valid, std::nullopt otherwise.
 * @since 0.1.0
 */
inline std::optional<std::string> cookieUnsign(const std::string& signedVal,
                                                const std::string& secret) {
    auto dotPos = signedVal.rfind('.');
    if (dotPos == std::string::npos) {
        return std::nullopt;
    }
    auto val = signedVal.substr(0, dotPos);
    auto expected = cookieSign(val, secret);
    if (expected == signedVal) {
        return val;
    }
    return std::nullopt;
}

// ══════════════════════════════════════════════════════════════════════
// etag: Generate an ETag for a string body or stat result
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Generate a strong ETag for a body string.
 *
 * @param body The response body.
 * @return ETag string (quoted).
 * @since 0.1.0
 */
inline std::string generateETag(const std::string& body) {
    if (body.empty()) {
        // Special case: empty body
        return "\"0-2jmj7l5rSw0yVb/vlWAYkK/YBwk\"";
    }
    auto hash = crypto::createHash("sha1");
    hash.update(body);
    auto digest = hash.digest("base64");
    // Trim trailing '='
    while (!digest.empty() && digest.back() == '=') {
        digest.pop_back();
    }
    // Truncate to 27 chars like Node.js etag
    if (digest.size() > 27) {
        digest = digest.substr(0, 27);
    }
    return "\"" + Number::toString(static_cast<double>(body.size()), 16) + "-" + digest + "\"";
}

/**
 * @brief Generate a weak ETag for a body string.
 *
 * @param body The response body.
 * @return Weak ETag string.
 * @since 0.1.0
 */
inline std::string generateWeakETag(const std::string& body) {
    return "W/" + generateETag(body);
}

// ══════════════════════════════════════════════════════════════════════
// fresh: Check if a cache is still fresh (for 304 responses)
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Check freshness of a cache based on request/response headers.
 *
 * @param reqHeaders Request headers (If-None-Match, If-Modified-Since).
 * @param resHeaders Response headers (ETag, Last-Modified).
 * @return true if the cache is fresh (304 should be sent).
 * @since 0.1.0
 */
inline bool isFresh(const std::map<std::string, std::string>& reqHeaders,
                    const std::map<std::string, std::string>& resHeaders) {
    // Check If-None-Match (ETag)
    auto inmIt = reqHeaders.find("if-none-match");
    auto etagIt = resHeaders.find("etag");

    if (inmIt != reqHeaders.end() && etagIt != resHeaders.end()) {
        auto inm = inmIt->second;
        auto etag = etagIt->second;

        if (inm == "*") {
            return true;
        }

        // Parse the If-None-Match list
        // Simple check: see if etag appears in the list
        // Remove weak validator prefix for comparison
        auto normalize = [](const std::string& tag) -> std::string {
            if (tag.size() >= 2 && tag[0] == 'W' && tag[1] == '/') {
                return tag.substr(2);
            }
            return tag;
        };

        auto normalizedEtag = normalize(etag);

        // Split by comma and check each
        std::string token;
        std::istringstream stream(inm);
        while (std::getline(stream, token, ',')) {
            // Trim whitespace
            auto start = token.find_first_not_of(" \t");
            auto end = token.find_last_not_of(" \t");
            if (start != std::string::npos) {
                auto trimmed = token.substr(start, end - start + 1);
                if (normalize(trimmed) == normalizedEtag) {
                    return true;
                }
            }
        }
        return false;
    }

    // Check If-Modified-Since
    auto imsIt = reqHeaders.find("if-modified-since");
    auto lmIt = resHeaders.find("last-modified");

    if (imsIt != reqHeaders.end() && lmIt != resHeaders.end()) {
        // Simple string comparison -- both should be HTTP-date format
        // If Last-Modified <= If-Modified-Since, the resource hasn't changed
        return lmIt->second <= imsIt->second;
    }

    return false;
}

// ══════════════════════════════════════════════════════════════════════
// vary: Manipulate the Vary header
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Append a field to the Vary header of a response.
 *
 * @param res The server response.
 * @param field The field to add to Vary.
 * @since 0.1.0
 */
inline void varyAppend(http::ServerResponse& res, const std::string& field) {
    auto existing = res.getHeader("Vary");
    if (existing == "*") {
        return; // Already varies on everything
    }
    if (field == "*") {
        res.setHeader("Vary", "*");
        return;
    }
    if (existing.empty()) {
        res.setHeader("Vary", field);
    } else {
        // Check if field is already present (case-insensitive)
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                          [](unsigned char c) { return std::tolower(c); });
            return s;
        };
        auto lowerExisting = lower(existing);
        auto lowerField = lower(field);

        // Simple check: tokenize existing by comma
        std::istringstream stream(lowerExisting);
        std::string token;
        while (std::getline(stream, token, ',')) {
            auto start = token.find_first_not_of(" \t");
            auto end = token.find_last_not_of(" \t");
            if (start != std::string::npos) {
                auto trimmed = token.substr(start, end - start + 1);
                if (trimmed == lowerField) {
                    return; // Already present
                }
            }
        }
        res.setHeader("Vary", existing + ", " + field);
    }
}

// ══════════════════════════════════════════════════════════════════════
// range-parser: Parse HTTP Range header
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief A single range specification.
 * @since 0.1.0
 */
struct RangeSpec {
    size_t start; ///< Start byte offset (inclusive).
    size_t end;   ///< End byte offset (inclusive).
};

/**
 * @brief Parse an HTTP Range header.
 *
 * @param size The total size of the resource.
 * @param header The Range header value (e.g., "bytes=0-499").
 * @return A vector of RangeSpec, or empty if the header is invalid or unsatisfiable.
 * @since 0.1.0
 */
inline std::vector<RangeSpec> parseRange(size_t size, const std::string& header) {
    // Must start with "bytes="
    if (header.substr(0, 6) != "bytes=") {
        return {};
    }

    std::vector<RangeSpec> ranges;
    auto specs = header.substr(6);

    std::istringstream stream(specs);
    std::string part;
    while (std::getline(stream, part, ',')) {
        // Trim
        auto start = part.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        part = part.substr(start);
        auto end = part.find_last_not_of(" \t");
        if (end != std::string::npos) part = part.substr(0, end + 1);

        auto dashPos = part.find('-');
        if (dashPos == std::string::npos) return {}; // Invalid

        auto startStr = part.substr(0, dashPos);
        auto endStr = part.substr(dashPos + 1);

        size_t rangeStart, rangeEnd;

        if (startStr.empty()) {
            // Suffix range: -500 means last 500 bytes
            auto suffix = Number::parseInt(endStr);
            if (Number::isNaN(suffix) || suffix <= 0) return {};
            rangeStart = size > static_cast<size_t>(suffix) ? size - static_cast<size_t>(suffix) : 0;
            rangeEnd = size - 1;
        } else if (endStr.empty()) {
            // Open range: 500- means from 500 to end
            auto s = Number::parseInt(startStr);
            if (Number::isNaN(s) || s < 0) return {};
            rangeStart = static_cast<size_t>(s);
            rangeEnd = size - 1;
        } else {
            auto s = Number::parseInt(startStr);
            auto e = Number::parseInt(endStr);
            if (Number::isNaN(s) || Number::isNaN(e) || s < 0 || e < 0) return {};
            rangeStart = static_cast<size_t>(s);
            rangeEnd = static_cast<size_t>(e);
        }

        // Validate
        if (rangeStart > rangeEnd || rangeStart >= size) continue;
        if (rangeEnd >= size) rangeEnd = size - 1;

        ranges.push_back({rangeStart, rangeEnd});
    }

    return ranges;
}

// ══════════════════════════════════════════════════════════════════════
// bytes: Parse byte strings like "1kb", "5mb"
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Parse a byte-size string into number of bytes.
 *
 * Supports: b, kb, mb, gb, tb. Case insensitive.
 *
 * @param str The byte string (e.g., "100kb", "5mb", "1024").
 * @return The number of bytes, or -1 on parse failure.
 * @since 0.1.0
 */
inline int64_t parseBytes(const std::string& str) {
    if (str.empty()) return -1;

    // Try parsing as plain number first
    auto val = Number::parseFloat(str);
    if (!Number::isNaN(val) && str.find_first_not_of("0123456789.") == std::string::npos) {
        return static_cast<int64_t>(val);
    }

    // Find the numeric part and the unit
    size_t unitStart = 0;
    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];
        if (c == '.' || (c >= '0' && c <= '9')) {
            unitStart = i + 1;
        } else {
            break;
        }
    }

    if (unitStart == 0) return -1;

    auto numStr = str.substr(0, unitStart);
    auto unit = str.substr(unitStart);

    // Trim whitespace from unit
    while (!unit.empty() && unit[0] == ' ') unit = unit.substr(1);

    val = Number::parseFloat(numStr);
    if (Number::isNaN(val)) return -1;

    // Lowercase the unit
    std::string lowerUnit;
    for (char c : unit) lowerUnit += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (lowerUnit == "b" || lowerUnit.empty()) {
        return static_cast<int64_t>(val);
    } else if (lowerUnit == "kb") {
        return static_cast<int64_t>(val * 1024);
    } else if (lowerUnit == "mb") {
        return static_cast<int64_t>(val * 1024 * 1024);
    } else if (lowerUnit == "gb") {
        return static_cast<int64_t>(val * 1024 * 1024 * 1024);
    } else if (lowerUnit == "tb") {
        return static_cast<int64_t>(val * 1024.0 * 1024 * 1024 * 1024);
    }

    return -1;
}

// ══════════════════════════════════════════════════════════════════════
// ms: Parse time strings like "1d", "2h", "30s"
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Parse a time string into milliseconds.
 *
 * Supports: ms, s, m, h, d, w, y.
 *
 * @param str The time string (e.g., "1d", "2h", "30s", "500ms").
 * @return The number of milliseconds, or -1 on parse failure.
 * @since 0.1.0
 */
inline int64_t parseMs(const std::string& str) {
    if (str.empty()) return -1;

    // Find the numeric part and the unit
    size_t unitStart = 0;
    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];
        if (c == '.' || (c >= '0' && c <= '9') || (i == 0 && c == '-')) {
            unitStart = i + 1;
        } else {
            break;
        }
    }

    if (unitStart == 0) return -1;

    auto numStr = str.substr(0, unitStart);
    auto unit = str.substr(unitStart);

    // Trim whitespace from unit
    while (!unit.empty() && unit[0] == ' ') unit = unit.substr(1);

    auto val = Number::parseFloat(numStr);
    if (Number::isNaN(val)) return -1;

    // Lowercase the unit
    std::string lowerUnit;
    for (char c : unit) lowerUnit += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (lowerUnit == "ms" || lowerUnit.empty()) {
        return static_cast<int64_t>(val);
    } else if (lowerUnit == "s") {
        return static_cast<int64_t>(val * 1000);
    } else if (lowerUnit == "m") {
        return static_cast<int64_t>(val * 60 * 1000);
    } else if (lowerUnit == "h") {
        return static_cast<int64_t>(val * 60 * 60 * 1000);
    } else if (lowerUnit == "d") {
        return static_cast<int64_t>(val * 24 * 60 * 60 * 1000);
    } else if (lowerUnit == "w") {
        return static_cast<int64_t>(val * 7 * 24 * 60 * 60 * 1000);
    } else if (lowerUnit == "y") {
        return static_cast<int64_t>(val * 365.25 * 24 * 60 * 60 * 1000);
    }

    return -1;
}

// ══════════════════════════════════════════════════════════════════════
// content-disposition: Generate Content-Disposition header
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Generate a Content-Disposition header value.
 *
 * @param type Disposition type: "inline" or "attachment".
 * @param filename Optional filename.
 * @return The Content-Disposition header value.
 * @since 0.1.0
 */
inline std::string contentDisposition(const std::string& type = "attachment",
                                       const std::string& filename = "") {
    if (filename.empty()) {
        return type;
    }

    // Check if filename needs encoding (non-ASCII or special chars)
    bool needsEncoding = false;
    for (unsigned char c : filename) {
        if (c > 127 || c == '"' || c == '\\') {
            needsEncoding = true;
            break;
        }
    }

    if (needsEncoding) {
        // Use RFC 5987 encoding (filename*=UTF-8''...)
        std::string encoded;
        static const char hex[] = "0123456789ABCDEF";
        for (unsigned char c : filename) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == '~') {
                encoded += static_cast<char>(c);
            } else {
                encoded += '%';
                encoded += hex[c >> 4];
                encoded += hex[c & 0x0F];
            }
        }
        return type + "; filename*=UTF-8''" + encoded;
    } else {
        return type + "; filename=\"" + filename + "\"";
    }
}

// ══════════════════════════════════════════════════════════════════════
// forwarded: Parse X-Forwarded-For header
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Parse the X-Forwarded-For header into a list of addresses.
 *
 * @param header The X-Forwarded-For header value.
 * @return Vector of IP address strings.
 * @since 0.1.0
 */
inline std::vector<std::string> parseForwarded(const std::string& header) {
    std::vector<std::string> addrs;
    if (header.empty()) return addrs;

    std::istringstream stream(header);
    std::string token;
    while (std::getline(stream, token, ',')) {
        // Trim whitespace
        auto start = token.find_first_not_of(" \t");
        auto end = token.find_last_not_of(" \t");
        if (start != std::string::npos) {
            addrs.push_back(token.substr(start, end - start + 1));
        }
    }
    return addrs;
}

// ══════════════════════════════════════════════════════════════════════
// String helpers
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Convert a string to lowercase.
 * @param s Input string.
 * @return Lowercased copy.
 * @since 0.1.0
 */
inline std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                  [](unsigned char c) { return std::tolower(c); });
    return result;
}

/**
 * @brief Trim whitespace from both ends of a string.
 * @param s Input string.
 * @return Trimmed copy.
 * @since 0.1.0
 */
inline std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

} // namespace detail
} // namespace express
} // namespace polycpp
