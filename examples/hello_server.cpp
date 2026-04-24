// Smallest useful express server.
//
//   curl http://localhost:3000/        # → Hello, world!

#include <polycpp/express/express.hpp>
#include <polycpp/event_loop.hpp>
#include <iostream>

int main() {
    auto app = polycpp::express::express();

    app.get("/", [](auto& /*req*/, auto& res, auto /*next*/) {
        res.send("Hello, world!\n");
    });

    app.listen(3000, []() {
        std::cout << "Listening on http://localhost:3000\n";
    });

    polycpp::event_loop::run();
}
