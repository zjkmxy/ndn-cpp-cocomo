#pragma once

#include <exception>
#include <string>
#if defined(__GNUC__) && (__GNUC__ >= 10)
#include <coroutine>
#define CONVERTIBLE_TO(TYPE) std::convertible_to<TYPE>
#else
#include <experimental/coroutine>
#define CONVERTIBLE_TO(TYPE) typename
#endif

namespace asyncio {

#if defined(__GNUC__) && (__GNUC__ >= 10)
using std::coroutine_handle;
using std::suspend_always;
#else
using std::experimental::coroutine_handle;
using std::experimental::suspend_always;
#endif

struct double_await: public std::exception{
 constexpr const char* what() const noexcept override {
   return "A promise awaits on a second one before the first has finished.";
  }
};

struct resume_unfinished: public std::exception{
 constexpr const char* what() const noexcept override {
   return "await_resume() is called on an unfinished generator/coroutine.";
  }
};

struct no_value_returned: public std::exception{
 constexpr const char* what() const noexcept override {
   return "A generator/coroutine with return type returns no value.";
  }
};

struct not_implemented: public std::exception{
  std::string msg;

  not_implemented(std::string func_name):
    msg(func_name + " is not implemented.")
  {}

  const char* what() const noexcept override {
   return msg.c_str();
  }
};

struct no_engine: public std::exception{
 constexpr const char* what() const noexcept override {
   return "A coroutine is scheduled on a thread without any engine.";
  }
};

struct hanging_task: public std::exception{
 constexpr const char* what() const noexcept override {
   return "A task is created but neither co_awaited nor scheduled.";
  }
};

} // namespace asyncio