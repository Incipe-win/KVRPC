#include "kvrpc/executor.h"
#include "test_support.h"

int main() {
    Throws([&] { kvrpc::Executor executor({0, 1, 1}); });
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    std::future<int> running, queued, rejected;
    {
        kvrpc::Executor executor({1, 1, 1024});
        running = executor.Submit([&] { entered.set_value(); gate.wait(); return 1; });
        entered.get_future().get();
        queued = executor.Submit([] { return 2; });
        rejected = executor.Submit([] { return 3; });
        ErrorIs(kvrpc::ErrorCode::overloaded, [&] { rejected.get(); });
        release.set_value();
    }
    CHECK(running.get() == 1 && queued.get() == 2);
    kvrpc::Executor executor;
    auto failure = executor.Submit([]() -> int { throw std::runtime_error("worker failure"); });
    Throws([&] { failure.get(); });
    CHECK(executor.Submit([] { return 42; }).get() == 42);
}
