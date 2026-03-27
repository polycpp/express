#pragma once

/**
 * @file router.hpp
 * @brief Router class -- the middleware stack and routing engine.
 *
 * C++ port of npm router (pillarjs/router). Implements the Express.js
 * middleware dispatch loop with Layer-based routing, param handlers,
 * and sub-router mounting.
 *
 * @see https://expressjs.com/en/api.html#router
 * @see https://github.com/pillarjs/router
 * @since 0.1.0
 */

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <polycpp/express/types.hpp>
#include <polycpp/express/http_error.hpp>
#include <polycpp/express/layer.hpp>
#include <polycpp/express/route.hpp>
#include <polycpp/express/detail/utils.hpp>

namespace polycpp {
namespace express {

/**
 * @brief Express.js-style router with middleware stack.
 *
 * Implements the Layer-based middleware dispatch loop:
 * 1. For each incoming request, iterate through the stack of Layers.
 * 2. For each Layer that matches the request path:
 *    - If it's a middleware Layer, call the handler.
 *    - If it's a route Layer, check the HTTP method and dispatch.
 * 3. Error handlers (4-arg) are skipped unless an error is being propagated.
 * 4. Param handlers are called before route handlers for matching params.
 *
 * @par Example
 * @code{.cpp}
 *   auto router = Router();
 *   router.get("/users", [](auto& req, auto& res) {
 *       res.json({{"users", JsonArray{}}});
 *   });
 *   router.use([](auto& req, auto& res, auto next) {
 *       std::println("Request: {} {}", req.method(), req.url());
 *       next(std::nullopt);
 *   });
 * @endcode
 *
 * @since 0.1.0
 */
class Router {
public:
    /**
     * @brief Construct a Router with options.
     * @param opts Router options.
     * @since 0.1.0
     */
    explicit Router(const RouterOptions& opts = {}) : options_(opts) {}

    // ── HTTP Method Routing ──────────────────────────────────────────

    /**
     * @brief Add a GET route handler.
     * @param path The path pattern.
     * @param handler The route handler.
     * @return Reference to this Router for chaining.
     * @since 0.1.0
     */
    Router& get(const std::string& path, RouteHandler handler) {
        return addRoute("GET", path, std::move(handler));
    }

    /** @brief Add a POST route handler. @since 0.1.0 */
    Router& post(const std::string& path, RouteHandler handler) {
        return addRoute("POST", path, std::move(handler));
    }

    /** @brief Add a PUT route handler. @since 0.1.0 */
    Router& put(const std::string& path, RouteHandler handler) {
        return addRoute("PUT", path, std::move(handler));
    }

    /** @brief Add a DELETE route handler. @since 0.1.0 */
    Router& del(const std::string& path, RouteHandler handler) {
        return addRoute("DELETE", path, std::move(handler));
    }

    /** @brief Add a PATCH route handler. @since 0.1.0 */
    Router& patch(const std::string& path, RouteHandler handler) {
        return addRoute("PATCH", path, std::move(handler));
    }

    /** @brief Add a HEAD route handler. @since 0.1.0 */
    Router& head(const std::string& path, RouteHandler handler) {
        return addRoute("HEAD", path, std::move(handler));
    }

    /** @brief Add an OPTIONS route handler. @since 0.1.0 */
    Router& options(const std::string& path, RouteHandler handler) {
        return addRoute("OPTIONS", path, std::move(handler));
    }

    /** @brief Add a handler for all HTTP methods. @since 0.1.0 */
    Router& all(const std::string& path, RouteHandler handler) {
        return addRoute("*", path, std::move(handler));
    }

    // ── Middleware-style route handlers (with next) ──────────────────

    /** @brief Add a GET middleware handler. @since 0.1.0 */
    Router& get(const std::string& path, MiddlewareHandler handler) {
        return addRouteMiddleware("GET", path, std::move(handler));
    }

