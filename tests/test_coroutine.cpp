#include <cstdio>
#include <string>
#include <cmath>
#include <array>
#include "asyncio/coroutine.hpp"
#include "asyncio/sleep_engine.hpp"

using namespace asyncio;

sleep_engine engine;

task<void> hello_world() {
  std::cout << "hello ..." << std::endl;
  co_await engine.sleep(1000);
  std::cout << "... world!" << std::endl;
  co_return;
}

task<std::string> g(task<void>& inner) {
  std::cout << "g() starts" << std::endl;
  co_await inner;
  std::cout << "g() finishes" << std::endl;
  co_return "g";
}

task<double> h(task<void>& inner) {
  std::cout << "h() starts" << std::endl;
  co_await inner;
  std::cout << "h() finishes" << std::endl;
  co_return 0.23;
} 

task<int> func() {
  std::cout << "func() starts sleep" << std::endl;
  co_await engine.sleep(1000);
  std::cout << "func() ends sleep" << std::endl;

  auto hello = hello_world();
  std::cout << "func() starts awaiting hello world" << std::endl;
  co_await hello;
  std::cout << "func() finishes awaiting hello world" << std::endl;

  // Await for scheduled task test
  auto hello2 = hello_world();
  auto g_task = g(hello2);
  auto h_task = h(hello2);
  engine.schedule_task(g_task, 1000);
  std::cout << "func() scheduled g()" << std::endl;
  engine.schedule_task(h_task, 500);
  std::cout << "func() scheduled h()" << std::endl;

  std::cout << "func() starts awaiting g()" << std::endl;
  auto g_ret = co_await g_task;
  std::cout << "func(): g() returned " << g_ret << std::endl;
  std::cout << "func() starts awaiting h()" << std::endl;
  auto h_ret = co_await h_task;
  std::cout << "func(): h() returned " << h_ret << std::endl;
  std::cout << "func() finishes" << std::endl;

  co_return 5;
}

int main() {
  auto f = func();

  engine.schedule_task(f, 0);

  std::cout << "engine started!" << std::endl;
  engine.run();
  std::cout << "engine finished!" << std::endl;

  return 0;
}
