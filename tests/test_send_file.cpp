/**
 * @file test_send_file.cpp
 * @brief Tests for sendFile() and serve_static streaming file serving.
 *
 * Covers Content-Type, Content-Length, Last-Modified, ETag, Accept-Ranges,
 * conditional GET (304), Range requests (206), dotfiles, and serve_static.
 */

#include <fstream>

#include <gtest/gtest.h>

#include <polycpp/io/event_context.hpp>
#include <polycpp/io/tcp_socket.hpp>
#include <polycpp/event_loop.hpp>
#include <polycpp/express/express.hpp>
#include <polycpp/fs.hpp>
#include <polycpp/mime/detail/aggregator.hpp>
#include <polycpp/path.hpp>

using namespace polycpp::express;
namespace edetail = polycpp::express::detail;

// ═══════════════════════════════════════════════════════════════════════
// Test fixture: creates temp files for serving
// ═══════════════════════════════════════════════════════════════════════

class SendFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory with test files
        testDir_ = "/tmp/polycpp_sendfile_test_" + std::to_string(getpid());
        polycpp::fs::mkdirSync(testDir_, true);

        // Create a text file
        polycpp::fs::writeFileSync(testDir_ + "/hello.txt", "Hello, World!");

        // Create an HTML file
        polycpp::fs::writeFileSync(testDir_ + "/index.html",
            "<html><body>Index</body></html>");

        // Create a JSON file
        polycpp::fs::writeFileSync(testDir_ + "/data.json",
            R"({"key":"value"})");

        // Create a dotfile
        polycpp::fs::writeFileSync(testDir_ + "/.hidden", "secret");

        // Create a subdirectory with index
        polycpp::fs::mkdirSync(testDir_ + "/subdir", true);
        polycpp::fs::writeFileSync(testDir_ + "/subdir/index.html",
            "<html><body>Subdir Index</body></html>");

        // Create a larger file for range testing (100 bytes: 0123456789 x10)
        std::string largeContent;
        for (int i = 0; i < 10; ++i) {
            largeContent += "0123456789";
        }
        polycpp::fs::writeFileSync(testDir_ + "/range.txt", largeContent);
    }

    void TearDown() override {
        polycpp::fs::rmSync(testDir_, true);
    }

    // Helper to create a mock request/response pair with a real socket
    struct MockHttp {
        polycpp::http::IncomingMessage msg;
        polycpp::http::ServerResponse sres;
        std::unique_ptr<Request> req;
        std::unique_ptr<Response> res;

        MockHttp(const std::string& method, const std::string& url)
            : msg(), sres(createSocket(), "", 1, false) {
            msg.method() = method;
            msg.url() = url;
            req = std::make_unique<Request>(msg, nullptr);
            res = std::make_unique<Response>(sres, nullptr);
            req->setRes(res.get());
            res->setReq(req.get());
            // Suppress write errors on unconnected socket (test mock)
            sres.on(polycpp::stream::event::Error_, [](const polycpp::Error&) {});
        }

        void setHeader(const std::string& name, const std::string& value) {
            msg.headers().set(edetail::toLower(name), value);
        }

    private:
        static polycpp::net::Socket createSocket() {
            return polycpp::net::Socket();
        }
    };

    std::string testDir_;
};

// ═══════════════════════════════════════════════════════════════════════
// Helper: extract getHeader() result as std::string
static std::string getHeaderStr(polycpp::http::ServerResponse& res, const std::string& name) {
    auto val = res.getHeader(name);
    if (val.empty()) return {};
    std::string result = val.front();
    for (size_t i = 1; i < val.size(); ++i) {
        result += ", ";
        result += val[i];
    }
    return result;
}

// Utility Tests: httpDate, statEtag
// ═══════════════════════════════════════════════════════════════════════

TEST(UtilsHttpDateTest, FormatsRFC7231Date) {
    // 2026-01-01 00:00:00 UTC
    auto tp = std::chrono::system_clock::from_time_t(1767225600);
    auto dateStr = edetail::httpDate(tp);
    EXPECT_EQ(dateStr, "Thu, 01 Jan 2026 00:00:00 GMT");
}

TEST(UtilsHttpDateTest, FormatsFromMilliseconds) {
    // 2026-01-01 00:00:00 UTC = 1767225600000 ms
    auto dateStr = edetail::httpDate(1767225600000.0);
    EXPECT_EQ(dateStr, "Thu, 01 Jan 2026 00:00:00 GMT");
}

