#include "event_loop.h"
#include "network.h"
#include <iostream>

using namespace mpmc;
using namespace evtlp;

void multithreaded_test() {
    TCPListener listener("127.0.0.1", 8080);
    ThreadPool<HTTPHandler> pool(6);

    for (auto &stream : listener) {
        fmt::print("Server received a connection from {}\n", stream.get_client_addr());
        pool.submit(HTTPHandler(&stream));
    }
}

void event_loop_test() {
    EventLoop loop;
    loop.listen("0.0.0.0", 8080);
    loop.run();
}

void timer_test(){
    EventLoop loop;
    loop.add_timer(1000, []{
        fmt::print("Timer1 fired!\n");
    });
    loop.add_timer(2000, []{
        fmt::print("Timer2 fired!\n");
    });
    loop.run();
}

int main() {
    // timer_test();
    event_loop_test();
    return 0;
}
