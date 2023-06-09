#include "event_loop.h"
#include "network.h"
#include <stdexcept>
#include <sys/timerfd.h>
#include <unistd.h>

namespace mpmc {

namespace evtlp {

constexpr const char *LOREM =
    "But I must explain to you how all this mistaken idea of denouncing pleasure and praising pain "
    "was born and I will give you a complete account of the system, and expound the actual "
    "teachings of the great explorer of the truth, the master-builder of human happiness. No one "
    "rejects, dislikes, or avoids pleasure itself, because it is pleasure, but because those who "
    "do not know how to pursue pleasure rationally encounter consequences that are extremely "
    "painful. Nor again is there anyone who loves or pursues or desires to obtain pain of itself, "
    "because it is pain, but because occasionally circumstances occur in which toil and pain can "
    "procure him some great pleasure. To take a trivial example, which of us ever undertakes "
    "laborious physical exercise, except to obtain some advantage from it? But who has any right "
    "to find fault with a man who chooses to enjoy a pleasure that has no annoying consequences, "
    "or one who avoids a pain that produces no resultant pleasure?";

RingEventLoop::RingEventLoop() {
    if (io_uring_queue_init(QUEUE_LENGTH, &ring, 0) < 0) {
        throw std::runtime_error(
            fmt::format("Failed to init io_uring, errors: {}", strerror(errno)));
    }
}

RingEventLoop::~RingEventLoop() { io_uring_queue_exit(&ring); }

void RingEventLoop::listen(const char *ip, int port) {
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
    socket_map.insert({
        fd, {addr, sizeof(addr)}
    });
}

void RingEventLoop::prepare_accept(int socket_fd) {
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_accept(sqe, socket_fd, reinterpret_cast<sockaddr *>(&socket_map[socket_fd].first),
                         &socket_map[socket_fd].second, 0);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void *>(socket_fd));
}

void RingEventLoop::accept(int socket_fd, int client_fd) {
    if (client_fd == -1) {
        throw std::runtime_error(
            fmt::format("Failed to accept client, error: {}", strerror(errno)));
    }
    client_buffers.insert({client_fd, new char[BUFFER_SIZE]});
    prepare_read(client_fd);
    prepare_accept(socket_fd);
}

void RingEventLoop::prepare_read(int client_fd) {
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_read(sqe, client_fd, client_buffers[client_fd], BUFFER_SIZE, 0);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void *>(client_fd));
}

bool RingEventLoop::read(int client_fd, int n) {
    if (n < 0) {
        delete[] client_buffers[client_fd];
        client_buffers.erase(client_fd);
        close(client_fd);
        return false;
        // throw std::runtime_error(
        // fmt::format("Failed to read from socket, error: {}", strerror(errno)));
    }
    if (n == 0) {
        delete[] client_buffers[client_fd];
        client_buffers.erase(client_fd);
        close(client_fd);
        return false;
    }
    prepare_read(client_fd);
    return true;
}

void RingEventLoop::write(int fd, const std::string &data) {
    int n = ::write(fd, data.c_str(), data.size());
    if (n == -1) {
        throw std::runtime_error(
            fmt::format("Failed to write to socket, error: {}", strerror(errno)));
    }
}

void RingEventLoop::run() {
    for (auto &socket : socket_map) {
        prepare_accept(socket.first);
    }
    io_uring_submit(&ring);
    while (true) {
        io_uring_cqe *cqe;
        if (io_uring_wait_cqe(&ring, &cqe) < 0) {
            throw std::runtime_error(
                fmt::format("Failed to wait for cqe, error: {}", strerror(errno)));
        }
        long long fd = reinterpret_cast<long long>(io_uring_cqe_get_data(cqe));
        if (socket_map.find(fd) != socket_map.end()) {
            accept(cqe->user_data, cqe->res);
        } else {
            int n = cqe->res;
            if (read(fd, cqe->res)) {
                std::string request_str(client_buffers[fd], n);
                Request request(request_str);
                Response response;

                std::string response_body(fmt::format(
                    "<html><body><h1>{} {}</h1><p>{}</p><p>{}</p></body></html>",
                    request.get_method(), request.get_path(), request.get_body(), LOREM));

                response.set_header("Content-Type", "text/html");
                response.set_header("Content-Length", std::to_string(response_body.size()));
                response.set_body(response_body);

                write(cqe->user_data, response.to_string());
            }
        }
        io_uring_cqe_seen(&ring, cqe);
        io_uring_submit(&ring);
    }
}

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
    epoll_event events[EVENTS_LENGTH];
    while (true) {
        int n = epoll_wait(epoll_fd, events, EVENTS_LENGTH, -1);
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

                    std::string response_body(fmt::format(
                        "<html><body><h1>{} {}</h1><p>{}</p><p>{}</p></body></html>",
                        request.get_method(), request.get_path(), request.get_body(), LOREM));

                    response.set_header("Content-Type", "text/html");
                    response.set_header("Content-Length", std::to_string(response_body.size()));
                    response.set_body(response_body);

                    write(event.data.fd, response.to_string());
                }
            }
        }
    }
}

} // namespace evtlp

} // namespace mpmc