TEST(UtilsStatEtagTest, GeneratesWeakEtag) {
    auto etag = edetail::statEtag(1024, 1767225600000.0);
    EXPECT_TRUE(etag.starts_with("W/\""));
    EXPECT_TRUE(etag.ends_with("\""));
    // Should contain size in hex and mtime in hex
    EXPECT_NE(etag.find("-"), std::string::npos);
}

TEST(UtilsStatEtagTest, DifferentSizeGivesDifferentEtag) {
    auto etag1 = edetail::statEtag(1024, 1767225600000.0);
    auto etag2 = edetail::statEtag(2048, 1767225600000.0);
    EXPECT_NE(etag1, etag2);
}

TEST(UtilsStatEtagTest, DifferentMtimeGivesDifferentEtag) {
    auto etag1 = edetail::statEtag(1024, 1767225600000.0);
    auto etag2 = edetail::statEtag(1024, 1767225601000.0);
    EXPECT_NE(etag1, etag2);
}

// ═══════════════════════════════════════════════════════════════════════
// sendFile: Content-Type
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, SetsCorrectContentTypeForTxt) {
    MockHttp http("GET", "/hello.txt");
    http.res->sendFile(testDir_ + "/hello.txt");

    auto ct = getHeaderStr(http.sres,"Content-Type");
    EXPECT_TRUE(ct.find("text/plain") != std::string::npos);
}

TEST_F(SendFileTest, SetsCorrectContentTypeForHtml) {
    MockHttp http("GET", "/index.html");
    http.res->sendFile(testDir_ + "/index.html");

    auto ct = getHeaderStr(http.sres,"Content-Type");
    EXPECT_TRUE(ct.find("text/html") != std::string::npos);
}

TEST_F(SendFileTest, SetsCorrectContentTypeForJson) {
    MockHttp http("GET", "/data.json");
    http.res->sendFile(testDir_ + "/data.json");

    auto ct = getHeaderStr(http.sres,"Content-Type");
    EXPECT_TRUE(ct.find("application/json") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════
// sendFile: Content-Length
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, SetsContentLength) {
    MockHttp http("GET", "/hello.txt");
    http.res->sendFile(testDir_ + "/hello.txt");

    auto cl = getHeaderStr(http.sres,"Content-Length");
    EXPECT_EQ(cl, "13"); // "Hello, World!" is 13 bytes
}

// ═══════════════════════════════════════════════════════════════════════
// sendFile: Last-Modified
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, SetsLastModifiedHeader) {
    MockHttp http("GET", "/hello.txt");
    http.res->sendFile(testDir_ + "/hello.txt");

    auto lm = getHeaderStr(http.sres,"Last-Modified");
    EXPECT_FALSE(lm.empty());
    // Should be in RFC 7231 format ending with GMT
    EXPECT_TRUE(lm.find("GMT") != std::string::npos);
}

TEST_F(SendFileTest, OmitsLastModifiedWhenDisabled) {
    MockHttp http("GET", "/hello.txt");
    SendFileOptions opts;
    opts.lastModified = false;
    http.res->sendFile(testDir_ + "/hello.txt", opts);

    auto lm = getHeaderStr(http.sres,"Last-Modified");
    EXPECT_TRUE(lm.empty());
}

// ═══════════════════════════════════════════════════════════════════════
// sendFile: ETag
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, SetsETagHeader) {
    MockHttp http("GET", "/hello.txt");
    http.res->sendFile(testDir_ + "/hello.txt");

    auto etag = getHeaderStr(http.sres,"ETag");
    EXPECT_FALSE(etag.empty());
    EXPECT_TRUE(etag.starts_with("W/\""));
}

TEST_F(SendFileTest, OmitsETagWhenDisabled) {
    MockHttp http("GET", "/hello.txt");
    SendFileOptions opts;
    opts.etag = false;
    http.res->sendFile(testDir_ + "/hello.txt", opts);

    auto etag = getHeaderStr(http.sres,"ETag");
    EXPECT_TRUE(etag.empty());
}

