#include<iostream>
#include<memory>
#include<vector>
#include<list>
using namespace std;

int main()
{
    int* a =new int(10);
    shared_ptr<int> p1 = shared_ptr<int>(a);
    shared_ptr<int> p2 = p1;

    cout<<p1.use_count()<<endl;
    cout<<p2.use_count()<<endl;

    vector<int> b(5,99);
    try{
        cout<<b[100]<<endl;
        cout<<b.at(100)<<endl;
    }
    catch(const out_of_range& e)
    {
        cout<<"ERROR: out of range"<<endl;
    }
    catch(const exception& e)
    {
        cout<<"ERROR: exception"<<endl;
    }

    list<int> c = {2,4,6,8};
    auto it  = c.begin();
    advance(it,3);
    cout<<*it<<endl;

    auto rit = c.rbegin();
    cout<<*rit<<endl;
    advance(rit,2);
    cout<<*rit<<endl;




    return 0;
}
