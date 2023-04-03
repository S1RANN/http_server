#ifndef NETWORK_H_
#define NETWORK_H_

/// @file
/// @brief Network communication

#include "thread_pool.h"
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unordered_map>
#include <vector>

namespace mpmc {

class TCPStream {
  private:
    std::string ip;
    std::string client_ip;
    int port;
    int client_port;
    int socket_fd;

  public:
    TCPStream(int socket_fd, const char *ip, int port, const char *client_ip, int client_port);
    ~TCPStream();

    std::string get_addr() const;
    std::string get_client_addr() const;
    int read(char *buffer, int size);
    void write(const std::string &data);
};

class TCPListener;

class TCPStreamIterator {
  private:
    TCPListener *listener;
    TCPStream *stream;

  public:
    TCPStreamIterator(TCPListener *listener);
    TCPStreamIterator &operator++();
    TCPStream &operator*();
    bool operator==(const TCPStreamIterator &other);
    bool operator!=(const TCPStreamIterator &other);
};

class TCPListener {
  private:
    int socket_fd;
    int port;
    std::string ip;
    sockaddr_in address;

  public:
    TCPListener(const char *ip, int port);
    ~TCPListener();
    TCPStream *accept();
    TCPStreamIterator begin();
    TCPStreamIterator end();
};

auto split(const std::string &s, const std::string &delimiter) -> std::vector<std::string>;
std::string trim(const std::string &s);
std::string ltrim(const std::string &s);
std::string rtrim(const std::string &s);

class Request {
    using Headers = std::unordered_map<std::string, std::string>;

  private:
    std::string method;
    std::string path;
    std::string version;
    Headers headers;
    std::string body;

  public:
    Request(const std::string &request_str);
    Request(const std::string &method, const std::string &path, const std::string &version,
            const Headers &headers, const std::string &body);
    ~Request();
    std::string get_method() const;
    std::string get_path() const;
    std::string get_version() const;
    Headers get_headers() const;
    std::string get_body() const;
    std::string to_string() const;
};

class Response {
    using Headers = std::unordered_map<std::string, std::string>;

  private:
    std::string version;
    int status_code;
    std::string status_message;
    Headers headers;
    std::string body;

  public:
    Response();
    Response(const std::string &response_str);
    Response(const std::string &version, int status_code, const std::string &status_message,
             const Headers &headers, const std::string &body);
    ~Response();
    std::string get_version() const;
    int get_status_code() const;
    std::string get_status_message() const;
    Headers get_headers() const;
    std::string get_body() const;
    void set_version(const std::string &version);
    void set_status_code(int status_code);
    void set_status_message(const std::string &status_message);
    void set_headers(const Headers &headers);
    void set_header(const std::string &key, const std::string &value);
    void set_body(const std::string &body);
    std::string to_string() const;
};

class HTTPHandler {
  private:
    TCPStream *stream;
    static constexpr int BUFFER_SIZE = 1024;

  public:
    HTTPHandler();
    HTTPHandler(TCPStream *stream);
    ~HTTPHandler();
    HTTPHandler(const HTTPHandler &other) = delete;
    HTTPHandler &operator=(const HTTPHandler &other) = delete;
    HTTPHandler(HTTPHandler &&other);
    HTTPHandler &operator=(HTTPHandler &&other);

    void operator()(Worker<HTTPHandler> *worker);
};

} // namespace mpmc

#endif