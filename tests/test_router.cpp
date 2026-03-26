/**
 * @file test_router.cpp
 * @brief Tests for the Router, Route, and Layer classes.
 */

#include <gtest/gtest.h>
#include <polycpp/express/express.hpp>

using namespace polycpp::express;
namespace edetail = polycpp::express::detail;

// ═══════════════════════════════════════════════════════════════════════
// Helper: Mock Request/Response for testing middleware dispatch
// ═══════════════════════════════════════════════════════════════════════

class MockRequestResponse {
public:
    MockRequestResponse(const std::string& method, const std::string& url)
        : msg_(), res_(msg_.socket(), "", 1, false) {
        msg_.method() = method;
        msg_.url() = url;
        msg_.headers() = polycpp::JsonObject{};
        req_ = std::make_unique<Request>(msg_, nullptr);
        resp_ = std::make_unique<Response>(res_, nullptr);
        req_->setRes(resp_.get());
        resp_->setReq(req_.get());
    }

    Request& req() { return *req_; }
    Response& res() { return *resp_; }

private:
    polycpp::http::IncomingMessage msg_;
    polycpp::http::ServerResponse res_;
    std::unique_ptr<Request> req_;
    std::unique_ptr<Response> resp_;
};

// ═══════════════════════════════════════════════════════════════════════
// Layer Tests (Extended)
// ═══════════════════════════════════════════════════════════════════════

TEST(LayerTest, MatchMultipleParams) {
    Layer layer("/users/:userId/posts/:postId",
                MiddlewareHandler([](Request&, Response&, NextFunction next) {
                    next(std::nullopt);
                }));

    EXPECT_TRUE(layer.match("/users/42/posts/100"));
    auto& params = layer.matchedParams();

    auto userIt = params.find("userId");
    ASSERT_NE(userIt, params.end());
    EXPECT_EQ(std::get<std::string>(userIt->second), "42");

    auto postIt = params.find("postId");
    ASSERT_NE(postIt, params.end());
    EXPECT_EQ(std::get<std::string>(postIt->second), "100");
}

TEST(LayerTest, NoMatchDifferentPath) {
    Layer layer("/api/v1/users",
                MiddlewareHandler([](Request&, Response&, NextFunction next) {
                    next(std::nullopt);
                }));

    EXPECT_FALSE(layer.match("/api/v2/users"));
}

TEST(LayerTest, NameProperty) {
    Layer layer("/", MiddlewareHandler([](Request&, Response&, NextFunction next) {
        next(std::nullopt);
    }));

    EXPECT_EQ(layer.name(), "<anonymous>");
    layer.setName("myMiddleware");
    EXPECT_EQ(layer.name(), "myMiddleware");
}

// ═══════════════════════════════════════════════════════════════════════
// Route Tests (Extended)
// ═══════════════════════════════════════════════════════════════════════

TEST(RouteTest, DispatchCallsHandler) {
    Route route("/test");
    bool handlerCalled = false;

    route.get([&](Request& req, Response& res) {
        handlerCalled = true;
    });

    MockRequestResponse mock("GET", "/test");
    bool doneCalled = false;

    route.dispatch(mock.req(), mock.res(), [&](std::optional<HttpError> err) {
        doneCalled = true;
    });

    EXPECT_TRUE(handlerCalled);
}

TEST(RouteTest, DispatchSkipsWrongMethod) {
    Route route("/test");
    bool handlerCalled = false;

    route.get([&](Request& req, Response& res) {
        handlerCalled = true;
    });

    MockRequestResponse mock("POST", "/test");
    bool doneCalled = false;

    route.dispatch(mock.req(), mock.res(), [&](std::optional<HttpError> err) {
        doneCalled = true;
    });

    EXPECT_FALSE(handlerCalled);
    EXPECT_TRUE(doneCalled);
}

