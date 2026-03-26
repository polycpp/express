#pragma once

/**
 * @file view.hpp
 * @brief View class -- template path resolution and engine invocation.
 *
 * Mirrors Express.js view.js: resolves template files by searching root
 * directories, applying extensions, and falling back to index files.
 *
 * @see https://github.com/expressjs/express/blob/master/lib/view.js
 * @since 0.1.0
 */

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <polycpp/core/json.hpp>
#include <polycpp/fs.hpp>
#include <polycpp/path.hpp>

#include <polycpp/express/types.hpp>

namespace polycpp {
namespace express {

/**
 * @brief View handles template path resolution and engine invocation.
 *
 * Given a view name, a set of registered engines, a default engine extension,
 * and one or more root directories, the View class resolves the template file
 * path and renders it using the matching engine function.
 *
 * Resolution algorithm (mirrors Express.js view.js):
 * 1. Extract extension from name, or use defaultEngine.
 * 2. Look up the engine from the engines map using the extension.
 * 3. For each root directory:
 *    a. Try `<root>/<name>.<ext>` (if name has no extension).
 *    b. Try `<root>/<name>/index.<ext>` (directory index fallback).
 * 4. Return the first path that exists as a regular file.
 *
 * @par Example
 * @code{.cpp}
 *   std::map<std::string, EngineFunction> engines;
 *   engines[".ejs"] = myEjsEngine;
 *   View view("index", engines, "ejs", {"/app/views"});
 *   if (view.lookup()) {
 *       view.render(options, [](auto err, auto html) { ... });
 *   }
 * @endcode
 *
 * @see https://expressjs.com/en/api.html#app.render
 * @since 0.1.0
 */
class View {
public:
    /**
     * @brief Construct a View for a given template name.
     *
     * Extracts the extension, looks up the engine, and resolves the
     * template file path across all root directories.
     *
     * @param name The view name (e.g., "index", "users/profile", "admin.pug").
     * @param engines Map of extension to engine function (e.g., {".ejs": fn}).
     * @param defaultEngine The default engine extension (e.g., "ejs").
     * @param roots List of root directories to search for templates.
     *
     * @throws std::runtime_error If no extension and no default engine.
     * @since 0.1.0
     */
    inline View(const std::string& name,
                const std::map<std::string, EngineFunction>& engines,
                const std::string& defaultEngine,
                const std::vector<std::string>& roots);

    /**
     * @brief Check whether the view file was found.
     *
     * @return true if the view was resolved to an existing file.
     * @since 0.1.0
     */
    bool lookup() const { return !path_.empty(); }

    /**
     * @brief Get the resolved absolute file path.
     *
     * @return The resolved path, or empty string if not found.
     * @since 0.1.0
     */
    const std::string& filePath() const { return path_; }

    /**
     * @brief Get the view name.
     *
     * @return The view name as provided to the constructor.
     * @since 0.1.0
     */
    const std::string& name() const { return name_; }

    /**
     * @brief Get the file extension (with leading dot).
     *
     * @return The extension (e.g., ".ejs").
     * @since 0.1.0
     */
    const std::string& ext() const { return ext_; }

    /**
     * @brief Render the view with the given options.
     *
     * Invokes the registered engine function with the resolved path
     * and template variables.
     *
     * @param options Template variables as a JsonValue object.
     * @param callback Called with (error, rendered_string).
     *
     * @since 0.1.0
     */
    inline void render(const JsonValue& options,
                       std::function<void(std::optional<std::string>, std::string)> callback);

private:
    std::string name_;
    std::string ext_;
    std::string path_;
    EngineFunction engine_;
    std::vector<std::string> roots_;

    /**
     * @brief Search all root directories for the named file.
     *
     * @param fileName The filename (with extension) to look for.
     * @return The resolved path if found, empty string otherwise.
     */
    inline std::string lookupFile(const std::string& fileName);

    /**
     * @brief Try to resolve a file within a directory.
     *
     * Checks for the file directly, then tries an index fallback:
     * `<dir>/<file>` then `<dir>/<basename>/index.<ext>`.
     *
     * @param dir The directory to search in.
     * @param file The filename to look for.
     * @return The resolved path if found, empty string otherwise.
     */
    inline std::string resolve(const std::string& dir, const std::string& file);
};

// ── Inline Implementations ──────────────────────────────────────────

inline View::View(const std::string& name,
                  const std::map<std::string, EngineFunction>& engines,
                  const std::string& defaultEngine,
                  const std::vector<std::string>& roots)
    : name_(name), roots_(roots)
{
    // Extract extension from the view name
    ext_ = path::extname(name);

    if (ext_.empty() && defaultEngine.empty()) {
        throw std::runtime_error(
            "No default engine was specified and no extension was provided.");
    }

    std::string fileName = name;

    if (ext_.empty()) {
        // Get extension from default engine name
        ext_ = (defaultEngine[0] != '.') ? ("." + defaultEngine) : defaultEngine;
        fileName += ext_;
    }

    // Look up the engine for this extension
    auto it = engines.find(ext_);
    if (it != engines.end()) {
        engine_ = it->second;
    }
    // Note: unlike Express.js, we do not auto-require modules.
    // The engine must be registered via app.engine() before use.

    // Perform initial lookup
    path_ = lookupFile(fileName);
}

inline std::string View::lookupFile(const std::string& fileName) {
    for (const auto& root : roots_) {
        // Resolve the path relative to this root
        auto loc = path::resolve(root, fileName);
        auto dir = path::dirname(loc);
        auto file = path::basename(loc);

        // Try to resolve in this directory
        auto result = resolve(dir, file);
        if (!result.empty()) {
            return result;
        }
    }
    return "";
}

inline void View::render(
    const JsonValue& options,
    std::function<void(std::optional<std::string>, std::string)> callback)
{
    if (!engine_) {
        if (callback) {
            callback("No engine registered for extension \"" + ext_ + "\"", "");
        }
        return;
    }

    engine_(path_, options, std::move(callback));
}

inline std::string View::resolve(const std::string& dir, const std::string& file) {
    // Try <dir>/<file> directly
    auto filePath = path::join(dir, file);
    auto stat = fs::statSync(filePath, false);
    if (stat && stat->isFile()) {
        return filePath;
    }

    // Try <dir>/<basename-without-ext>/index.<ext>
    auto base = path::basename(file, ext_);
    auto indexPath = path::join(dir, base, "index" + ext_);
    stat = fs::statSync(indexPath, false);
    if (stat && stat->isFile()) {
        return indexPath;
    }

    return "";
}

} // namespace express
} // namespace polycpp
