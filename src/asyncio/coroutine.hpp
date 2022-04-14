#pragma once

#include "common.hpp"
#include "utils.hpp"
#include <optional>
#include <list>
#include <iostream>

namespace asyncio {

struct abstract_task {
  virtual bool is_done() = 0;

  virtual ~abstract_task(){}
};

struct abstract_engine {
  // Note: coroutine_handle is like a view, which does not hold ownership
  virtual void schedule(coroutine_handle<> handle, msec tim) = 0;

  virtual bool is_scheduled(coroutine_handle<> handle) const = 0;

  virtual ~abstract_engine(){}

  static thread_local abstract_engine* current_loop;
};

struct sleep_awaiter: suspend_always {
  abstract_engine* engine_ptr;
  msec awake_at;

  sleep_awaiter(abstract_engine* engine, msec awake_at):
    engine_ptr(engine), awake_at(awake_at)
  {}

  void await_suspend(coroutine_handle<> caller) const {
    if(!engine_ptr){
      throw no_engine{};
    }
    engine_ptr->schedule(caller, awake_at);
  }
};

template<typename T>
struct result_awaiter;

template<>
struct result_awaiter<void> {
  bool& done;
  std::function<void(coroutine_handle<>)> call_after;
  uint64_t promise_id;

  result_awaiter(bool& done, std::function<void(coroutine_handle<>)> call_after, uint64_t promise_id):
    done(done), call_after(call_after), promise_id(promise_id)
  {}

  bool await_ready() const noexcept {
    return done;
  }

  /*constexpr*/ void await_resume() {
    std::cout << "await_resume of " << promise_id;
    if(!done) {
      std::cout << " is not done!!!" << std::endl;
    } else {
      std::cout << " returned void" << std::endl;
    }
  }

  void await_suspend(coroutine_handle<> caller) {
    call_after(caller);
  }
};

template<typename T>
struct result_awaiter {
  std::optional<T>& result;
  std::function<void(coroutine_handle<>)> call_after;
  uint64_t promise_id;

  result_awaiter(std::optional<T>& result, std::function<void(coroutine_handle<>)> call_after, uint64_t promise_id):
    result(result), call_after(call_after), promise_id(promise_id)
  {}

  bool await_ready() const noexcept {
    return result.has_value();
  }

  /*constexpr*/ T await_resume() {
    std::cout << "await_resume of " << promise_id;
    if(!result.has_value()) {
      std::cout << " returned no value, error" << std::endl;
      throw no_value_returned{};
    }
    std::cout << " returned " << result.value() << std::endl;
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
  uint64_t promise_id;

  base_task_promise(abstract_engine* engine):
    engine_ptr(engine), on_finish(), error(), promise_id(generate_id())
  {}

  auto initial_suspend() {
    std::cout << "initial_suspend of " << promise_id << std::endl;
    return suspend_always();
  }

  auto final_suspend() noexcept {
    std::cout << "final_suspend of " << promise_id << std::endl;
    if(!engine_ptr){
      // throw no_engine{};
      std::cerr << no_engine{}.what() << std::endl;
      std::terminate();
    }
    for(auto& h: on_finish){
      std::cout << "call_after of " << promise_id << " try to schedule " << h.address() << " ... ";
      if(!engine_ptr->is_scheduled(h)) {
        std::cout << "done" << std::endl;
        engine_ptr->schedule(h, 0);
      } else {
        std::cout << "skipped" << std::endl;
        // IMPOSSIBLE because one task can only co_await on one thing
      }
    }
    return suspend_never();
  }

  void unhandled_exception() {
    error = std::current_exception();
    // Probably shoudln't rethrow in real world; should let the user handle it.
    std::cout << "catched unhandled exception" << std::endl;
    std::rethrow_exception(error);
  }
};

template<typename T>
struct task;

template<>
struct task<void>: public abstract_task {
  using awaiter = result_awaiter<void>;

  struct promise_type: public base_task_promise {
    // NOTE: Promise is destryoed because final_suspend() returns suspend_never, so it cannot hold any result value!
    bool* done;

    void return_void() {
      std::cout << "return_value of " << promise_id << " returned void" << std::endl;
      *done = true;
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
    const auto& handle = this->handle;
    auto& promise = handle.promise();
    return awaiter(done,
      [handle, &promise](coroutine_handle<> event){
        // Schedule the current one if it is not
        if(!promise.engine_ptr) {
          promise.engine_ptr = abstract_engine::current_loop;
          if(!promise.engine_ptr) {
            throw no_engine{};
          }
          promise.engine_ptr->schedule(handle, 0);
        }
        // Remark parent coroutine to schedule it after the current one finishes
        promise.on_finish.push_back(event);
      }, promise.promise_id);
  }

  bool is_done() {
    return done;
  }

  task(coroutine_handle<promise_type> handle):
    handle(handle), done(false)
  {
    std::cout << "task created: id=" << handle.promise().promise_id << " addr=" << handle.address() << std::endl;
    handle.promise().done = &done;
  }

  ~task() noexcept {
    if(!handle.promise().engine_ptr){
      // throw hanging_task{};
      std::cerr << hanging_task{}.what() << std::endl;
      std::terminate();
    }
  }

  coroutine_handle<promise_type> handle;
  bool done;
};

template<typename T>
struct task: public abstract_task {
  using awaiter = result_awaiter<T>;

  struct promise_type: public base_task_promise {
    // NOTE: Promise is destryoed because final_suspend() returns suspend_never, so it cannot hold any result value!
    std::optional<T> *result_ptr;

    template<CONVERTIBLE_TO(T) From>
    void return_value(From&& value) {
      std::cout << "return_value of " << promise_id << " returned " << value << std::endl;
      *result_ptr = std::forward<From>(value);
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
    const auto& handle = this->handle;
    auto& promise = handle.promise();
    return awaiter(result_val,
      [handle, &promise](coroutine_handle<> event){
        // Schedule the current one if it is not
        if(!promise.engine_ptr) {
          promise.engine_ptr = abstract_engine::current_loop;
          if(!promise.engine_ptr) {
            throw no_engine{};
          }
          promise.engine_ptr->schedule(handle, 0);
        }
        // Remark parent coroutine to schedule it after the current one finishes
        promise.on_finish.push_back(event);
      }, promise.promise_id);
  }

  bool is_done() {
    return result_val.has_value();
  }

  T result() {
    return std::move(result_val.value());
  }

  task(coroutine_handle<promise_type> handle):
    handle(handle), result_val(std::nullopt)
  {
    std::cout << "task created: id=" << handle.promise().promise_id << " addr=" << handle.address() << std::endl;
    handle.promise().result_ptr = &result_val;
  }

  task(const task&) = delete;
  void operator=(const task&) = delete;

  ~task() noexcept {
    if(!handle.promise().engine_ptr){
      // throw hanging_task{};
      std::cerr << hanging_task{}.what() << std::endl;
      std::terminate();
    }
  }

  coroutine_handle<promise_type> handle;
  std::optional<T> result_val;
};

} // namespace asyncio