TEST(RouteTest, DispatchMultipleHandlers) {
    Route route("/test");
    int callCount = 0;

    route.get(MiddlewareHandler([&](Request& req, Response& res, NextFunction next) {
        callCount++;
        next(std::nullopt);
    }));

    route.get([&](Request& req, Response& res) {
        callCount++;
    });

    MockRequestResponse mock("GET", "/test");

    route.dispatch(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_EQ(callCount, 2);
}

TEST(RouteTest, DispatchErrorPropagation) {
    Route route("/test");

    route.get(MiddlewareHandler([](Request& req, Response& res, NextFunction next) {
        next(HttpError(400, "Bad Request"));
    }));

    route.get([](Request& req, Response& res) {
        // Should not be called
        FAIL() << "Handler should not be called after error";
    });

    MockRequestResponse mock("GET", "/test");
    bool errorReceived = false;

    route.dispatch(mock.req(), mock.res(), [&](std::optional<HttpError> err) {
        if (err) {
            errorReceived = true;
            EXPECT_EQ(err->statusCode(), 400);
        }
    });

    EXPECT_TRUE(errorReceived);
}

TEST(RouteTest, PathAccessor) {
    Route route("/api/v1/users");
    EXPECT_EQ(route.path(), "/api/v1/users");
}

// ═══════════════════════════════════════════════════════════════════════
// Router Tests (Extended)
// ═══════════════════════════════════════════════════════════════════════

TEST(RouterTest, MiddlewareExecution) {
    Router router;
    bool middlewareCalled = false;

    router.use([&](Request& req, Response& res, NextFunction next) {
        middlewareCalled = true;
        next(std::nullopt);
    });

    MockRequestResponse mock("GET", "/anything");

    router.handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(middlewareCalled);
}

TEST(RouterTest, RouteMatching) {
    Router router;
    bool handlerCalled = false;

    router.get("/test", [&](Request& req, Response& res) {
        handlerCalled = true;
    });

    MockRequestResponse mock("GET", "/test");

    router.handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(handlerCalled);
}

TEST(RouterTest, RouteNotMatching) {
    Router router;
    bool handlerCalled = false;

    router.get("/test", [&](Request& req, Response& res) {
        handlerCalled = true;
    });

    MockRequestResponse mock("GET", "/other");
    bool doneCalled = false;

    router.handle(mock.req(), mock.res(), [&](std::optional<HttpError> err) {
        doneCalled = true;
        EXPECT_FALSE(err.has_value());
    });

    EXPECT_FALSE(handlerCalled);
    EXPECT_TRUE(doneCalled);
}

TEST(RouterTest, MiddlewareChaining) {
    Router router;
    std::vector<int> order;

    router.use([&](Request& req, Response& res, NextFunction next) {
        order.push_back(1);
        next(std::nullopt);
    });

    router.use([&](Request& req, Response& res, NextFunction next) {
        order.push_back(2);
        next(std::nullopt);
    });

    router.get("/test", [&](Request& req, Response& res) {
        order.push_back(3);
    });

    MockRequestResponse mock("GET", "/test");
    router.handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(RouterTest, ErrorHandlerCatchesError) {
    Router router;
    bool errorHandled = false;

    router.use([](Request& req, Response& res, NextFunction next) {
        next(HttpError(500, "test error"));
    });

    router.use(ErrorHandler([&](const HttpError& err, Request& req, Response& res, NextFunction next) {
        errorHandled = true;
        EXPECT_EQ(err.statusCode(), 500);
        EXPECT_STREQ(err.what(), "test error");
        next(std::nullopt);
    }));

    MockRequestResponse mock("GET", "/");
    router.handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(errorHandled);
}

TEST(RouterTest, ErrorHandlerSkippedWhenNoError) {
    Router router;
    bool errorHandlerCalled = false;
    bool routeHandlerCalled = false;

    router.use(ErrorHandler([&](const HttpError& err, Request& req, Response& res, NextFunction next) {
        errorHandlerCalled = true;
        next(std::nullopt);
    }));

    router.get("/test", [&](Request& req, Response& res) {
        routeHandlerCalled = true;
    });

    MockRequestResponse mock("GET", "/test");
    router.handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_FALSE(errorHandlerCalled);
    EXPECT_TRUE(routeHandlerCalled);
}

TEST(RouterTest, ParamExtraction) {
    Router router;
    std::string capturedId;

    router.get("/users/:id", [&](Request& req, Response& res) {
        capturedId = req.params["id"].asString();
    });

    MockRequestResponse mock("GET", "/users/42");
    router.handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_EQ(capturedId, "42");
}

TEST(RouterTest, RouteChaining) {
    Router router;
    bool getCalled = false;
    bool postCalled = false;

    router.route("/api")
        .get([&](Request& req, Response& res) { getCalled = true; })
        .post([&](Request& req, Response& res) { postCalled = true; });

    {
        MockRequestResponse mock("GET", "/api");
        router.handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});
    }
    EXPECT_TRUE(getCalled);

    {
        MockRequestResponse mock("POST", "/api");
        router.handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});
    }
    EXPECT_TRUE(postCalled);
}

