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
#include <variant>
#include <vector>

#include <polycpp/buffer.hpp>
#include <polycpp/core/json.hpp>
#include <polycpp/core/number.hpp>
#include <polycpp/crypto.hpp>
#include <polycpp/http.hpp>
#include <polycpp/ipaddr/detail/aggregator.hpp>
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

// ══════════════════════════════════════════════════════════════════════
// Trust function type and proxy-addr: Determine client IP behind proxies
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Trust function type: takes an IP string and hop index, returns bool.
 *
 * Used to determine which proxy addresses are trusted when resolving
 * the client IP from X-Forwarded-For headers.
 *
 * @since 0.1.0
 */
using TrustFunction = std::function<bool(const std::string& addr, int hopIndex)>;

/**
 * @brief Pre-defined IP ranges for named trust specifications.
 *
 * Matches proxy-addr's IP_RANGES: "loopback", "linklocal", "uniquelocal".
 *
 * @since 0.1.0
 */
inline const std::map<std::string, std::vector<std::string>>& ipRanges() {
    static const std::map<std::string, std::vector<std::string>> ranges = {
        {"loopback",    {"127.0.0.1/8", "::1/128"}},
        {"linklocal",   {"169.254.0.0/16", "fe80::/10"}},
        {"uniquelocal", {"10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "fc00::/7"}}
    };
    return ranges;
}

/**
 * @brief A parsed subnet: IP address + prefix length.
 * @since 0.1.0
 */
struct Subnet {
    std::variant<ipaddr::IPv4, ipaddr::IPv6> ip;
    int prefix;
};

/**
 * @brief Parse an IP or CIDR notation string into a Subnet.
 *
 * Handles plain IPs (e.g. "10.0.0.1"), CIDR (e.g. "10.0.0.0/8"),
 * and IPv4-mapped IPv6 addresses (converted to IPv4).
 *
 * @param notation The IP/CIDR string.
 * @return The parsed Subnet.
 * @throws std::invalid_argument If the notation is not valid.
 * @since 0.1.0
 */
inline Subnet parseIpNotation(const std::string& notation) {
    auto slashPos = notation.rfind('/');
    auto ipStr = (slashPos != std::string::npos)
        ? notation.substr(0, slashPos)
        : notation;

    if (!ipaddr::isValid(ipStr)) {
        throw std::invalid_argument("invalid IP address: " + ipStr);
    }

    auto ip = ipaddr::parse(ipStr);

    // If it's an IPv4-mapped IPv6 without CIDR, convert to IPv4
    if (slashPos == std::string::npos) {
        if (auto* v6 = std::get_if<ipaddr::IPv6>(&ip)) {
            if (v6->isIPv4MappedAddress()) {
                ip = v6->toIPv4Address();
            }
        }
    }

    int maxPrefix = std::holds_alternative<ipaddr::IPv6>(ip) ? 128 : 32;

    int prefix;
    if (slashPos == std::string::npos) {
        prefix = maxPrefix;
    } else {
        auto rangeStr = notation.substr(slashPos + 1);
        // Check if it's all digits
        bool allDigits = !rangeStr.empty() &&
            std::all_of(rangeStr.begin(), rangeStr.end(),
                        [](char c) { return c >= '0' && c <= '9'; });
        if (allDigits) {
            prefix = std::stoi(rangeStr);
        } else if (std::holds_alternative<ipaddr::IPv4>(ip) &&
                   ipaddr::IPv4::isValid(rangeStr)) {
            // Netmask notation (IPv4 only)
            auto mask = ipaddr::IPv4::parse(rangeStr);
            auto pl = mask.prefixLengthFromSubnetMask();
            if (!pl) {
                throw std::invalid_argument("invalid range on address: " + notation);
            }
            prefix = *pl;
        } else {
            throw std::invalid_argument("invalid range on address: " + notation);
        }
    }

    if (prefix <= 0 || prefix > maxPrefix) {
        throw std::invalid_argument("invalid range on address: " + notation);
    }

    return {ip, prefix};
}

/**
 * @brief Check if an IP address matches a parsed subnet.
 *
 * Handles cross-version matching: if the IP is IPv6 but the subnet
 * is IPv4 (or vice versa), attempts IPv4-mapped address conversion.
 *
 * @param addr The IP address string.
 * @param subnet The subnet to match against.
 * @return true if the address is within the subnet.
 * @since 0.1.0
 */
inline bool ipMatchesSubnet(const std::string& addr, const Subnet& subnet) {
    if (!ipaddr::isValid(addr)) return false;

    try {
        auto ip = ipaddr::parse(addr);

        bool ipIsV4 = std::holds_alternative<ipaddr::IPv4>(ip);
        bool subnetIsV4 = std::holds_alternative<ipaddr::IPv4>(subnet.ip);

        if (ipIsV4 == subnetIsV4) {
            // Same IP version: direct match
            if (ipIsV4) {
                return std::get<ipaddr::IPv4>(ip).match(
                    std::get<ipaddr::IPv4>(subnet.ip), subnet.prefix);
            } else {
                return std::get<ipaddr::IPv6>(ip).match(
                    std::get<ipaddr::IPv6>(subnet.ip), subnet.prefix);
            }
        }

        // Cross-version: try IPv4-mapped conversion
        if (subnetIsV4) {
            // Subnet is IPv4, IP is IPv6 — convert IPv6 to IPv4 if mapped
            auto& v6 = std::get<ipaddr::IPv6>(ip);
            if (!v6.isIPv4MappedAddress()) return false;
            auto v4 = v6.toIPv4Address();
            return v4.match(std::get<ipaddr::IPv4>(subnet.ip), subnet.prefix);
        } else {
            // Subnet is IPv6, IP is IPv4 — convert IPv4 to IPv4-mapped IPv6
            auto v6 = std::get<ipaddr::IPv4>(ip).toIPv4MappedAddress();
            return v6.match(std::get<ipaddr::IPv6>(subnet.ip), subnet.prefix);
        }
    } catch (...) {
        return false;
    }
}

/**
 * @brief Compile a list of IP/CIDR strings into a trust function.
 *
 * Expands named ranges ("loopback", "linklocal", "uniquelocal")
 * and parses all entries into subnets for efficient matching.
 *
 * @param specs Vector of IP/CIDR or named range strings.
 * @return A TrustFunction that checks if an address matches any subnet.
 * @since 0.1.0
 */
inline TrustFunction compileSubnetTrust(const std::vector<std::string>& specs) {
    // Expand named ranges and parse all entries
    std::vector<std::string> expanded;
    for (const auto& spec : specs) {
        auto it = ipRanges().find(spec);
        if (it != ipRanges().end()) {
            for (const auto& r : it->second) {
                expanded.push_back(r);
            }
        } else {
            expanded.push_back(spec);
        }
    }

    std::vector<Subnet> subnets;
    subnets.reserve(expanded.size());
    for (const auto& s : expanded) {
        subnets.push_back(parseIpNotation(s));
    }

    if (subnets.empty()) {
        return [](const std::string&, int) { return false; };
    }

    return [subnets](const std::string& addr, int) -> bool {
        for (const auto& subnet : subnets) {
            if (ipMatchesSubnet(addr, subnet)) {
                return true;
            }
        }
        return false;
    };
}

/**
 * @brief Compile the "trust proxy" setting into a TrustFunction.
 *
 * Matches Express.js utils.js compileTrust and proxy-addr behavior:
 * - bool true: trust all proxies
 * - bool false / null: trust nothing
 * - number N: trust N hops (hop index < N)
 * - string: single IP, CIDR, comma-separated list, or named range
 * - array: list of IPs/CIDRs/named ranges
 *
 * @param val The "trust proxy" setting value.
 * @return A TrustFunction.
 * @since 0.1.0
 */
inline TrustFunction compileTrust(const JsonValue& val) {
    if (val.isNull()) {
        return [](const std::string&, int) { return false; };
    }
    if (val.isBool()) {
        bool trust = val.asBool();
        if (trust) {
            return [](const std::string&, int) { return true; };
        }
        return [](const std::string&, int) { return false; };
    }
    if (val.isNumber()) {
        int hops = static_cast<int>(val.asNumber());
        return [hops](const std::string&, int hop) { return hop < hops; };
    }
    if (val.isString()) {
        auto spec = val.asString();
        // Split comma-separated values
        std::vector<std::string> specs;
        std::istringstream stream(spec);
        std::string token;
        while (std::getline(stream, token, ',')) {
            auto trimmed = trim(token);
            if (!trimmed.empty()) {
                specs.push_back(trimmed);
            }
        }
        return compileSubnetTrust(specs);
    }
    if (val.isArray()) {
        std::vector<std::string> specs;
        for (const auto& item : val.asArray()) {
            if (item.isString()) {
                specs.push_back(item.asString());
            }
        }
        return compileSubnetTrust(specs);
    }
    // Default: trust nothing
    return [](const std::string&, int) { return false; };
}

/**
 * @brief Build the full address chain: X-Forwarded-For addresses + socket address.
 *
 * Mirrors the npm "forwarded" module: returns [xff[0], xff[1], ..., socketAddr].
 * The socket address is always the last element.
 *
 * @param socketAddr The socket remote address.
 * @param xForwardedFor The X-Forwarded-For header value.
 * @return Vector of addresses with socket address at the end.
 * @since 0.1.0
 */
inline std::vector<std::string> buildForwardedAddrs(
    const std::string& socketAddr,
    const std::string& xForwardedFor) {
    auto xffAddrs = parseForwarded(xForwardedFor);
    xffAddrs.push_back(socketAddr);
    return xffAddrs;
}

/**
 * @brief Determine the client IP by walking the proxy chain.
 *
 * Mirrors npm proxy-addr: builds the full address chain
 * [xff[0], xff[1], ..., socketAddr], then walks from the socket
 * end (rightmost) towards the client. The first untrusted address
 * is the client IP. If all are trusted, returns the leftmost address.
 *
 * @param socketAddr The socket remote address.
 * @param xForwardedFor The X-Forwarded-For header value.
 * @param trust The trust function.
 * @return The determined client IP address.
 * @since 0.1.0
 */
inline std::string proxyAddr(const std::string& socketAddr,
                              const std::string& xForwardedFor,
                              const TrustFunction& trust) {
    auto addrs = buildForwardedAddrs(socketAddr, xForwardedFor);

    // Walk from socket address (end) towards client (beginning).
    // Trust check uses index i=0 for socket addr, i=1 for next hop, etc.
    // Stop at first untrusted — that's the client.
    // addrs = [client, proxy1, proxy2, ..., socketAddr]
    // We iterate from the end (socket) backwards.
    for (int i = static_cast<int>(addrs.size()) - 1; i >= 0; --i) {
        // Hop index for trust function: distance from socket address
        int hopIndex = static_cast<int>(addrs.size()) - 1 - i;
        if (i == 0 || !trust(addrs[i], hopIndex)) {
            return addrs[i];
        }
    }

    return addrs.empty() ? socketAddr : addrs[0];
}

/**
 * @brief Get all trusted addresses in the proxy chain.
 *
 * Mirrors npm proxy-addr "all" function with trust filtering.
 * Returns addresses from closest to farthest (trust chain),
 * excluding the socket address, reversed.
 *
 * Per Express.js behavior:
 * 1. Build full chain [xff[0], ..., xff[N], socketAddr]
 * 2. Walk from socket end, truncate at first untrusted
 * 3. Reverse order (farthest -> closest)
 * 4. Remove socket address (pop last after reverse)
 *
 * @param socketAddr The socket remote address.
 * @param xForwardedFor The X-Forwarded-For header value.
 * @param trust The trust function.
 * @return Vector of trusted proxy addresses (farthest to closest).
 * @since 0.1.0
 */
inline std::vector<std::string> allAddrs(const std::string& socketAddr,
                                          const std::string& xForwardedFor,
                                          const TrustFunction& trust) {
    auto addrs = buildForwardedAddrs(socketAddr, xForwardedFor);

    // Matches proxy-addr alladdrs: iterate from index 0 (leftmost/client)
    // through to length-2 (last proxy before socket). Truncate at first
    // untrusted address.
    for (size_t i = 0; i + 1 < addrs.size(); ++i) {
        if (!trust(addrs[i], static_cast<int>(i))) {
            addrs.resize(i + 1);
            break;
        }
    }

    // Matches Express.js: addrs.reverse().pop()
    // Reverse order, then remove the leftmost XFF entry (now last after reverse).
    std::reverse(addrs.begin(), addrs.end());
    if (!addrs.empty()) {
        addrs.pop_back();
    }

    return addrs;
}

// ══════════════════════════════════════════════════════════════════════
// HTTP date formatting (RFC 7231)
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Format a time_point as an HTTP date string (RFC 7231).
 *
 * Produces dates like "Thu, 01 Jan 2026 00:00:00 GMT".
 *
 * @param tp The time point to format.
 * @return The formatted HTTP date string.
 * @since 0.2.0
 */
inline std::string httpDate(std::chrono::system_clock::time_point tp) {
    auto tt = std::chrono::system_clock::to_time_t(tp);
    std::tm gmt{};
    gmtime_r(&tt, &gmt);

    static const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* monthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s, %02d %s %04d %02d:%02d:%02d GMT",
                  dayNames[gmt.tm_wday], gmt.tm_mday, monthNames[gmt.tm_mon],
                  gmt.tm_year + 1900, gmt.tm_hour, gmt.tm_min, gmt.tm_sec);
    return buf;
}

/**
 * @brief Format milliseconds-since-epoch as an HTTP date string.
 *
 * @param mtimeMs Modification time in milliseconds since epoch.
 * @return The formatted HTTP date string.
 * @since 0.2.0
 */
inline std::string httpDate(double mtimeMs) {
    auto tp = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(static_cast<int64_t>(mtimeMs)));
    return httpDate(tp);
}

// ══════════════════════════════════════════════════════════════════════
// Stat-based ETag: weak ETag from file size + mtime
// ══════════════════════════════════════════════════════════════════════

/**
 * @brief Generate a weak ETag from file stats (size + mtime).
 *
 * Matches the npm etag package behavior for fs.Stats objects:
 * W/"<size in hex>-<mtime in hex>"
 *
 * @param size File size in bytes.
 * @param mtimeMs Modification time in milliseconds since epoch.
 * @return Weak ETag string.
 * @since 0.2.0
 */
inline std::string statEtag(size_t size, double mtimeMs) {
    auto mtime = static_cast<int64_t>(mtimeMs);
    return "W/\"" + Number::toString(static_cast<double>(size), 16) +
           "-" + Number::toString(static_cast<double>(mtime), 16) + "\"";
}

} // namespace detail
} // namespace express
} // namespace polycpp
