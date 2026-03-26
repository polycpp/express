#pragma once

/**
 * @file application.hpp
 * @brief Application class -- the main Express application object.
 *
 * The Application class is the core of an Express app. It extends the Router
 * with settings management, template engine registration, and HTTP server
 * creation via listen().
 *
 * @see https://expressjs.com/en/api.html#app
 * @since 0.1.0
 */

#include <functional>
#include <map>
#include <memory>
#include <string>

#include <polycpp/core/json.hpp>
#include <polycpp/events.hpp>
#include <polycpp/http.hpp>
#include <polycpp/path.hpp>

#include <polycpp/express/types.hpp>
#include <polycpp/express/http_error.hpp>
#include <polycpp/express/router.hpp>
#include <polycpp/express/request.hpp>
#include <polycpp/express/response.hpp>
#include <polycpp/express/view.hpp>
#include <polycpp/express/detail/utils.hpp>

namespace polycpp {
namespace express {

/**
 * @brief The main Express application class.
 *
 * An Application manages settings, template engines, and a Router
 * (middleware stack). It creates HTTP servers via listen() and dispatches
 * incoming requests through the middleware chain.
 *
 * @par Example
 * @code{.cpp}
 *   auto app = express();
 *   app.get("/", [](auto& req, auto& res) {
 *       res.send("Hello World!");
 *   });
 *   app.listen(3000, []() {
 *       std::println("Server running on port 3000");
 *   });
 * @endcode
 *
 * @see https://expressjs.com/en/api.html#app
 * @since 0.1.0
 */
class Application {
public:
    /**
     * @brief Construct an Application with default settings.
     * @since 0.1.0
     */
    Application() {
        init();
    }

    // ── Settings ─────────────────────────────────────────────────────

    /**
     * @brief Set an application setting.
     *
     * @param setting The setting name.
     * @param val The setting value.
     * @return Reference to this Application for chaining.
     *
     * @par Example
     * @code{.cpp}
     *   app.set("view engine", "ejs");
     *   app.set("trust proxy", true);
     * @endcode
     *
     * @since 0.1.0
     */
    Application& set(const std::string& setting, JsonValue val) {
        settings_[setting] = val;
        // Compile trust function when "trust proxy" is set
        if (setting == "trust proxy") {
            trustFunction_ = detail::compileTrust(val);
        }
        return *this;
    }

    /**
     * @brief Get an application setting.
     *
     * @param setting The setting name.
     * @return The setting value, or null if not set.
     * @since 0.1.0
     */
    JsonValue getSetting(const std::string& setting) const {
        auto it = settings_.find(setting);
        if (it != settings_.end()) {
            return it->second;
        }
        return JsonValue();
    }

    /**
     * @brief Enable a boolean setting.
     *
     * @param setting The setting name.
     * @return Reference to this Application for chaining.
     * @since 0.1.0
     */
    Application& enable(const std::string& setting) {
        return set(setting, true);
    }

    /**
     * @brief Disable a boolean setting.
     *
     * @param setting The setting name.
     * @return Reference to this Application for chaining.
     * @since 0.1.0
     */
    Application& disable(const std::string& setting) {
        return set(setting, false);
    }

    /**
     * @brief Check if a setting is enabled (truthy).
     *
     * @param setting The setting name.
     * @return true if the setting is truthy.
     * @since 0.1.0
     */
    bool enabled(const std::string& setting) const {
        auto val = getSetting(setting);
        if (val.isBool()) return val.asBool();
        if (val.isString()) return !val.asString().empty();
        if (val.isNumber()) return val.asNumber() != 0;
        return !val.isNull();
    }

    /**
     * @brief Check if a setting is disabled (falsy).
     *
     * @param setting The setting name.
     * @return true if the setting is falsy.
     * @since 0.1.0
     */
    bool disabled(const std::string& setting) const {
        return !enabled(setting);
    }

    // ── Template Engine ──────────────────────────────────────────────

    /**
     * @brief Register a template engine for a file extension.
     *
     * @param ext The file extension (e.g., "ejs", "pug").
     * @param fn The engine function.
     * @return Reference to this Application for chaining.
     * @since 0.1.0
     */
    Application& engine(const std::string& ext, EngineFunction fn) {
        auto dotExt = ext[0] == '.' ? ext : "." + ext;
        engines_[dotExt] = std::move(fn);
        return *this;
    }

    // ── Template Rendering ───────────────────────────────────────────

