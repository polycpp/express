/**
 * @file test_integration.cpp
 * @brief Integration tests for Express application scenarios.
 *
 * Tests full app workflows: create app, register routes, simulate requests,
 * and verify responses.
 */

#include <gtest/gtest.h>
#include <polycpp/io/event_context.hpp>
#include <polycpp/io/tcp_socket.hpp>
#include <polycpp/event_loop.hpp>
#include <polycpp/express/express.hpp>
#include <polycpp/negotiate/detail/aggregator.hpp>
#include <polycpp/cookie/detail/aggregator.hpp>
#include <polycpp/mime/detail/aggregator.hpp>

using namespace polycpp::express;

// ═══════════════════════════════════════════════════════════════════════
// Helper: Mock Request/Response for testing
// ═══════════════════════════════════════════════════════════════════════

class IntegrationMock {
public:
    IntegrationMock(const std::string& method, const std::string& url,
                    const polycpp::http::Headers& headers = {},
                    const std::string& bodyContent = "",
                    Application* app = nullptr)
        : msg_(), res_(createSocket(), "", 1, false) {
        msg_.method() = method;
        msg_.url() = url;
        msg_.headers() = headers;
        if (!bodyContent.empty()) {
            msg_.impl()->push(polycpp::Buffer::from(bodyContent));
        }
        msg_.impl()->push(std::nullopt);
        res_.on(polycpp::stream::event::Error_, [](const polycpp::Error&) {});
        req_ = std::make_unique<Request>(msg_, app);
        resp_ = std::make_unique<Response>(res_, app);
        req_->setRes(resp_.get());
        resp_->setReq(req_.get());
    }

    Request& req() { return *req_; }
    Response& res() { return *resp_; }
    polycpp::http::ServerResponse& rawRes() { return res_; }

private:
    static polycpp::net::Socket createSocket() {
        return polycpp::net::Socket();
    }

    polycpp::http::IncomingMessage msg_;
    polycpp::http::ServerResponse res_;
    std::unique_ptr<Request> req_;
    std::unique_ptr<Response> resp_;
};

// Helper: extract getHeader() result as std::string
static std::string getHeaderStr(polycpp::http::ServerResponse& res, const std::string& name) {
    auto val = res.getHeader(name);
    if (val.isString()) return val.asString();
    if (val.isArray()) {
        std::string result;
        for (const auto& item : val.asArray()) {
            if (!result.empty()) result += ", ";
            if (item.isString()) result += item.asString();
        }
        return result;
    }
    return {};
}

// ═══════════════════════════════════════════════════════════════════════
// Basic Routing Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(IntegrationRoutingTest, GetRouteReturnsCorrectResponse) {
    Application app;
    bool handlerCalled = false;

    app.get("/hello", [&](Request& req, Response& res) {
        handlerCalled = true;
        res.send("Hello World");
    });

    IntegrationMock mock("GET", "/hello");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(handlerCalled);
    EXPECT_EQ(getHeaderStr(mock.rawRes(),"Content-Type"), "text/html; charset=utf-8");
}

TEST(IntegrationRoutingTest, PostRouteWithBody) {
    Application app;
    std::string capturedName;

    app.use(json());
    app.post("/users", [&](Request& req, Response& res) {
        capturedName = req.body["name"].asString();
        res.status(201).json(polycpp::JsonObject{{"created", true}});
    });

    IntegrationMock mock("POST", "/users",
        {{"content-type", "application/json"}},
        "{\"name\":\"Alice\"}");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_EQ(capturedName, "Alice");
    EXPECT_EQ(mock.rawRes().statusCode(), 201);
}

TEST(IntegrationRoutingTest, UnmatchedRouteCallsDone) {
    Application app;
    app.get("/exists", [](Request& req, Response& res) {
        res.send("found");
    });

    IntegrationMock mock("GET", "/does-not-exist");
    bool doneCalled = false;

    app.router().handle(mock.req(), mock.res(), [&](std::optional<HttpError> err) {
        doneCalled = true;
        // Router calls done with no error when no route matches
        EXPECT_FALSE(err.has_value());
    });

    EXPECT_TRUE(doneCalled);
}