// ═══════════════════════════════════════════════════════════════════════
// sendFile: Accept-Ranges
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, SetsAcceptRangesBytes) {
    MockHttp http("GET", "/hello.txt");
    http.res->sendFile(testDir_ + "/hello.txt");

    auto ar = getHeaderStr(http.sres,"Accept-Ranges");
    EXPECT_EQ(ar, "bytes");
}

// ═══════════════════════════════════════════════════════════════════════
// sendFile: Conditional GET — 304 Not Modified with If-None-Match
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, Returns304WithIfNoneMatch) {
    // First request: get the ETag
    MockHttp http1("GET", "/hello.txt");
    http1.res->sendFile(testDir_ + "/hello.txt");
    auto etag = getHeaderStr(http1.sres,"ETag");
    ASSERT_FALSE(etag.empty());

    // Second request: send If-None-Match with that ETag
    MockHttp http2("GET", "/hello.txt");
    http2.setHeader("If-None-Match", etag);
    http2.res->sendFile(testDir_ + "/hello.txt");

    EXPECT_EQ(http2.sres.statusCode(), 304);
}

// ═══════════════════════════════════════════════════════════════════════
// sendFile: Conditional GET — 304 Not Modified with If-Modified-Since
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, Returns304WithIfModifiedSince) {
    // First request: get the Last-Modified
    MockHttp http1("GET", "/hello.txt");
    SendFileOptions opts1;
    opts1.etag = false;
    http1.res->sendFile(testDir_ + "/hello.txt", opts1);
    auto lm = getHeaderStr(http1.sres,"Last-Modified");
    ASSERT_FALSE(lm.empty());

    // Second request: send If-Modified-Since with the same Last-Modified value
    // (string <= comparison in isFresh: lm <= ims means not modified)
    MockHttp http2("GET", "/hello.txt");
    http2.setHeader("If-Modified-Since", lm);
    SendFileOptions opts2;
    opts2.etag = false;
    http2.res->sendFile(testDir_ + "/hello.txt", opts2);

    EXPECT_EQ(http2.sres.statusCode(), 304);
}

// ═══════════════════════════════════════════════════════════════════════
// sendFile: Range request — 206 Partial Content
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, RangeRequest206PartialContent) {
    MockHttp http("GET", "/range.txt");
    http.setHeader("Range", "bytes=0-9");
    http.res->sendFile(testDir_ + "/range.txt");

    EXPECT_EQ(http.sres.statusCode(), 206);
}

TEST_F(SendFileTest, RangeRequestSetsContentRange) {
    MockHttp http("GET", "/range.txt");
    http.setHeader("Range", "bytes=0-9");
    http.res->sendFile(testDir_ + "/range.txt");

    auto cr = getHeaderStr(http.sres,"Content-Range");
    EXPECT_EQ(cr, "bytes 0-9/100");
}

TEST_F(SendFileTest, RangeRequestSetsContentLength) {
    MockHttp http("GET", "/range.txt");
    http.setHeader("Range", "bytes=0-9");
    http.res->sendFile(testDir_ + "/range.txt");

    auto cl = getHeaderStr(http.sres,"Content-Length");
    EXPECT_EQ(cl, "10");
}

TEST_F(SendFileTest, RangeRequestMiddleOfFile) {
    MockHttp http("GET", "/range.txt");
    http.setHeader("Range", "bytes=10-19");
    http.res->sendFile(testDir_ + "/range.txt");

    EXPECT_EQ(http.sres.statusCode(), 206);
    auto cr = getHeaderStr(http.sres,"Content-Range");
    EXPECT_EQ(cr, "bytes 10-19/100");
}

TEST_F(SendFileTest, RangeRequestSuffixRange) {
    MockHttp http("GET", "/range.txt");
    http.setHeader("Range", "bytes=-10");
    http.res->sendFile(testDir_ + "/range.txt");

    EXPECT_EQ(http.sres.statusCode(), 206);
    auto cr = getHeaderStr(http.sres,"Content-Range");
    EXPECT_EQ(cr, "bytes 90-99/100");
}

// ═══════════════════════════════════════════════════════════════════════
// sendFile: Invalid range — 416 Range Not Satisfiable
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, InvalidRangeReturns416) {
    MockHttp http("GET", "/range.txt");
    http.setHeader("Range", "bytes=200-300");
    http.res->sendFile(testDir_ + "/range.txt");

    EXPECT_EQ(http.sres.statusCode(), 416);
    auto cr = getHeaderStr(http.sres,"Content-Range");
    EXPECT_EQ(cr, "bytes */100");
}

