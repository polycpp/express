/**
 * @file test_express_basic.cpp
 * @brief Basic tests for polycpp Express.
 */

#include <gtest/gtest.h>
#include <polycpp/express/express.hpp>

using namespace polycpp::express;
namespace edetail = polycpp::express::detail;

// ═══════════════════════════════════════════════════════════════════════
// HttpError Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(HttpErrorTest, ConstructWithCodeAndMessage) {
    HttpError err(404, "Not Found");
    EXPECT_EQ(err.statusCode(), 404);
    EXPECT_STREQ(err.what(), "Not Found");
    EXPECT_TRUE(err.expose());  // 4xx errors are exposed
}

TEST(HttpErrorTest, ConstructWith5xxNotExposed) {
    HttpError err(500, "Internal Server Error");
    EXPECT_EQ(err.statusCode(), 500);
    EXPECT_FALSE(err.expose());  // 5xx errors are not exposed
}

TEST(HttpErrorTest, DefaultMessage) {
    HttpError err(404);
    EXPECT_EQ(err.statusCode(), 404);
    EXPECT_STREQ(err.what(), "Not Found");
}

TEST(HttpErrorTest, FactoryMethods) {
    auto bad = HttpError::badRequest("bad");
    EXPECT_EQ(bad.statusCode(), 400);

    auto unauth = HttpError::unauthorized();
    EXPECT_EQ(unauth.statusCode(), 401);

    auto forbidden = HttpError::forbidden();
    EXPECT_EQ(forbidden.statusCode(), 403);

    auto notFound = HttpError::notFound();
    EXPECT_EQ(notFound.statusCode(), 404);

    auto notAllowed = HttpError::methodNotAllowed();
    EXPECT_EQ(notAllowed.statusCode(), 405);

    auto ise = HttpError::internalServerError();
    EXPECT_EQ(ise.statusCode(), 500);
}

// ═══════════════════════════════════════════════════════════════════════
// Utility Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(UtilsTest, EscapeHtml) {
    EXPECT_EQ(edetail::escapeHtml("hello"), "hello");
    EXPECT_EQ(edetail::escapeHtml("<script>"), "&lt;script&gt;");
    EXPECT_EQ(edetail::escapeHtml("a&b"), "a&amp;b");
    EXPECT_EQ(edetail::escapeHtml("\"quoted\""), "&quot;quoted&quot;");
    EXPECT_EQ(edetail::escapeHtml("it's"), "it&#39;s");
}

TEST(UtilsTest, EncodeUrl) {
    EXPECT_EQ(edetail::encodeUrl("/path/to/file"), "/path/to/file");
    EXPECT_EQ(edetail::encodeUrl("/path with spaces"), "/path%20with%20spaces");
}

TEST(UtilsTest, ParseBytes) {
    EXPECT_EQ(edetail::parseBytes("100"), 100);
    EXPECT_EQ(edetail::parseBytes("1kb"), 1024);
    EXPECT_EQ(edetail::parseBytes("1mb"), 1048576);
    EXPECT_EQ(edetail::parseBytes("100kb"), 102400);
    EXPECT_EQ(edetail::parseBytes(""), -1);
}

TEST(UtilsTest, ParseMs) {
    EXPECT_EQ(edetail::parseMs("100"), 100);
    EXPECT_EQ(edetail::parseMs("1s"), 1000);
    EXPECT_EQ(edetail::parseMs("1m"), 60000);
    EXPECT_EQ(edetail::parseMs("1h"), 3600000);
    EXPECT_EQ(edetail::parseMs("1d"), 86400000);
    EXPECT_EQ(edetail::parseMs(""), -1);
}

TEST(UtilsTest, ContentDisposition) {
    EXPECT_EQ(edetail::contentDisposition(), "attachment");
    EXPECT_EQ(edetail::contentDisposition("attachment", "file.txt"),
              "attachment; filename=\"file.txt\"");
    EXPECT_EQ(edetail::contentDisposition("inline", "report.pdf"),
              "inline; filename=\"report.pdf\"");
}

TEST(UtilsTest, ParseForwarded) {
    auto addrs = edetail::parseForwarded("1.2.3.4, 5.6.7.8, 9.10.11.12");
    ASSERT_EQ(addrs.size(), 3u);
    EXPECT_EQ(addrs[0], "1.2.3.4");
    EXPECT_EQ(addrs[1], "5.6.7.8");
    EXPECT_EQ(addrs[2], "9.10.11.12");

    auto empty = edetail::parseForwarded("");
    EXPECT_TRUE(empty.empty());
}