    /** @brief Add a POST middleware handler. @since 0.1.0 */
    Router& post(const std::string& path, MiddlewareHandler handler) {
        return addRouteMiddleware("POST", path, std::move(handler));
    }

    /** @brief Add a PUT middleware handler. @since 0.1.0 */
    Router& put(const std::string& path, MiddlewareHandler handler) {
        return addRouteMiddleware("PUT", path, std::move(handler));
    }

    /** @brief Add a DELETE middleware handler. @since 0.1.0 */
    Router& del(const std::string& path, MiddlewareHandler handler) {
        return addRouteMiddleware("DELETE", path, std::move(handler));
    }

    /** @brief Add a PATCH middleware handler. @since 0.1.0 */
    Router& patch(const std::string& path, MiddlewareHandler handler) {
        return addRouteMiddleware("PATCH", path, std::move(handler));
    }

    /** @brief Add a handler for all methods (middleware-style). @since 0.1.0 */
    Router& all(const std::string& path, MiddlewareHandler handler) {
        return addRouteMiddleware("*", path, std::move(handler));
    }

    // ── Middleware ────────────────────────────────────────────────────

    /**
     * @brief Add middleware at the root path.
     * @param handler The middleware handler.
     * @return Reference to this Router for chaining.
     * @since 0.1.0
     */
    Router& use(MiddlewareHandler handler) {
        return use("/", std::move(handler));
    }

    /**
     * @brief Add middleware at a specific path.
     * @param path The path prefix.
     * @param handler The middleware handler.
     * @return Reference to this Router for chaining.
     * @since 0.1.0
     */
    Router& use(const std::string& path, MiddlewareHandler handler) {
        stack_.emplace_back(
            std::make_unique<Layer>(path, std::move(handler), options_)
        );
        return *this;
    }

    /**
     * @brief Add an error handler.
     * @param handler The error handler.
     * @return Reference to this Router for chaining.
     * @since 0.1.0
     */
    Router& use(ErrorHandler handler) {
        return use("/", std::move(handler));
    }

    /**
     * @brief Add an error handler at a specific path.
     * @param path The path prefix.
     * @param handler The error handler.
     * @return Reference to this Router for chaining.
     * @since 0.1.0
     */
    Router& use(const std::string& path, ErrorHandler handler) {
        stack_.emplace_back(
            std::make_unique<Layer>(path, std::move(handler), options_)
        );
        return *this;
    }

    /**
     * @brief Mount a sub-router at a path.
     * @param path The mount path.
     * @param router The sub-router.
     * @return Reference to this Router for chaining.
     * @since 0.1.0
     */
    Router& use(const std::string& path, Router& router) {
        // Wrap the sub-router as middleware
        Router* routerPtr = &router;
        use(path, MiddlewareHandler([routerPtr](Request& req, Response& res, NextFunction next) {
            routerPtr->handle(req, res, std::move(next));
        }));
        return *this;
    }

    // ── Route Object ─────────────────────────────────────────────────

    /**
     * @brief Create a Route for the given path.
     *
     * Returns a reference to a Route that can have method-specific handlers
     * added via chaining.
     *
     * @param path The route path.
     * @return Reference to the new Route.
     * @since 0.1.0
     */
    Route& route(const std::string& path) {
        auto routePtr = std::make_unique<Route>(path);
        Route& ref = *routePtr;
        routes_.push_back(std::move(routePtr));

        // Add a Layer that points to this Route
        stack_.emplace_back(
            std::make_unique<Layer>(path, &ref, options_)
        );
        return ref;
    }

    // ── Param Handlers ───────────────────────────────────────────────

    /**
     * @brief Register a param handler for the given parameter name.
     *
     * @param name The parameter name.
     * @param handler The param handler.
     * @return Reference to this Router for chaining.
     * @since 0.1.0
     */
    Router& param(const std::string& name, ParamHandler handler) {
        params_[name].push_back(std::move(handler));
        return *this;
    }