// ═══════════════════════════════════════════════════════════════════════
// sendFile: Dotfiles handling
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, DotfilesIgnoreSkips) {
    MockHttp http("GET", "/.hidden");
    SendFileOptions opts;
    opts.dotfiles = "ignore";
    http.res->sendFile(testDir_ + "/.hidden", opts);

    EXPECT_EQ(http.sres.statusCode(), 404);
}

TEST_F(SendFileTest, DotfilesDenyReturns403) {
    MockHttp http("GET", "/.hidden");
    SendFileOptions opts;
    opts.dotfiles = "deny";
    http.res->sendFile(testDir_ + "/.hidden", opts);

    EXPECT_EQ(http.sres.statusCode(), 403);
}

TEST_F(SendFileTest, DotfilesAllowServes) {
    MockHttp http("GET", "/.hidden");
    SendFileOptions opts;
    opts.dotfiles = "allow";
    http.res->sendFile(testDir_ + "/.hidden", opts);

    // Should succeed (200 or similar, not 403/404)
    auto cl = getHeaderStr(http.sres,"Content-Length");
    EXPECT_EQ(cl, "6"); // "secret" is 6 bytes
}

// ═══════════════════════════════════════════════════════════════════════
// sendFile: Custom headers from opts
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, CustomHeadersFromOpts) {
    MockHttp http("GET", "/hello.txt");
    SendFileOptions opts;
    opts.headers = {{"X-Custom", "test-value"}, {"X-Another", "42"}};
    http.res->sendFile(testDir_ + "/hello.txt", opts);

    EXPECT_EQ(getHeaderStr(http.sres,"X-Custom"), "test-value");
    EXPECT_EQ(getHeaderStr(http.sres,"X-Another"), "42");
}

// ═══════════════════════════════════════════════════════════════════════
// sendFile: File not found — callback with error
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, FileNotFoundCallsCallbackWithError) {
    MockHttp http("GET", "/nonexistent.txt");
    std::optional<std::string> callbackError;
    http.res->sendFile(testDir_ + "/nonexistent.txt", {},
        [&](std::optional<std::string> err) {
            callbackError = err;
        });

    ASSERT_TRUE(callbackError.has_value());
    EXPECT_FALSE(callbackError->empty());
}

TEST_F(SendFileTest, FileNotFoundThrowsWithoutCallback) {
    MockHttp http("GET", "/nonexistent.txt");
    EXPECT_THROW(
        http.res->sendFile(testDir_ + "/nonexistent.txt"),
        std::exception
    );
}

// ═══════════════════════════════════════════════════════════════════════
// sendFile: Cache-Control with maxAge
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, SetsCacheControlWithMaxAge) {
    MockHttp http("GET", "/hello.txt");
    SendFileOptions opts;
    opts.maxAge = std::chrono::milliseconds(86400000); // 1 day
    http.res->sendFile(testDir_ + "/hello.txt", opts);

    auto cc = getHeaderStr(http.sres,"Cache-Control");
    EXPECT_EQ(cc, "public, max-age=86400");
}

// ═══════════════════════════════════════════════════════════════════════
// sendFile: Root option
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, ResolvesRelativeToRoot) {
    MockHttp http("GET", "/hello.txt");
    SendFileOptions opts;
    opts.root = testDir_;
    http.res->sendFile("hello.txt", opts);

    auto cl = getHeaderStr(http.sres,"Content-Length");
    EXPECT_EQ(cl, "13");
}

// ═══════════════════════════════════════════════════════════════════════
// sendFile: Callback success
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, CallbackWithNulloptOnSuccess) {
    MockHttp http("GET", "/hello.txt");
    std::optional<std::string> callbackError = "not called";
    http.res->sendFile(testDir_ + "/hello.txt", {},
        [&](std::optional<std::string> err) {
            callbackError = err;
        });

    EXPECT_FALSE(callbackError.has_value());
}

// ═══════════════════════════════════════════════════════════════════════
// serve_static: Serves files from root directory
// ═══════════════════════════════════════════════════════════════════════

class ServeStaticTest : public SendFileTest {};