TEST(UtilsTest, ToLower) {
    EXPECT_EQ(edetail::toLower("Content-Type"), "content-type");
    EXPECT_EQ(edetail::toLower("HELLO"), "hello");
    EXPECT_EQ(edetail::toLower("already"), "already");
}

TEST(UtilsTest, Trim) {
    EXPECT_EQ(edetail::trim("  hello  "), "hello");
    EXPECT_EQ(edetail::trim("hello"), "hello");
    EXPECT_EQ(edetail::trim("  "), "");
    EXPECT_EQ(edetail::trim(""), "");
}

TEST(UtilsTest, CookieSign) {
    auto signed_ = edetail::cookieSign("hello", "secret");
    EXPECT_FALSE(signed_.empty());
    EXPECT_NE(signed_, "hello");

    auto unsigned_ = edetail::cookieUnsign(signed_, "secret");
    ASSERT_TRUE(unsigned_.has_value());
    EXPECT_EQ(*unsigned_, "hello");

    // Wrong secret
    auto bad = edetail::cookieUnsign(signed_, "wrong");
    EXPECT_FALSE(bad.has_value());
}

TEST(UtilsTest, GenerateETag) {
    auto etag = edetail::generateETag("Hello World");
    EXPECT_FALSE(etag.empty());
    EXPECT_EQ(etag.front(), '"');
    EXPECT_EQ(etag.back(), '"');

    // Same input gives same ETag
    auto etag2 = edetail::generateETag("Hello World");
    EXPECT_EQ(etag, etag2);

    // Different input gives different ETag
    auto etag3 = edetail::generateETag("Goodbye World");
    EXPECT_NE(etag, etag3);
}

TEST(UtilsTest, GenerateWeakETag) {
    auto etag = edetail::generateWeakETag("Hello World");
    EXPECT_TRUE(etag.starts_with("W/\""));
}

TEST(UtilsTest, ParseRange) {
    auto ranges = edetail::parseRange(1000, "bytes=0-499");
    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(ranges[0].start, 0u);
    EXPECT_EQ(ranges[0].end, 499u);

    auto multi = edetail::parseRange(1000, "bytes=0-499, 500-999");
    ASSERT_EQ(multi.size(), 2u);

    auto suffix = edetail::parseRange(1000, "bytes=-500");
    ASSERT_EQ(suffix.size(), 1u);
    EXPECT_EQ(suffix[0].start, 500u);
    EXPECT_EQ(suffix[0].end, 999u);

    auto invalid = edetail::parseRange(1000, "invalid");
    EXPECT_TRUE(invalid.empty());
}

TEST(UtilsTest, Freshness) {
    // Match ETag
    std::map<std::string, std::string> reqH = {{"if-none-match", "\"abc\""}};
    std::map<std::string, std::string> resH = {{"etag", "\"abc\""}};
    EXPECT_TRUE(edetail::isFresh(reqH, resH));

    // Non-matching ETag
    reqH = {{"if-none-match", "\"xyz\""}};
    EXPECT_FALSE(edetail::isFresh(reqH, resH));

    // Wildcard
    reqH = {{"if-none-match", "*"}};
    EXPECT_TRUE(edetail::isFresh(reqH, resH));

    // Weak ETag comparison
    reqH = {{"if-none-match", "W/\"abc\""}};
    resH = {{"etag", "W/\"abc\""}};
    EXPECT_TRUE(edetail::isFresh(reqH, resH));
}

// ═══════════════════════════════════════════════════════════════════════
// Route Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(RouteTest, HandlesMethod) {
    Route route("/users");
    route.get([](Request& req, Response& res) {});
    route.post([](Request& req, Response& res) {});

    EXPECT_TRUE(route.handlesMethod("GET"));
    EXPECT_TRUE(route.handlesMethod("POST"));
    EXPECT_FALSE(route.handlesMethod("DELETE"));
}

TEST(RouteTest, Methods) {
    Route route("/users");
    route.get([](Request& req, Response& res) {});
    route.post([](Request& req, Response& res) {});

    auto methods = route.methods();
    ASSERT_EQ(methods.size(), 2u);
}

TEST(RouteTest, AllMethod) {
    Route route("/any");
    route.all([](Request& req, Response& res) {});

    EXPECT_TRUE(route.handlesMethod("GET"));
    EXPECT_TRUE(route.handlesMethod("POST"));
    EXPECT_TRUE(route.handlesMethod("DELETE"));
}

TEST(RouteTest, Chaining) {
    Route route("/chained");
    route.get([](Request& req, Response& res) {})
         .post([](Request& req, Response& res) {})
         .del([](Request& req, Response& res) {});

    EXPECT_TRUE(route.handlesMethod("GET"));
    EXPECT_TRUE(route.handlesMethod("POST"));
    EXPECT_TRUE(route.handlesMethod("DELETE"));
}

