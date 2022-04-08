#pragma once

#include "common.hpp"
#include "utils.hpp"
#include <optional>
#include <list>

namespace asyncio {

struct abstract_engine {
  virtual void schedule(coroutine_handle<> handle, msec tim) = 0;

  virtual ~abstract_engine(){}

  static thread_local abstract_engine* current_loop;
};

struct sleep_awaiter: suspend_always {
  abstract_engine& engine;
  msec awake_at;

  sleep_awaiter(abstract_engine& engine, msec awake_at):
    engine(engine), awake_at(awake_at)
  {}

  void await_suspend(coroutine_handle<> caller) const noexcept {
    engine.schedule(caller, awake_at);
  }
};

template<typename T>
struct result_awaiter;

template<>
struct result_awaiter<void> {
  bool& done;
  std::function<void(coroutine_handle<>)> call_after;

  result_awaiter(bool& done, std::function<void(coroutine_handle<>)> call_after):
    done(done), call_after(call_after)
  {}

  bool await_ready() const noexcept {
    return done;
  }

  constexpr void await_resume() {}

  void await_suspend(coroutine_handle<> caller) {
    call_after(caller);
  }
};

template<typename T>
struct result_awaiter {
  std::optional<T>& result;
  std::function<void(coroutine_handle<>)> call_after;

  result_awaiter(std::optional<T>& result, std::function<void(coroutine_handle<>)> call_after):
    result(result), call_after(call_after)
  {}

  bool await_ready() const noexcept {
    return result.has_value();
  }

  constexpr T await_resume() {
    return std::move(result.value());
  }

  void await_suspend(coroutine_handle<> caller) {
    call_after(caller);
  }
};

struct base_task_promise {
  abstract_engine* engine_ptr;
  std::list<coroutine_handle<>> on_finish;
  std::exception_ptr error;

  base_task_promise(abstract_engine* engine):
    engine_ptr(engine), on_finish(), error()
  {}

  auto initial_suspend() {
    return suspend_always();
  }

  auto final_suspend() noexcept {
    if(!engine_ptr){
      throw no_engine{};
    }
    for(auto& h: on_finish){
      engine_ptr->schedule(h, 0);
    }
    return suspend_always();
  }

  void unhandled_exception() {
    error = std::current_exception();
  }
};

template<typename T>
struct task;

template<>
struct task<void> {
  using awaiter = result_awaiter<void>;

  struct promise_type: public base_task_promise {
    bool done;

    auto get_awaiter() {
      return awaiter(done,
        [this](coroutine_handle<> event){
          on_finish.push_back(event);
        });
    }

    void return_void() {
      done = true;
    }

    task get_return_object() {
      return task(coroutine_handle<promise_type>::from_promise(*this));
    }

    promise_type():
      base_task_promise(nullptr)
    {}

    promise_type(const promise_type&) = delete;
    void operator=(const promise_type&) = delete;
  };

  void set_engine(abstract_engine& engine){
    handle.promise().engine_ptr = &engine;
  }

  awaiter operator co_await(){
    co_awaited = true;
    return handle.promise().get_awaiter();
  }

  bool is_done() {
    return handle.promise().done;
  }

  task(coroutine_handle<promise_type> handle):
    handle(handle), co_awaited(false)
  {}

  ~task() {
    if(!co_awaited && !handle.promise().engine_ptr){
      throw hanging_task{};
    }
  }

  coroutine_handle<promise_type> handle;
  bool co_awaited;
};

template<typename T>
struct task {
  using awaiter = result_awaiter<T>;

  struct promise_type: public base_task_promise {
    std::optional<T> result;

    auto get_awaiter() {
      return awaiter(result,
        [this](coroutine_handle<> event){
          on_finish.push_back(event);
        });
    }

    template<CONVERTIBLE_TO(T) From>
    void return_value(From&& value) {
      result = std::forward<From>(value);
    }

    task get_return_object() {
      return task(coroutine_handle<promise_type>::from_promise(*this));
    }

    promise_type():
      base_task_promise(nullptr)
    {}

    promise_type(const promise_type&) = delete;
    void operator=(const promise_type&) = delete;
  };

  void set_engine(abstract_engine& engine){
    handle.promise().engine_ptr = &engine;
  }

  awaiter operator co_await(){
    co_awaited = true;
    return handle.promise().get_awaiter();
  }

  bool is_done() {
    return handle.promise().result.has_value();
  }

  T result() {
    return std::move(handle.promise().result.value());
  }

  task(coroutine_handle<promise_type> handle):
    handle(handle), co_awaited(false)
  {}

  ~task() {
    if(!co_awaited && !handle.promise().engine_ptr){
      throw hanging_task{};
    }
  }

  coroutine_handle<promise_type> handle;
  bool co_awaited;
};


} // namespace asyncio