    /**
     * @brief Render a view template.
     *
     * Creates a View object to resolve the template file, checks the
     * view cache, and invokes the template engine.
     *
     * @param name The view name.
     * @param options Template variables.
     * @param callback Called with (error, rendered_string).
     *
     * @par Example
     * @code{.cpp}
     *   app.render("email", {{"name", "Tobi"}}, [](auto err, auto html) {
     *       if (!err) {
     *           // use rendered html
     *       }
     *   });
     * @endcode
     *
     * @see https://expressjs.com/en/api.html#app.render
     * @since 0.1.0
     */
    void render(const std::string& name, const JsonValue& options,
                std::function<void(std::optional<std::string>, std::string)> callback) {
        // Merge render options: app.locals + options
        JsonObject renderOptions;
        if (locals.isObject()) {
            for (const auto& [k, v] : locals.asObject()) {
                renderOptions[k] = v;
            }
        }
        if (options.isObject()) {
            for (const auto& [k, v] : options.asObject()) {
                renderOptions[k] = v;
            }
        }

        // Check view cache
        bool cacheEnabled = enabled("view cache");

        if (cacheEnabled) {
            auto it = viewCache_.find(name);
            if (it != viewCache_.end()) {
                // Use cached view
                it->second->render(JsonValue(renderOptions), std::move(callback));
                return;
            }
        }

        // Get view settings
        auto engineSetting = getSetting("view engine");
        std::string defaultEngine;
        if (engineSetting.isString()) {
            defaultEngine = engineSetting.asString();
        }

        auto viewsSetting = getSetting("views");
        std::vector<std::string> viewDirs;
        if (viewsSetting.isString()) {
            viewDirs.push_back(viewsSetting.asString());
        } else if (viewsSetting.isArray()) {
            for (const auto& v : viewsSetting.asArray()) {
                if (v.isString()) {
                    viewDirs.push_back(v.asString());
                }
            }
        }

        // Create View
        auto view = std::make_shared<View>(name, engines_, defaultEngine, viewDirs);

        if (!view->lookup()) {
            std::string dirs;
            if (viewDirs.size() > 1) {
                dirs = "directories \"";
                for (size_t i = 0; i + 1 < viewDirs.size(); ++i) {
                    if (i > 0) dirs += "\", \"";
                    dirs += viewDirs[i];
                }
                dirs += "\" or \"" + viewDirs.back() + "\"";
            } else if (viewDirs.size() == 1) {
                dirs = "directory \"" + viewDirs[0] + "\"";
            } else {
                dirs = "undefined views directory";
            }
            callback("Failed to lookup view \"" + name + "\" in views " + dirs, "");
            return;
        }

        // Cache if enabled
        if (cacheEnabled) {
            viewCache_[name] = view;
        }

        // Render
        view->render(JsonValue(renderOptions), std::move(callback));
    }

    // ── Routing (delegate to Router) ─────────────────────────────────

    /** @brief Add a GET route handler. @since 0.1.0 */
    Application& get(const std::string& path, RouteHandler handler) {
        router_.get(path, std::move(handler));
        return *this;
    }

    /** @brief Add a POST route handler. @since 0.1.0 */
    Application& post(const std::string& path, RouteHandler handler) {
        router_.post(path, std::move(handler));
        return *this;
    }

    /** @brief Add a PUT route handler. @since 0.1.0 */
    Application& put(const std::string& path, RouteHandler handler) {
        router_.put(path, std::move(handler));
        return *this;
    }

    /** @brief Add a DELETE route handler. @since 0.1.0 */
    Application& del(const std::string& path, RouteHandler handler) {
        router_.del(path, std::move(handler));
        return *this;
    }

    /** @brief Add a PATCH route handler. @since 0.1.0 */
    Application& patch(const std::string& path, RouteHandler handler) {
        router_.patch(path, std::move(handler));
        return *this;
    }

    /** @brief Add a HEAD route handler. @since 0.1.0 */
    Application& head(const std::string& path, RouteHandler handler) {
        router_.head(path, std::move(handler));
        return *this;
    }

    /** @brief Add an OPTIONS route handler. @since 0.1.0 */
    Application& options(const std::string& path, RouteHandler handler) {
        router_.options(path, std::move(handler));
        return *this;
    }

    /** @brief Add a handler for all HTTP methods. @since 0.1.0 */
    Application& all(const std::string& path, RouteHandler handler) {
        router_.all(path, std::move(handler));
        return *this;
    }

    // ── Middleware-style route handlers ───────────────────────────────

    /** @brief Add a GET middleware handler. @since 0.1.0 */
    Application& get(const std::string& path, MiddlewareHandler handler) {
        router_.get(path, std::move(handler));
        return *this;
    }

    /** @brief Add a POST middleware handler. @since 0.1.0 */
    Application& post(const std::string& path, MiddlewareHandler handler) {
        router_.post(path, std::move(handler));
        return *this;
    }