TEST(IntegrationRoutingTest, MultipleRoutesOnSamePathDifferentMethods) {
    Application app;
    std::string lastHandler;

    app.get("/resource", [&](Request& req, Response& res) {
        lastHandler = "GET";
    });
    app.post("/resource", [&](Request& req, Response& res) {
        lastHandler = "POST";
    });
    app.put("/resource", [&](Request& req, Response& res) {
        lastHandler = "PUT";
    });
    app.del("/resource", [&](Request& req, Response& res) {
        lastHandler = "DELETE";
    });

    {
        IntegrationMock mock("GET", "/resource");
        app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});
        EXPECT_EQ(lastHandler, "GET");
    }

    {
        IntegrationMock mock("POST", "/resource");
        app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});
        EXPECT_EQ(lastHandler, "POST");
    }

    {
        IntegrationMock mock("PUT", "/resource");
        app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});
        EXPECT_EQ(lastHandler, "PUT");
    }

    {
        IntegrationMock mock("DELETE", "/resource");
        app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});
        EXPECT_EQ(lastHandler, "DELETE");
    }
}

TEST(IntegrationRoutingTest, RouteWithParams) {
    Application app;
    std::string capturedUserId;
    std::string capturedPostId;

    app.get("/users/:userId/posts/:postId", [&](Request& req, Response& res) {
        capturedUserId = req.params["userId"].asString();
        capturedPostId = req.params["postId"].asString();
    });

    IntegrationMock mock("GET", "/users/42/posts/100");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_EQ(capturedUserId, "42");
    EXPECT_EQ(capturedPostId, "100");
}

// ═══════════════════════════════════════════════════════════════════════
// Middleware Chain Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(IntegrationMiddlewareTest, MiddlewareExecutesInOrder) {
    Application app;
    std::vector<int> order;

    app.use([&](Request& req, Response& res, NextFunction next) {
        order.push_back(1);
        next(std::nullopt);
    });

    app.use([&](Request& req, Response& res, NextFunction next) {
        order.push_back(2);
        next(std::nullopt);
    });

    app.get("/test", [&](Request& req, Response& res) {
        order.push_back(3);
    });

    IntegrationMock mock("GET", "/test");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(IntegrationMiddlewareTest, MiddlewareCanModifyRequest) {
    Application app;
    bool bodyModified = false;

    // Middleware that sets a custom property via body
    app.use([](Request& req, Response& res, NextFunction next) {
        req.body = polycpp::JsonObject{{"injected", "value"}};
        next(std::nullopt);
    });

    app.get("/test", [&](Request& req, Response& res) {
        if (req.body.isObject() &&
            req.body["injected"].asString() == "value") {
            bodyModified = true;
        }
    });

    IntegrationMock mock("GET", "/test");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(bodyModified);
}

TEST(IntegrationMiddlewareTest, MiddlewareCanModifyResponse) {
    Application app;

    app.use([](Request& req, Response& res, NextFunction next) {
        res.set("X-Custom-Header", "middleware-value");
        next(std::nullopt);
    });

    app.get("/test", [](Request& req, Response& res) {
        res.send("ok");
    });

    IntegrationMock mock("GET", "/test");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_EQ(getHeaderStr(mock.rawRes(),"X-Custom-Header"), "middleware-value");
}

TEST(IntegrationMiddlewareTest, NextPassesToNextMiddleware) {
    Application app;
    bool firstCalled = false;
    bool secondCalled = false;

    app.use([&](Request& req, Response& res, NextFunction next) {
        firstCalled = true;
        next(std::nullopt);  // Pass to next
    });

    app.use([&](Request& req, Response& res, NextFunction next) {
        secondCalled = true;
        next(std::nullopt);
    });

    IntegrationMock mock("GET", "/");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(firstCalled);
    EXPECT_TRUE(secondCalled);
}

