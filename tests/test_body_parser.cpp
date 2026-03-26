/**
 * @file test_body_parser.cpp
 * @brief Tests for the body-parser middleware (json, urlencoded, raw, text).
 */

#include <gtest/gtest.h>
#include <polycpp/backend/event_context.hpp>
#include <polycpp/backend/tcp_socket.hpp>
#include <polycpp/event_loop.hpp>
#include <polycpp/express/express.hpp>
#include <polycpp/negotiate/detail/aggregator.hpp>

using namespace polycpp::express;

// ═══════════════════════════════════════════════════════════════════════
// Helper: Mock Request/Response for testing middleware dispatch
// ═══════════════════════════════════════════════════════════════════════

class BodyParserTestFixture {
public:
    BodyParserTestFixture(const std::string& method, const std::string& url,
                          const polycpp::JsonObject& headers = {},
                          const std::string& bodyContent = "")
        : msg_(), res_(createSocket(), "", 1, false) {
        msg_.method() = method;
        msg_.url() = url;
        msg_.headers() = headers;
        if (!bodyContent.empty()) {
            msg_.body() = polycpp::Buffer::from(bodyContent);
        }
        res_.on("error", [](const std::vector<std::any>&) {});
        req_ = std::make_unique<Request>(msg_, nullptr);
        resp_ = std::make_unique<Response>(res_, nullptr);
        req_->setRes(resp_.get());
        resp_->setReq(req_.get());
    }

    Request& req() { return *req_; }
    Response& res() { return *resp_; }
    polycpp::http::ServerResponse& rawRes() { return res_; }
    polycpp::http::IncomingMessage& rawReq() { return msg_; }

private:
    static std::shared_ptr<polycpp::backend::TcpSocket> createSocket() {
        auto& ctx = polycpp::EventLoop::instance().context();
        return std::make_shared<polycpp::backend::TcpSocket>(ctx);
    }

    polycpp::http::IncomingMessage msg_;
    polycpp::http::ServerResponse res_;
    std::unique_ptr<Request> req_;
    std::unique_ptr<Response> resp_;
};

// ═══════════════════════════════════════════════════════════════════════
// express::json() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(JsonParserTest, ParsesValidJsonBody) {
    BodyParserTestFixture f("POST", "/api",
        {{"content-type", "application/json"}},
        "{\"name\":\"John\",\"age\":30}");

    auto parser = json();
    bool nextCalled = false;
    std::optional<HttpError> nextErr;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        nextErr = err;
    });

    EXPECT_TRUE(nextCalled);
    EXPECT_FALSE(nextErr.has_value());
    ASSERT_TRUE(f.req().body.isObject());
    EXPECT_EQ(f.req().body["name"].asString(), "John");
    EXPECT_EQ(f.req().body["age"].asNumber(), 30.0);
}

TEST(JsonParserTest, SetsReqBodyToParsedJsonValue) {
    BodyParserTestFixture f("POST", "/api",
        {{"content-type", "application/json"}},
        "[1,2,3]");

    auto parser = json();
    parser(f.req(), f.res(), [](std::optional<HttpError>) {});

    ASSERT_TRUE(f.req().body.isArray());
    EXPECT_EQ(f.req().body.asArray().size(), 3u);
}

TEST(JsonParserTest, RejectsNonJsonContentType) {
    BodyParserTestFixture f("POST", "/api",
        {{"content-type", "text/plain"}},
        "{\"key\":\"value\"}");

    auto parser = json();
    bool nextCalled = false;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        // Should call next without error (just skip parsing)
        EXPECT_FALSE(err.has_value());
    });

    EXPECT_TRUE(nextCalled);
    // Body should not be parsed
    EXPECT_TRUE(f.req().body.isNull());
}

TEST(JsonParserTest, HandlesEmptyBody) {
    BodyParserTestFixture f("POST", "/api",
        {{"content-type", "application/json"}},
        "");

    auto parser = json();
    bool nextCalled = false;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        // Empty body should pass through without error
        EXPECT_FALSE(err.has_value());
    });

    EXPECT_TRUE(nextCalled);
}

TEST(JsonParserTest, RejectsMalformedJson) {
    BodyParserTestFixture f("POST", "/api",
        {{"content-type", "application/json"}},
        "{invalid json}");

    auto parser = json();
    bool nextCalled = false;
    std::optional<HttpError> nextErr;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        nextErr = err;
    });

    EXPECT_TRUE(nextCalled);
    ASSERT_TRUE(nextErr.has_value());
    EXPECT_EQ(nextErr->statusCode(), 400);
}

