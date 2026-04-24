// Serve the ./public directory under /public, with an index-html fallback.
//
//   mkdir -p public && echo '<h1>hi</h1>' > public/index.html
//   curl http://localhost:3000/public/

#include <polycpp/express/express.hpp>
#include <polycpp/event_loop.hpp>

#include <iostream>

int main() {
    auto app = polycpp::express::express();

    // One-hour cache for static assets; bump if the bundle is content-hashed.
    polycpp::express::StaticOptions opts{
        .maxAge = 60 * 60,
        .fallthrough = true,
        .index = "index.html",
    };

    app.use("/public", polycpp::express::static_("./public", opts));

    app.get("/", [](auto& /*req*/, auto& res, auto /*next*/) {
        res.redirect("/public/");
    });

    app.listen(3000, []() {
        std::cout << "Static server on http://localhost:3000/public/\n";
    });

    polycpp::event_loop::run();
}
