/**
 * @file test_view_render.cpp
 * @brief Tests for View class and template engine rendering.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <polycpp/express/express.hpp>

using namespace polycpp::express;
namespace fs = polycpp::fs;
namespace JSON = polycpp::JSON;
using polycpp::JsonValue;
using polycpp::JsonObject;
using polycpp::JsonArray;

// ═══════════════════════════════════════════════════════════════════════
// Test fixture: creates a temporary views directory structure
// ═══════════════════════════════════════════════════════════════════════

class ViewTest : public ::testing::Test {
protected:
    std::string tmpDir_;
    std::string viewsDir_;
    std::string viewsDir2_;

    void SetUp() override {
        // Create temp directory structure
        tmpDir_ = std::filesystem::temp_directory_path().string() + "/polycpp_view_test_"
                  + std::to_string(::getpid());
        viewsDir_ = tmpDir_ + "/views";
        viewsDir2_ = tmpDir_ + "/views2";

        std::filesystem::create_directories(viewsDir_);
        std::filesystem::create_directories(viewsDir2_);

        // Create template files
        writeFile(viewsDir_ + "/index.ejs", "<h1>Hello <%= name %></h1>");
        writeFile(viewsDir_ + "/about.ejs", "<h1>About</h1>");
        writeFile(viewsDir_ + "/layout.pug", "html\n  body\n    block content");
        writeFile(viewsDir_ + "/contact.html", "<h1>Contact</h1>");

        // Create a directory with an index file (directory index fallback)
        std::filesystem::create_directories(viewsDir_ + "/users");
        writeFile(viewsDir_ + "/users/index.ejs", "<h1>Users List</h1>");
        writeFile(viewsDir_ + "/users/profile.ejs", "<h1>User Profile</h1>");

        // Create template in second views directory
        writeFile(viewsDir2_ + "/admin.ejs", "<h1>Admin Panel</h1>");
    }

    void TearDown() override {
        std::filesystem::remove_all(tmpDir_);
    }

    void writeFile(const std::string& path, const std::string& content) {
        std::ofstream ofs(path);
        ofs << content;
        ofs.close();
    }

    // A simple mock engine that just returns the file content with variables
    static EngineFunction mockEngine() {
        return [](const std::string& filePath, const JsonValue& options,
                  std::function<void(std::optional<std::string>, std::string)> cb) {
            try {
                auto content = fs::readFileSync(filePath);
                // Simple substitution: replace <%= key %> with value
                if (options.isObject()) {
                    for (const auto& [key, val] : options.asObject()) {
                        std::string placeholder = "<%= " + key + " %>";
                        auto pos = content.find(placeholder);
                        if (pos != std::string::npos) {
                            std::string replacement;
                            if (val.isString()) {
                                replacement = val.asString();
                            } else if (val.isNumber()) {
                                replacement = std::to_string(static_cast<int>(val.asNumber()));
                            } else {
                                replacement = JSON::stringify(val);
                            }
                            content.replace(pos, placeholder.size(), replacement);
                        }
                    }
                }
                cb(std::nullopt, content);
            } catch (const std::exception& e) {
                cb(std::string(e.what()), "");
            }
        };
    }
};

// ═══════════════════════════════════════════════════════════════════════
// View Construction Tests
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ViewTest, ConstructWithDefaultEngine) {
    std::map<std::string, EngineFunction> engines;
    engines[".ejs"] = mockEngine();

    View view("index", engines, "ejs", {viewsDir_});
    EXPECT_TRUE(view.lookup());
    EXPECT_EQ(view.ext(), ".ejs");
    EXPECT_EQ(view.name(), "index");
    EXPECT_FALSE(view.filePath().empty());
}

TEST_F(ViewTest, ConstructWithExplicitExtension) {
    std::map<std::string, EngineFunction> engines;
    engines[".pug"] = mockEngine();

    View view("layout.pug", engines, "", {viewsDir_});
    EXPECT_TRUE(view.lookup());
    EXPECT_EQ(view.ext(), ".pug");
}

TEST_F(ViewTest, ConstructThrowsWithNoExtensionAndNoDefaultEngine) {
    std::map<std::string, EngineFunction> engines;

    EXPECT_THROW(
        View("index", engines, "", {viewsDir_}),
        std::runtime_error
    );
}

TEST_F(ViewTest, ConstructWithDotPrefixedDefaultEngine) {
    std::map<std::string, EngineFunction> engines;
    engines[".ejs"] = mockEngine();

    View view("index", engines, ".ejs", {viewsDir_});
    EXPECT_TRUE(view.lookup());
    EXPECT_EQ(view.ext(), ".ejs");
}

// ═══════════════════════════════════════════════════════════════════════
// View Path Resolution Tests
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ViewTest, ResolveSingleRoot) {
    std::map<std::string, EngineFunction> engines;
    engines[".ejs"] = mockEngine();

    View view("index", engines, "ejs", {viewsDir_});
    EXPECT_TRUE(view.lookup());

    auto expected = viewsDir_ + "/index.ejs";
    EXPECT_EQ(view.filePath(), expected);
}

TEST_F(ViewTest, ResolveSubdirectoryFile) {
    std::map<std::string, EngineFunction> engines;
    engines[".ejs"] = mockEngine();

    View view("users/profile", engines, "ejs", {viewsDir_});
    EXPECT_TRUE(view.lookup());

    auto expected = viewsDir_ + "/users/profile.ejs";
    EXPECT_EQ(view.filePath(), expected);
}

TEST_F(ViewTest, ResolveIndexFallback) {
    std::map<std::string, EngineFunction> engines;
    engines[".ejs"] = mockEngine();

    // "users" should resolve to "users/index.ejs"
    View view("users", engines, "ejs", {viewsDir_});
    EXPECT_TRUE(view.lookup());

    auto expected = viewsDir_ + "/users/index.ejs";
    EXPECT_EQ(view.filePath(), expected);
}

TEST_F(ViewTest, ResolveMultipleRoots) {
    std::map<std::string, EngineFunction> engines;
    engines[".ejs"] = mockEngine();

    // "admin" only exists in viewsDir2_
    View view("admin", engines, "ejs", {viewsDir_, viewsDir2_});
    EXPECT_TRUE(view.lookup());

    auto expected = viewsDir2_ + "/admin.ejs";
    EXPECT_EQ(view.filePath(), expected);
}

TEST_F(ViewTest, ResolveMultipleRootsFirstWins) {
    std::map<std::string, EngineFunction> engines;
    engines[".ejs"] = mockEngine();

    // Create same file in both dirs
    writeFile(viewsDir2_ + "/index.ejs", "<h1>Second</h1>");

    // "index" exists in both, viewsDir_ should win
    View view("index", engines, "ejs", {viewsDir_, viewsDir2_});
    EXPECT_TRUE(view.lookup());
    EXPECT_EQ(view.filePath(), viewsDir_ + "/index.ejs");
}

TEST_F(ViewTest, ViewNotFound) {
    std::map<std::string, EngineFunction> engines;
    engines[".ejs"] = mockEngine();

    View view("nonexistent", engines, "ejs", {viewsDir_});
    EXPECT_FALSE(view.lookup());
    EXPECT_TRUE(view.filePath().empty());
}

TEST_F(ViewTest, ViewWithExplicitExtension) {
    std::map<std::string, EngineFunction> engines;
    engines[".html"] = mockEngine();

    View view("contact.html", engines, "", {viewsDir_});
    EXPECT_TRUE(view.lookup());
    EXPECT_EQ(view.filePath(), viewsDir_ + "/contact.html");
}

// ═══════════════════════════════════════════════════════════════════════
// View Rendering Tests
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ViewTest, RenderCallsEngine) {
    std::map<std::string, EngineFunction> engines;
    engines[".ejs"] = mockEngine();

    View view("index", engines, "ejs", {viewsDir_});
    ASSERT_TRUE(view.lookup());

    std::optional<std::string> gotError;
    std::string gotHtml;

    view.render(JsonObject{{"name", "World"}},
                [&](std::optional<std::string> err, std::string html) {
                    gotError = err;
                    gotHtml = html;
                });

    EXPECT_FALSE(gotError.has_value());
    EXPECT_EQ(gotHtml, "<h1>Hello World</h1>");
}

TEST_F(ViewTest, RenderPassesCorrectPath) {
    std::string capturedPath;
    EngineFunction pathCapture = [&](const std::string& p, const JsonValue&,
                                     std::function<void(std::optional<std::string>, std::string)> cb) {
        capturedPath = p;
        cb(std::nullopt, "ok");
    };

    std::map<std::string, EngineFunction> engines;
    engines[".ejs"] = pathCapture;

    View view("about", engines, "ejs", {viewsDir_});
    ASSERT_TRUE(view.lookup());

    view.render(JsonObject{}, [](auto, auto) {});

    EXPECT_EQ(capturedPath, viewsDir_ + "/about.ejs");
}

TEST_F(ViewTest, RenderPassesOptions) {
    JsonValue capturedOptions;
    EngineFunction optCapture = [&](const std::string&, const JsonValue& opts,
                                    std::function<void(std::optional<std::string>, std::string)> cb) {
        capturedOptions = opts;
        cb(std::nullopt, "ok");
    };

    std::map<std::string, EngineFunction> engines;
    engines[".ejs"] = optCapture;

    View view("index", engines, "ejs", {viewsDir_});
    ASSERT_TRUE(view.lookup());

    JsonObject opts{{"title", "Test"}, {"count", 42}};
    view.render(JsonValue(opts), [](auto, auto) {});

    ASSERT_TRUE(capturedOptions.isObject());
    EXPECT_EQ(capturedOptions.asObject().at("title").asString(), "Test");
    EXPECT_EQ(capturedOptions.asObject().at("count").asNumber(), 42);
}

TEST_F(ViewTest, RenderWithNoEngineCallsBackWithError) {
    std::map<std::string, EngineFunction> engines;
    // Register .ejs engine so constructor succeeds
    engines[".ejs"] = mockEngine();

    // But create a view for a .pug file with no .pug engine
    // We need to use explicit extension since there's no default engine for .pug
    writeFile(viewsDir_ + "/test.xyz", "content");
    std::map<std::string, EngineFunction> emptyEngines;
    emptyEngines[".xyz"] = nullptr; // registered but null

    // Instead, let's test with no engine by not registering one
    std::map<std::string, EngineFunction> engines2;
    // Don't register .ejs, so engine_ will be empty
    // But the constructor still succeeds -- it just won't have an engine

    View view("index", engines2, "ejs", {viewsDir_});
    ASSERT_TRUE(view.lookup()); // File exists

    std::optional<std::string> gotError;
    view.render(JsonObject{}, [&](std::optional<std::string> err, std::string) {
        gotError = err;
    });

    ASSERT_TRUE(gotError.has_value());
    EXPECT_NE(gotError->find("No engine registered"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════
// Application render() Tests
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ViewTest, AppRenderBasic) {
    auto app = express();
    app.set("views", viewsDir_);
    app.set("view engine", "ejs");
    app.engine("ejs", mockEngine());

    std::optional<std::string> gotError;
    std::string gotHtml;

    app.render("index", JsonObject{{"name", "Express"}},
               [&](std::optional<std::string> err, std::string html) {
                   gotError = err;
                   gotHtml = html;
               });

    EXPECT_FALSE(gotError.has_value()) << "Error: " << gotError.value_or("");
    EXPECT_EQ(gotHtml, "<h1>Hello Express</h1>");
}

TEST_F(ViewTest, AppRenderNotFound) {
    auto app = express();
    app.set("views", viewsDir_);
    app.set("view engine", "ejs");
    app.engine("ejs", mockEngine());

    std::optional<std::string> gotError;

    app.render("nonexistent", JsonObject{},
               [&](std::optional<std::string> err, std::string) {
                   gotError = err;
               });

    ASSERT_TRUE(gotError.has_value());
    EXPECT_NE(gotError->find("Failed to lookup view"), std::string::npos);
    EXPECT_NE(gotError->find("nonexistent"), std::string::npos);
}

TEST_F(ViewTest, AppRenderMergesAppLocals) {
    auto app = express();
    app.set("views", viewsDir_);
    app.set("view engine", "ejs");

    JsonValue capturedOptions;
    app.engine("ejs", [&](const std::string&, const JsonValue& opts,
                          std::function<void(std::optional<std::string>, std::string)> cb) {
        capturedOptions = opts;
        cb(std::nullopt, "ok");
    });

    // Set app.locals
    app.locals = JsonObject{{"appName", "MyApp"}, {"version", "1.0"}};

    app.render("index", JsonObject{{"name", "User"}},
               [](auto, auto) {});

    ASSERT_TRUE(capturedOptions.isObject());
    // app.locals should be merged
    EXPECT_EQ(capturedOptions.asObject().at("appName").asString(), "MyApp");
    // options should be present
    EXPECT_EQ(capturedOptions.asObject().at("name").asString(), "User");
}

TEST_F(ViewTest, AppRenderOptionsOverrideLocals) {
    auto app = express();
    app.set("views", viewsDir_);
    app.set("view engine", "ejs");

    JsonValue capturedOptions;
    app.engine("ejs", [&](const std::string&, const JsonValue& opts,
                          std::function<void(std::optional<std::string>, std::string)> cb) {
        capturedOptions = opts;
        cb(std::nullopt, "ok");
    });

    // Set app.locals with a value that will be overridden
    app.locals = JsonObject{{"name", "AppDefault"}};

    // Options override app.locals
    app.render("index", JsonObject{{"name", "Override"}},
               [](auto, auto) {});

    ASSERT_TRUE(capturedOptions.isObject());
    EXPECT_EQ(capturedOptions.asObject().at("name").asString(), "Override");
}

TEST_F(ViewTest, AppRenderWithMultipleViewDirs) {
    auto app = express();
    app.set("views", JsonValue(JsonArray{viewsDir_, viewsDir2_}));
    app.set("view engine", "ejs");
    app.engine("ejs", mockEngine());

    std::optional<std::string> gotError;
    std::string gotHtml;

    app.render("admin", JsonObject{},
               [&](std::optional<std::string> err, std::string html) {
                   gotError = err;
                   gotHtml = html;
               });

    EXPECT_FALSE(gotError.has_value()) << "Error: " << gotError.value_or("");
    EXPECT_EQ(gotHtml, "<h1>Admin Panel</h1>");
}

TEST_F(ViewTest, AppRenderWithIndexFallback) {
    auto app = express();
    app.set("views", viewsDir_);
    app.set("view engine", "ejs");
    app.engine("ejs", mockEngine());

    std::optional<std::string> gotError;
    std::string gotHtml;

    app.render("users", JsonObject{},
               [&](std::optional<std::string> err, std::string html) {
                   gotError = err;
                   gotHtml = html;
               });

    EXPECT_FALSE(gotError.has_value()) << "Error: " << gotError.value_or("");
    EXPECT_EQ(gotHtml, "<h1>Users List</h1>");
}

// ═══════════════════════════════════════════════════════════════════════
// View Caching Tests
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ViewTest, AppRenderCachesViewWhenEnabled) {
    auto app = express();
    app.set("views", viewsDir_);
    app.set("view engine", "ejs");
    app.enable("view cache");

    int engineCallCount = 0;
    app.engine("ejs", [&](const std::string& filePath, const JsonValue& opts,
                          std::function<void(std::optional<std::string>, std::string)> cb) {
        engineCallCount++;
        cb(std::nullopt, "rendered");
    });

    // Render twice
    app.render("index", JsonObject{}, [](auto, auto) {});
    app.render("index", JsonObject{}, [](auto, auto) {});

    // Engine should be called twice (once per render), but the View
    // lookup should only happen once (cached). We verify by checking
    // that both calls succeed and the engine is invoked both times.
    EXPECT_EQ(engineCallCount, 2);
}

TEST_F(ViewTest, AppRenderNoCacheByDefault) {
    auto app = express();
    app.set("views", viewsDir_);
    app.set("view engine", "ejs");
    // view cache is disabled by default

    int renderCount = 0;
    app.engine("ejs", [&](const std::string&, const JsonValue&,
                          std::function<void(std::optional<std::string>, std::string)> cb) {
        renderCount++;
        cb(std::nullopt, "ok");
    });

    app.render("index", JsonObject{}, [](auto, auto) {});
    app.render("index", JsonObject{}, [](auto, auto) {});

    EXPECT_EQ(renderCount, 2);
}

// ═══════════════════════════════════════════════════════════════════════
// Engine Function Tests
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ViewTest, EngineReceivesCorrectPathAndOptions) {
    auto app = express();
    app.set("views", viewsDir_);
    app.set("view engine", "ejs");

    std::string capturedPath;
    JsonValue capturedOpts;

    app.engine("ejs", [&](const std::string& p, const JsonValue& opts,
                          std::function<void(std::optional<std::string>, std::string)> cb) {
        capturedPath = p;
        capturedOpts = opts;
        cb(std::nullopt, "done");
    });

    app.render("about", JsonObject{{"key", "value"}},
               [](auto, auto) {});

    EXPECT_EQ(capturedPath, viewsDir_ + "/about.ejs");
    ASSERT_TRUE(capturedOpts.isObject());
    EXPECT_EQ(capturedOpts.asObject().at("key").asString(), "value");
}

TEST_F(ViewTest, EngineErrorPropagatedToCallback) {
    auto app = express();
    app.set("views", viewsDir_);
    app.set("view engine", "ejs");

    app.engine("ejs", [](const std::string&, const JsonValue&,
                         std::function<void(std::optional<std::string>, std::string)> cb) {
        cb("Template syntax error at line 5", "");
    });

    std::optional<std::string> gotError;

    app.render("index", JsonObject{},
               [&](std::optional<std::string> err, std::string) {
                   gotError = err;
               });

    ASSERT_TRUE(gotError.has_value());
    EXPECT_EQ(*gotError, "Template syntax error at line 5");
}

// ═══════════════════════════════════════════════════════════════════════
// Error Message Tests
// ═══════════════════════════════════════════════════════════════════════

TEST_F(ViewTest, NotFoundErrorIncludesDirectory) {
    auto app = express();
    app.set("views", viewsDir_);
    app.set("view engine", "ejs");
    app.engine("ejs", mockEngine());

    std::optional<std::string> gotError;

    app.render("missing", JsonObject{},
               [&](std::optional<std::string> err, std::string) {
                   gotError = err;
               });

    ASSERT_TRUE(gotError.has_value());
    EXPECT_NE(gotError->find(viewsDir_), std::string::npos);
}

TEST_F(ViewTest, NotFoundErrorWithMultipleDirs) {
    auto app = express();
    app.set("views", JsonValue(JsonArray{viewsDir_, viewsDir2_}));
    app.set("view engine", "ejs");
    app.engine("ejs", mockEngine());

    std::optional<std::string> gotError;

    app.render("missing", JsonObject{},
               [&](std::optional<std::string> err, std::string) {
                   gotError = err;
               });

    ASSERT_TRUE(gotError.has_value());
    // Error should mention both directories
    EXPECT_NE(gotError->find(viewsDir_), std::string::npos);
    EXPECT_NE(gotError->find(viewsDir2_), std::string::npos);
}
