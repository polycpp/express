#pragma once

/**
 * @file route.hpp
 * @brief Route class -- a collection of handlers for a single path.
 *
 * A Route is created by Router.route() and represents all HTTP method handlers
 * for a given path pattern.
 *
 * @see https://expressjs.com/en/api.html#router.route
 * @see https://github.com/pillarjs/router/blob/master/lib/route.js
 * @since 0.1.0
 */

#include <algorithm>
#include <string>
#include <vector>

#include <polycpp/express/types.hpp>
#include <polycpp/express/http_error.hpp>

namespace polycpp {
namespace express {

/**
 * @brief A single route entry containing method-specific handlers.
 *
 * Created by Router::route() to group handlers for the same path.
 * Supports chaining: route.get(handler).post(handler).
 *
 * @par Example
 * @code{.cpp}
 *   router.route("/users")
 *       .get([](auto& req, auto& res) { res.json({{"users", "..."}}); })
 *       .post([](auto& req, auto& res) { res.status(201).send("Created"); });
 * @endcode
 *
 * @since 0.1.0
 */
class Route {
public:
    /**
     * @brief Construct a Route for the given path.
     * @param path The route path.
     * @since 0.1.0
     */
    explicit Route(const std::string& path) : path_(path) {}

    // ── HTTP Method Handlers ─────────────────────────────────────────

    /** @brief Add a GET handler. @since 0.1.0 */
    Route& get(RouteHandler handler) { return addHandler("GET", std::move(handler)); }

    /** @brief Add a POST handler. @since 0.1.0 */
    Route& post(RouteHandler handler) { return addHandler("POST", std::move(handler)); }

    /** @brief Add a PUT handler. @since 0.1.0 */
    Route& put(RouteHandler handler) { return addHandler("PUT", std::move(handler)); }

    /** @brief Add a DELETE handler. @since 0.1.0 */
    Route& del(RouteHandler handler) { return addHandler("DELETE", std::move(handler)); }

    /** @brief Add a PATCH handler. @since 0.1.0 */
    Route& patch(RouteHandler handler) { return addHandler("PATCH", std::move(handler)); }

    /** @brief Add a HEAD handler. @since 0.1.0 */
    Route& head(RouteHandler handler) { return addHandler("HEAD", std::move(handler)); }

    /** @brief Add an OPTIONS handler. @since 0.1.0 */
    Route& options(RouteHandler handler) { return addHandler("OPTIONS", std::move(handler)); }

    /** @brief Add a handler for all HTTP methods. @since 0.1.0 */
    Route& all(RouteHandler handler) { return addHandler("*", std::move(handler)); }

    // ── Middleware-style method handlers (with next) ─────────────────

    /** @brief Add a GET middleware handler. @since 0.1.0 */
    Route& get(MiddlewareHandler handler) { return addMiddlewareHandler("GET", std::move(handler)); }

    /** @brief Add a POST middleware handler. @since 0.1.0 */
    Route& post(MiddlewareHandler handler) { return addMiddlewareHandler("POST", std::move(handler)); }

    /** @brief Add a PUT middleware handler. @since 0.1.0 */
    Route& put(MiddlewareHandler handler) { return addMiddlewareHandler("PUT", std::move(handler)); }

    /** @brief Add a DELETE middleware handler. @since 0.1.0 */
    Route& del(MiddlewareHandler handler) { return addMiddlewareHandler("DELETE", std::move(handler)); }

    /** @brief Add a PATCH middleware handler. @since 0.1.0 */
    Route& patch(MiddlewareHandler handler) { return addMiddlewareHandler("PATCH", std::move(handler)); }

    /** @brief Add a handler for all HTTP methods (middleware style). @since 0.1.0 */
    Route& all(MiddlewareHandler handler) { return addMiddlewareHandler("*", std::move(handler)); }

    // ── Dispatch ─────────────────────────────────────────────────────

    /**
     * @brief Dispatch a request through this route's handler stack.
     *
     * Iterates through method-matching handlers, calling each in sequence.
     *
     * @param req The request.
     * @param res The response.
     * @param done Called when all handlers are exhausted or an error occurs.
     * @since 0.1.0
     */
    void dispatch(Request& req, Response& res, NextFunction done) {
        auto method = req.method();

        size_t idx = 0;
        auto& handlers = handlers_;

        std::function<void(std::optional<HttpError>)> next;
        next = [&, idx](std::optional<HttpError> err) mutable {
            if (err) {
                done(std::move(err));
                return;
            }

            // Find next matching handler
            while (idx < handlers.size()) {
                auto& entry = handlers[idx++];

                if (entry.method != "*" && entry.method != method) {
                    continue;
                }

                if (entry.routeHandler) {
                    try {
                        entry.routeHandler(req, res);
                    } catch (const HttpError& e) {
                        done(e);
                    } catch (const std::exception& e) {
                        done(HttpError(500, e.what()));
                    }
                    return;
                }

                if (entry.middlewareHandler) {
                    try {
                        entry.middlewareHandler(req, res, next);
                    } catch (const HttpError& e) {
                        done(e);
                    } catch (const std::exception& e) {
                        done(HttpError(500, e.what()));
                    }
                    return;
                }
            }

            // No more handlers
            done(std::nullopt);
        };

        next(std::nullopt);
    }

    /**
     * @brief Check if this route handles a given HTTP method.
     *
     * @param method The HTTP method (uppercase).
     * @return true if at least one handler exists for this method.
     * @since 0.1.0
     */
    bool handlesMethod(const std::string& method) const {
        for (const auto& entry : handlers_) {
            if (entry.method == "*" || entry.method == method) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Get all HTTP methods this route handles.
     * @return Vector of method strings.
     * @since 0.1.0
     */
    std::vector<std::string> methods() const {
        std::vector<std::string> result;
        for (const auto& entry : handlers_) {
            if (entry.method != "*" &&
                std::find(result.begin(), result.end(), entry.method) == result.end()) {
                result.push_back(entry.method);
            }
        }
        return result;
    }

    /** @brief Get the route path. @since 0.1.0 */
    const std::string& path() const { return path_; }

private:
    struct HandlerEntry {
        std::string method;
        RouteHandler routeHandler;
        MiddlewareHandler middlewareHandler;
    };

    std::string path_;
    std::vector<HandlerEntry> handlers_;

    Route& addHandler(const std::string& method, RouteHandler handler) {
        handlers_.push_back({method, std::move(handler), nullptr});
        return *this;
    }

    Route& addMiddlewareHandler(const std::string& method, MiddlewareHandler handler) {
        handlers_.push_back({method, nullptr, std::move(handler)});
        return *this;
    }
};

} // namespace express
} // namespace polycpp