TEST(IntegrationMiddlewareTest, MiddlewareWithoutNextStopsChain) {
    Application app;
    bool firstCalled = false;
    bool secondCalled = false;

    app.use([&](Request& req, Response& res, NextFunction next) {
        firstCalled = true;
        // Deliberately NOT calling next()
    });

    app.use([&](Request& req, Response& res, NextFunction next) {
        secondCalled = true;
        next(std::nullopt);
    });

    IntegrationMock mock("GET", "/");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(firstCalled);
    EXPECT_FALSE(secondCalled);
}

TEST(IntegrationMiddlewareTest, ErrorInMiddlewareTriggersErrorHandler) {
    Application app;
    bool errorHandled = false;

    app.use([](Request& req, Response& res, NextFunction next) {
        next(HttpError(500, "Something went wrong"));
    });

    app.use(ErrorHandler([&](const HttpError& err, Request& req, Response& res, NextFunction next) {
        errorHandled = true;
        EXPECT_EQ(err.statusCode(), 500);
        EXPECT_STREQ(err.what(), "Something went wrong");
        next(std::nullopt);
    }));

    IntegrationMock mock("GET", "/");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(errorHandled);
}

// ═══════════════════════════════════════════════════════════════════════
// Sub-Router Mounting Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(IntegrationSubRouterTest, RouterMountedAtPrefix) {
    Application app;
    Router apiRouter;
    bool handlerCalled = false;

    apiRouter.get("/users", [&](Request& req, Response& res) {
        handlerCalled = true;
    });

    app.use("/api", apiRouter);

    IntegrationMock mock("GET", "/api/users");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(handlerCalled);
}

TEST(IntegrationSubRouterTest, NestedRouters) {
    Application app;
    Router apiRouter;
    Router v1Router;
    bool handlerCalled = false;

    v1Router.get("/data", [&](Request& req, Response& res) {
        handlerCalled = true;
    });

    apiRouter.use("/v1", v1Router);
    app.use("/api", apiRouter);

    IntegrationMock mock("GET", "/api/v1/data");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(handlerCalled);
}

TEST(IntegrationSubRouterTest, RouterLevelMiddleware) {
    Application app;
    Router apiRouter;
    bool middlewareCalled = false;
    bool handlerCalled = false;

    apiRouter.use([&](Request& req, Response& res, NextFunction next) {
        middlewareCalled = true;
        next(std::nullopt);
    });

    apiRouter.get("/test", [&](Request& req, Response& res) {
        handlerCalled = true;
    });

    app.use("/api", apiRouter);

    IntegrationMock mock("GET", "/api/test");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(middlewareCalled);
    EXPECT_TRUE(handlerCalled);
}

TEST(IntegrationSubRouterTest, RouterDoesNotMatchOtherPrefixes) {
    Application app;
    Router apiRouter;
    bool handlerCalled = false;

    apiRouter.get("/users", [&](Request& req, Response& res) {
        handlerCalled = true;
    });

    app.use("/api", apiRouter);

    IntegrationMock mock("GET", "/admin/users");
    bool doneCalled = false;
    app.router().handle(mock.req(), mock.res(), [&](std::optional<HttpError>) {
        doneCalled = true;
    });

    EXPECT_FALSE(handlerCalled);
    EXPECT_TRUE(doneCalled);
}

// ═══════════════════════════════════════════════════════════════════════
// Sub-App Mounting Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(IntegrationSubAppTest, SubAppMountedAtPrefix) {
    Application mainApp;
    Application adminApp;
    bool adminHandlerCalled = false;

    adminApp.get("/dashboard", [&](Request& req, Response& res) {
        adminHandlerCalled = true;
    });

    mainApp.use("/admin", adminApp);

    IntegrationMock mock("GET", "/admin/dashboard");
    mainApp.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(adminHandlerCalled);
}

TEST(IntegrationSubAppTest, SubAppHasOwnSettings) {
    Application mainApp;
    Application subApp;

    mainApp.set("custom", "main-value");
    subApp.set("custom", "sub-value");

    EXPECT_EQ(mainApp.getSetting("custom").asString(), "main-value");
    EXPECT_EQ(subApp.getSetting("custom").asString(), "sub-value");
}

