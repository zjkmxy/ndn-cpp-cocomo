#include <cstdio>
#include <string>
#include <cmath>
#include "asyncio/coroutine.hpp"

using namespace asyncio;

generator<int, double> g() {
  printf("enter g\n");
  co_yield 3;
  co_yield 4;
  printf("exit g\n");
  co_return -1.0;
}

generator<int, std::string> h2() {
  printf("execute h2\n");
  co_return "result from h2()";
}

generator<int, std::string> h1() {
  printf("execute h1\n");
  co_return co_await h2();
}

generator<int, int> f(){
  printf("enter f\n");
  co_yield 1;
  co_yield 2;
  printf("f calls g\n");
  auto gv = co_await g();
  printf("f gains back from g with value: %lf\n", gv);
  co_yield 5;
  printf("f calls h\n");
  auto hv = co_await h1();
  printf("f gains back from h with value: %s\n", hv.c_str());
  co_yield 6;
  printf("exit f\n");
  co_return -3;
}

bool is_prime(int x) {
  if(x > 1) {
    if(x == 2) {
      return true;
    }
    if(x % 2 == 0) {
      return false;
    }
    for(int i = 3, end = int(std::sqrt(x) + 1); i < end; i ++) {
      if(x % i == 0) {
        return false;
      }
    }
    return true;
  } else {
    return false;
  }
}

send_generator<int, int> get_primes(int number) {
  printf("Hello\n");
  while(true) {
    if(is_prime(number)) {
      number = co_yield number;
    }
    number ++;
  }
}

void print_successive_primes(int iterations, int base = 10) {
  auto prime_generator = get_primes(base);
  printf("Before start\n");
  prime_generator.next();
  printf("Init start\n");
  for(int i = 0; i < iterations; i ++) {
    const auto& val = prime_generator.send(std::pow(base, i));
    printf("%d\n", val.value());
  }
}

int main() {
  auto g = f();
  for(auto v = g.next(); v.has_value(); v = g.next()) {
    printf("Yield with %d\n", v.value());
  }
  printf("Finished with %d\n", g.result());
  printf("\n");

  auto g2 = f();
  for(auto v: g2) {
    printf("Yield with %d\n", v);
  }
  printf("Finished with %d\n", g2.result());
  printf("\n");

  print_successive_primes(8);
}
