/**
 * @file test_response.cpp
 * @brief Comprehensive tests for the Express Response methods.
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
namespace edetail = polycpp::express::detail;

// ═══════════════════════════════════════════════════════════════════════
// Helper: Mock Request/Response for testing
// ═══════════════════════════════════════════════════════════════════════

class ResponseTestFixture {
public:
    ResponseTestFixture(const std::string& method, const std::string& url,
                        const polycpp::http::Headers& headers = {},
                        Application* app = nullptr)
        : msg_(), res_(createSocket(), "", 1, false) {
        msg_.method() = method;
        msg_.url() = url;
        msg_.headers() = headers;
        // Suppress write errors on unconnected socket (test mock)
        res_.on("error", [](const std::vector<std::any>&) {});
        req_ = std::make_unique<Request>(msg_, app);
        resp_ = std::make_unique<Response>(res_, app);
        req_->setRes(resp_.get());
        resp_->setReq(req_.get());
    }

    Request& req() { return *req_; }
    Response& res() { return *resp_; }
    polycpp::http::ServerResponse& rawRes() { return res_; }
    polycpp::http::IncomingMessage& rawReq() { return msg_; }

private:
    static polycpp::net::Socket createSocket() {
        return polycpp::net::Socket();
    }

    polycpp::http::IncomingMessage msg_;
    polycpp::http::ServerResponse res_;
    std::unique_ptr<Request> req_;
    std::unique_ptr<Response> resp_;
};

// Helper: extract getHeader() result as std::string (returns "" for non-string/array values)
static std::string getHeaderStr(polycpp::http::ServerResponse& res, const std::string& name) {
    auto val = res.getHeader(name);
    if (val.isString()) return val.asString();
    if (val.isArray()) {
        // Join array values with ", " (e.g., multiple Set-Cookie values)
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
// res.send() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ResponseSendTest, SendStringSetsContentTypeToHtml) {
    ResponseTestFixture f("GET", "/");
    f.res().send("Hello World");

    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Type"), "text/html; charset=utf-8");
}

TEST(ResponseSendTest, SendBufferSetsContentTypeToOctetStream) {
    ResponseTestFixture f("GET", "/");
    auto buf = polycpp::Buffer::from("binary data");
    f.res().send(buf);

    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Type"), "application/octet-stream");
}

TEST(ResponseSendTest, SendSetsContentLength) {
    ResponseTestFixture f("GET", "/");
    std::string body = "Hello";
    f.res().send(body);

    auto cl = getHeaderStr(f.rawRes(),"Content-Length");
    EXPECT_EQ(cl, "5");
}

TEST(ResponseSendTest, SendEmptyString) {
    ResponseTestFixture f("GET", "/");
    f.res().send("");

    // Even an empty body should set Content-Type
    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Type"), "text/html; charset=utf-8");
    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Length"), "0");
}

TEST(ResponseSendTest, SendWithPreSetContentTypeDoesNotOverride) {
    ResponseTestFixture f("GET", "/");
    f.res().set("Content-Type", "text/plain");
    f.res().send("Hello");

    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Type"), "text/plain");
}

TEST(ResponseSendTest, HeadRequestSetsHeadersButNoBody) {
    ResponseTestFixture f("HEAD", "/");
    f.res().send("This body should not be sent");

    // Headers should be set
    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Type"), "text/html; charset=utf-8");
    auto cl = getHeaderStr(f.rawRes(),"Content-Length");
    EXPECT_FALSE(cl.empty());
}

TEST(ResponseSendTest, SendGeneratesETag) {
    ResponseTestFixture f("GET", "/");
    f.res().send("Hello World");

    auto etag = getHeaderStr(f.rawRes(),"ETag");
    EXPECT_FALSE(etag.empty());
    EXPECT_TRUE(etag.starts_with("W/\""));
}

TEST(ResponseSendTest, SendBufferSetsContentLengthCorrectly) {
    ResponseTestFixture f("GET", "/");
    auto buf = polycpp::Buffer::from("12345");
    f.res().send(buf);

    auto cl = getHeaderStr(f.rawRes(),"Content-Length");
    EXPECT_EQ(cl, "5");
}

// ═══════════════════════════════════════════════════════════════════════
// res.json() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ResponseJsonTest, JsonEmptyObjectSetsContentType) {
    ResponseTestFixture f("GET", "/");
    f.res().json(polycpp::JsonObject{});

    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Type"), "application/json; charset=utf-8");
}

TEST(ResponseJsonTest, JsonNestedObjects) {
    ResponseTestFixture f("GET", "/");
    polycpp::JsonObject inner = {{"key", "value"}};
    polycpp::JsonObject outer = {{"nested", polycpp::JsonValue(inner)}};
    f.res().json(polycpp::JsonValue(outer));

    auto ct = getHeaderStr(f.rawRes(),"Content-Type");
    EXPECT_EQ(ct, "application/json; charset=utf-8");
    // Content-Length should be set
    EXPECT_FALSE(getHeaderStr(f.rawRes(),"Content-Length").empty());
}

TEST(ResponseJsonTest, JsonArray) {
    ResponseTestFixture f("GET", "/");
    polycpp::JsonArray arr = {1.0, 2.0, 3.0};
    f.res().json(polycpp::JsonValue(arr));

    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Type"), "application/json; charset=utf-8");
}

TEST(ResponseJsonTest, JsonNull) {
    ResponseTestFixture f("GET", "/");
    f.res().json(polycpp::JsonValue());

    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Type"), "application/json; charset=utf-8");
}

TEST(ResponseJsonTest, JsonWithPreSetContentTypeDoesNotOverride) {
    ResponseTestFixture f("GET", "/");
    f.res().set("Content-Type", "application/vnd.api+json");
    f.res().json(polycpp::JsonObject{{"key", "val"}});

    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Type"), "application/vnd.api+json");
}

TEST(ResponseJsonTest, JsonSetsContentLength) {
    ResponseTestFixture f("GET", "/");
    f.res().json(polycpp::JsonObject{{"a", "b"}});

    auto cl = getHeaderStr(f.rawRes(),"Content-Length");
    EXPECT_FALSE(cl.empty());
    // The JSON body should be {"a":"b"}, length depends on JSON::stringify format
    int len = std::stoi(cl);
    EXPECT_GT(len, 0);
}

TEST(ResponseJsonTest, HeadRequestSendsNoBody) {
    ResponseTestFixture f("HEAD", "/");
    f.res().json(polycpp::JsonObject{{"key", "value"}});

    // Headers should still be set
    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Type"), "application/json; charset=utf-8");
    EXPECT_FALSE(getHeaderStr(f.rawRes(),"Content-Length").empty());
}

// ═══════════════════════════════════════════════════════════════════════
// res.jsonp() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ResponseJsonpTest, JsonpWithoutCallbackSameAsJson) {
    ResponseTestFixture f("GET", "/api");  // no callback query param
    f.res().jsonp(polycpp::JsonObject{{"key", "value"}});

    // Without callback, should act like json
    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Type"), "application/json; charset=utf-8");
}

TEST(ResponseJsonpTest, JsonpWithCallbackParameter) {
    ResponseTestFixture f("GET", "/api?callback=myFunc");
    f.res().jsonp(polycpp::JsonObject{{"key", "value"}});

    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Type"), "text/javascript; charset=utf-8");
    EXPECT_EQ(getHeaderStr(f.rawRes(),"X-Content-Type-Options"), "nosniff");
}

// ═══════════════════════════════════════════════════════════════════════
// res.sendStatus() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ResponseSendStatusTest, SendStatus200SendsOK) {
    ResponseTestFixture f("GET", "/");
    f.res().sendStatus(200);

    EXPECT_EQ(f.rawRes().statusCode(), 200);
    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Type"), "text/plain; charset=utf-8");
    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Length"), "2");  // "OK" is 2 chars
}

TEST(ResponseSendStatusTest, SendStatus404SendsNotFound) {
    ResponseTestFixture f("GET", "/");
    f.res().sendStatus(404);

    EXPECT_EQ(f.rawRes().statusCode(), 404);
    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Length"), "9");  // "Not Found" is 9 chars
}

TEST(ResponseSendStatusTest, SendStatus500SendsInternalServerError) {
    ResponseTestFixture f("GET", "/");
    f.res().sendStatus(500);

    EXPECT_EQ(f.rawRes().statusCode(), 500);
    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Length"), "21");  // "Internal Server Error" is 21 chars
}

TEST(ResponseSendStatusTest, SendStatus201SendsCreated) {
    ResponseTestFixture f("GET", "/");
    f.res().sendStatus(201);

    EXPECT_EQ(f.rawRes().statusCode(), 201);
    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Length"), "7");  // "Created" is 7 chars
}

TEST(ResponseSendStatusTest, SendStatus204SendsNoContent) {
    ResponseTestFixture f("GET", "/");
    f.res().sendStatus(204);

    EXPECT_EQ(f.rawRes().statusCode(), 204);
    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Length"), "10");  // "No Content" is 10 chars
}

// ═══════════════════════════════════════════════════════════════════════
// res.redirect() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ResponseRedirectTest, RedirectDefaultsTo302) {
    ResponseTestFixture f("GET", "/");
    f.res().redirect("/new-location");

    EXPECT_EQ(f.rawRes().statusCode(), 302);
    EXPECT_EQ(getHeaderStr(f.rawRes(),"Location"), "/new-location");
}

TEST(ResponseRedirectTest, RedirectWithCustomStatus) {
    ResponseTestFixture f("GET", "/");
    f.res().redirect(301, "/permanent");

    EXPECT_EQ(f.rawRes().statusCode(), 301);
    EXPECT_EQ(getHeaderStr(f.rawRes(),"Location"), "/permanent");
}

TEST(ResponseRedirectTest, RedirectSetsContentType) {
    ResponseTestFixture f("GET", "/");
    f.res().redirect("/target");

    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Type"), "text/html; charset=utf-8");
}

TEST(ResponseRedirectTest, RedirectEncodesUrl) {
    ResponseTestFixture f("GET", "/");
    f.res().redirect("/path with spaces");

    EXPECT_EQ(getHeaderStr(f.rawRes(),"Location"), "/path%20with%20spaces");
}

TEST(ResponseRedirectTest, RedirectBackUsesReferer) {
    ResponseTestFixture f("GET", "/", {{"referer", "http://example.com/previous"}});
    f.res().redirect("back");

    // Location should encode the referer URL
    auto loc = getHeaderStr(f.rawRes(),"Location");
    EXPECT_NE(loc.find("example.com"), std::string::npos);
}

TEST(ResponseRedirectTest, RedirectBackWithNoRefererGoesToRoot) {
    ResponseTestFixture f("GET", "/");
    f.res().redirect("back");

    EXPECT_EQ(getHeaderStr(f.rawRes(),"Location"), "/");
}

TEST(ResponseRedirectTest, HeadRequestSetsHeadersNoBody) {
    ResponseTestFixture f("HEAD", "/");
    f.res().redirect("/new");

    EXPECT_EQ(f.rawRes().statusCode(), 302);
    EXPECT_EQ(getHeaderStr(f.rawRes(),"Location"), "/new");
}

// ═══════════════════════════════════════════════════════════════════════
// res.type() / res.contentType() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ResponseTypeTest, TypeJsonSetsApplicationJson) {
    ResponseTestFixture f("GET", "/");
    f.res().type("json");

    auto ct = getHeaderStr(f.rawRes(),"Content-Type");
    EXPECT_NE(ct.find("application/json"), std::string::npos);
}

TEST(ResponseTypeTest, TypeHtmlSetsTextHtml) {
    ResponseTestFixture f("GET", "/");
    f.res().type("html");

    auto ct = getHeaderStr(f.rawRes(),"Content-Type");
    EXPECT_NE(ct.find("text/html"), std::string::npos);
}

TEST(ResponseTypeTest, TypeExactMimePassedThrough) {
    ResponseTestFixture f("GET", "/");
    f.res().type("application/json");

    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Type"), "application/json");
}

TEST(ResponseTypeTest, TypeWithDotPrefix) {
    ResponseTestFixture f("GET", "/");
    f.res().type(".json");

    auto ct = getHeaderStr(f.rawRes(),"Content-Type");
    EXPECT_NE(ct.find("application/json"), std::string::npos);
}

TEST(ResponseTypeTest, ContentTypeIsAliasForType) {
    ResponseTestFixture f("GET", "/");
    f.res().contentType("json");

    auto ct = getHeaderStr(f.rawRes(),"Content-Type");
    EXPECT_NE(ct.find("application/json"), std::string::npos);
}

TEST(ResponseTypeTest, TypePng) {
    ResponseTestFixture f("GET", "/");
    f.res().type("png");

    auto ct = getHeaderStr(f.rawRes(),"Content-Type");
    EXPECT_NE(ct.find("image/png"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════
// res.set() / res.header() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ResponseSetTest, SetSingleHeader) {
    ResponseTestFixture f("GET", "/");
    f.res().set("X-Custom", "my-value");

    EXPECT_EQ(getHeaderStr(f.rawRes(),"X-Custom"), "my-value");
}

TEST(ResponseSetTest, SetMultipleHeaders) {
    ResponseTestFixture f("GET", "/");
    f.res().set({
        {"X-Custom-One", "one"},
        {"X-Custom-Two", "two"}
    });

    EXPECT_EQ(getHeaderStr(f.rawRes(),"X-Custom-One"), "one");
    EXPECT_EQ(getHeaderStr(f.rawRes(),"X-Custom-Two"), "two");
}

TEST(ResponseSetTest, HeaderIsAliasForSet) {
    ResponseTestFixture f("GET", "/");
    f.res().header("X-Alias", "test-value");

    EXPECT_EQ(getHeaderStr(f.rawRes(),"X-Alias"), "test-value");
}

TEST(ResponseSetTest, GetHeader) {
    ResponseTestFixture f("GET", "/");
    f.res().set("X-Test", "hello");

    auto val = f.res().get("X-Test");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, "hello");
}

TEST(ResponseSetTest, GetNonexistentHeaderReturnsNullopt) {
    ResponseTestFixture f("GET", "/");

    auto val = f.res().get("X-Missing");
    EXPECT_FALSE(val.has_value());
}

// ═══════════════════════════════════════════════════════════════════════
// res.append() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ResponseAppendTest, AppendCreatesNewHeader) {
    ResponseTestFixture f("GET", "/");
    f.res().append("X-Custom", "value1");

    EXPECT_EQ(getHeaderStr(f.rawRes(),"X-Custom"), "value1");
}

TEST(ResponseAppendTest, AppendToExistingHeaderAddsValue) {
    ResponseTestFixture f("GET", "/");
    f.res().set("X-Custom", "value1");
    f.res().append("X-Custom", "value2");

    EXPECT_EQ(getHeaderStr(f.rawRes(),"X-Custom"), "value1, value2");
}

// ═══════════════════════════════════════════════════════════════════════
// res.links() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ResponseLinksTest, LinksSetsLinkHeader) {
    ResponseTestFixture f("GET", "/");
    f.res().links({{"next", "/users?page=2"}, {"last", "/users?page=5"}});

    auto link = getHeaderStr(f.rawRes(),"Link");
    EXPECT_NE(link.find("/users?page=2"), std::string::npos);
    EXPECT_NE(link.find("rel=\"next\""), std::string::npos);
    EXPECT_NE(link.find("/users?page=5"), std::string::npos);
    EXPECT_NE(link.find("rel=\"last\""), std::string::npos);
}

TEST(ResponseLinksTest, LinksAppendsToExisting) {
    ResponseTestFixture f("GET", "/");
    f.res().links({{"next", "/page2"}});
    f.res().links({{"prev", "/page0"}});

    auto link = getHeaderStr(f.rawRes(),"Link");
    EXPECT_NE(link.find("/page2"), std::string::npos);
    EXPECT_NE(link.find("/page0"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════
// res.cookie() / res.clearCookie() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ResponseCookieTest, CookieSetsSetCookieHeader) {
    ResponseTestFixture f("GET", "/");
    f.res().cookie("session", "abc123");

    auto header = getHeaderStr(f.rawRes(),"Set-Cookie");
    EXPECT_NE(header.find("session=abc123"), std::string::npos);
}

TEST(ResponseCookieTest, CookieWithHttpOnly) {
    ResponseTestFixture f("GET", "/");
    CookieOptions opts;
    opts.httpOnly = true;
    f.res().cookie("token", "xyz", opts);

    auto header = getHeaderStr(f.rawRes(),"Set-Cookie");
    EXPECT_NE(header.find("token=xyz"), std::string::npos);
    EXPECT_NE(header.find("HttpOnly"), std::string::npos);
}

TEST(ResponseCookieTest, CookieWithSecure) {
    ResponseTestFixture f("GET", "/");
    CookieOptions opts;
    opts.secure = true;
    f.res().cookie("token", "xyz", opts);

    auto header = getHeaderStr(f.rawRes(),"Set-Cookie");
    EXPECT_NE(header.find("Secure"), std::string::npos);
}

TEST(ResponseCookieTest, CookieWithPath) {
    ResponseTestFixture f("GET", "/");
    CookieOptions opts;
    opts.path = "/api";
    f.res().cookie("token", "xyz", opts);

    auto header = getHeaderStr(f.rawRes(),"Set-Cookie");
    EXPECT_NE(header.find("Path=/api"), std::string::npos);
}

TEST(ResponseCookieTest, CookieWithDomain) {
    ResponseTestFixture f("GET", "/");
    CookieOptions opts;
    opts.domain = "example.com";
    f.res().cookie("token", "xyz", opts);

    auto header = getHeaderStr(f.rawRes(),"Set-Cookie");
    EXPECT_NE(header.find("Domain=example.com"), std::string::npos);
}

TEST(ResponseCookieTest, CookieWithSameSite) {
    ResponseTestFixture f("GET", "/");
    CookieOptions opts;
    opts.sameSite = "Strict";
    f.res().cookie("token", "xyz", opts);

    auto header = getHeaderStr(f.rawRes(),"Set-Cookie");
    EXPECT_NE(header.find("SameSite=Strict"), std::string::npos);
}

TEST(ResponseCookieTest, CookieWithMaxAge) {
    ResponseTestFixture f("GET", "/");
    CookieOptions opts;
    opts.maxAge = std::chrono::milliseconds(3600000);  // 1 hour
    f.res().cookie("session", "abc", opts);

    auto header = getHeaderStr(f.rawRes(),"Set-Cookie");
    EXPECT_NE(header.find("Max-Age=3600"), std::string::npos);
}

TEST(ResponseCookieTest, ClearCookieSetsExpiry) {
    ResponseTestFixture f("GET", "/");
    f.res().clearCookie("session");

    auto header = getHeaderStr(f.rawRes(),"Set-Cookie");
    EXPECT_NE(header.find("session="), std::string::npos);
    // Should have Max-Age=0 or an Expires in the past
    bool hasMaxAge0 = header.find("Max-Age=0") != std::string::npos;
    bool hasExpires = header.find("Expires=") != std::string::npos;
    EXPECT_TRUE(hasMaxAge0 || hasExpires);
}

TEST(ResponseCookieTest, MultipleCookies) {
    ResponseTestFixture f("GET", "/");
    f.res().cookie("name1", "val1");
    f.res().cookie("name2", "val2");

    // Both cookies should be set via appendHeader
    auto header = getHeaderStr(f.rawRes(),"Set-Cookie");
    EXPECT_FALSE(header.empty());
}

// ═══════════════════════════════════════════════════════════════════════
// res.format() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ResponseFormatTest, FormatDispatchesBasedOnAcceptHeader) {
    ResponseTestFixture f("GET", "/", {{"accept", "application/json"}});
    bool jsonCalled = false;
    bool htmlCalled = false;

    f.res().format({
        {"text/html", [&]() { htmlCalled = true; }},
        {"application/json", [&]() { jsonCalled = true; }}
    });

    EXPECT_TRUE(jsonCalled);
    EXPECT_FALSE(htmlCalled);
}

TEST(ResponseFormatTest, FormatReturns406WhenNoMatch) {
    ResponseTestFixture f("GET", "/", {{"accept", "image/png"}});

    f.res().format({
        {"text/html", []() {}},
        {"application/json", []() {}}
    });

    EXPECT_EQ(f.rawRes().statusCode(), 406);
}

TEST(ResponseFormatTest, FormatCallsDefaultWhenNoMatch) {
    ResponseTestFixture f("GET", "/", {{"accept", "image/png"}});
    bool defaultCalled = false;

    f.res().format({
        {"text/html", []() {}},
        {"default", [&]() { defaultCalled = true; }}
    });

    EXPECT_TRUE(defaultCalled);
}

TEST(ResponseFormatTest, FormatAddsVaryAccept) {
    ResponseTestFixture f("GET", "/", {{"accept", "text/html"}});

    f.res().format({
        {"text/html", []() {}}
    });

    auto vary = getHeaderStr(f.rawRes(),"Vary");
    EXPECT_NE(vary.find("Accept"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════
// res.location() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ResponseLocationTest, LocationSetsHeader) {
    ResponseTestFixture f("GET", "/");
    f.res().location("/users/1");

    EXPECT_EQ(getHeaderStr(f.rawRes(),"Location"), "/users/1");
}

TEST(ResponseLocationTest, LocationEncodesUrl) {
    ResponseTestFixture f("GET", "/");
    f.res().location("/path with spaces");

    EXPECT_EQ(getHeaderStr(f.rawRes(),"Location"), "/path%20with%20spaces");
}

// ═══════════════════════════════════════════════════════════════════════
// res.attachment() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ResponseAttachmentTest, AttachmentWithNoFilename) {
    ResponseTestFixture f("GET", "/");
    f.res().attachment();

    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Disposition"), "attachment");
}

TEST(ResponseAttachmentTest, AttachmentWithFilename) {
    ResponseTestFixture f("GET", "/");
    f.res().attachment("report.pdf");

    auto cd = getHeaderStr(f.rawRes(),"Content-Disposition");
    EXPECT_NE(cd.find("attachment"), std::string::npos);
    EXPECT_NE(cd.find("filename=\"report.pdf\""), std::string::npos);
}

TEST(ResponseAttachmentTest, AttachmentSetsContentType) {
    ResponseTestFixture f("GET", "/");
    f.res().attachment("photo.jpg");

    auto ct = getHeaderStr(f.rawRes(),"Content-Type");
    EXPECT_NE(ct.find("image/jpeg"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════
// res.status() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ResponseStatusTest, StatusSetsCodeChainable) {
    ResponseTestFixture f("GET", "/");
    auto& ret = f.res().status(404);

    EXPECT_EQ(f.rawRes().statusCode(), 404);
    // Check chaining returns the same object
    EXPECT_EQ(&ret, &f.res());
}

TEST(ResponseStatusTest, StatusChainWithSend) {
    ResponseTestFixture f("GET", "/");
    f.res().status(201).send("Created!");

    EXPECT_EQ(f.rawRes().statusCode(), 201);
    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Type"), "text/html; charset=utf-8");
}

TEST(ResponseStatusTest, StatusChainWithJson) {
    ResponseTestFixture f("GET", "/");
    f.res().status(200).json(polycpp::JsonObject{{"ok", true}});

    EXPECT_EQ(f.rawRes().statusCode(), 200);
    EXPECT_EQ(getHeaderStr(f.rawRes(),"Content-Type"), "application/json; charset=utf-8");
}

// ═══════════════════════════════════════════════════════════════════════
// res.vary() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ResponseVaryTest, VarySetsHeader) {
    ResponseTestFixture f("GET", "/");
    f.res().vary("Accept");

    EXPECT_EQ(getHeaderStr(f.rawRes(),"Vary"), "Accept");
}

TEST(ResponseVaryTest, VaryAppendsToExisting) {
    ResponseTestFixture f("GET", "/");
    f.res().vary("Accept");
    f.res().vary("Accept-Encoding");

    auto vary = getHeaderStr(f.rawRes(),"Vary");
    EXPECT_NE(vary.find("Accept"), std::string::npos);
    EXPECT_NE(vary.find("Accept-Encoding"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════
// res.locals Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ResponseLocalsTest, LocalsDefaultsToEmptyObject) {
    ResponseTestFixture f("GET", "/");

    EXPECT_TRUE(f.res().locals.isObject());
    EXPECT_TRUE(f.res().locals.asObject().empty());
}

TEST(ResponseLocalsTest, LocalsCanBeSet) {
    ResponseTestFixture f("GET", "/");
    f.res().locals = polycpp::JsonObject{{"user", "John"}};

    EXPECT_EQ(f.res().locals["user"].asString(), "John");
}

// ═══════════════════════════════════════════════════════════════════════
// Bug-fix regression tests
// ═══════════════════════════════════════════════════════════════════════

// BUG 2: statusCode() must reflect the code set via status()
TEST(ResponseStatusCodeTest, StatusCodeReflectsSetValue) {
    ResponseTestFixture f("GET", "/");

    // Default should be 200
    EXPECT_EQ(f.res().statusCode(), 200);

    // After setting to 404, statusCode() must return 404
    f.res().status(404);
    EXPECT_EQ(f.res().statusCode(), 404);

    // After setting to 201
    f.res().status(201);
    EXPECT_EQ(f.res().statusCode(), 201);

    // After setting to 500
    f.res().status(500);
    EXPECT_EQ(f.res().statusCode(), 500);
}

// BUG 3: JSONP callback sanitization -- invalid callback falls back to JSON
TEST(ResponseJsonpTest, InvalidCallbackFallsBackToJson) {
    polycpp::JsonObject queryObj = {{"callback", std::string("alert('xss')")}};
    ResponseTestFixture f("GET", "/?callback=alert('xss')",
                          {{"host", "localhost"}});
    // Set the query on request
    f.rawReq().url() = "/?callback=alert('xss')";
    auto& resp = f.res();
    resp.jsonp(polycpp::JsonObject{{"key", "value"}});

    // Since callback is invalid, should fall back to application/json
    auto ct = getHeaderStr(f.rawRes(),"Content-Type");
    EXPECT_NE(ct.find("application/json"), std::string::npos);
}

// BUG 3: JSONP with valid callback should set text/javascript
TEST(ResponseJsonpTest, ValidCallbackSetsJavascriptContentType) {
    ResponseTestFixture f("GET", "/?callback=myFunc",
                          {{"host", "localhost"}});
    f.rawReq().url() = "/?callback=myFunc";
    auto& resp = f.res();
    resp.jsonp(polycpp::JsonObject{{"key", "value"}});

    auto ct = getHeaderStr(f.rawRes(),"Content-Type");
    EXPECT_NE(ct.find("text/javascript"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════
// Second-round bugfix regression tests
// ═══════════════════════════════════════════════════════════════════════

// BUG 1 regression: send(Buffer) should not inflate binary data
TEST(ResponseBugfixTest, SendBufferPreservesExactByteCount) {
    ResponseTestFixture f("GET", "/", {{"host", "localhost"}});
    // Create a Buffer with bytes 0x00-0xFF
    auto buf = polycpp::Buffer::alloc(256);
    for (int i = 0; i < 256; ++i) {
        buf[i] = static_cast<uint8_t>(i);
    }
    f.res().send(buf);
    auto cl = getHeaderStr(f.rawRes(),"Content-Length");
    // Content-Length must be 256 (not inflated by latin1 encoding)
    EXPECT_EQ(cl, "256");
}

// BUG 1 regression: bufferToRawString preserves exact bytes
TEST(ResponseBugfixTest, BufferToRawStringExactBytes) {
    auto buf = polycpp::Buffer::alloc(4);
    buf[0] = 0x00;
    buf[1] = 0x7F;
    buf[2] = 0x80;
    buf[3] = 0xFF;
    auto str = polycpp::express::detail::bufferToRawString(buf);
    EXPECT_EQ(str.size(), 4u);
    EXPECT_EQ(static_cast<unsigned char>(str[0]), 0x00);
    EXPECT_EQ(static_cast<unsigned char>(str[1]), 0x7F);
    EXPECT_EQ(static_cast<unsigned char>(str[2]), 0x80);
    EXPECT_EQ(static_cast<unsigned char>(str[3]), 0xFF);
}

// BUG 2 regression: CRLF injection in header names
TEST(ResponseBugfixTest, CrlfInjectionInHeaderNameThrows) {
    ResponseTestFixture f("GET", "/", {{"host", "localhost"}});
    EXPECT_THROW(f.res().set("X-Bad\r\nHeader", "value"), HttpError);
}

// BUG 2 regression: CRLF injection in header values
TEST(ResponseBugfixTest, CrlfInjectionInHeaderValueThrows) {
    ResponseTestFixture f("GET", "/", {{"host", "localhost"}});
    EXPECT_THROW(f.res().set("X-Custom", "value\r\nEvil: injected"), HttpError);
}

// BUG 2 regression: null byte in header value
TEST(ResponseBugfixTest, NullByteInHeaderValueThrows) {
    ResponseTestFixture f("GET", "/", {{"host", "localhost"}});
    std::string val = std::string("good") + '\0' + "evil";
    EXPECT_THROW(f.res().set("X-Custom", val), HttpError);
}

// BUG 2 regression: append() also validates
TEST(ResponseBugfixTest, AppendValidatesHeaderValue) {
    ResponseTestFixture f("GET", "/", {{"host", "localhost"}});
    EXPECT_THROW(f.res().append("X-Custom", "a\r\nb"), HttpError);
}

// BUG 4 regression: null byte in path rejected by sendFile
TEST(ResponseBugfixTest, SendFileRejectsNullByteInPath) {
    ResponseTestFixture f("GET", "/", {{"host", "localhost"}});
    std::string maliciousPath = std::string("/etc/passwd") + '\0' + ".jpg";
    bool callbackCalled = false;
    std::optional<std::string> callbackErr;
    f.res().sendFile(maliciousPath, {}, [&](auto err) {
        callbackCalled = true;
        callbackErr = err;
    });
    EXPECT_TRUE(callbackCalled);
    EXPECT_TRUE(callbackErr.has_value());
    EXPECT_EQ(*callbackErr, "Bad Request");
}

// BUG 5 regression: ETags not generated for POST
TEST(ResponseBugfixTest, NoETagForPostRequest) {
    ResponseTestFixture f("POST", "/", {{"host", "localhost"}});
    f.res().send("some body data");
    auto etag = getHeaderStr(f.rawRes(),"ETag");
    EXPECT_TRUE(etag.empty());
}

// BUG 5 regression: ETags still generated for GET
TEST(ResponseBugfixTest, ETagGeneratedForGetRequest) {
    ResponseTestFixture f("GET", "/", {{"host", "localhost"}});
    f.res().send("some body data");
    auto etag = getHeaderStr(f.rawRes(),"ETag");
    EXPECT_FALSE(etag.empty());
}

// BUG 8 regression: json() generates ETag via send()
TEST(ResponseBugfixTest, JsonGeneratesETag) {
    ResponseTestFixture f("GET", "/", {{"host", "localhost"}});
    f.res().json(polycpp::JsonObject{{"key", "value"}});
    auto etag = getHeaderStr(f.rawRes(),"ETag");
    EXPECT_FALSE(etag.empty());
}

// BUG 10 regression: engine() with empty extension does not crash
TEST(ApplicationBugfixTest, EngineEmptyExtNoCrash) {
    Application app;
    EXPECT_NO_THROW(app.engine("", [](const std::string&, const polycpp::JsonValue&,
                                       std::function<void(std::optional<std::string>, std::string)> cb) {
        cb(std::nullopt, "");
    }));
}