TEST(JsonParserTest, StrictModeRejectsPrimitives) {
    BodyParserTestFixture f("POST", "/api",
        {{"content-type", "application/json"}},
        "\"just a string\"");

    JsonParserOptions opts;
    opts.strict = true;
    auto parser = jsonParser(opts);
    bool nextCalled = false;
    std::optional<HttpError> nextErr;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        nextErr = err;
    });

    EXPECT_TRUE(nextCalled);
    ASSERT_TRUE(nextErr.has_value());
    EXPECT_EQ(nextErr->statusCode(), 400);
}

TEST(JsonParserTest, NonStrictModeAcceptsPrimitives) {
    BodyParserTestFixture f("POST", "/api",
        {{"content-type", "application/json"}},
        "\"just a string\"");

    JsonParserOptions opts;
    opts.strict = false;
    auto parser = jsonParser(opts);
    bool nextCalled = false;
    std::optional<HttpError> nextErr;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        nextErr = err;
    });

    EXPECT_TRUE(nextCalled);
    EXPECT_FALSE(nextErr.has_value());
    EXPECT_TRUE(f.req().body.isString());
    EXPECT_EQ(f.req().body.asString(), "just a string");
}

TEST(JsonParserTest, NoContentTypeSkipsParsing) {
    BodyParserTestFixture f("POST", "/api",
        {},  // no content-type header
        "{\"key\":\"value\"}");

    auto parser = json();
    bool nextCalled = false;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        EXPECT_FALSE(err.has_value());
    });

    EXPECT_TRUE(nextCalled);
    EXPECT_TRUE(f.req().body.isNull());
}

TEST(JsonParserTest, ParsesNestedJson) {
    BodyParserTestFixture f("POST", "/api",
        {{"content-type", "application/json"}},
        "{\"user\":{\"name\":\"Alice\",\"address\":{\"city\":\"NYC\"}}}");

    auto parser = json();
    parser(f.req(), f.res(), [](std::optional<HttpError>) {});

    ASSERT_TRUE(f.req().body.isObject());
    ASSERT_TRUE(f.req().body["user"].isObject());
    EXPECT_EQ(f.req().body["user"]["name"].asString(), "Alice");
    EXPECT_EQ(f.req().body["user"]["address"]["city"].asString(), "NYC");
}

TEST(JsonParserTest, ContentTypeWithCharset) {
    BodyParserTestFixture f("POST", "/api",
        {{"content-type", "application/json; charset=utf-8"}},
        "{\"key\":\"value\"}");

    auto parser = json();
    bool nextCalled = false;
    std::optional<HttpError> nextErr;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        nextErr = err;
    });

    EXPECT_TRUE(nextCalled);
    EXPECT_FALSE(nextErr.has_value());
    ASSERT_TRUE(f.req().body.isObject());
    EXPECT_EQ(f.req().body["key"].asString(), "value");
}

// ═══════════════════════════════════════════════════════════════════════
// express::urlencoded() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(UrlencodedParserTest, ParsesSimpleUrlEncodedBody) {
    BodyParserTestFixture f("POST", "/login",
        {{"content-type", "application/x-www-form-urlencoded"}},
        "username=john&password=secret");

    auto parser = urlencoded();
    bool nextCalled = false;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        EXPECT_FALSE(err.has_value());
    });

    EXPECT_TRUE(nextCalled);
    ASSERT_TRUE(f.req().body.isObject());
    EXPECT_EQ(f.req().body["username"].asString(), "john");
    EXPECT_EQ(f.req().body["password"].asString(), "secret");
}

TEST(UrlencodedParserTest, RejectsNonUrlencodedContentType) {
    BodyParserTestFixture f("POST", "/",
        {{"content-type", "application/json"}},
        "key=value");

    auto parser = urlencoded();
    bool nextCalled = false;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        EXPECT_FALSE(err.has_value());
    });

    EXPECT_TRUE(nextCalled);
    EXPECT_TRUE(f.req().body.isNull());
}

TEST(UrlencodedParserTest, HandlesEmptyBody) {
    BodyParserTestFixture f("POST", "/",
        {{"content-type", "application/x-www-form-urlencoded"}},
        "");

    auto parser = urlencoded();
    bool nextCalled = false;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        EXPECT_FALSE(err.has_value());
    });

    EXPECT_TRUE(nextCalled);
}

TEST(UrlencodedParserTest, ExtendedParsesNestedKeys) {
    BodyParserTestFixture f("POST", "/",
        {{"content-type", "application/x-www-form-urlencoded"}},
        "a[b]=c&a[d]=e");

    UrlencodedOptions opts;
    opts.extended = true;
    auto parser = urlencodedParser(opts);

    parser(f.req(), f.res(), [](std::optional<HttpError>) {});

    ASSERT_TRUE(f.req().body.isObject());
    ASSERT_TRUE(f.req().body["a"].isObject());
    EXPECT_EQ(f.req().body["a"]["b"].asString(), "c");
    EXPECT_EQ(f.req().body["a"]["d"].asString(), "e");
}

