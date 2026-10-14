// SupplyChainX API server.   Usage: scx_server [--port 8080] [--data-dir <path>]
// Listens on 127.0.0.1 only. Linked with AllocationHooks so each run reports real peak heap use.
#include <cstdlib>
#include <iostream>
#include <string>

#include "api/HttpServer.h"

int main(int argc, char** argv) {
    int port = 8080;
    std::string dataDirectory = "data";
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (argument == "--data-dir" && i + 1 < argc) dataDirectory = argv[++i];
        else { std::cerr << "usage: scx_server [--port N] [--data-dir PATH]\n"; return 2; }
    }
    if (port < 1 || port > 65535) { std::cerr << "port must be between 1 and 65535\n"; return 2; }

    scx::ApiService service(dataDirectory);
    scx::HttpServer server(service);
    server.allowOrigin("http://localhost:5173");
    server.allowOrigin("http://127.0.0.1:5173");
    if (!server.bind("127.0.0.1", port)) {
        std::cerr << "cannot listen on 127.0.0.1:" << port << " (is it already in use?)\n";
        return 1;
    }
    std::cout << "SupplyChainX API listening on http://127.0.0.1:" << port << "  (data: " << dataDirectory << ")\n" << std::flush;
    server.serveUntilStopped();
    return 0;
}
