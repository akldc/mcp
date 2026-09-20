#include <iostream>
#include <vector>
using namespace std;

class Buffer {
public:
    Buffer(size_t n) : data_(new int[n]), size_(n) {
        cout << "ctor\n";
    }

    Buffer(const Buffer& other) : data_(new int[other.size_]), size_(other.size_) {
        cout << "copy\n";
        for (size_t i = 0; i < size_; ++i) data_[i] = other.data_[i];
    }

    Buffer(Buffer&& other) noexcept : data_(other.data_), size_(other.size_) {
        cout << "move\n";
        other.data_ = nullptr;
        other.size_ = 0;
    }

    ~Buffer() { delete[] data_; }

private:
    int* data_ = nullptr;
    size_t size_ = 0;
};

int main() {
    vector<Buffer> v;
    v.push_back(Buffer(10));
    return 0;
}