    /** @brief Add a PUT middleware handler. @since 0.1.0 */
    Application& put(const std::string& path, MiddlewareHandler handler) {
        router_.put(path, std::move(handler));
        return *this;
    }

    /** @brief Add a DELETE middleware handler. @since 0.1.0 */
    Application& del(const std::string& path, MiddlewareHandler handler) {
        router_.del(path, std::move(handler));
        return *this;
    }

    // ── Middleware ────────────────────────────────────────────────────

    /**
     * @brief Add middleware at the root path.
     * @param handler The middleware handler.
     * @return Reference to this Application for chaining.
     * @since 0.1.0
     */
    Application& use(MiddlewareHandler handler) {
        router_.use(std::move(handler));
        return *this;
    }

    /**
     * @brief Add middleware at a specific path.
     * @param path The path prefix.
     * @param handler The middleware handler.
     * @return Reference to this Application for chaining.
     * @since 0.1.0
     */
    Application& use(const std::string& path, MiddlewareHandler handler) {
        router_.use(path, std::move(handler));
        return *this;
    }

    /**
     * @brief Add an error handler.
     * @param handler The error handler.
     * @return Reference to this Application for chaining.
     * @since 0.1.0
     */
    Application& use(ErrorHandler handler) {
        router_.use(std::move(handler));
        return *this;
    }

    /**
     * @brief Add an error handler at a specific path.
     * @param path The path prefix.
     * @param handler The error handler.
     * @return Reference to this Application for chaining.
     * @since 0.1.0
     */
    Application& use(const std::string& path, ErrorHandler handler) {
        router_.use(path, std::move(handler));
        return *this;
    }

    /**
     * @brief Mount a sub-router at a path.
     * @param path The mount path.
     * @param router The sub-router.
     * @return Reference to this Application for chaining.
     * @since 0.1.0
     */
    Application& use(const std::string& path, Router& router) {
        router_.use(path, router);
        return *this;
    }

    /**
     * @brief Mount a sub-application at a path.
     * @param path The mount path.
     * @param subApp The sub-application.
     * @return Reference to this Application for chaining.
     * @since 0.1.0
     */
    Application& use(const std::string& path, Application& subApp) {
        subApp.mountpath_ = path;
        subApp.parent_ = this;

        // Wrap the sub-app as middleware
        Application* appPtr = &subApp;
        router_.use(path, MiddlewareHandler([appPtr](Request& req, Response& res, NextFunction next) {
            (*appPtr)(req, res, std::move(next));
        }));
        return *this;
    }

    // ── Route Object ─────────────────────────────────────────────────

    /**
     * @brief Create a Route for the given path.
     * @param path The route path.
     * @return Reference to the new Route.
     * @since 0.1.0
     */
    Route& route(const std::string& path) {
        return router_.route(path);
    }

    // ── Param Handlers ───────────────────────────────────────────────

    /**
     * @brief Register a param handler.
     * @param name The parameter name.
     * @param handler The param handler.
     * @return Reference to this Application for chaining.
     * @since 0.1.0
     */
    Application& param(const std::string& name, ParamHandler handler) {
        router_.param(name, std::move(handler));
        return *this;
    }

    // ── Server ───────────────────────────────────────────────────────

    /**
     * @brief Start listening for HTTP connections.
     *
     * Creates an HTTP server and begins accepting connections.
     *
     * @param port The port to listen on.
     * @param callback Called when the server starts listening.
     * @return Reference to the created HTTP server.
     *
     * @par Example
     * @code{.cpp}
     *   app.listen(3000, []() {
     *       std::println("Listening on port 3000");
     *   });
     * @endcode
     *
     * @since 0.1.0
     */
    http::Server& listen(uint16_t port,
                         std::function<void()> callback = nullptr) {
        return listen(port, "0.0.0.0", std::move(callback));
    }

    /**
     * @brief Start listening on a specific host and port.
     *
     * @param port The port to listen on.
     * @param host The host address to bind.
     * @param callback Called when the server starts listening.
     * @return Reference to the created HTTP server.
     * @since 0.1.0
     */
    http::Server& listen(uint16_t port, const std::string& host,
                         std::function<void()> callback = nullptr) {
        Application* self = this;
        server_ = std::make_unique<http::Server>(
            http::RequestHandler([self](http::IncomingMessage req, http::ServerResponse res) {
                self->handleRequest(req, res);
            })
        );

        server_->listen(port, host, std::move(callback));
        return *server_;
    }

    // ── Callable ─────────────────────────────────────────────────────

    /**
     * @brief Make this application callable as middleware.
     *
     * Allows mounting an app as middleware in another app:
     * @code{.cpp}
     *   mainApp.use("/admin", adminApp);
     * @endcode
     *
     * @since 0.1.0
     */
    void operator()(Request& req, Response& res, NextFunction next) {
        router_.handle(req, res, std::move(next));
    }

