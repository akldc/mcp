#include <iostream>
#include <sys/types.h>
using namespace std;

class A {
public:
  virtual void a() { cout << "a() in A" << endl; }

  virtual void b() { cout << "b() in A" << endl; }

  virtual void c() { cout << "c() in A" << endl; }
};
class B {
public:
  virtual void a() { cout << "a() in B" << endl; }

  virtual void b() { cout << "b() in B" << endl; }

  void c() { cout << "c() in B" << endl; }

  void d() { cout << "d() in B" << endl; }
};
class C : public A, public B {
public:
  virtual void a() { cout << "a() in C" << endl; }
  void c() { cout << "c() in C" << endl; }
  void d() { cout << "d() in C" << endl; }
};
int main() {
  C c;
  printf("&c: %p\n", &c);
  c.A::b();
  cout << endl;

  A *pA = &c;
  printf("pA: %p\n", pA);
  pA->a();
  pA->b();
  pA->c();
  cout << endl;

  B *pB = &c;
  printf("pB: %p\n", pB);
  pB->a();
  pB->b();
  pB->c();
  pB->d();
  cout << endl;
  
  C *pC = &c;
  printf("pC: %p\n", pC);
  pC->a();
  pC->A::b(); // 此处就是二义性
  pC->c(); // 此处的c()走的是虚函数机制还是非虚函数机制，如何判别？
  pC->d(); // 此处是隐藏，不是重写
  return 0;
}