TEST(RouterTest, AllMethodHandler) {
    Router router;
    int callCount = 0;

    router.all("/catch-all", [&](Request& req, Response& res) {
        callCount++;
    });

    {
        MockRequestResponse mock("GET", "/catch-all");
        router.handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});
    }
    {
        MockRequestResponse mock("POST", "/catch-all");
        router.handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});
    }
    {
        MockRequestResponse mock("DELETE", "/catch-all");
        router.handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});
    }

    EXPECT_EQ(callCount, 3);
}

TEST(RouterTest, HttpMethodRouting) {
    Router router;
    std::string lastMethod;

    router.get("/test", [&](Request& req, Response& res) { lastMethod = "GET"; });
    router.post("/test", [&](Request& req, Response& res) { lastMethod = "POST"; });
    router.put("/test", [&](Request& req, Response& res) { lastMethod = "PUT"; });
    router.del("/test", [&](Request& req, Response& res) { lastMethod = "DELETE"; });
    router.patch("/test", [&](Request& req, Response& res) { lastMethod = "PATCH"; });

    auto testMethod = [&](const std::string& method, const std::string& expected) {
        MockRequestResponse mock(method, "/test");
        router.handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});
        EXPECT_EQ(lastMethod, expected) << "Failed for method: " << method;
    };

    testMethod("GET", "GET");
    testMethod("POST", "POST");
    testMethod("PUT", "PUT");
    testMethod("DELETE", "DELETE");
    testMethod("PATCH", "PATCH");
}

TEST(RouterTest, ExceptionInHandlerBecomesError) {
    Router router;
    bool errorHandled = false;

    router.get("/throws", [](Request& req, Response& res) {
        throw std::runtime_error("oops");
    });

    router.use(ErrorHandler([&](const HttpError& err, Request& req, Response& res, NextFunction next) {
        errorHandled = true;
        EXPECT_EQ(err.statusCode(), 500);
        next(std::nullopt);
    }));

    MockRequestResponse mock("GET", "/throws");
    router.handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(errorHandled);
}

// ═══════════════════════════════════════════════════════════════════════
// Application Tests (Extended)
// ═══════════════════════════════════════════════════════════════════════

TEST(ApplicationTest, RoutingDelegation) {
    Application app;
    bool handlerCalled = false;

    app.get("/hello", [&](Request& req, Response& res) {
        handlerCalled = true;
    });

    MockRequestResponse mock("GET", "/hello");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(handlerCalled);
}

TEST(ApplicationTest, MiddlewareDelegation) {
    Application app;
    bool middlewareCalled = false;

    app.use([&](Request& req, Response& res, NextFunction next) {
        middlewareCalled = true;
        next(std::nullopt);
    });

    MockRequestResponse mock("GET", "/");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(middlewareCalled);
}

TEST(ApplicationTest, NumericSettings) {
    Application app;
    app.set("port", 3000.0);
    auto val = app.getSetting("port");
    EXPECT_TRUE(val.isNumber());
    EXPECT_EQ(val.asNumber(), 3000.0);
}

TEST(ApplicationTest, EngineRegistration) {
    Application app;
    bool engineCalled = false;

    app.engine("test", [&](const std::string& path, const polycpp::JsonValue& options,
                            std::function<void(std::optional<std::string>, std::string)> cb) {
        engineCalled = true;
        cb(std::nullopt, "<html>rendered</html>");
    });

    // Engine registration should succeed without throwing
    EXPECT_FALSE(engineCalled);
}

TEST(ApplicationTest, SubAppCallable) {
    Application mainApp;
    Application subApp;
    bool subHandlerCalled = false;

    subApp.get("/dashboard", [&](Request& req, Response& res) {
        subHandlerCalled = true;
    });

    // Sub-app should be callable
    MockRequestResponse mock("GET", "/dashboard");
    NextFunction next = [](std::optional<HttpError>) {};
    subApp(mock.req(), mock.res(), next);

    EXPECT_TRUE(subHandlerCalled);
}

