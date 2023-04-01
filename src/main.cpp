#include "network.h"
#include <iostream>

using namespace mpmc;

int main() {

    TCPListener listener("127.0.0.1", 8080);
    ThreadPool<HTTPHandler> pool(3);

    for (auto &stream : listener) {
        fmt::print("Server received a connection from {}\n", stream.get_client_addr());
        pool.submit(HTTPHandler(&stream));
    }

    return 0;
}