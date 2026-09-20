#include <vector>
#include <iostream>
using namespace std;

int main() {
    vector<int> v = {1,2,3,4,5};
    cout << "swap前:size=" << v.size() << ", capacity=" << v.capacity() << endl; 
    // 输出:size=5, capacity=5

    vector<int>{}.swap(v); // 空临时vector交换v的资源
    cout << "swap后:size=" << v.size() << ", capacity=" << v.capacity() << endl; 
    // 输出:size=0, capacity=0 → 内存彻底释放

    v.emplace_back((5));

    v.clear();
    v.shrink_to_fit();
    cout << "clear+shrink后:size=" << v.size() << ", capacity=" << v.capacity() << endl; 

    return 0;
}