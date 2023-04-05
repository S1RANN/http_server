#pragma once

#include "network.h"
#include <any>
#include <arpa/inet.h>
#include <functional>
#include <string>
#include <sys/socket.h>
#include <unordered_set>

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

class EventLoop {
  private:
    int epoll_fd;
    std::unordered_set<int> socket_fd;
    std::unordered_map<int, int> timer_timeouts;
    std::unordered_map<int, std::function<void()>> timer_callbacks;

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