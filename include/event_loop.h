#pragma once

#include "network.h"
#include <any>
#include <arpa/inet.h>
#include <functional>
#include <string>
#include <sys/socket.h>
#include <unordered_set>
#include <liburing.h>
#include <sys/epoll.h>

namespace mpmc {

namespace evtlp {

class SubEventLoop {
  private:
    int epoll_fd;
    std::unordered_set<int> client_fd;

  public:
    SubEventLoop();
    ~SubEventLoop();

    void add_client(int fd);
    void remove_client(int fd);
    void run();
    void stop();
};

class RingEventLoop {
  private:
    std::unordered_map<int, std::pair<sockaddr_in, socklen_t>> socket_map;
    std::unordered_map<int, char*> client_buffers;
    static constexpr int QUEUE_LENGTH = 512;
    static constexpr int BUFFER_SIZE = 1024;
    io_uring ring;

  public:
    RingEventLoop();
    ~RingEventLoop();

    void listen(const char *ip, int port);
    void prepare_accept(int socket_fd);
    void accept(int socket_fd, int client_fd);
    void prepare_read(int client_fd);
    bool read(int fd, int n);
    void write(int fd, const std::string &data);
    void run();
};

class EventLoop {
  private:
    std::unordered_set<int> socket_fd;
    std::unordered_map<int, int> timer_timeouts;
    std::unordered_map<int, std::function<void()>> timer_callbacks;
    static constexpr int EVENTS_LENGTH = 10;
    int epoll_fd;

  public:
    EventLoop();
    ~EventLoop();

    void listen(const char *ip, int port);
    void accept(int fd);
    bool read(int fd, char *buffer, int size);
    void write(int fd, const std::string &data);
    void add_timer(int timeout, std::function<void()> callback);
    void read_timer(int fd);
    void run();
    void stop();
};

} // namespace evtlp

} // namespace mpmc