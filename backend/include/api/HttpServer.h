#pragma once
// Thin network layer: maps cpp-httplib requests onto ApiService and adds CORS for the local Vite dev
// server. The server binds to the loopback address only; it is a single-user local tool.

#include <memory>
#include <string>

#include "api/ApiService.h"

namespace scx {

class HttpServer {
public:
    explicit HttpServer(ApiService& service);
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Allows browser pages served from these origins (e.g. "http://localhost:5173") to call the API.
    void allowOrigin(const std::string& origin);

    int bindToAnyPort(const std::string& host);          // returns the port, or -1 on failure
    bool bind(const std::string& host, int port);
    void serveUntilStopped();                            // blocks
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace scx
