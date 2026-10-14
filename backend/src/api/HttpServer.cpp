#include "api/HttpServer.h"

#include <algorithm>
#include <vector>

#include "httplib/httplib.h"

namespace scx {

struct HttpServer::Impl {
    explicit Impl(ApiService& service) : service(service) {}
    ApiService& service;
    httplib::Server server;
    std::vector<std::string> allowedOrigins;
};

HttpServer::HttpServer(ApiService& service) : impl_(std::make_unique<Impl>(service)) {
    Impl& impl = *impl_;
    impl.server.set_payload_max_length(5 * 1024 * 1024);   // scenarios are small; refuse anything huge

    auto addCors = [&impl](const httplib::Request& request, httplib::Response& response) {
        const std::string origin = request.get_header_value("Origin");
        if (!origin.empty() && std::find(impl.allowedOrigins.begin(), impl.allowedOrigins.end(), origin) != impl.allowedOrigins.end()) {
            response.set_header("Access-Control-Allow-Origin", origin);
            response.set_header("Vary", "Origin");
            response.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            response.set_header("Access-Control-Allow-Headers", "Content-Type");
        }
    };

    auto forward = [&impl, addCors](const httplib::Request& request, httplib::Response& response) {
        ApiRequest apiRequest;
        apiRequest.method = request.method;
        apiRequest.path = request.path;
        apiRequest.body = request.body;
        for (const auto& [key, value] : request.params) apiRequest.query[key] = value;
        const ApiResponse apiResponse = impl.service.handle(apiRequest);
        response.status = apiResponse.status;
        response.set_content(apiResponse.body, apiResponse.contentType);
        addCors(request, response);
    };

    impl.server.Get(".*", forward);
    impl.server.Post(".*", forward);
    impl.server.Options(".*", [addCors](const httplib::Request& request, httplib::Response& response) {
        response.status = 204;
        addCors(request, response);
    });
    impl.server.set_exception_handler([addCors](const httplib::Request& request, httplib::Response& response, std::exception_ptr) {
        response.status = 500;
        response.set_content(R"({"error":{"code":"internal_error","message":"unexpected server error"}})", "application/json");
        addCors(request, response);
    });
}

HttpServer::~HttpServer() = default;

void HttpServer::allowOrigin(const std::string& origin) { impl_->allowedOrigins.push_back(origin); }
int HttpServer::bindToAnyPort(const std::string& host) { return impl_->server.bind_to_any_port(host); }
bool HttpServer::bind(const std::string& host, int port) { return impl_->server.bind_to_port(host, port); }
void HttpServer::serveUntilStopped() { impl_->server.listen_after_bind(); }
void HttpServer::stop() { impl_->server.stop(); }

}  // namespace scx