    // ── Properties ───────────────────────────────────────────────────

    /** @brief Per-application local variables for templates. @since 0.1.0 */
    JsonValue locals = JsonObject{};

    /** @brief The path at which this app is mounted. @since 0.1.0 */
    std::string mountpath_ = "/";

    /** @brief Get the Router. @since 0.1.0 */
    Router& router() { return router_; }

    /**
     * @brief Get the compiled trust proxy function.
     *
     * Returns the trust function compiled from the "trust proxy" setting.
     * Used internally by Request to determine whether to read proxy headers.
     *
     * @return The compiled TrustFunction.
     * @since 0.1.0
     */
    const detail::TrustFunction& trustFunction() const { return trustFunction_; }

private:
    Router router_;
    std::map<std::string, JsonValue> settings_;
    std::map<std::string, EngineFunction> engines_;
    std::map<std::string, std::shared_ptr<View>> viewCache_;
    detail::TrustFunction trustFunction_;
    Application* parent_ = nullptr;
    std::unique_ptr<http::Server> server_;

    /**
     * @brief Initialize default settings.
     */
    void init() {
        // Default settings matching Express.js
        enable("x-powered-by");
        set("etag", "weak");
        set("env", "development");
        set("query parser", "extended");
        set("subdomain offset", 2.0);
        set("trust proxy", false);
        set("view engine", "");
        set("views", "views");
    }

    /**
     * @brief Handle an incoming HTTP request.
     *
     * Creates Request/Response wrappers and dispatches through the
     * middleware stack. If no middleware handles the request, a default
     * 404 response is sent.
     */
    void handleRequest(http::IncomingMessage& rawReq,
                       http::ServerResponse& rawRes) {
        // Set X-Powered-By header
        if (enabled("x-powered-by")) {
            rawRes.setHeader("X-Powered-By", "Express");
        }

        // Create Express req/res wrappers
        Request req(rawReq, this);
        Response res(rawRes, this);

        // Link them together
        req.setRes(&res);
        res.setReq(&req);

        // Dispatch through the router
        router_.handle(req, res, [&rawRes](std::optional<HttpError> err) {
            if (err) {
                // Final error handler
                auto statusCode = err->statusCode();
                auto& codes = http::STATUS_CODES();
                auto it = codes.find(statusCode);
                std::string message = err->expose() ? err->what() : (it != codes.end() ? it->second : "Internal Server Error");

                rawRes.status(statusCode);
                rawRes.setHeader("Content-Type", "text/html; charset=utf-8");
                auto body = detail::escapeHtml(message);
                rawRes.setHeader("Content-Length", static_cast<int>(body.size()));
                rawRes.end(body);
            } else {
                // No route matched -- send 404
                rawRes.status(404);
                rawRes.setHeader("Content-Type", "text/html; charset=utf-8");
                auto msg = std::string("Cannot ") + "find the requested resource";
                rawRes.setHeader("Content-Length", static_cast<int>(msg.size()));
                rawRes.end(msg);
            }
        });
    }
};

// ── Response::render implementation (needs Application) ──────────────

inline void Response::render(const std::string& view, const JsonValue& options,
                             std::function<void(std::optional<std::string>, std::string)> callback) {
    if (!app_) {
        if (callback) callback("No app context", "");
        return;
    }

    // Merge locals: app.locals < res.locals < options
    JsonObject merged;

    // Add app.locals
    if (app_->locals.isObject()) {
        for (const auto& [k, v] : app_->locals.asObject()) {
            merged[k] = v;
        }
    }

    // Add res.locals (overrides app.locals)
    if (locals.isObject()) {
        for (const auto& [k, v] : locals.asObject()) {
            merged[k] = v;
        }
    }

    // Add options (overrides all)
    if (options.isObject()) {
        for (const auto& [k, v] : options.asObject()) {
            merged[k] = v;
        }
    }

    // Default callback sends the rendered HTML or a 500 on error
    auto done = callback;
    if (!done) {
        // Capture raw_ by reference -- res must outlive the callback
        auto& rawRef = raw_;
        done = [this, &rawRef](std::optional<std::string> err, std::string html) {
            if (err) {
                rawRef.status(500);
                rawRef.setHeader("Content-Type", "text/plain; charset=utf-8");
                rawRef.setHeader("Content-Length", static_cast<int>(err->size()));
                rawRef.end(*err);
                return;
            }
            send(html);
        };
    }

    // Delegate to app.render()
    app_->render(view, JsonValue(merged), std::move(done));
}

} // namespace express
} // namespace polycpp
