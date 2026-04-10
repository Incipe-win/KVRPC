#include "kvrpc/serializer.h"
#include <iostream>
#include <cassert>

int main() {
    // Test Serialization
    kvrpc::Serializer sz;
    sz.Serialize(42, std::string("hello"), 3.14159, true);

    // Initial Verification
    std::cout << "Serialized buffer size: " << sz.GetBuffer().size() << " bytes\n";

    // Test Deserialization
    int a;
    std::string b;
    double c;
    bool d;

    kvrpc::Serializer r_sz(sz.GetBuffer());
    r_sz.Deserialize(a, b, c, d);

    // Assertions mapping back to original variables
    assert(a == 42);
    assert(b == "hello");
    // Double precision needs careful comparison, but direct works here 
    assert(c == 3.14159);
    assert(d == true);

    std::cout << "All serializer tests passed!" << std::endl;
    return 0;
}
