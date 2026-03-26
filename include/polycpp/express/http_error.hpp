#pragma once

/**
 * @file http_error.hpp
 * @brief HTTP error class for Express error handling.
 *
 * C++ port of npm http-errors. Provides an HttpError class that carries
 * an HTTP status code, message, and expose flag.
 *
 * @see https://www.npmjs.com/package/http-errors
 * @since 0.1.0
 */

#include <stdexcept>
#include <string>

#include <polycpp/http.hpp>

namespace polycpp {
namespace express {

/**
 * @brief HTTP error with status code, used for Express error handling.
 *
 * @par Example
 * @code{.cpp}
 *   throw HttpError(404, "User not found");
 *   // or:
 *   throw HttpError::notFound("User not found");
 * @endcode
 *
 * @see https://www.npmjs.com/package/http-errors
 * @since 0.1.0
 */
class HttpError : public std::runtime_error {
public:
    /**
     * @brief Construct an HTTP error with status code and message.
     *
     * @param statusCode HTTP status code (e.g., 404, 500).
     * @param message Error message. If empty, uses default for status code.
     *
     * @since 0.1.0
     */
    explicit HttpError(int statusCode = 500, const std::string& message = "")
        : std::runtime_error(message.empty() ? defaultMessage(statusCode) : message),
          statusCode_(statusCode),
          expose_(statusCode < 500) {}

    /**
     * @brief Get the HTTP status code.
     * @return The status code.
     * @since 0.1.0
     */
    int statusCode() const noexcept { return statusCode_; }

    /**
     * @brief Whether the error message should be exposed to clients.
     *
     * True for 4xx errors, false for 5xx errors.
     *
     * @return Whether the message is safe to expose.
     * @since 0.1.0
     */
    bool expose() const noexcept { return expose_; }

    // ── Factory Methods ──────────────────────────────────────────────

    /** @brief Create a 400 Bad Request error. @since 0.1.0 */
    static HttpError badRequest(const std::string& msg = "") {
        return HttpError(400, msg);
    }

    /** @brief Create a 401 Unauthorized error. @since 0.1.0 */
    static HttpError unauthorized(const std::string& msg = "") {
        return HttpError(401, msg);
    }

    /** @brief Create a 403 Forbidden error. @since 0.1.0 */
    static HttpError forbidden(const std::string& msg = "") {
        return HttpError(403, msg);
    }

    /** @brief Create a 404 Not Found error. @since 0.1.0 */
    static HttpError notFound(const std::string& msg = "") {
        return HttpError(404, msg);
    }

    /** @brief Create a 405 Method Not Allowed error. @since 0.1.0 */
    static HttpError methodNotAllowed(const std::string& msg = "") {
        return HttpError(405, msg);
    }

    /** @brief Create a 406 Not Acceptable error. @since 0.1.0 */
    static HttpError notAcceptable(const std::string& msg = "") {
        return HttpError(406, msg);
    }

    /** @brief Create a 408 Request Timeout error. @since 0.1.0 */
    static HttpError requestTimeout(const std::string& msg = "") {
        return HttpError(408, msg);
    }

    /** @brief Create a 409 Conflict error. @since 0.1.0 */
    static HttpError conflict(const std::string& msg = "") {
        return HttpError(409, msg);
    }

    /** @brief Create a 413 Payload Too Large error. @since 0.1.0 */
    static HttpError payloadTooLarge(const std::string& msg = "") {
        return HttpError(413, msg);
    }

    /** @brief Create a 415 Unsupported Media Type error. @since 0.1.0 */
    static HttpError unsupportedMediaType(const std::string& msg = "") {
        return HttpError(415, msg);
    }

    /** @brief Create a 422 Unprocessable Entity error. @since 0.1.0 */
    static HttpError unprocessableEntity(const std::string& msg = "") {
        return HttpError(422, msg);
    }

    /** @brief Create a 429 Too Many Requests error. @since 0.1.0 */
    static HttpError tooManyRequests(const std::string& msg = "") {
        return HttpError(429, msg);
    }

    /** @brief Create a 500 Internal Server Error. @since 0.1.0 */
    static HttpError internalServerError(const std::string& msg = "") {
        return HttpError(500, msg);
    }

    /** @brief Create a 501 Not Implemented error. @since 0.1.0 */
    static HttpError notImplemented(const std::string& msg = "") {
        return HttpError(501, msg);
    }

    /** @brief Create a 502 Bad Gateway error. @since 0.1.0 */
    static HttpError badGateway(const std::string& msg = "") {
        return HttpError(502, msg);
    }

    /** @brief Create a 503 Service Unavailable error. @since 0.1.0 */
    static HttpError serviceUnavailable(const std::string& msg = "") {
        return HttpError(503, msg);
    }

private:
    int statusCode_;
    bool expose_;

    /**
     * @brief Get the default message for a status code.
     */
    static std::string defaultMessage(int code) {
        const auto& codes = http::STATUS_CODES();
        auto it = codes.find(code);
        if (it != codes.end()) {
            return it->second;
        }
        return "Unknown Error";
    }
};

} // namespace express
} // namespace polycpp
