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
    int n = recv(socket_fd, buffer, BUFFER_SIZE, 0);
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

HTTPHandler::HTTPHandler() : stream() {}
HTTPHandler::HTTPHandler(TCPStream *stream) : stream(stream) {}
HTTPHandler::~HTTPHandler() {}

auto HTTPHandler::split(const std::string &s, const std::string &delimiter)
    -> std::vector<std::string> {
    std::vector<std::string> tokens;
    std::string::size_type begin, end;
    begin = 0;
    end = s.find(delimiter);
    while (end != std::string::npos) {
        tokens.push_back(s.substr(begin, end - begin));
        begin = end + delimiter.size();
        end = s.find(delimiter, begin);
    }
    tokens.push_back(s.substr(begin));
    return tokens;
}

std::string HTTPHandler::trim(const std::string &s) {
    auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c) { return isspace(c); });
    auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](int c) { return isspace(c); }).base();
    return (wsback <= wsfront ? std::string() : std::string(wsfront, wsback));
}

std::string HTTPHandler::ltrim(const std::string &s) {
    auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c) { return isspace(c); });
    return std::string(wsfront, s.end());
}

std::string HTTPHandler::rtrim(const std::string &s) {
    auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](int c) { return isspace(c); }).base();
    return std::string(s.begin(), wsback);
}

auto HTTPHandler::parse_http_request(const std::string &request)
    -> std::unordered_map<std::string, std::string> * {
    /// parse a http request into key-value pair
    /// return a pointer to the map
    /// the caller is responsible for deleting the map

    std::unordered_map<std::string, std::string> *map =
        new std::unordered_map<std::string, std::string>();

    auto lines = split(trim(request), "\r\n");
    auto request_line = split(lines[0], " ");

    map->insert({"method", request_line[0]});
    map->insert({"path", request_line[1]});
    map->insert({"version", request_line[2]});

    for (int i = 1; i < lines.size(); i++) {
        auto line = lines[i];
        auto key_value = split(line, ":");
        map->insert({key_value[0], ltrim(key_value[1])});
    }
    return map;
}

void HTTPHandler::operator()(Worker<HTTPHandler> *worker) {
    fmt::print("Worker {} received a connection from {}\n", worker->get_id(),
               stream->get_client_addr());

    char buffer[BUFFER_SIZE];
    int n = stream->read(buffer, BUFFER_SIZE);

    std::string request(buffer, n);
    auto map = parse_http_request(request);

    fmt::print("method: {}\n", map->at("method"));
    fmt::print("path: {}\n", map->at("path"));
    fmt::print("version: {}\n", map->at("version"));

    std::string response = "HTTP/1.1 200 OK\r\nContent-Type: "
                           "text/html\r\n\r\n<html><body><h1>Hello World</h1></body></html>";
    stream->write(response);

    delete map;
}

} // namespace mpmc