// ═══════════════════════════════════════════════════════════════════════
// Application Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ApplicationTest, DefaultSettings) {
    Application app;
    EXPECT_TRUE(app.enabled("x-powered-by"));
    EXPECT_FALSE(app.enabled("trust proxy"));
}

TEST(ApplicationTest, SetAndGet) {
    Application app;
    app.set("custom", "value");
    auto val = app.getSetting("custom");
    EXPECT_TRUE(val.isString());
    EXPECT_EQ(val.asString(), "value");
}

TEST(ApplicationTest, EnableDisable) {
    Application app;
    app.enable("custom flag");
    EXPECT_TRUE(app.enabled("custom flag"));
    EXPECT_FALSE(app.disabled("custom flag"));

    app.disable("custom flag");
    EXPECT_FALSE(app.enabled("custom flag"));
    EXPECT_TRUE(app.disabled("custom flag"));
}

TEST(ApplicationTest, ExpressFactory) {
    auto app = polycpp::express::express();
    EXPECT_TRUE(app.enabled("x-powered-by"));
}

// ═══════════════════════════════════════════════════════════════════════
// Router Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(RouterTest, DefaultConstruction) {
    Router router;
    // Should not throw
}

TEST(RouterTest, OptionsConstruction) {
    RouterOptions opts;
    opts.caseSensitive = true;
    opts.strict = true;
    Router router(opts);

    EXPECT_TRUE(router.options().caseSensitive);
    EXPECT_TRUE(router.options().strict);
}

// ═══════════════════════════════════════════════════════════════════════
// CookieOptions Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(TypesTest, CookieOptionsDefaults) {
    CookieOptions opts;
    EXPECT_FALSE(opts.domain.has_value());
    EXPECT_FALSE(opts.maxAge.has_value());
    EXPECT_FALSE(opts.httpOnly);
    EXPECT_EQ(opts.path, "/");
    EXPECT_FALSE(opts.secure);
    EXPECT_FALSE(opts.signed_);
    EXPECT_TRUE(opts.sameSite.empty());
}

TEST(TypesTest, SendFileOptionsDefaults) {
    SendFileOptions opts;
    EXPECT_FALSE(opts.maxAge.has_value());
    EXPECT_TRUE(opts.root.empty());
    EXPECT_EQ(opts.dotfiles, "ignore");
    EXPECT_TRUE(opts.etag);
    EXPECT_TRUE(opts.lastModified);
}

TEST(TypesTest, StaticOptionsDefaults) {
    StaticOptions opts;
    EXPECT_EQ(opts.index, "index.html");
    EXPECT_TRUE(opts.redirect);
    EXPECT_TRUE(opts.fallthrough);
}

TEST(TypesTest, JsonParserOptionsDefaults) {
    JsonParserOptions opts;
    EXPECT_EQ(opts.limit, "100kb");
    EXPECT_EQ(opts.type, "application/json");
    EXPECT_TRUE(opts.strict);
}

TEST(TypesTest, UrlencodedOptionsDefaults) {
    UrlencodedOptions opts;
    EXPECT_EQ(opts.limit, "100kb");
    EXPECT_TRUE(opts.extended);
    EXPECT_EQ(opts.parameterLimit, 1000);
}

// ═══════════════════════════════════════════════════════════════════════
// Layer Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(LayerTest, MatchRootPath) {
    Layer layer("/", MiddlewareHandler([](Request&, Response&, NextFunction next) {
        next(std::nullopt);
    }));

    EXPECT_TRUE(layer.match("/anything"));
    EXPECT_TRUE(layer.match("/"));
    EXPECT_FALSE(layer.isErrorHandler());
}

TEST(LayerTest, MatchSpecificPath) {
    Layer layer("/users/:id", MiddlewareHandler([](Request&, Response&, NextFunction next) {
        next(std::nullopt);
    }));

    EXPECT_TRUE(layer.match("/users/42"));
    EXPECT_FALSE(layer.match("/posts/42"));
}

TEST(LayerTest, ErrorHandler) {
    Layer layer("/", ErrorHandler([](const HttpError&, Request&, Response&, NextFunction next) {
        next(std::nullopt);
    }));

    EXPECT_TRUE(layer.isErrorHandler());
}

TEST(LayerTest, MatchParams) {
    Layer layer("/users/:id", MiddlewareHandler([](Request&, Response&, NextFunction next) {
        next(std::nullopt);
    }));

    EXPECT_TRUE(layer.match("/users/42"));
    auto& params = layer.matchedParams();
    auto it = params.find("id");
    ASSERT_NE(it, params.end());
    auto* val = std::get_if<std::string>(&it->second);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, "42");
}
