#include "thread_pool.h"

namespace mpmc {

Semaphore::Semaphore(int n) : count(n) {}
Semaphore::~Semaphore() {}

} // namespace mpmc