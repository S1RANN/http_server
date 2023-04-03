#include "network.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace mpmc {

std::string TCPStream::get_addr() const { return fmt::format("{}:{}", ip, port); }

std::string TCPStream::get_client_addr() const {
    return fmt::format("{}:{}", client_ip, client_port);
}

int TCPStream::read(char *buffer, int size) {
    int n = recv(socket_fd, buffer, size, 0);
    if (n < 0) {
        throw std::runtime_error(
            fmt::format("Failed to read from socket: {} {}:{}", std::strerror(errno), ip, port));
    }
    return n;
}

void TCPStream::write(const std::string &data) {
    int n = send(socket_fd, data.c_str(), data.size(), 0);
    if (n < 0) {
        throw std::runtime_error(
            fmt::format("Failed to write to socket: {} {}:{}", std::strerror(errno), ip, port));
    }
}

TCPStream::TCPStream(int socket_fd, const char *ip, int port, const char *client_ip,
                     int client_port)
    : socket_fd(socket_fd), ip(ip), port(port), client_ip(client_ip), client_port(client_port) {}

TCPStream::~TCPStream() { close(socket_fd); }

TCPStreamIterator::TCPStreamIterator(TCPListener *listener) : listener(listener), stream(nullptr) {}

TCPStreamIterator &TCPStreamIterator::operator++() {
    try {
        stream = listener->accept();
    } catch (std::exception &e) {
        std::cerr << e.what() << "\n";
        stream = nullptr;
    }
    return *this;
}

TCPStream &TCPStreamIterator::operator*() { return *stream; }

bool TCPStreamIterator::operator==(const TCPStreamIterator &other) {
    return stream == other.stream && listener == other.listener;
}

bool TCPStreamIterator::operator!=(const TCPStreamIterator &other) { return !(*this == other); }

TCPListener::TCPListener(const char *_ip, int _port) : ip(_ip), port(_port) {
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        throw std::runtime_error(
            fmt::format("Failed to create socket: {} {}:{}", std::strerror(errno), _ip, _port));
    }

    bzero((char *)&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(_ip);
    address.sin_port = htons(_port);

    if (bind(socket_fd, (sockaddr *)&address, sizeof(address)) < 0) {
        throw std::runtime_error(
            fmt::format("Failed to bind socket: {} {}:{}", std::strerror(errno), _ip, _port));
    }

    if (listen(socket_fd, 5) < 0) {
        throw std::runtime_error(
            fmt::format("Failed to listen on socket: {} {}:{}", std::strerror(errno), _ip, _port));
    }
}

TCPListener::~TCPListener() { close(socket_fd); }

TCPStream *TCPListener::accept() {
    sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_socket_fd = ::accept(socket_fd, (sockaddr *)&client_addr, &client_addr_len);

    if (client_socket_fd < 0) {
        throw std::runtime_error(
            fmt::format("Failed to accept connection: {} {}:{}", std::strerror(errno), ip, port));
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr.sin_port);

    return new TCPStream(client_socket_fd, ip.c_str(), port, client_ip, client_port);
}

TCPStreamIterator TCPListener::begin() { return ++TCPStreamIterator(this); }

TCPStreamIterator TCPListener::end() { return TCPStreamIterator(this); }

Request::Request(const std::string &request_str) {
    auto body_pos = request_str.find("\r\n\r\n");

    auto request_header = request_str.substr(0, body_pos);
    std::string request_body("");

    if (body_pos != std::string::npos) {
        request_body = request_str.substr(body_pos + 4);
    }

    auto lines = split(request_header, "\r\n");
    auto request_line = split(lines[0], " ");
    method = request_line[0];
    path = request_line[1];
    version = request_line[2];

    for (int i = 1; i < lines.size(); i++) {
        auto header = split(lines[i], ": ");
        headers[header[0]] = header[1];
    }

    body = request_body;
}

Request::Request(const std::string &method, const std::string &path, const std::string &version,
                 const Headers &headers, const std::string &body)
    : method(method), path(path), version(version), body(body) {}

Request::~Request() {}

std::string Request::get_method() const { return method; }
std::string Request::get_path() const { return path; }
std::string Request::get_version() const { return version; }
Request::Headers Request::get_headers() const { return headers; }
std::string Request::get_body() const { return body; }