TEST(UrlencodedParserTest, NoContentTypeSkipsParsing) {
    BodyParserTestFixture f("POST", "/",
        {},  // no content-type header
        "key=value");

    auto parser = urlencoded();
    bool nextCalled = false;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        EXPECT_FALSE(err.has_value());
    });

    EXPECT_TRUE(nextCalled);
    EXPECT_TRUE(f.req().body.isNull());
}

TEST(UrlencodedParserTest, ParsesSpecialCharacters) {
    BodyParserTestFixture f("POST", "/",
        {{"content-type", "application/x-www-form-urlencoded"}},
        "msg=hello%20world&special=%26%3D");

    auto parser = urlencoded();
    parser(f.req(), f.res(), [](std::optional<HttpError>) {});

    ASSERT_TRUE(f.req().body.isObject());
    EXPECT_EQ(f.req().body["msg"].asString(), "hello world");
    EXPECT_EQ(f.req().body["special"].asString(), "&=");
}

// ═══════════════════════════════════════════════════════════════════════
// express::text() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(TextParserTest, ParsesTextBody) {
    BodyParserTestFixture f("POST", "/",
        {{"content-type", "text/plain"}},
        "Hello, World!");

    auto parser = text();
    bool nextCalled = false;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        EXPECT_FALSE(err.has_value());
    });

    EXPECT_TRUE(nextCalled);
    EXPECT_TRUE(f.req().body.isString());
    EXPECT_EQ(f.req().body.asString(), "Hello, World!");
}

TEST(TextParserTest, RejectsNonTextContentType) {
    BodyParserTestFixture f("POST", "/",
        {{"content-type", "application/json"}},
        "some text");

    auto parser = text();
    bool nextCalled = false;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        EXPECT_FALSE(err.has_value());
    });

    EXPECT_TRUE(nextCalled);
    EXPECT_TRUE(f.req().body.isNull());
}

TEST(TextParserTest, HandlesEmptyBody) {
    BodyParserTestFixture f("POST", "/",
        {{"content-type", "text/plain"}},
        "");

    auto parser = text();
    bool nextCalled = false;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        EXPECT_FALSE(err.has_value());
    });

    EXPECT_TRUE(nextCalled);
}

TEST(TextParserTest, NoContentTypeSkipsParsing) {
    BodyParserTestFixture f("POST", "/",
        {},
        "text body");

    auto parser = text();
    bool nextCalled = false;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        EXPECT_FALSE(err.has_value());
    });

    EXPECT_TRUE(nextCalled);
    EXPECT_TRUE(f.req().body.isNull());
}

// ═══════════════════════════════════════════════════════════════════════
// express::raw() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(RawParserTest, ParsesRawBody) {
    BodyParserTestFixture f("POST", "/",
        {{"content-type", "application/octet-stream"}},
        "raw binary data");

    auto parser = raw();
    bool nextCalled = false;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        EXPECT_FALSE(err.has_value());
    });

    EXPECT_TRUE(nextCalled);
    EXPECT_TRUE(f.req().body.isString());
    EXPECT_EQ(f.req().body.asString(), "raw binary data");
}

TEST(RawParserTest, RejectsNonOctetStreamContentType) {
    BodyParserTestFixture f("POST", "/",
        {{"content-type", "text/plain"}},
        "some data");

    auto parser = raw();
    bool nextCalled = false;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        EXPECT_FALSE(err.has_value());
    });

    EXPECT_TRUE(nextCalled);
    EXPECT_TRUE(f.req().body.isNull());
}

TEST(RawParserTest, HandlesEmptyBody) {
    BodyParserTestFixture f("POST", "/",
        {{"content-type", "application/octet-stream"}},
        "");

    auto parser = raw();
    bool nextCalled = false;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        EXPECT_FALSE(err.has_value());
    });

    EXPECT_TRUE(nextCalled);
    // Empty raw body is set as empty string
    EXPECT_TRUE(f.req().body.isString());
    EXPECT_EQ(f.req().body.asString(), "");
}

TEST(RawParserTest, NoContentTypeSkipsParsing) {
    BodyParserTestFixture f("POST", "/",
        {},
        "raw data");

    auto parser = raw();
    bool nextCalled = false;

    parser(f.req(), f.res(), [&](std::optional<HttpError> err) {
        nextCalled = true;
        EXPECT_FALSE(err.has_value());
    });

    EXPECT_TRUE(nextCalled);
    EXPECT_TRUE(f.req().body.isNull());
}
