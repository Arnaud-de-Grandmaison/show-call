namespace N {
  class C {
  public:
    C() {}
    void f(double){}
  };
  int g(double);
}

namespace Z {
  class C {
  public:
    C() {}
    void f(int){}
    void f(double){}
    void f(char){}
  };
}

Z::C e;

int main (int argc, char *argv[]) {
  Z::C c;
  c.f(1);
  e = c;

  N::C *d = new N::C();
  d->f(2);

  delete d;

  N::g(3.14);

  return 0;
}
