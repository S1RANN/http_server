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
    static constexpr int BUFFER_SIZE = 1024;
    int socket_fd;
    char buffer[BUFFER_SIZE];

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

class HTTPHandler {
  private:
    std::shared_ptr<TCPStream> stream;
    static constexpr int BUFFER_SIZE = 1024;

  public:
    HTTPHandler();
    HTTPHandler(TCPStream *stream);
    ~HTTPHandler();

    static auto split(const std::string &s, const std::string &delimiter)
        -> std::vector<std::string>;
    static std::string trim(const std::string &s);
    static std::string ltrim(const std::string &s);
    static std::string rtrim(const std::string &s);
    static auto parse_http_request(const std::string &request)
        -> std::unordered_map<std::string, std::string> *;
    void operator()(Worker<HTTPHandler> *worker);
};

} // namespace mpmc

#endif