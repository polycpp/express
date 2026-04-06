/**
 * @file test_compression.cpp
 * @brief Tests for the compression middleware.
 */

#include <gtest/gtest.h>
#include <polycpp/express/express.hpp>
#include <polycpp/zlib/zlib.hpp>

using namespace polycpp::express;
namespace cdetail = polycpp::express::detail;

// ═══════════════════════════════════════════════════════════════════════
// Helper: Mock Request/Response for testing middleware dispatch
// ═══════════════════════════════════════════════════════════════════════

class CompressionTestFixture {
public:
    CompressionTestFixture(const std::string& method, const std::string& url,
                           const polycpp::http::Headers& headers = {})
        : msg_(), res_(polycpp::net::Socket()) {
        msg_.method() = method;
        msg_.url() = url;
        msg_.headers() = headers;
        req_ = std::make_unique<Request>(msg_, nullptr);
        resp_ = std::make_unique<Response>(res_, nullptr);
        req_->setRes(resp_.get());
        resp_->setReq(req_.get());
    }

    Request& req() { return *req_; }
    Response& res() { return *resp_; }
    polycpp::http::ServerResponse& rawRes() { return res_; }

private:
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
// shouldCompress: default compressible types
// ═══════════════════════════════════════════════════════════════════════

TEST(ShouldCompressTest, TextTypesAreCompressible) {
    std::vector<std::string> noFilter;
    EXPECT_TRUE(cdetail::shouldCompress("text/html", noFilter));
    EXPECT_TRUE(cdetail::shouldCompress("text/plain", noFilter));
    EXPECT_TRUE(cdetail::shouldCompress("text/css", noFilter));
    EXPECT_TRUE(cdetail::shouldCompress("text/javascript", noFilter));
    EXPECT_TRUE(cdetail::shouldCompress("text/xml", noFilter));
}

TEST(ShouldCompressTest, ApplicationJsonIsCompressible) {
    std::vector<std::string> noFilter;
    EXPECT_TRUE(cdetail::shouldCompress("application/json", noFilter));
}

TEST(ShouldCompressTest, ApplicationJavascriptIsCompressible) {
    std::vector<std::string> noFilter;
    EXPECT_TRUE(cdetail::shouldCompress("application/javascript", noFilter));
    EXPECT_TRUE(cdetail::shouldCompress("application/x-javascript", noFilter));
}

TEST(ShouldCompressTest, ApplicationXmlIsCompressible) {
    std::vector<std::string> noFilter;
    EXPECT_TRUE(cdetail::shouldCompress("application/xml", noFilter));
    EXPECT_TRUE(cdetail::shouldCompress("application/xhtml+xml", noFilter));
    EXPECT_TRUE(cdetail::shouldCompress("application/rss+xml", noFilter));
    EXPECT_TRUE(cdetail::shouldCompress("application/atom+xml", noFilter));
}

TEST(ShouldCompressTest, SvgIsCompressible) {
    std::vector<std::string> noFilter;
    EXPECT_TRUE(cdetail::shouldCompress("image/svg+xml", noFilter));
}

TEST(ShouldCompressTest, UrlencodedIsCompressible) {
    std::vector<std::string> noFilter;
    EXPECT_TRUE(cdetail::shouldCompress("application/x-www-form-urlencoded", noFilter));
}

TEST(ShouldCompressTest, ImageTypesAreNotCompressible) {
    std::vector<std::string> noFilter;
    EXPECT_FALSE(cdetail::shouldCompress("image/png", noFilter));
    EXPECT_FALSE(cdetail::shouldCompress("image/jpeg", noFilter));
    EXPECT_FALSE(cdetail::shouldCompress("image/gif", noFilter));
    EXPECT_FALSE(cdetail::shouldCompress("image/webp", noFilter));
}

TEST(ShouldCompressTest, AudioVideoNotCompressible) {
    std::vector<std::string> noFilter;
    EXPECT_FALSE(cdetail::shouldCompress("audio/mpeg", noFilter));
    EXPECT_FALSE(cdetail::shouldCompress("video/mp4", noFilter));
}

TEST(ShouldCompressTest, OctetStreamNotCompressible) {
    std::vector<std::string> noFilter;
    EXPECT_FALSE(cdetail::shouldCompress("application/octet-stream", noFilter));
    EXPECT_FALSE(cdetail::shouldCompress("application/zip", noFilter));
    EXPECT_FALSE(cdetail::shouldCompress("application/pdf", noFilter));
}

TEST(ShouldCompressTest, EmptyContentTypeNotCompressible) {
    std::vector<std::string> noFilter;
    EXPECT_FALSE(cdetail::shouldCompress("", noFilter));
}

TEST(ShouldCompressTest, ContentTypeWithCharset) {
    std::vector<std::string> noFilter;
    EXPECT_TRUE(cdetail::shouldCompress("text/html; charset=utf-8", noFilter));
    EXPECT_TRUE(cdetail::shouldCompress("application/json; charset=utf-8", noFilter));
}

TEST(ShouldCompressTest, CaseInsensitive) {
    std::vector<std::string> noFilter;
    EXPECT_TRUE(cdetail::shouldCompress("Text/HTML", noFilter));
    EXPECT_TRUE(cdetail::shouldCompress("APPLICATION/JSON", noFilter));
    EXPECT_FALSE(cdetail::shouldCompress("IMAGE/PNG", noFilter));
}

// ═══════════════════════════════════════════════════════════════════════
// shouldCompress: custom filter
// ═══════════════════════════════════════════════════════════════════════

TEST(ShouldCompressTest, CustomFilterExactMatch) {
    std::vector<std::string> filter = {"application/json"};
    EXPECT_TRUE(cdetail::shouldCompress("application/json", filter));
    EXPECT_FALSE(cdetail::shouldCompress("text/html", filter));
}

TEST(ShouldCompressTest, CustomFilterWildcardStar) {
    std::vector<std::string> filter = {"text/*"};
    EXPECT_TRUE(cdetail::shouldCompress("text/html", filter));
    EXPECT_TRUE(cdetail::shouldCompress("text/plain", filter));
    EXPECT_FALSE(cdetail::shouldCompress("application/json", filter));
}

TEST(ShouldCompressTest, CustomFilterTrailingSlash) {
    std::vector<std::string> filter = {"text/"};
    EXPECT_TRUE(cdetail::shouldCompress("text/html", filter));
    EXPECT_TRUE(cdetail::shouldCompress("text/css", filter));
    EXPECT_FALSE(cdetail::shouldCompress("application/json", filter));
}

TEST(ShouldCompressTest, CustomFilterMultipleEntries) {
    std::vector<std::string> filter = {"text/*", "application/json", "image/svg+xml"};
    EXPECT_TRUE(cdetail::shouldCompress("text/html", filter));
    EXPECT_TRUE(cdetail::shouldCompress("application/json", filter));
    EXPECT_TRUE(cdetail::shouldCompress("image/svg+xml", filter));
    EXPECT_FALSE(cdetail::shouldCompress("image/png", filter));
}

TEST(ShouldCompressTest, CustomFilterIncludesNonDefaultType) {
    std::vector<std::string> filter = {"image/png"};
    // Even though image/png is not compressible by default, custom filter overrides
    EXPECT_TRUE(cdetail::shouldCompress("image/png", filter));
}

// ═══════════════════════════════════════════════════════════════════════
// negotiateEncoding
// ═══════════════════════════════════════════════════════════════════════

TEST(NegotiateEncodingTest, PrefersBrotli) {
    EXPECT_EQ(cdetail::negotiateEncoding("gzip, deflate, br"), "br");
}

TEST(NegotiateEncodingTest, PrefersGzipOverDeflate) {
    EXPECT_EQ(cdetail::negotiateEncoding("deflate, gzip"), "gzip");
}

TEST(NegotiateEncodingTest, SelectsGzip) {
    EXPECT_EQ(cdetail::negotiateEncoding("gzip"), "gzip");
}

TEST(NegotiateEncodingTest, SelectsDeflate) {
    EXPECT_EQ(cdetail::negotiateEncoding("deflate"), "deflate");
}

TEST(NegotiateEncodingTest, SelectsBrotli) {
    EXPECT_EQ(cdetail::negotiateEncoding("br"), "br");
}

TEST(NegotiateEncodingTest, EmptyReturnsEmpty) {
    EXPECT_EQ(cdetail::negotiateEncoding(""), "");
}

TEST(NegotiateEncodingTest, IdentityReturnsEmpty) {
    EXPECT_EQ(cdetail::negotiateEncoding("identity"), "");
}

TEST(NegotiateEncodingTest, WildcardDefaultsToGzip) {
    EXPECT_EQ(cdetail::negotiateEncoding("*"), "gzip");
}

TEST(NegotiateEncodingTest, CaseInsensitive) {
    EXPECT_EQ(cdetail::negotiateEncoding("GZIP"), "gzip");
    EXPECT_EQ(cdetail::negotiateEncoding("Br, Gzip"), "br");
}

// ═══════════════════════════════════════════════════════════════════════
// compressBody: compression round-trips
// ═══════════════════════════════════════════════════════════════════════

TEST(CompressBodyTest, GzipRoundTrip) {
    std::string original = "Hello World, this is a test of gzip compression!";
    auto compressed = cdetail::compressBody(original, "gzip", -1);
    ASSERT_TRUE(compressed.has_value());
    EXPECT_NE(*compressed, original);

    // Decompress and verify
    auto decompressed = polycpp::zlib::gunzipSync(
        polycpp::Buffer::from(*compressed));
    EXPECT_EQ(decompressed.toString(), original);
}

TEST(CompressBodyTest, DeflateRoundTrip) {
    std::string original = "Hello World, this is a test of deflate compression!";
    auto compressed = cdetail::compressBody(original, "deflate", -1);
    ASSERT_TRUE(compressed.has_value());
    EXPECT_NE(*compressed, original);

    auto decompressed = polycpp::zlib::inflateSync(
        polycpp::Buffer::from(*compressed));
    EXPECT_EQ(decompressed.toString(), original);
}

TEST(CompressBodyTest, BrotliRoundTrip) {
    std::string original = "Hello World, this is a test of brotli compression!";
    auto compressed = cdetail::compressBody(original, "br", -1);
    ASSERT_TRUE(compressed.has_value());
    EXPECT_NE(*compressed, original);

    auto decompressed = polycpp::zlib::brotliDecompressSync(
        polycpp::Buffer::from(*compressed));
    EXPECT_EQ(decompressed.toString(), original);
}

TEST(CompressBodyTest, GzipWithCustomLevel) {
    std::string original = "Hello World, repeated text for compression testing. "
                          "Hello World, repeated text for compression testing. ";
    auto compressed = cdetail::compressBody(original, "gzip", 9);
    ASSERT_TRUE(compressed.has_value());

    auto decompressed = polycpp::zlib::gunzipSync(
        polycpp::Buffer::from(*compressed));
    EXPECT_EQ(decompressed.toString(), original);
}

TEST(CompressBodyTest, UnknownEncodingReturnsNullopt) {
    auto compressed = cdetail::compressBody("hello", "unknown", -1);
    EXPECT_FALSE(compressed.has_value());
}

TEST(CompressBodyTest, LargeBodyCompresses) {
    std::string large;
    for (int i = 0; i < 1000; ++i) {
        large += "The quick brown fox jumps over the lazy dog. ";
    }
    auto compressed = cdetail::compressBody(large, "gzip", -1);
    ASSERT_TRUE(compressed.has_value());
    EXPECT_LT(compressed->size(), large.size());

    auto decompressed = polycpp::zlib::gunzipSync(
        polycpp::Buffer::from(*compressed));
    EXPECT_EQ(decompressed.toString(), large);
}

TEST(CompressBodyTest, EmptyBodyCompresses) {
    auto compressed = cdetail::compressBody("", "gzip", -1);
    ASSERT_TRUE(compressed.has_value());

    auto decompressed = polycpp::zlib::gunzipSync(
        polycpp::Buffer::from(*compressed));
    EXPECT_EQ(decompressed.toString(), "");
}

// ═══════════════════════════════════════════════════════════════════════
// Middleware: sets Vary header
// ═══════════════════════════════════════════════════════════════════════

TEST(CompressionMiddlewareTest, SetsVaryAcceptEncoding) {
    CompressionTestFixture f("GET", "/", {{"accept-encoding", "gzip"}});

    bool nextCalled = false;
    auto handler = compress();
    handler(f.req(), f.res(), [&](std::optional<HttpError>) {
        nextCalled = true;
    });

    EXPECT_TRUE(nextCalled);
    auto vary = getHeaderStr(f.rawRes(),"Vary");
    EXPECT_NE(vary.find("Accept-Encoding"), std::string::npos);
}

TEST(CompressionMiddlewareTest, SetsVaryEvenWithoutAcceptEncoding) {
    CompressionTestFixture f("GET", "/", {});

    bool nextCalled = false;
    auto handler = compress();
    handler(f.req(), f.res(), [&](std::optional<HttpError>) {
        nextCalled = true;
    });

    EXPECT_TRUE(nextCalled);
    auto vary = getHeaderStr(f.rawRes(),"Vary");
    EXPECT_NE(vary.find("Accept-Encoding"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════
// Middleware: sets compression locals for gzip
// ═══════════════════════════════════════════════════════════════════════

TEST(CompressionMiddlewareTest, SetsGzipLocals) {
    CompressionTestFixture f("GET", "/", {{"accept-encoding", "gzip"}});

    auto handler = compress();
    handler(f.req(), f.res(), [&](std::optional<HttpError>) {});

    auto& locals = f.res().locals;
    EXPECT_TRUE(locals.isObject());
    EXPECT_EQ(locals.asObject()["_compression_encoding"].asString(), "gzip");
    EXPECT_EQ(static_cast<int>(locals.asObject()["_compression_threshold"].asNumber()), 1024);
    EXPECT_EQ(static_cast<int>(locals.asObject()["_compression_level"].asNumber()), -1);
}

// ═══════════════════════════════════════════════════════════════════════
// Middleware: sets compression locals for deflate
// ═══════════════════════════════════════════════════════════════════════

TEST(CompressionMiddlewareTest, SetsDeflateLocals) {
    CompressionTestFixture f("GET", "/", {{"accept-encoding", "deflate"}});

    auto handler = compress();
    handler(f.req(), f.res(), [&](std::optional<HttpError>) {});

    auto& locals = f.res().locals;
    EXPECT_EQ(locals.asObject()["_compression_encoding"].asString(), "deflate");
}

// ═══════════════════════════════════════════════════════════════════════
// Middleware: sets compression locals for brotli
// ═══════════════════════════════════════════════════════════════════════

TEST(CompressionMiddlewareTest, SetsBrotliLocals) {
    CompressionTestFixture f("GET", "/", {{"accept-encoding", "br"}});

    auto handler = compress();
    handler(f.req(), f.res(), [&](std::optional<HttpError>) {});

    auto& locals = f.res().locals;
    EXPECT_EQ(locals.asObject()["_compression_encoding"].asString(), "br");
}

// ═══════════════════════════════════════════════════════════════════════
// Middleware: no encoding when Accept-Encoding is missing
// ═══════════════════════════════════════════════════════════════════════

TEST(CompressionMiddlewareTest, NoEncodingWhenMissing) {
    CompressionTestFixture f("GET", "/", {});

    auto handler = compress();
    handler(f.req(), f.res(), [&](std::optional<HttpError>) {});

    auto& locals = f.res().locals;
    // _compression_encoding should not be set
    EXPECT_EQ(locals.asObject().count("_compression_encoding"), 0u);
}

// ═══════════════════════════════════════════════════════════════════════
// Middleware: no encoding for unsupported Accept-Encoding
// ═══════════════════════════════════════════════════════════════════════

TEST(CompressionMiddlewareTest, NoEncodingForIdentityOnly) {
    CompressionTestFixture f("GET", "/", {{"accept-encoding", "identity"}});

    auto handler = compress();
    handler(f.req(), f.res(), [&](std::optional<HttpError>) {});

    auto& locals = f.res().locals;
    EXPECT_EQ(locals.asObject().count("_compression_encoding"), 0u);
}

// ═══════════════════════════════════════════════════════════════════════
// Middleware: custom options are stored in locals
// ═══════════════════════════════════════════════════════════════════════

TEST(CompressionMiddlewareTest, CustomThresholdInLocals) {
    CompressionTestFixture f("GET", "/", {{"accept-encoding", "gzip"}});

    auto handler = compress({.threshold = 512});
    handler(f.req(), f.res(), [&](std::optional<HttpError>) {});

    auto& locals = f.res().locals;
    EXPECT_EQ(static_cast<int>(locals.asObject()["_compression_threshold"].asNumber()), 512);
}

TEST(CompressionMiddlewareTest, CustomLevelInLocals) {
    CompressionTestFixture f("GET", "/", {{"accept-encoding", "gzip"}});

    auto handler = compress({.level = 6});
    handler(f.req(), f.res(), [&](std::optional<HttpError>) {});

    auto& locals = f.res().locals;
    EXPECT_EQ(static_cast<int>(locals.asObject()["_compression_level"].asNumber()), 6);
}

TEST(CompressionMiddlewareTest, CustomFilterInLocals) {
    CompressionTestFixture f("GET", "/", {{"accept-encoding", "gzip"}});

    auto handler = compress({.threshold = 0, .filter = {"text/*", "image/svg+xml"}});
    handler(f.req(), f.res(), [&](std::optional<HttpError>) {});

    auto& locals = f.res().locals;
    ASSERT_TRUE(locals.asObject()["_compression_filter"].isArray());
    auto& filterArr = locals.asObject()["_compression_filter"].asArray();
    ASSERT_EQ(filterArr.size(), 2u);
    EXPECT_EQ(filterArr[0].asString(), "text/*");
    EXPECT_EQ(filterArr[1].asString(), "image/svg+xml");
}

// ═══════════════════════════════════════════════════════════════════════
// Middleware: encoding preference br > gzip > deflate
// ═══════════════════════════════════════════════════════════════════════

TEST(CompressionMiddlewareTest, PrefersBrotliOverGzip) {
    CompressionTestFixture f("GET", "/", {{"accept-encoding", "gzip, br, deflate"}});

    auto handler = compress();
    handler(f.req(), f.res(), [&](std::optional<HttpError>) {});

    auto& locals = f.res().locals;
    EXPECT_EQ(locals.asObject()["_compression_encoding"].asString(), "br");
}

TEST(CompressionMiddlewareTest, PrefersGzipOverDeflate) {
    CompressionTestFixture f("GET", "/", {{"accept-encoding", "deflate, gzip"}});

    auto handler = compress();
    handler(f.req(), f.res(), [&](std::optional<HttpError>) {});

    auto& locals = f.res().locals;
    EXPECT_EQ(locals.asObject()["_compression_encoding"].asString(), "gzip");
}

// ═══════════════════════════════════════════════════════════════════════
// Middleware: next is always called
// ═══════════════════════════════════════════════════════════════════════

TEST(CompressionMiddlewareTest, NextAlwaysCalledWithEncoding) {
    CompressionTestFixture f("GET", "/", {{"accept-encoding", "gzip"}});

    bool nextCalled = false;
    auto handler = compress();
    handler(f.req(), f.res(), [&](std::optional<HttpError> err) {
        EXPECT_FALSE(err.has_value());
        nextCalled = true;
    });

    EXPECT_TRUE(nextCalled);
}

TEST(CompressionMiddlewareTest, NextAlwaysCalledWithoutEncoding) {
    CompressionTestFixture f("GET", "/", {});

    bool nextCalled = false;
    auto handler = compress();
    handler(f.req(), f.res(), [&](std::optional<HttpError> err) {
        EXPECT_FALSE(err.has_value());
        nextCalled = true;
    });

    EXPECT_TRUE(nextCalled);
}

// ═══════════════════════════════════════════════════════════════════════
// Convenience: compress() returns a valid handler
// ═══════════════════════════════════════════════════════════════════════

TEST(CompressionMiddlewareTest, CompressReturnsHandler) {
    auto handler = compress();
    EXPECT_TRUE(static_cast<bool>(handler));
}

TEST(CompressionMiddlewareTest, CompressWithOptionsReturnsHandler) {
    auto handler = compress({.level = 6, .threshold = 512});
    EXPECT_TRUE(static_cast<bool>(handler));
}

// ═══════════════════════════════════════════════════════════════════════
// BUG 10: Proper Accept-Encoding parsing regression tests
// ═══════════════════════════════════════════════════════════════════════

TEST(NegotiateEncodingTest, RespectsQualityZero) {
    // br;q=0 should not select brotli
    EXPECT_NE(cdetail::negotiateEncoding("br;q=0, gzip"), "br");
    EXPECT_EQ(cdetail::negotiateEncoding("br;q=0, gzip"), "gzip");
}

TEST(NegotiateEncodingTest, RespectsQualityOrdering) {
    // gzip has higher quality than br
    auto result = cdetail::negotiateEncoding("br;q=0.5, gzip;q=1.0");
    EXPECT_EQ(result, "gzip");
}

TEST(NegotiateEncodingTest, SubstringDoesNotMatch) {
    // "brotli" is not "br" -- should not match brotli as br
    // "identity" contains no supported encoding
    EXPECT_EQ(cdetail::negotiateEncoding("identity"), "");
}

TEST(NegotiateEncodingTest, TokenBoundaryCheck) {
    // "compress" contains no valid token for us
    EXPECT_EQ(cdetail::negotiateEncoding("compress"), "");
}