TEST(ApplicationTest, ParamHandler) {
    Application app;
    bool paramHandlerCalled = false;
    std::string capturedParam;

    app.param("id", [&](Request& req, Response& res, NextFunction next, const std::string& id) {
        paramHandlerCalled = true;
        capturedParam = id;
        next(std::nullopt);
    });

    app.get("/users/:id", [](Request& req, Response& res) {});

    MockRequestResponse mock("GET", "/users/123");
    app.router().handle(mock.req(), mock.res(), [](std::optional<HttpError>) {});

    EXPECT_TRUE(paramHandlerCalled);
    EXPECT_EQ(capturedParam, "123");
}

// ═══════════════════════════════════════════════════════════════════════
// Request Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(RequestTest, MethodAndUrl) {
    polycpp::http::IncomingMessage msg;
    msg.method() = "POST";
    msg.url() = "/api/data?key=value";
    msg.headers() = polycpp::JsonObject{};

    Request req(msg, nullptr);
    EXPECT_EQ(req.method(), "POST");
    EXPECT_EQ(req.url(), "/api/data?key=value");
}

TEST(RequestTest, PathExtraction) {
    polycpp::http::IncomingMessage msg;
    msg.method() = "GET";
    msg.url() = "/api/data?key=value#hash";
    msg.headers() = polycpp::JsonObject{};

    Request req(msg, nullptr);
    // path() strips query string
    EXPECT_EQ(req.path(), "/api/data");
}

TEST(RequestTest, QueryParsing) {
    polycpp::http::IncomingMessage msg;
    msg.method() = "GET";
    msg.url() = "/search?q=hello&page=2";
    msg.headers() = polycpp::JsonObject{};

    Request req(msg, nullptr);
    auto q = req.query();
    ASSERT_TRUE(q.isObject());
    EXPECT_EQ(q["q"].asString(), "hello");
    EXPECT_EQ(q["page"].asString(), "2");
}

TEST(RequestTest, QueryEmpty) {
    polycpp::http::IncomingMessage msg;
    msg.method() = "GET";
    msg.url() = "/no-query";
    msg.headers() = polycpp::JsonObject{};

    Request req(msg, nullptr);
    auto q = req.query();
    ASSERT_TRUE(q.isObject());
    EXPECT_TRUE(q.asObject().empty());
}

TEST(RequestTest, HeaderAccess) {
    polycpp::http::IncomingMessage msg;
    msg.method() = "GET";
    msg.url() = "/";
    msg.headers() = polycpp::JsonObject{
        {"content-type", "application/json"},
        {"accept", "text/html"}
    };

    Request req(msg, nullptr);

    auto ct = req.get("Content-Type");
    ASSERT_TRUE(ct.has_value());
    EXPECT_EQ(*ct, "application/json");

    auto accept = req.header("Accept");
    ASSERT_TRUE(accept.has_value());
    EXPECT_EQ(*accept, "text/html");

    auto missing = req.get("X-Custom");
    EXPECT_FALSE(missing.has_value());
}

TEST(RequestTest, XhrDetection) {
    polycpp::http::IncomingMessage msg;
    msg.method() = "GET";
    msg.url() = "/";
    msg.headers() = polycpp::JsonObject{
        {"x-requested-with", "XMLHttpRequest"}
    };

    Request req(msg, nullptr);
    EXPECT_TRUE(req.xhr());
}

TEST(RequestTest, XhrDetectionFalse) {
    polycpp::http::IncomingMessage msg;
    msg.method() = "GET";
    msg.url() = "/";
    msg.headers() = polycpp::JsonObject{};

    Request req(msg, nullptr);
    EXPECT_FALSE(req.xhr());
}

TEST(RequestTest, ProtocolFromHeaderNoTrust) {
    // Without trust proxy, X-Forwarded-Proto is ignored
    polycpp::http::IncomingMessage msg;
    msg.method() = "GET";
    msg.url() = "/";
    msg.headers() = polycpp::JsonObject{
        {"x-forwarded-proto", "https"}
    };

    Request req(msg, nullptr);
    EXPECT_EQ(req.protocol(), "http");
    EXPECT_FALSE(req.secure());
}

TEST(RequestTest, ProtocolFromHeaderWithTrust) {
    // With trust proxy = true, X-Forwarded-Proto is read
    Application app;
    app.set("trust proxy", true);

    polycpp::http::IncomingMessage msg;
    msg.method() = "GET";
    msg.url() = "/";
    msg.headers() = polycpp::JsonObject{
        {"x-forwarded-proto", "https"}
    };

    Request req(msg, &app);
    EXPECT_EQ(req.protocol(), "https");
    EXPECT_TRUE(req.secure());
}

