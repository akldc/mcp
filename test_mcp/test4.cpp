#include <iostream>
#include <cstring>
using namespace std;

int main()
{
    char * str = "hello";

    //str[0] = 'H';
    cout<<str<<endl;
    cout<<strlen(str)<<endl;
    cout<<sizeof(str)<<endl;

    char str1[10] = "hello";
    cout<<str1<<endl;
    cout<<strlen(str1)<<endl;
    cout<<sizeof(str1)<<endl;

    cout<<sizeof("hello")<<endl;
}