TEST_F(ServeStaticTest, ServesFileFromRoot) {
    auto middleware = serveStatic(testDir_);

    MockHttp http("GET", "/hello.txt");
    bool nextCalled = false;
    middleware(*http.req, *http.res, [&](auto) { nextCalled = true; });

    EXPECT_FALSE(nextCalled);
    auto ct = getHeaderStr(http.sres,"Content-Type");
    EXPECT_TRUE(ct.find("text/plain") != std::string::npos);
    auto cl = getHeaderStr(http.sres,"Content-Length");
    EXPECT_EQ(cl, "13");
}

// ═══════════════════════════════════════════════════════════════════════
// serve_static: Serves index.html for directory
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ServeStaticTest, ServesIndexHtmlForDirectory) {
    auto middleware = serveStatic(testDir_);

    MockHttp http("GET", "/subdir/");
    bool nextCalled = false;
    middleware(*http.req, *http.res, [&](auto) { nextCalled = true; });

    EXPECT_FALSE(nextCalled);
    auto ct = getHeaderStr(http.sres,"Content-Type");
    EXPECT_TRUE(ct.find("text/html") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════
// serve_static: Sets Cache-Control with maxAge
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ServeStaticTest, SetsCacheControlWithMaxAge) {
    StaticOptions opts;
    opts.maxAge = std::chrono::milliseconds(3600000); // 1 hour
    auto middleware = serveStatic(testDir_, opts);

    MockHttp http("GET", "/hello.txt");
    bool nextCalled = false;
    middleware(*http.req, *http.res, [&](auto) { nextCalled = true; });

    auto cc = getHeaderStr(http.sres,"Cache-Control");
    EXPECT_EQ(cc, "public, max-age=3600");
}

// ═══════════════════════════════════════════════════════════════════════
// serve_static: 304 for conditional GET
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ServeStaticTest, Returns304ForConditionalGet) {
    auto middleware = serveStatic(testDir_);

    // First request: get ETag
    MockHttp http1("GET", "/hello.txt");
    bool next1 = false;
    middleware(*http1.req, *http1.res, [&](auto) { next1 = true; });
    auto etag = getHeaderStr(http1.sres,"ETag");
    ASSERT_FALSE(etag.empty());

    // Second request: conditional GET
    MockHttp http2("GET", "/hello.txt");
    http2.setHeader("If-None-Match", etag);
    bool next2 = false;
    middleware(*http2.req, *http2.res, [&](auto) { next2 = true; });

    EXPECT_EQ(http2.sres.statusCode(), 304);
}

// ═══════════════════════════════════════════════════════════════════════
// serve_static: Last-Modified header
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ServeStaticTest, SetsLastModifiedHeader) {
    auto middleware = serveStatic(testDir_);

    MockHttp http("GET", "/hello.txt");
    bool nextCalled = false;
    middleware(*http.req, *http.res, [&](auto) { nextCalled = true; });

    auto lm = getHeaderStr(http.sres,"Last-Modified");
    EXPECT_FALSE(lm.empty());
    EXPECT_TRUE(lm.find("GMT") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════
// serve_static: Accept-Ranges header
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ServeStaticTest, SetsAcceptRangesHeader) {
    auto middleware = serveStatic(testDir_);

    MockHttp http("GET", "/hello.txt");
    bool nextCalled = false;
    middleware(*http.req, *http.res, [&](auto) { nextCalled = true; });

    auto ar = getHeaderStr(http.sres,"Accept-Ranges");
    EXPECT_EQ(ar, "bytes");
}

// ═══════════════════════════════════════════════════════════════════════
// serve_static: ETag header
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ServeStaticTest, SetsETagHeader) {
    auto middleware = serveStatic(testDir_);

    MockHttp http("GET", "/hello.txt");
    bool nextCalled = false;
    middleware(*http.req, *http.res, [&](auto) { nextCalled = true; });

    auto etag = getHeaderStr(http.sres,"ETag");
    EXPECT_FALSE(etag.empty());
    EXPECT_TRUE(etag.starts_with("W/\""));
}

// ═══════════════════════════════════════════════════════════════════════
// serve_static: Range request support
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ServeStaticTest, RangeRequestReturns206) {
    auto middleware = serveStatic(testDir_);

    MockHttp http("GET", "/range.txt");
    http.setHeader("Range", "bytes=0-9");
    bool nextCalled = false;
    middleware(*http.req, *http.res, [&](auto) { nextCalled = true; });

    EXPECT_FALSE(nextCalled);
    EXPECT_EQ(http.sres.statusCode(), 206);
    auto cr = getHeaderStr(http.sres,"Content-Range");
    EXPECT_EQ(cr, "bytes 0-9/100");
}