TEST(IntegrationSubAppTest, SubAppMiddleware) {
    Application mainApp;
    Application adminApp;
    bool adminMiddlewareCalled = false;
    bool adminHandlerCalled = false;

    adminApp.use([&](Request& req, Response& res, NextFunction next) {
        adminMiddlewareCalled = true;
        next(std::nullopt);
    });

    adminApp.get("/panel", [&](Request& req, Response& res) {
        adminHandlerCalled = true;
    });

    mainApp.use("/admin", adminApp);

    IntegrationMock mock("GET", "/admin/panel");
    mainApp.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(adminMiddlewareCalled);
    EXPECT_TRUE(adminHandlerCalled);
}

// ═══════════════════════════════════════════════════════════════════════
// Error Handling Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(IntegrationErrorTest, SynchronousErrorInHandlerCaughtByErrorMiddleware) {
    Application app;
    bool errorHandled = false;

    app.get("/error", [](Request& req, Response& res) {
        throw std::runtime_error("Sync error!");
    });

    app.use(ErrorHandler([&](const HttpError& err, Request& req, Response& res, NextFunction next) {
        errorHandled = true;
        EXPECT_EQ(err.statusCode(), 500);
        next(std::nullopt);
    }));

    IntegrationMock mock("GET", "/error");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(errorHandled);
}

TEST(IntegrationErrorTest, ErrorPassedViaNextReachesErrorHandler) {
    Application app;
    bool errorHandled = false;

    app.get("/error", MiddlewareHandler([](Request& req, Response& res, NextFunction next) {
        next(HttpError(403, "Forbidden!"));
    }));

    app.use(ErrorHandler([&](const HttpError& err, Request& req, Response& res, NextFunction next) {
        errorHandled = true;
        EXPECT_EQ(err.statusCode(), 403);
        EXPECT_STREQ(err.what(), "Forbidden!");
        next(std::nullopt);
    }));

    IntegrationMock mock("GET", "/error");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(errorHandled);
}

TEST(IntegrationErrorTest, DefaultErrorHandlerPropagatesError) {
    Application app;

    app.get("/error", MiddlewareHandler([](Request& req, Response& res, NextFunction next) {
        next(HttpError(422, "Unprocessable"));
    }));

    // No custom error handler registered
    IntegrationMock mock("GET", "/error");
    bool doneCalled = false;
    int errorCode = 0;

    app.router().handle(mock.req(), mock.res(), [&](std::optional<HttpError> err) {
        doneCalled = true;
        if (err) {
            errorCode = err->statusCode();
        }
    });

    EXPECT_TRUE(doneCalled);
    EXPECT_EQ(errorCode, 422);
}

TEST(IntegrationErrorTest, ErrorSkipsNormalMiddleware) {
    Application app;
    bool normalMiddlewareCalled = false;
    bool errorHandlerCalled = false;

    app.use([](Request& req, Response& res, NextFunction next) {
        next(HttpError(500, "Error"));
    });

    // This normal middleware should be skipped when error is propagating
    app.use([&](Request& req, Response& res, NextFunction next) {
        normalMiddlewareCalled = true;
        next(std::nullopt);
    });

    app.use(ErrorHandler([&](const HttpError& err, Request& req, Response& res, NextFunction next) {
        errorHandlerCalled = true;
        next(std::nullopt);
    }));

    IntegrationMock mock("GET", "/");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_FALSE(normalMiddlewareCalled);
    EXPECT_TRUE(errorHandlerCalled);
}

TEST(IntegrationErrorTest, MultipleErrorHandlers) {
    Application app;
    std::vector<int> handlerOrder;

    app.use([](Request& req, Response& res, NextFunction next) {
        next(HttpError(500, "Error"));
    });

    app.use(ErrorHandler([&](const HttpError& err, Request& req, Response& res, NextFunction next) {
        handlerOrder.push_back(1);
        next(err);  // Pass error to next error handler
    }));

    app.use(ErrorHandler([&](const HttpError& err, Request& req, Response& res, NextFunction next) {
        handlerOrder.push_back(2);
        next(std::nullopt);  // Clear the error
    }));

    IntegrationMock mock("GET", "/");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    ASSERT_EQ(handlerOrder.size(), 2u);
    EXPECT_EQ(handlerOrder[0], 1);
    EXPECT_EQ(handlerOrder[1], 2);
}