    // ── Dispatch ─────────────────────────────────────────────────────

    /**
     * @brief Handle a request through this router's middleware stack.
     *
     * This is the core dispatch loop. It iterates through the Layer stack,
     * matching each layer against the request path, and calling handlers
     * in sequence.
     *
     * @param req The request.
     * @param res The response.
     * @param done Called when the stack is exhausted or a terminal error occurs.
     * @since 0.1.0
     */
    void handle(Request& req, Response& res, NextFunction done) {
        // Use a shared state object for the dispatch loop to avoid
        // self-referencing shared_ptr issues
        struct DispatchState {
            std::vector<std::unique_ptr<Layer>>& stack;
            std::map<std::string, std::vector<ParamHandler>>& params;
            Request& req;
            Response& res;
            NextFunction done;
            size_t idx = 0;
            std::string originalPath;
        };

        auto state = std::make_shared<DispatchState>(DispatchState{
            stack_, params_, req, res, std::move(done), 0, req.path()
        });

        // Create the next function that captures the shared state
        std::function<void(std::optional<HttpError>)> next;
        auto nextImpl = std::make_shared<std::function<void(std::optional<HttpError>)>>();

        *nextImpl = [state, nextImpl, this](std::optional<HttpError> err) {
            auto& stack = state->stack;
            auto& req = state->req;
            auto& res = state->res;

            while (state->idx < stack.size()) {
                auto& layer = *stack[state->idx++];

                auto reqPath = req.path();
                if (!layer.match(reqPath)) {
                    continue;
                }

                // Error propagation
                if (err) {
                    if (layer.isErrorHandler()) {
                        layer.handleError(*err, req, res, *nextImpl);
                        return;
                    }
                    continue;
                }

                // Skip error handlers when no error
                if (layer.isErrorHandler()) {
                    continue;
                }

                // Route layer
                if (layer.route()) {
                    auto method = req.method();
                    if (!layer.route()->handlesMethod(method) && method != "HEAD") {
                        continue;
                    }

                    // Set params from path matching
                    auto& matchedParams = layer.matchedParams();
                    for (const auto& [key, value] : matchedParams) {
                        if (auto* s = std::get_if<std::string>(&value)) {
                            req.params[key] = *s;
                        }
                    }

                    // Process param handlers, then dispatch
                    processParams(matchedParams, state->params, req, res,
                        [state, nextImpl, &layer](std::optional<HttpError> paramErr) {
                            if (paramErr) {
                                (*nextImpl)(std::move(paramErr));
                                return;
                            }
                            layer.route()->dispatch(state->req, state->res, *nextImpl);
                        });
                    return;
                }

                // Regular middleware layer
                auto& matchedParams = layer.matchedParams();
                for (const auto& [key, value] : matchedParams) {
                    if (auto* s = std::get_if<std::string>(&value)) {
                        req.params[key] = *s;
                    }
                }

                // Trim matched path prefix for sub-routers
                auto& matchedPath = layer.matchedPath();
                auto savedBaseUrl = req.baseUrl();
                if (!matchedPath.empty() && matchedPath != "/") {
                    auto newPath = reqPath.substr(matchedPath.size());
                    if (newPath.empty() || newPath[0] != '/') {
                        newPath = "/" + newPath;
                    }
                    req.setPath(newPath);
                    req.setBaseUrl(req.baseUrl() + matchedPath);
                }

                layer.handleRequest(req, res, *nextImpl);

                if (!matchedPath.empty() && matchedPath != "/") {
                    req.setPath(state->originalPath);
                    req.setBaseUrl(savedBaseUrl);
                }
                return;
            }

            // No more layers
            if (err) {
                state->done(std::move(err));
            } else {
                state->done(std::nullopt);
            }
        };

        (*nextImpl)(std::nullopt);
    }

