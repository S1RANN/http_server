#include "event_loop.h"
#include "network.h"
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace mpmc {

namespace evtlp {

EventLoop::EventLoop() {
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        throw std::runtime_error(fmt::format("Failed to create epoll, error: {}", strerror(errno)));
    }
}

EventLoop::~EventLoop() { close(epoll_fd); }

void EventLoop::listen(const char *ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        throw std::runtime_error(
            fmt::format("Failed to create socket, error: {}", strerror(errno)));
    }
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    if (bind(fd, (sockaddr *)&addr, sizeof(addr)) == -1) {
        throw std::runtime_error(fmt::format("Failed to bind socket, error: {}", strerror(errno)));
    }
    if (::listen(fd, 10) == -1) {
        throw std::runtime_error(
            fmt::format("Failed to listen on socket, error: {}", strerror(errno)));
    }
    socket_fd.insert(fd);
    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
        throw std::runtime_error(
            fmt::format("Failed to add socket to epoll, error: {}", strerror(errno)));
    }
}

void EventLoop::accept(int fd) {
    sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int client_fd = ::accept(fd, (sockaddr *)&addr, &addr_len);
    if (client_fd == -1) {
        throw std::runtime_error(
            fmt::format("Failed to accept connection, error: {}", strerror(errno)));
    }
    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = client_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
        throw std::runtime_error(
            fmt::format("Failed to add socket to epoll, error: {}", strerror(errno)));
    }
}

void EventLoop::add_timer(int timeout, std::function<void()> callback) {
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timer_fd == -1) {
        throw std::runtime_error(fmt::format("Failed to create timer, error: {}", strerror(errno)));
    }
    itimerspec timer;
    timer.it_value.tv_sec = timeout / 1000;
    timer.it_value.tv_nsec = (timeout % 1000) * 1000000;
    timer.it_interval.tv_sec = timeout / 1000;
    timer.it_interval.tv_nsec = (timeout % 1000) * 1000000;
    if (timerfd_settime(timer_fd, 0, &timer, NULL) == -1) {
        throw std::runtime_error(fmt::format("Failed to set timer, error: {}", strerror(errno)));
    }
    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = timer_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_fd, &event) == -1) {
        throw std::runtime_error(
            fmt::format("Failed to add timer to epoll, error: {}", strerror(errno)));
    }
    timer_timeouts[timer_fd] = timeout;
    timer_callbacks[timer_fd] = callback;
}

void EventLoop::read_timer(int fd) {
    char buffer[8];
    int n = ::read(fd, buffer, 8);
    if (n == -1) {
        throw std::runtime_error(
            fmt::format("Failed to read from timer, error: {}", strerror(errno)));
    }
}

bool EventLoop::read(int fd, char *buffer, int size) {
    int n = ::read(fd, buffer, size);
    if (n == -1) {
        // throw std::runtime_error(
        // fmt::format("Failed to read from socket, error: {}", strerror(errno)));
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return false;
    } else if (n == 0) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return false;
    } else {
        buffer[n] = '\0';
    }
    return true;
}

void EventLoop::write(int fd, const std::string &data) {
    int n = ::write(fd, data.c_str(), data.size());
    if (n == -1) {
        throw std::runtime_error(
            fmt::format("Failed to write to socket, error: {}", strerror(errno)));
    }
}

void EventLoop::run() {
    epoll_event events[10];
    while (true) {
        int n = epoll_wait(epoll_fd, events, 10, -1);
        if (n == -1) {
            throw std::runtime_error(
                fmt::format("Failed to wait on epoll, error: {}", strerror(errno)));
        }
        for (int i = 0; i < n; ++i) {
            epoll_event event = events[i];
            if (socket_fd.find(event.data.fd) != socket_fd.end()) {
                accept(event.data.fd);
            } else if (timer_callbacks.find(event.data.fd) != timer_callbacks.end()) {
                timer_callbacks[event.data.fd]();
                read_timer(event.data.fd);
            } else {
                char buffer[1024];
                if (read(event.data.fd, buffer, 1024)) {

                    Request request(buffer);
                    Response response;

                    std::string response_body(
                        fmt::format("<html><body><h1>{} {}</h1><p>{}</p></body></html>",
                                    request.get_method(), request.get_path(), request.get_body()));

                    response.set_header("Content-Type", "text/html");
                    response.set_header("Content-Length", std::to_string(response_body.size()));
                    response.set_body(response_body);

                    write(event.data.fd, response.to_string());
                }
            }
        }
    }
}

} // namespace asc

} // namespace mpmc