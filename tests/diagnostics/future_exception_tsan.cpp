#include <future>
#include <thread>
struct Failure {
    int code;
};
int main() {
    for (int i = 0; i < 2000; ++i) {
        std::promise<void> promise;
        auto future = promise.get_future();
        std::thread producer(
            [p = std::move(promise)]() mutable { p.set_exception(std::make_exception_ptr(Failure{42})); });
        try {
            future.get();
        } catch (const Failure& failure) {
            if (failure.code != 42) __builtin_abort();
        }
        producer.join();
    }
}