// ═══════════════════════════════════════════════════════════════════════
// serve_static: Fallthrough on 404
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ServeStaticTest, FallthroughOnNotFound) {
    auto middleware = serveStatic(testDir_);

    MockHttp http("GET", "/nonexistent.txt");
    bool nextCalled = false;
    middleware(*http.req, *http.res, [&](auto) { nextCalled = true; });

    EXPECT_TRUE(nextCalled);
}

// ═══════════════════════════════════════════════════════════════════════
// serve_static: Non-GET/HEAD methods pass through
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ServeStaticTest, PostRequestPassesThrough) {
    auto middleware = serveStatic(testDir_);

    MockHttp http("POST", "/hello.txt");
    bool nextCalled = false;
    middleware(*http.req, *http.res, [&](auto) { nextCalled = true; });

    EXPECT_TRUE(nextCalled);
}

// ═══════════════════════════════════════════════════════════════════════
// serve_static: Directory traversal prevention
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ServeStaticTest, PreventsDirectoryTraversal) {
    auto middleware = serveStatic(testDir_);

    MockHttp http("GET", "/../etc/passwd");
    bool nextCalled = false;
    middleware(*http.req, *http.res, [&](auto) { nextCalled = true; });

    EXPECT_TRUE(nextCalled);
}

// ═══════════════════════════════════════════════════════════════════════
// serve_static: Dotfiles handling
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ServeStaticTest, DotfilesDenyReturns403) {
    StaticOptions opts;
    opts.dotfiles = "deny";
    auto middleware = serveStatic(testDir_, opts);

    MockHttp http("GET", "/.hidden");
    bool nextCalled = false;
    middleware(*http.req, *http.res, [&](auto) { nextCalled = true; });

    EXPECT_FALSE(nextCalled);
    EXPECT_EQ(http.sres.statusCode(), 403);
}

TEST_F(ServeStaticTest, DotfilesIgnoreFallsThrough) {
    StaticOptions opts;
    opts.dotfiles = "ignore";
    auto middleware = serveStatic(testDir_, opts);

    MockHttp http("GET", "/.hidden");
    bool nextCalled = false;
    middleware(*http.req, *http.res, [&](auto) { nextCalled = true; });

    EXPECT_TRUE(nextCalled);
}

// ═══════════════════════════════════════════════════════════════════════
// Regression: Path traversal via string prefix matching (BUG 1)
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, PathTraversalPrefixMatchBlocked) {
    // Create a sibling directory that shares the root prefix
    // e.g., root=/tmp/xxx, evil=/tmp/xxx-evil/
    auto evilDir = testDir_ + "-evil";
    polycpp::fs::mkdirSync(evilDir, true);
    polycpp::fs::writeFileSync(evilDir + "/secret.txt", "stolen");

    MockHttp http("GET", "/secret.txt");
    SendFileOptions opts;
    opts.root = testDir_;

    // Attempt to serve a file from the evil sibling via root option
    // The file path resolves to testDir_-evil/secret.txt which starts with
    // testDir_ as a string prefix but is NOT within the root directory.
    std::optional<std::string> callbackError;
    http.res->sendFile(evilDir + "/secret.txt", opts,
        [&](std::optional<std::string> err) { callbackError = err; });

    EXPECT_TRUE(callbackError.has_value());
    EXPECT_EQ(*callbackError, "Forbidden");

    // Cleanup
    polycpp::fs::rmSync(evilDir, true);
}

TEST_F(ServeStaticTest, PathTraversalPrefixMatchBlocked) {
    // Create a sibling directory that shares the root prefix
    auto evilDir = testDir_ + "-evil";
    polycpp::fs::mkdirSync(evilDir, true);
    polycpp::fs::writeFileSync(evilDir + "/secret.txt", "stolen");

    // The serve_static middleware uses its own root. We can't directly trigger
    // the prefix issue through the URL (the resolve prevents it), but we verify
    // the check is robust by ensuring a crafted canonical path is rejected.
    auto middleware = serveStatic(testDir_);

    // This should fall through (file not found) not serve evil content
    MockHttp http("GET", "/../" + polycpp::path::basename(evilDir) + "/secret.txt");
    bool nextCalled = false;
    middleware(*http.req, *http.res, [&](auto) { nextCalled = true; });

    EXPECT_TRUE(nextCalled);

    // Cleanup
    polycpp::fs::rmSync(evilDir, true);
}