std::string Request::to_string() const {
    std::string request_str = fmt::format("{} {} {}\r\n", method, path, version);

    for (auto &header : headers) {
        request_str += fmt::format("{}: {}\r\n", header.first, header.second);
    }

    request_str += fmt::format("\r\n{}", body);

    return request_str;
}

Response::Response() : version("HTTP/1.1"), status_code(200), status_message("OK") {}

Response::Response(const std::string &response_str) {
    auto body_pos = response_str.find("\r\n\r\n");

    auto response_header = response_str.substr(0, body_pos);
    std::string response_body("");

    if (body_pos != std::string::npos) {
        response_body = response_str.substr(body_pos + 4);
    }

    auto lines = split(response_header, "\r\n");
    auto response_line = split(lines[0], " ");
    version = response_line[0];
    status_code = std::stoi(response_line[1]);
    status_message = response_line[2];

    for (int i = 1; i < lines.size(); i++) {
        auto header = split(lines[i], ": ");
        headers[header[0]] = header[1];
    }

    body = response_body;
}

Response::Response(const std::string &version, int status_code, const std::string &status_message,
                   const Headers &headers, const std::string &body)
    : version(version), status_code(status_code), status_message(status_message), body(body) {}

Response::~Response() {}

std::string Response::get_version() const { return version; }
int Response::get_status_code() const { return status_code; }
std::string Response::get_status_message() const { return status_message; }
Response::Headers Response::get_headers() const { return headers; }
std::string Response::get_body() const { return body; }

void Response::set_version(const std::string &version) { this->version = version; }
void Response::set_status_code(int status_code) { this->status_code = status_code; }
void Response::set_status_message(const std::string &status_message) {
    this->status_message = status_message;
}
void Response::set_headers(const Headers &headers) { this->headers = headers; }
void Response::set_header(const std::string &key, const std::string &value) {
    headers[key] = value;
}
void Response::set_body(const std::string &body) { this->body = body; }

std::string Response::to_string() const {
    std::string response_str = fmt::format("{} {} {}\r\n", version, status_code, status_message);

    for (auto &header : headers) {
        response_str += fmt::format("{}: {}\r\n", header.first, header.second);
    }

    response_str += fmt::format("\r\n{}", body);

    return response_str;
}

HTTPHandler::HTTPHandler() : stream(nullptr) {}
HTTPHandler::HTTPHandler(TCPStream *stream) : stream(stream) {}
HTTPHandler::~HTTPHandler() {
    if (stream != nullptr) {
        delete stream;
    }
}
HTTPHandler::HTTPHandler(HTTPHandler &&other) : stream(other.stream) { other.stream = nullptr; }

HTTPHandler &HTTPHandler::operator=(HTTPHandler &&other) {
    stream = other.stream;
    other.stream = nullptr;
    return *this;
}

auto split(const std::string &s, const std::string &delimiter) -> std::vector<std::string> {
    std::vector<std::string> tokens;
    std::string::size_type begin, end;
    begin = 0;
    end = s.find(delimiter);
    while (end != std::string::npos) {
        if (end != begin) {
            tokens.push_back(s.substr(begin, end - begin));
        }
        begin = end + delimiter.size();
        end = s.find(delimiter, begin);
    }
    if (s.size() > begin) {
        tokens.push_back(s.substr(begin));
    }
    return tokens;
}

std::string trim(const std::string &s) {
    auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c) { return isspace(c); });
    auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](int c) { return isspace(c); }).base();
    return (wsback <= wsfront ? std::string() : std::string(wsfront, wsback));
}

std::string ltrim(const std::string &s) {
    auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c) { return isspace(c); });
    return std::string(wsfront, s.end());
}

std::string rtrim(const std::string &s) {
    auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](int c) { return isspace(c); }).base();
    return std::string(s.begin(), wsback);
}

void HTTPHandler::operator()(Worker<HTTPHandler> *worker) {
    char buffer[BUFFER_SIZE];
    int n = stream->read(buffer, BUFFER_SIZE);

    std::string request_str(buffer, n);
    Request request(request_str);

    fmt::print("Worker {} received request: \n{}\n", worker->get_id(), request.to_string());

    Response response;

    std::string response_body("<html><body><h1>Hello World</h1></body></html>");

    response.set_header("Content-Type", "text/html");
    response.set_header("Content-Length", std::to_string(response_body.size()));
    response.set_body(response_body);

    stream->write(response.to_string());
}

} // namespace mpmc