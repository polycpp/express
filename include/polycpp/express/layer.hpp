#pragma once

/**
 * @file layer.hpp
 * @brief Layer class -- a single entry in the middleware/route stack.
 *
 * Each Layer wraps a handler function and optionally a path pattern
 * (compiled to a match function via path-to-regexp).
 *
 * @see https://github.com/pillarjs/router/blob/master/lib/layer.js
 * @since 0.1.0
 */

#include <functional>
#include <optional>
#include <string>
#include <variant>

#include <polycpp/core/json.hpp>
#include <polycpp/path_to_regexp/path_to_regexp.hpp>

#include <polycpp/express/types.hpp>

namespace polycpp {
namespace express {

/**
 * @brief A single layer in the middleware stack.
 *
 * A Layer wraps either:
 * - A MiddlewareHandler (3 args: req, res, next)
 * - An ErrorHandler (4 args: err, req, res, next)
 * - A Route pointer (for path-specific routing)
 *
 * Each Layer has an optional path pattern and match function.
 *
 * @since 0.1.0
 */
class Layer {
public:
    /**
     * @brief Construct a Layer for a middleware handler.
     *
     * @param path The path pattern to match (e.g., "/users/:id"). Use "/" for all paths.
     * @param handler The middleware handler.
     * @param opts Options (caseSensitive, strict).
     * @since 0.1.0
     */
    Layer(const std::string& path, MiddlewareHandler handler,
          const RouterOptions& opts = {})
        : path_(path), handler_(std::move(handler)),
          isErrorHandler_(false) {
        compilePath(path, opts);
    }

    /**
     * @brief Construct a Layer for an error handler.
     *
     * @param path The path pattern.
     * @param handler The error handler.
     * @param opts Options.
     * @since 0.1.0
     */
    Layer(const std::string& path, ErrorHandler handler,
          const RouterOptions& opts = {})
        : path_(path), errorHandler_(std::move(handler)),
          isErrorHandler_(true) {
        compilePath(path, opts);
    }

    /**
     * @brief Construct a Layer that delegates to a Route.
     *
     * @param path The path pattern.
     * @param route Pointer to the Route object.
     * @param opts Options.
     * @since 0.1.0
     */
    Layer(const std::string& path, Route* route,
          const RouterOptions& opts = {})
        : path_(path), route_(route),
          isErrorHandler_(false) {
        compilePath(path, opts);
    }

    /**
     * @brief Try to match the given path against this layer's pattern.
     *
     * @param reqPath The request path to match.
     * @return true if the path matches.
     * @since 0.1.0
     */
    bool match(const std::string& reqPath) {
        if (matchFn_) {
            auto result = matchFn_(reqPath);
            if (result) {
                matchedPath_ = result->path;
                matchedParams_ = result->params;
                return true;
            }
            return false;
        }

        // Wildcard layer (no specific path pattern -- matches everything)
        if (path_ == "/" || path_.empty()) {
            matchedPath_ = "";
            matchedParams_ = {};
            return true;
        }

        return false;
    }

    /**
     * @brief Handle a request through this layer.
     *
     * @param req The request.
     * @param res The response.
     * @param next The next function.
     * @since 0.1.0
     */
    void handleRequest(Request& req, Response& res, NextFunction next) {
        if (handler_) {
            try {
                handler_(req, res, std::move(next));
            } catch (const HttpError& e) {
                next(e);
            } catch (const std::exception& e) {
                next(HttpError(500, e.what()));
            }
        } else if (route_) {
            // Delegate to route
            // This will be called from Router::handle
            next(std::nullopt);
        } else {
            next(std::nullopt);
        }
    }

    /**
     * @brief Handle an error through this layer.
     *
     * @param error The error.
     * @param req The request.
     * @param res The response.
     * @param next The next function.
     * @since 0.1.0
     */
    void handleError(const HttpError& error, Request& req, Response& res,
                     NextFunction next) {
        if (errorHandler_) {
            try {
                errorHandler_(error, req, res, std::move(next));
            } catch (const HttpError& e) {
                next(e);
            } catch (const std::exception& e) {
                next(HttpError(500, e.what()));
            }
        } else {
            // Not an error handler -- pass the error along
            next(error);
        }
    }

    /** @brief Get the layer's path pattern. @since 0.1.0 */
    const std::string& path() const { return path_; }

    /** @brief Get the matched path from the last match() call. @since 0.1.0 */
    const std::string& matchedPath() const { return matchedPath_; }

    /** @brief Get the matched params from the last match() call. @since 0.1.0 */
    const path_to_regexp::ParamData& matchedParams() const { return matchedParams_; }

    /** @brief Whether this layer handles errors. @since 0.1.0 */
    bool isErrorHandler() const { return isErrorHandler_; }

    /** @brief Get the associated Route (may be nullptr). @since 0.1.0 */
    Route* route() const { return route_; }

    /** @brief Set the name for debug purposes. @since 0.1.0 */
    void setName(const std::string& name) { name_ = name; }

    /** @brief Get the layer name. @since 0.1.0 */
    const std::string& name() const { return name_; }

private:
    std::string path_;
    std::string name_ = "<anonymous>";
    MiddlewareHandler handler_;
    ErrorHandler errorHandler_;
    Route* route_ = nullptr;
    bool isErrorHandler_ = false;

    // Match state
    path_to_regexp::MatchFunction matchFn_;
    std::string matchedPath_;
    path_to_regexp::ParamData matchedParams_;

    /**
     * @brief Compile the path pattern into a match function.
     */
    void compilePath(const std::string& pathPattern, const RouterOptions& opts) {
        if (pathPattern == "/" || pathPattern.empty()) {
            // Wildcard -- no path-to-regexp needed
            matchFn_ = nullptr;
            return;
        }

        path_to_regexp::MatchOptions matchOpts;
        matchOpts.sensitive = opts.caseSensitive;
        matchOpts.end = (route_ != nullptr); // Route layers match end, middleware doesn't
        matchOpts.trailing = !opts.strict;

        try {
            matchFn_ = path_to_regexp::match(pathPattern, matchOpts);
        } catch (...) {
            matchFn_ = nullptr;
        }
    }
};

} // namespace express
} // namespace polycpp