TEST(RequestTest, ProtocolDefault) {
    polycpp::http::IncomingMessage msg;
    msg.method() = "GET";
    msg.url() = "/";
    msg.headers() = polycpp::JsonObject{};

    Request req(msg, nullptr);
    EXPECT_EQ(req.protocol(), "http");
    EXPECT_FALSE(req.secure());
}

TEST(RequestTest, IpsEmptyWithoutTrust) {
    // Without trust proxy, ips() returns empty
    polycpp::http::IncomingMessage msg;
    msg.method() = "GET";
    msg.url() = "/";
    msg.headers() = polycpp::JsonObject{
        {"x-forwarded-for", "1.2.3.4, 5.6.7.8"}
    };

    Request req(msg, nullptr);
    auto ips = req.ips();
    EXPECT_TRUE(ips.empty());
}

TEST(RequestTest, IpsFromForwardedWithTrust) {
    // With trust proxy = true, ips() returns the trusted addresses
    Application app;
    app.set("trust proxy", true);

    polycpp::http::IncomingMessage msg;
    msg.method() = "GET";
    msg.url() = "/";
    msg.headers() = polycpp::JsonObject{
        {"x-forwarded-for", "1.2.3.4, 5.6.7.8"}
    };

    Request req(msg, &app);
    auto ips = req.ips();
    // addrs = [1.2.3.4, 5.6.7.8, ""] (socket addr is empty for mock)
    // trust=true, all trusted, no truncation
    // reverse: ["", 5.6.7.8, 1.2.3.4]
    // pop: removes 1.2.3.4 -> ["", 5.6.7.8]
    ASSERT_EQ(ips.size(), 2u);
    EXPECT_EQ(ips[0], "");        // socket addr (empty in mock)
    EXPECT_EQ(ips[1], "5.6.7.8");
}

TEST(RequestTest, HostnameFromHeader) {
    polycpp::http::IncomingMessage msg;
    msg.method() = "GET";
    msg.url() = "/";
    msg.headers() = polycpp::JsonObject{
        {"host", "example.com:3000"}
    };

    Request req(msg, nullptr);
    auto hostname = req.hostname();
    ASSERT_TRUE(hostname.has_value());
    EXPECT_EQ(*hostname, "example.com");
}

TEST(RequestTest, HostnameNoPort) {
    polycpp::http::IncomingMessage msg;
    msg.method() = "GET";
    msg.url() = "/";
    msg.headers() = polycpp::JsonObject{
        {"host", "example.com"}
    };

    Request req(msg, nullptr);
    auto hostname = req.hostname();
    ASSERT_TRUE(hostname.has_value());
    EXPECT_EQ(*hostname, "example.com");
}

TEST(RequestTest, Params) {
    polycpp::http::IncomingMessage msg;
    msg.method() = "GET";
    msg.url() = "/users/42";
    msg.headers() = polycpp::JsonObject{};

    Request req(msg, nullptr);
    req.params = polycpp::JsonObject{{"id", "42"}};
    EXPECT_EQ(req.params["id"].asString(), "42");
}

TEST(RequestTest, Body) {
    polycpp::http::IncomingMessage msg;
    msg.method() = "POST";
    msg.url() = "/";
    msg.headers() = polycpp::JsonObject{};

    Request req(msg, nullptr);
    req.body = polycpp::JsonObject{{"name", "John"}, {"age", 30.0}};
    EXPECT_EQ(req.body["name"].asString(), "John");
    EXPECT_EQ(req.body["age"].asNumber(), 30.0);
}

TEST(RequestTest, StaleIsFreshInverse) {
    polycpp::http::IncomingMessage msg;
    msg.method() = "POST";
    msg.url() = "/";
    msg.headers() = polycpp::JsonObject{};

    Request req(msg, nullptr);
    // POST is never fresh
    EXPECT_FALSE(req.fresh());
    EXPECT_TRUE(req.stale());
}

TEST(RequestTest, BaseUrl) {
    polycpp::http::IncomingMessage msg;
    msg.method() = "GET";
    msg.url() = "/";
    msg.headers() = polycpp::JsonObject{};

    Request req(msg, nullptr);
    EXPECT_EQ(req.baseUrl(), "");

    req.setBaseUrl("/api");
    EXPECT_EQ(req.baseUrl(), "/api");
}