// ═══════════════════════════════════════════════════════════════════════
// Regression: fresh() with 500 status returns false (BUG 3)
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, FreshReturnsFalseFor500Status) {
    // First request: get the ETag
    MockHttp http1("GET", "/hello.txt");
    http1.res->sendFile(testDir_ + "/hello.txt");
    auto etag = getHeaderStr(http1.sres,"ETag");
    ASSERT_FALSE(etag.empty());

    // Second request: set matching ETag but force 500 status
    MockHttp http2("GET", "/hello.txt");
    http2.setHeader("If-None-Match", etag);
    http2.res->status(500);

    // fresh() should return false for 500 even with matching ETag
    EXPECT_FALSE(http2.req->fresh());
}

TEST_F(SendFileTest, FreshReturnsFalseFor404Status) {
    MockHttp http("GET", "/hello.txt");
    http.res->status(404);
    http.sres.setHeader("ETag", "\"some-etag\"");
    http.setHeader("If-None-Match", "\"some-etag\"");

    EXPECT_FALSE(http.req->fresh());
}

TEST_F(SendFileTest, FreshReturnsTrueFor200WithMatchingEtag) {
    MockHttp http("GET", "/hello.txt");
    http.res->status(200);
    http.sres.setHeader("ETag", "\"some-etag\"");
    http.setHeader("If-None-Match", "\"some-etag\"");

    EXPECT_TRUE(http.req->fresh());
}

// ═══════════════════════════════════════════════════════════════════════
// Regression: sendFile dotfile with callback only calls callback (BUG 4)
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, DotfileDenyWithCallbackOnlyCallsCallback) {
    MockHttp http("GET", "/.hidden");
    SendFileOptions opts;
    opts.dotfiles = "deny";

    std::optional<std::string> callbackError;
    http.res->sendFile(testDir_ + "/.hidden", opts,
        [&](std::optional<std::string> err) { callbackError = err; });

    // Callback should have been called with error
    ASSERT_TRUE(callbackError.has_value());
    EXPECT_EQ(*callbackError, "Forbidden");

    // Response should NOT have been sent (status should still be default 200)
    EXPECT_EQ(http.sres.statusCode(), 200);
}

TEST_F(SendFileTest, DotfileIgnoreWithCallbackOnlyCallsCallback) {
    MockHttp http("GET", "/.hidden");
    SendFileOptions opts;
    opts.dotfiles = "ignore";

    std::optional<std::string> callbackError;
    http.res->sendFile(testDir_ + "/.hidden", opts,
        [&](std::optional<std::string> err) { callbackError = err; });

    // Callback should have been called with error
    ASSERT_TRUE(callbackError.has_value());
    EXPECT_EQ(*callbackError, "Not Found");

    // Response should NOT have been sent (status should still be default 200)
    EXPECT_EQ(http.sres.statusCode(), 200);
}

// ═══════════════════════════════════════════════════════════════════════
// Regression: Malformed Range header ignored, not 416 (BUG 5)
// ═══════════════════════════════════════════════════════════════════════

TEST_F(SendFileTest, MalformedRangeHeaderServesFullFile) {
    MockHttp http("GET", "/range.txt");
    http.setHeader("Range", "items=0-9");
    http.res->sendFile(testDir_ + "/range.txt");

    // Should serve the full file (200), not 416
    EXPECT_EQ(http.sres.statusCode(), 200);
    auto cl = getHeaderStr(http.sres,"Content-Length");
    EXPECT_EQ(cl, "100");
}

TEST_F(ServeStaticTest, MalformedRangeHeaderServesFullFile) {
    auto middleware = serveStatic(testDir_);

    MockHttp http("GET", "/range.txt");
    http.setHeader("Range", "items=0-9");
    bool nextCalled = false;
    middleware(*http.req, *http.res, [&](auto) { nextCalled = true; });

    EXPECT_FALSE(nextCalled);
    EXPECT_EQ(http.sres.statusCode(), 200);
    auto cl = getHeaderStr(http.sres,"Content-Length");
    EXPECT_EQ(cl, "100");
}
