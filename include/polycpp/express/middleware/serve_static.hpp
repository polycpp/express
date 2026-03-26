#pragma once

/**
 * @file middleware/serve_static.hpp
 * @brief Static file serving middleware.
 *
 * C++ port of npm serve-static. Serves static files from a directory.
 *
 * @see https://www.npmjs.com/package/serve-static
 * @since 0.1.0
 */

#include <string>

#include <polycpp/buffer.hpp>
#include <polycpp/fs.hpp>
#include <polycpp/mime/mime.hpp>
#include <polycpp/path.hpp>

#include <polycpp/express/types.hpp>
#include <polycpp/express/http_error.hpp>
#include <polycpp/express/detail/utils.hpp>

namespace polycpp {
namespace express {

/**
 * @brief Create a static file serving middleware.
 *
 * Serves files from the given root directory.
 *
 * @param root The root directory to serve files from.
 * @param opts Static serving options.
 * @return A middleware handler.
 *
 * @par Example
 * @code{.cpp}
 *   app.use(express::static_("public"));
 *   // Files in ./public/ are now served
 * @endcode
 *
 * @since 0.1.0
 */
inline MiddlewareHandler serveStatic(const std::string& root,
                                      const StaticOptions& opts = {}) {
    auto resolvedRoot = path::resolve(root);

    return [resolvedRoot, opts](Request& req, Response& res, NextFunction next) {
        // Only handle GET and HEAD requests
        auto method = req.method();
        if (method != "GET" && method != "HEAD") {
            next(std::nullopt);
            return;
        }

        auto reqPath = req.path();

        // Security: prevent directory traversal
        if (reqPath.find("..") != std::string::npos) {
            if (opts.fallthrough) {
                next(std::nullopt);
            } else {
                next(HttpError::forbidden("Forbidden"));
            }
            return;
        }

        // Build the file path
        auto filePath = path::join(resolvedRoot, reqPath);

        try {
            // Check if the file exists
            auto stats = fs::statSync(filePath);

            if (stats.isDirectory()) {
                // Try index file
                if (!opts.index.empty()) {
                    auto indexPath = path::join(filePath, opts.index);
                    try {
                        stats = fs::statSync(indexPath);
                        filePath = indexPath;
                    } catch (...) {
                        // No index file
                        if (opts.redirect && reqPath.back() != '/') {
                            // Redirect to trailing slash
                            res.redirect(301, reqPath + "/");
                            return;
                        }
                        if (opts.fallthrough) {
                            next(std::nullopt);
                        } else {
                            next(HttpError::notFound());
                        }
                        return;
                    }
                } else {
                    if (opts.fallthrough) {
                        next(std::nullopt);
                    } else {
                        next(HttpError::notFound());
                    }
                    return;
                }
            }

            // Check dotfile
            auto basename = path::basename(filePath);
            if (!basename.empty() && basename[0] == '.') {
                if (opts.dotfiles == "deny") {
                    res.status(403);
                    res.raw().setHeader("Content-Type", "text/plain; charset=utf-8");
                    res.raw().end("Forbidden");
                    return;
                } else if (opts.dotfiles == "ignore") {
                    if (opts.fallthrough) {
                        next(std::nullopt);
                    } else {
                        next(HttpError::notFound());
                    }
                    return;
                }
                // "allow" falls through to serve the file
            }

            auto fileSize = stats.size;
            auto mtimeMs = stats.mtimeMs;

            // Set Content-Type
            auto ext = path::extname(filePath);
            if (!ext.empty()) {
                auto ct = mime::contentType(ext);
                if (ct) {
                    res.raw().setHeader("Content-Type", *ct);
                }
            }

            // Set custom headers
            for (const auto& [key, val] : opts.headers) {
                res.raw().setHeader(key, val);
            }

            // Set cache headers
            if (opts.maxAge) {
                auto maxAgeSec = std::chrono::duration_cast<std::chrono::seconds>(*opts.maxAge).count();
                res.raw().setHeader("Cache-Control", "public, max-age=" + std::to_string(maxAgeSec));
            }

            // Set Last-Modified
            if (opts.lastModified) {
                res.raw().setHeader("Last-Modified", detail::httpDate(mtimeMs));
            }

            // Set ETag from stats (size + mtime)
            if (opts.etag) {
                res.raw().setHeader("ETag", detail::statEtag(fileSize, mtimeMs));
            }

            // Set Accept-Ranges
            res.raw().setHeader("Accept-Ranges", "bytes");

            // Check freshness
            if (req.fresh()) {
                res.raw().status(304);
                res.raw().end();
                return;
            }

            // Handle Range requests
            if (fileSize > 0) {
                auto rangeHeader = req.get("range");
                if (rangeHeader) {
                    auto ranges = detail::parseRange(fileSize, *rangeHeader);
                    if (!ranges.empty()) {
                        auto& range = ranges[0];
                        auto contentLength = range.end - range.start + 1;
                        res.raw().status(206);
                        res.raw().setHeader("Content-Range",
                            "bytes " + std::to_string(range.start) + "-" +
                            std::to_string(range.end) + "/" + std::to_string(fileSize));
                        res.raw().setHeader("Content-Length",
                            static_cast<int>(contentLength));

                        if (method == "HEAD") {
                            res.raw().end();
                        } else {
                            int fd = fs::openSync(filePath, "r");
                            auto buf = Buffer::alloc(contentLength);
                            fs::readSync(fd, buf, 0,
                                         static_cast<ssize_t>(contentLength),
                                         static_cast<ssize_t>(range.start));
                            fs::closeSync(fd);
                            res.raw().end(buf.toString("latin1"));
                        }
                        return;
                    } else {
                        // Invalid or unsatisfiable range -> 416
                        res.raw().status(416);
                        res.raw().setHeader("Content-Range",
                            "bytes */" + std::to_string(fileSize));
                        res.raw().setHeader("Content-Type", "text/plain; charset=utf-8");
                        res.raw().end("Range Not Satisfiable");
                        return;
                    }
                }
            }

            // Full file: 200 OK
            res.raw().setHeader("Content-Length", static_cast<int>(fileSize));
            if (method == "HEAD") {
                res.raw().end();
            } else {
                auto contentStr = fs::readFileSync(filePath);
                res.raw().end(contentStr);
            }
        } catch (...) {
            // File not found or read error
            if (opts.fallthrough) {
                next(std::nullopt);
            } else {
                next(HttpError::notFound());
            }
        }
    };
}

} // namespace express
} // namespace polycpp
