#include <thread>

// Positive control: TSan must still reject a real, unsynchronized data race.
int main() {
    int value = 0;
    std::thread first([&] { value = 1; });
    std::thread second([&] { value = 2; });
    first.join();
    second.join();
    return value == 0;
}