// ═══════════════════════════════════════════════════════════════════════
// Settings and Configuration Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(IntegrationSettingsTest, XPoweredByEnabledByDefault) {
    Application app;
    EXPECT_TRUE(app.enabled("x-powered-by"));
}

TEST(IntegrationSettingsTest, CanDisableXPoweredBy) {
    Application app;
    app.disable("x-powered-by");
    EXPECT_FALSE(app.enabled("x-powered-by"));
}

TEST(IntegrationSettingsTest, CustomSettings) {
    Application app;
    app.set("custom-key", "custom-value");

    auto val = app.getSetting("custom-key");
    EXPECT_TRUE(val.isString());
    EXPECT_EQ(val.asString(), "custom-value");
}

// ═══════════════════════════════════════════════════════════════════════
// Route Object Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(IntegrationRouteTest, RouteChainingWithDifferentMethods) {
    Application app;
    std::string lastMethod;

    app.route("/resource")
        .get([&](Request& req, Response& res) { lastMethod = "GET"; })
        .post([&](Request& req, Response& res) { lastMethod = "POST"; })
        .del([&](Request& req, Response& res) { lastMethod = "DELETE"; });

    {
        IntegrationMock mock("GET", "/resource");
        app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});
        EXPECT_EQ(lastMethod, "GET");
    }

    {
        IntegrationMock mock("POST", "/resource");
        app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});
        EXPECT_EQ(lastMethod, "POST");
    }

    {
        IntegrationMock mock("DELETE", "/resource");
        app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});
        EXPECT_EQ(lastMethod, "DELETE");
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Param Handler Integration Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(IntegrationParamTest, ParamHandlerCalledBeforeRoute) {
    Application app;
    std::vector<std::string> callOrder;

    app.param("id", [&](Request& req, Response& res, NextFunction next, const std::string& id) {
        callOrder.push_back("param:" + id);
        next(std::nullopt);
    });

    app.get("/users/:id", [&](Request& req, Response& res) {
        callOrder.push_back("handler");
    });

    IntegrationMock mock("GET", "/users/42");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    ASSERT_EQ(callOrder.size(), 2u);
    EXPECT_EQ(callOrder[0], "param:42");
    EXPECT_EQ(callOrder[1], "handler");
}

TEST(IntegrationParamTest, ParamHandlerCanRejectRequest) {
    Application app;
    bool errorHandled = false;

    app.param("id", [](Request& req, Response& res, NextFunction next, const std::string& id) {
        // Validate: id must be numeric
        for (char c : id) {
            if (!std::isdigit(c)) {
                next(HttpError::badRequest("Invalid ID: must be numeric"));
                return;
            }
        }
        next(std::nullopt);
    });

    app.get("/users/:id", [](Request& req, Response& res) {
        // Should not be called for non-numeric IDs
        FAIL() << "Handler should not be reached";
    });

    app.use(ErrorHandler([&](const HttpError& err, Request& req, Response& res, NextFunction next) {
        errorHandled = true;
        EXPECT_EQ(err.statusCode(), 400);
        next(std::nullopt);
    }));

    IntegrationMock mock("GET", "/users/abc");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(errorHandled);
}

// ═══════════════════════════════════════════════════════════════════════
// Body Parser Integration Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(IntegrationBodyParserTest, JsonBodyParserWithRouting) {
    Application app;
    std::string capturedName;

    app.use(json());
    app.post("/users", [&](Request& req, Response& res) {
        capturedName = req.body["name"].asString();
        res.status(201).json(polycpp::JsonObject{{"name", capturedName}});
    });

    IntegrationMock mock("POST", "/users",
        {{"content-type", "application/json"}},
        "{\"name\":\"Bob\"}");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_EQ(capturedName, "Bob");
    EXPECT_EQ(mock.rawRes().statusCode(), 201);
    EXPECT_EQ(getHeaderStr(mock.rawRes(),"Content-Type"), "application/json; charset=utf-8");
}