    /**
     * @brief Make this router callable as middleware.
     * @since 0.1.0
     */
    void operator()(Request& req, Response& res, NextFunction next) {
        handle(req, res, std::move(next));
    }

    /** @brief Get the router options. @since 0.1.0 */
    const RouterOptions& options() const { return options_; }

private:
    RouterOptions options_;
    std::vector<std::unique_ptr<Layer>> stack_;
    std::vector<std::unique_ptr<Route>> routes_;
    std::map<std::string, std::vector<ParamHandler>> params_;

    /**
     * @brief Add a route handler for a specific method and path.
     */
    Router& addRoute(const std::string& method, const std::string& path,
                     RouteHandler handler) {
        auto& r = route(path);
        if (method == "GET") r.get(std::move(handler));
        else if (method == "POST") r.post(std::move(handler));
        else if (method == "PUT") r.put(std::move(handler));
        else if (method == "DELETE") r.del(std::move(handler));
        else if (method == "PATCH") r.patch(std::move(handler));
        else if (method == "HEAD") r.head(std::move(handler));
        else if (method == "OPTIONS") r.options(std::move(handler));
        else if (method == "*") r.all(std::move(handler));
        return *this;
    }

    /**
     * @brief Add a middleware-style route handler for a specific method and path.
     */
    Router& addRouteMiddleware(const std::string& method, const std::string& path,
                               MiddlewareHandler handler) {
        auto& r = route(path);
        if (method == "GET") r.get(std::move(handler));
        else if (method == "POST") r.post(std::move(handler));
        else if (method == "PUT") r.put(std::move(handler));
        else if (method == "DELETE") r.del(std::move(handler));
        else if (method == "PATCH") r.patch(std::move(handler));
        else if (method == "*") r.all(std::move(handler));
        return *this;
    }

    /**
     * @brief Process param handlers for matched parameters.
     */
    void processParams(const path_to_regexp::ParamData& matchedParams,
                       const std::map<std::string, std::vector<ParamHandler>>& paramHandlers,
                       Request& req, Response& res,
                       std::function<void(std::optional<HttpError>)> done) {
        // Collect params that have handlers
        auto paramsToProcess = std::make_shared<std::vector<std::pair<std::string, std::string>>>();
        for (const auto& [name, value] : matchedParams) {
            if (paramHandlers.count(name) > 0) {
                if (auto* s = std::get_if<std::string>(&value)) {
                    paramsToProcess->push_back({name, *s});
                }
            }
        }

        if (paramsToProcess->empty()) {
            done(std::nullopt);
            return;
        }

        struct ParamState {
            size_t paramIdx = 0;
            size_t handlerIdx = 0;
        };

        auto pState = std::make_shared<ParamState>();
        auto donePtr = std::make_shared<std::function<void(std::optional<HttpError>)>>(std::move(done));

        auto nextParam = std::make_shared<std::function<void(std::optional<HttpError>)>>();
        *nextParam = [paramsToProcess, &paramHandlers, &req, &res, pState, donePtr, nextParam](
                         std::optional<HttpError> err) {
            if (err) {
                (*donePtr)(std::move(err));
                return;
            }

            if (pState->paramIdx >= paramsToProcess->size()) {
                (*donePtr)(std::nullopt);
                return;
            }

            auto& [name, value] = (*paramsToProcess)[pState->paramIdx];
            auto it = paramHandlers.find(name);
            if (it == paramHandlers.end() || pState->handlerIdx >= it->second.size()) {
                pState->paramIdx++;
                pState->handlerIdx = 0;
                (*nextParam)(std::nullopt);
                return;
            }

            auto& handler = it->second[pState->handlerIdx++];
            try {
                handler(req, res, *nextParam, value);
            } catch (const HttpError& e) {
                (*donePtr)(e);
            } catch (const std::exception& e) {
                (*donePtr)(HttpError(500, e.what()));
            }
        };

        (*nextParam)(std::nullopt);
    }
};

} // namespace express
} // namespace polycpp
