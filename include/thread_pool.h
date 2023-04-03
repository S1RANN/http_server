#ifndef THREAD_POOL_H_
#define THREAD_POOL_H_

#include <condition_variable>
#include <fmt/format.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace mpmc {
class Semaphore {
  private:
    std::mutex mutex;
    std::condition_variable cv;
    int count;

  public:
    Semaphore(int n);
    ~Semaphore();

    template <typename Callback> void wait(Callback callback);
    template <typename Callback> void wait_n(int n, Callback callback);
    template <typename Callback> void signal(Callback callback);
    template <typename Callback> void signal_n(int n, Callback callback);
};

template <typename T> class Channel;

template <typename T> class Sender {
  private:
    std::shared_ptr<Channel<T>> channel;

  public:
    Sender(std::shared_ptr<Channel<T>> channel);
    ~Sender();

    void close();
    bool send(T &&data);
};

template <typename T> class Receiver {
  private:
    std::shared_ptr<Channel<T>> channel;

  public:
    Receiver(std::shared_ptr<Channel<T>> channel);
    ~Receiver();

    bool receive(T &data);
};

template <typename T> class Channel {
  private:
    std::queue<T> channel;
    std::mutex mutex;
    std::condition_variable empty_cond;
    int empty_count;
    std::condition_variable full_cond;
    int full_count;
    bool closed = false;

  public:
    Channel();
    Channel(int capacity);
    ~Channel();

    void close();
    bool push(T &&data);
    bool pop(T &data);
    static void create(Sender<T> **, Receiver<T> **);
};

template <typename Callback> void Semaphore::wait(Callback callback) {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [this] { return count > 0; });
    --count;
    callback();
}

template <typename Callback> void Semaphore::wait_n(int n, Callback callback) {
    if (n <= 0) {
        throw std::runtime_error("n must be greater than 0");
    }

    std::unique_lock<std::mutex> lock(mutex);
    while (true) {
        cv.wait(lock, [this] { return count > 0; });
        if (count >= n) {
            count -= n;
            callback();
            return;
        } else {
            n -= count;
            count = 0;
        }
    }
}

template <typename Callback> void Semaphore::signal(Callback callback) {
    std::unique_lock<std::mutex> lock(mutex);
    ++count;
    callback();
    cv.notify_all();
}

template <typename Callback> void Semaphore::signal_n(int n, Callback callback) {
    if (n <= 0) {
        throw std::runtime_error("n must be greater than 0");
    }

    std::unique_lock<std::mutex> lock(mutex);
    count += n;
    callback();
    cv.notify_all();
}

template <typename T> void Channel<T>::create(Sender<T> **sender, Receiver<T> **receiver) {
    auto channel_ptr = std::make_shared<Channel<T>>();
    *sender = new Sender<T>(channel_ptr);
    *receiver = new Receiver<T>(channel_ptr);
}

template <typename T> Channel<T>::Channel() : empty_count(10), full_count(0) {}

template <typename T> Channel<T>::Channel(int capacity) : empty_count(capacity), full_count(0) {}

template <typename T> Channel<T>::~Channel() {}

template <typename T> bool Channel<T>::push(T &&data) {
    std::unique_lock<std::mutex> lock(mutex);
    empty_cond.wait(lock, [this] { return empty_count > 0 || closed; });
    if (closed) {
        empty_cond.notify_all();
        full_cond.notify_all();
        return false;
    }
    --empty_count;
    channel.push(std::move(data));
    ++full_count;
    full_cond.notify_all();
    return true;
}

template <typename T> bool Channel<T>::pop(T &data) {
    std::unique_lock<std::mutex> lock(mutex);
    full_cond.wait(lock, [this] { return full_count > 0 || closed; });
    if (closed) {
        empty_cond.notify_all();
        full_cond.notify_all();
        return false;
    }
    --full_count;
    data = std::move(channel.front());
    channel.pop();
    ++empty_count;
    empty_cond.notify_all();
    return true;
}

template <typename T> void Channel<T>::close() {
    std::unique_lock<std::mutex> lock(mutex);
    closed = true;
    empty_cond.notify_all();
    full_cond.notify_all();
}

template <typename T> Sender<T>::Sender(std::shared_ptr<Channel<T>> channel) : channel(channel) {}

template <typename T> Sender<T>::~Sender() {}

template <typename T> bool Sender<T>::send(T &&data) {
    return channel != nullptr && channel->push(std::move(data));
}

template <typename T> void Sender<T>::close() {
    if (channel != nullptr) {
        channel->close();
    }
}

template <typename T>
Receiver<T>::Receiver(std::shared_ptr<Channel<T>> channel) : channel(channel) {}

template <typename T> Receiver<T>::~Receiver() {}

template <typename T> bool Receiver<T>::receive(T &data) {
    return channel != nullptr && channel->pop(data);
}

template <typename Job> class Worker {
  private:
    int id;
    std::thread thread;
    Receiver<Job> *receiver;

  public:
    Worker(int id, Receiver<Job> *receiver);
    ~Worker();

    int get_id() const;
    void join();
};

template <typename Job> class ThreadPool {
  private:
    std::vector<Worker<Job> *> workers;
    Sender<Job> *sender;

  public:
    ThreadPool(int num_workers);
    ~ThreadPool();

    void submit(Job &&job);
};

template <typename Job> ThreadPool<Job>::ThreadPool(int num_workers) {
    Receiver<Job> *receiver;
    Channel<Job>::create(&sender, &receiver);
    for (int i = 0; i < num_workers; ++i) {
        auto worker = new Worker(i, new Receiver<Job>(*receiver));
        workers.push_back(worker);
    }

    delete receiver;
}

template <typename Job> ThreadPool<Job>::~ThreadPool() {
    sender->close();
    for (auto worker : workers) {
        worker->join();
        fmt::print("Worker {} joined", worker->get_id());
        delete worker;
    }
}

template <typename Job> void ThreadPool<Job>::submit(Job &&job) { sender->send(std::move(job)); }

template <typename Job>
Worker<Job>::Worker(int id, Receiver<Job> *receiver) : id(id), receiver(receiver) {
    thread = std::thread([this] {
        Job job;
        while (this->receiver->receive(job)) {
            job(this);
        }
    });
}
template <typename Job> Worker<Job>::~Worker() { delete receiver; }

template <typename Job> int Worker<Job>::get_id() const { return id; }
template <typename Job> void Worker<Job>::join() { thread.join(); }

} // namespace mpmc

#endif