TEST(IntegrationBodyParserTest, UrlencodedBodyParserWithRouting) {
    Application app;
    std::string capturedUsername;

    app.use(urlencoded());
    app.post("/login", [&](Request& req, Response& res) {
        capturedUsername = req.body["username"].asString();
        res.send("Welcome " + capturedUsername);
    });

    IntegrationMock mock("POST", "/login",
        {{"content-type", "application/x-www-form-urlencoded"}},
        "username=admin&password=secret");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_EQ(capturedUsername, "admin");
}

TEST(IntegrationBodyParserTest, JsonBodyParserSkipsGetRequests) {
    Application app;
    bool handlerCalled = false;

    app.use(json());
    app.get("/test", [&](Request& req, Response& res) {
        handlerCalled = true;
        // Body should be null since GET with no JSON content-type
        EXPECT_TRUE(req.body.isNull());
    });

    IntegrationMock mock("GET", "/test");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(handlerCalled);
}

// ═══════════════════════════════════════════════════════════════════════
// All Methods Handler Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(IntegrationAllMethodsTest, AllHandlerMatchesEveryMethod) {
    Application app;
    int callCount = 0;

    app.all("/any", [&](Request& req, Response& res) {
        callCount++;
    });

    std::vector<std::string> methods = {"GET", "POST", "PUT", "DELETE", "PATCH"};
    for (const auto& method : methods) {
        IntegrationMock mock(method, "/any");
        app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});
    }

    EXPECT_EQ(callCount, 5);
}

// ═══════════════════════════════════════════════════════════════════════
// Application Callable (sub-app behavior) Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(IntegrationCallableTest, AppCanBeCalledAsMiddleware) {
    Application subApp;
    bool subHandlerCalled = false;

    subApp.get("/page", [&](Request& req, Response& res) {
        subHandlerCalled = true;
    });

    IntegrationMock mock("GET", "/page");
    NextFunction done = [](std::optional<HttpError>) {};
    subApp(mock.req(), mock.res(), done);

    EXPECT_TRUE(subHandlerCalled);
}

// ═══════════════════════════════════════════════════════════════════════
// Complex middleware pipeline
// ═══════════════════════════════════════════════════════════════════════

TEST(IntegrationPipelineTest, FullMiddlewarePipeline) {
    Application app;
    std::vector<std::string> callLog;

    // Logger middleware
    app.use([&](Request& req, Response& res, NextFunction next) {
        callLog.push_back("logger");
        next(std::nullopt);
    });

    // Auth middleware
    app.use([&](Request& req, Response& res, NextFunction next) {
        callLog.push_back("auth");
        next(std::nullopt);
    });

    // JSON body parser
    app.use(json());

    // Route handler
    app.post("/api/data", [&](Request& req, Response& res) {
        callLog.push_back("handler");
        res.status(200).json(polycpp::JsonObject{{"status", "ok"}});
    });

    IntegrationMock mock("POST", "/api/data",
        {{"content-type", "application/json"}},
        "{\"key\":\"value\"}");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    ASSERT_EQ(callLog.size(), 3u);
    EXPECT_EQ(callLog[0], "logger");
    EXPECT_EQ(callLog[1], "auth");
    EXPECT_EQ(callLog[2], "handler");
    EXPECT_EQ(mock.rawRes().statusCode(), 200);
}

// ═══════════════════════════════════════════════════════════════════════
// Application locals Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(IntegrationLocalsTest, AppLocalsAccessible) {
    Application app;
    app.locals = polycpp::JsonObject{{"appName", "TestApp"}};

    EXPECT_EQ(app.locals["appName"].asString(), "TestApp");
}

TEST(IntegrationLocalsTest, ResponseLocalsAccessible) {
    Application app;
    std::string capturedLocal;

    app.use([](Request& req, Response& res, NextFunction next) {
        res.locals = polycpp::JsonObject{{"user", "testUser"}};
        next(std::nullopt);
    });

    app.get("/test", [&](Request& req, Response& res) {
        capturedLocal = res.locals["user"].asString();
    });

    IntegrationMock mock("GET", "/test");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_EQ(capturedLocal, "testUser");
}
