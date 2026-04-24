// In-memory todo REST API demonstrating routing, JSON middleware,
// path parameters, and a mounted Router.
//
//   POST   /api/todos          create
//   GET    /api/todos          list
//   GET    /api/todos/:id      show
//   DELETE /api/todos/:id      remove

#include <polycpp/express/express.hpp>
#include <polycpp/event_loop.hpp>

#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>

namespace express = polycpp::express;
using polycpp::JsonValue;

int main() {
    // In-memory store. A real service would put this behind a repository.
    struct Todo {
        std::string id;
        std::string title;
        bool done = false;
    };
    std::mutex storeMu;
    std::unordered_map<std::string, Todo> store;
    int nextId = 1;

    auto app = express::express();
    app.use(express::json());

    auto api = express::Router();

    api.post("/todos", [&](auto& req, auto& res, auto /*next*/) {
        std::lock_guard<std::mutex> g(storeMu);
        auto id = std::to_string(nextId++);
        Todo t{id, req.body.get("title").value_or(std::string{"(untitled)"}), false};
        store.emplace(id, t);
        res.status(201).json(JsonValue::object({
            {"id", t.id}, {"title", t.title}, {"done", t.done},
        }));
    });

    api.get("/todos", [&](auto& /*req*/, auto& res, auto /*next*/) {
        std::lock_guard<std::mutex> g(storeMu);
        auto arr = JsonValue::array();
        for (const auto& [_, t] : store) {
            arr.push_back(JsonValue::object({
                {"id", t.id}, {"title", t.title}, {"done", t.done},
            }));
        }
        res.json(arr);
    });

    api.get("/todos/:id", [&](auto& req, auto& res, auto /*next*/) {
        std::lock_guard<std::mutex> g(storeMu);
        auto it = store.find(req.params.at("id"));
        if (it == store.end()) { res.status(404).send("not found"); return; }
        const auto& t = it->second;
        res.json(JsonValue::object({
            {"id", t.id}, {"title", t.title}, {"done", t.done},
        }));
    });

    api.del("/todos/:id", [&](auto& req, auto& res, auto /*next*/) {
        std::lock_guard<std::mutex> g(storeMu);
        auto erased = store.erase(req.params.at("id"));
        res.status(erased ? 204 : 404).end();
    });

    app.use("/api", api);

    // 404 funnel for everything unmatched.
    app.use([](auto& /*req*/, auto& res, auto /*next*/) {
        res.status(404).send("not found");
    });

    app.listen(3000, []() { std::cout << "Listening on :3000\n"; });
    polycpp::event_loop::run();
}
