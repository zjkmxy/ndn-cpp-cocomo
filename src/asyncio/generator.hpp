#pragma once

#include <optional>
#include <exception>
#include <memory>
#include <iostream>
#include "common.hpp"
#include "utils.hpp"

namespace asyncio {

/** @brief Common interface of yield-based coroutine and generator.
 */
template<typename YieldType>
struct abstract_fiber {
  virtual ~abstract_fiber() {}

  /** @brief Returns whether the fiber is finished.
   */
  virtual bool is_done() const noexcept = 0;

  /** @brief Continues the execution.
   *  @pre is_done() == false.
   *  @return YieldType value if the fiber calls co_yield on some value.
   *          std::nullopt if the fiber co_return.
   */
  virtual std::optional<YieldType> next() = 0;
};

/** @brief A promise that can be chained with another.
 */
template<typename YieldType, typename SendAwaitable = suspend_always>
struct chainable_promise {
  abstract_fiber<YieldType>* nested;
  std::exception_ptr error;
  std::optional<YieldType> yielded_value;
  bool done;
  bool no_yield_finish;
  SendAwaitable sender;
  uint64_t promise_id;

  constexpr chainable_promise():
    nested(nullptr), error(), yielded_value(std::nullopt), done(false), no_yield_finish(false),
    sender(), promise_id(generate_id())
  {
    std::cout << "Generator (" << this->promise_id << ") created" << std::endl;
  }

  /** @brief Called when a new coroutine is created
   *         Python generator does not execute before next() is called, so suspend here.
   */
  constexpr auto initial_suspend() {
    return suspend_always();
  }

  /** @brief Called when a coroutine finishes and before the self-destruction of promise.
   *         Here we suspends to retain the result.
   */
  constexpr auto final_suspend() noexcept {
    std::cout << "Generator (" << promise_id << ") final_suspend" << std::endl;
    return suspend_always();
  }

  void unhandled_exception() {
    error = std::current_exception();
  }

  void chain(abstract_fiber<YieldType>* inner_fiber) {
    if(nested && !nested->is_done()){
      throw double_await();
    }
    nested = inner_fiber;
  }

  std::optional<YieldType> wait_nested(){
    if(!nested) {
      return std::nullopt;
    }
    if(nested->is_done()){
      nested = nullptr;
      return std::nullopt;
    }
    return nested->next();
  }

  /** @brief Called when a value is yielded.
   *         co_yield XXX = co_await yield_value(XXX)
   */
  template<CONVERTIBLE_TO(YieldType) From>
  auto& yield_value(From&& value) {
    std::cout << "Generator (" << this->promise_id << ") yielded " << value << std::endl;
    yielded_value = std::forward<From>(value);
    // To imitate Python's send(), replace SendAwaitable with a user-defined sender
    return sender;
  }
};

template<typename YieldType, typename SendAwaitable = suspend_always>
struct void_promise: public chainable_promise<YieldType, SendAwaitable> {
  /** @brief Called on co_return.
   */
  void return_void() {
    std::cout << "Generator (" << this->promise_id << ") returned void" << std::endl;
    this->done = true;
  }
};

template<typename YieldType, typename ReturnType, typename SendAwaitable = suspend_always>
struct ret_value_promise: public chainable_promise<YieldType, SendAwaitable> {
  std::optional<ReturnType> returned_value;

  ret_value_promise():
    returned_value(std::nullopt)
  {}

  template<CONVERTIBLE_TO(ReturnType) From>
  void return_value(From&& value) {
    std::cout << "Generator (" << this->promise_id << ") returned " << value << std::endl;
    returned_value = std::forward<From>(value);
    this->done = true;
  }
};

template <typename YieldType, typename SendAwaitable = suspend_always>
struct base_generator: public abstract_fiber<YieldType> {
  using base_promise_type = chainable_promise<YieldType, SendAwaitable>;
  virtual base_promise_type& promise() = 0;
  virtual const base_promise_type& promise() const = 0;
  virtual void resume() = 0;

  std::optional<YieldType> next() override {
    // If there is a chained generator, run the inner one first.
    // Note: the inner one may exit immediately.
    auto& promise = this->promise();
    std::cout << "Generator (" << promise.promise_id << ") next() called" << std::endl;
    auto nested_yield = promise.wait_nested();
    if(nested_yield.has_value()) {
      std::cout << "Generator (" << promise.promise_id << ") yielded from inner generator with "
                << nested_yield.value() << std::endl;
      return nested_yield.value();
    }
    do {
      // This is used to handle the case when inner generator finishes immediately
      // So we call resume() again to trigger await_resume to set the value
      promise.no_yield_finish = false;
      std::cout << "Generator (" << promise.promise_id << ") ready to resume in next()" << std::endl;
      resume();
      std::cout << "Generator (" << promise.promise_id << ") resumed in next()" << std::endl;
      if(promise.error){
        std::rethrow_exception(promise.error);
      } else if(promise.done){
        return std::nullopt;
      }
    } while (promise.no_yield_finish);
    return promise.yielded_value;
  }

  bool is_done() const noexcept override {
    return promise().done;
  }

  /** @brief Called when the generator is awaited.
   *         Here we want to use await_suspend to set the outer generator's nested field,
   *         so always suspend.
   */
  bool await_ready() const noexcept {
    std::cout << "Generator (" << promise().promise_id << ") is awaited" << std::endl;
    return false;
  }

  template<CONVERTIBLE_TO(base_promise_type) outer_promise_type>
  void await_suspend(coroutine_handle<outer_promise_type> outer) {
    base_promise_type& outer_promise = outer.promise();
    std::cout << "Generator (" << promise().promise_id << ") is suspended to be chained after ("
              << outer_promise.promise_id << ")" << std::endl;
    outer_promise.chain(this);
    outer_promise.yielded_value = next();
    if(promise().done) {
      std::cout << "Generator (" << promise().promise_id << ") finished immediately without yielding" << std::endl;
      outer_promise.no_yield_finish = true;
    }
  }

  /** @brief used to implement range-based loop
   */
  struct iterator{
    base_generator* gen;
    std::optional<YieldType> value;

    iterator& operator++(){
      value = gen->next();
      return *this;
    }

    bool operator!=(bool){
      return !gen->is_done();
    }

    YieldType operator*() {
      return value.value();
    }
  };

  iterator begin() {
    return iterator{
      .gen = this,
      .value = next(),
    };
  }

  constexpr bool end() {
    return false;
  }
};

template <typename YieldType, typename ReturnType = void>
struct generator;

template <typename YieldType>
struct generator<YieldType, void>: public base_generator<YieldType> {
  using base_promise_type = chainable_promise<YieldType, suspend_always>;
  struct promise_type: public void_promise<YieldType, suspend_always> {
    generator get_return_object() {
      return generator(coroutine_handle<promise_type>::from_promise(*this));
    }
  };

  generator(const coroutine_handle<promise_type>& handle):
    handle(handle)
  {}

  ~generator() override {
    std::cout << "Generator (" << promise().promise_id << ") destroyed" << std::endl;
    handle.destroy();
  }

  coroutine_handle<promise_type> handle;

  void await_resume() const {
    std::cout << "Generator (" << promise().promise_id << ") await_resumed" << std::endl;
    if(!handle.promise().done){
      throw resume_unfinished{};
    }
  }

  base_promise_type& promise() override {
    return handle.promise();
  }

  const base_promise_type& promise() const override {
    return handle.promise();
  }

  virtual void resume() override {
    handle.resume();
  }
};

template <typename YieldType, typename ReturnType>
struct generator: public base_generator<YieldType> {
  using base_promise_type = chainable_promise<YieldType, suspend_always>;
  struct promise_type: public ret_value_promise<YieldType, ReturnType, suspend_always> {
    generator get_return_object() {
      return generator(coroutine_handle<promise_type>::from_promise(*this));
    }
  };

  generator(const coroutine_handle<promise_type>& handle):
    handle(handle)
  {}

  ~generator() override {
    std::cout << "Generator (" << promise().promise_id << ") destroyed" << std::endl;
    handle.destroy();
  }

  coroutine_handle<promise_type> handle;

  ReturnType result() const {
    const auto& promise = handle.promise();
    if(!promise.returned_value.has_value()){
      throw no_value_returned{};
    }
    return std::move(promise.returned_value.value());
  }

  ReturnType await_resume() const {
    std::cout << "Generator (" << promise().promise_id << ") await_resumed" << std::endl;
    if(!handle.promise().done){
      throw resume_unfinished{};
    }
    return result();
  }

  base_promise_type& promise() override {
    return handle.promise();
  }

  const base_promise_type& promise() const override {
    return handle.promise();
  }

  virtual void resume() override {
    handle.resume();
  }
};

template<typename SendType>
struct send_awaitable {
  // clang is fine with just optional but GCC seems to copy this object so we need a pointer
  std::optional<SendType>* value;

  send_awaitable(){}

  constexpr bool await_ready() const noexcept {
    return false;
  }

  constexpr void await_suspend(coroutine_handle<>) const noexcept {}

  template<CONVERTIBLE_TO(SendType) From>
  void send(From&& input) {
    std::cout << "sender obtained value " << input << std::endl;
    *value = std::forward<From>(input);
  }

  SendType await_resume() const noexcept {
    std::cout << "sender passed value " << value->value() << " to co_yield caller" << std::endl;
    return value->value();
  }
};

// send_generator does not work for nest generator via co_await.
template <typename YieldType, typename SendType, typename ReturnType = void>
struct send_generator;

template <typename YieldType, typename SendType>
struct send_generator<YieldType, SendType, void>: public base_generator<YieldType, send_awaitable<SendType>> {
  using base_promise_type = chainable_promise<YieldType, send_awaitable<SendType>>;
  struct promise_type: public void_promise<YieldType, send_awaitable<SendType>> {
    send_generator get_return_object() {
      return send_generator(coroutine_handle<promise_type>::from_promise(*this));
    }
  };

  coroutine_handle<promise_type> handle;
  std::optional<SendType> send_value;

  send_generator(const coroutine_handle<promise_type>& handle):
    handle(handle), send_value(std::nullopt)
  {
    handle.promise().sender.value = &send_value;
  }

  ~send_generator() override {
    std::cout << "SendGenerator (" << promise().promise_id << ") destroyed" << std::endl;
    handle.destroy();
  }

  base_promise_type& promise() override {
    return handle.promise();
  }

  const base_promise_type& promise() const override {
    return handle.promise();
  }

  virtual void resume() override {
    handle.resume();
  }

  template<CONVERTIBLE_TO(SendType) From>
  std::optional<YieldType> send(From&& input) {
    handle.promise().sender.send(input);
    return this->next();
  }
};

template <typename YieldType, typename SendType, typename ReturnType>
struct send_generator: public base_generator<YieldType, send_awaitable<SendType>> {
  using base_promise_type = chainable_promise<YieldType, send_awaitable<SendType>>;
  struct promise_type: public ret_value_promise<YieldType, ReturnType, send_awaitable<SendType>> {
    send_generator get_return_object() {
      return send_generator(coroutine_handle<promise_type>::from_promise(*this));
    }
  };

  coroutine_handle<promise_type> handle;
  std::optional<SendType> send_value;

  send_generator(const coroutine_handle<promise_type>& handle):
    handle(handle), send_value(std::nullopt)
  {
    handle.promise().sender.value = &send_value;
  }

  ~send_generator() override {
    std::cout << "SendGenerator (" << promise().promise_id << ") destroyed" << std::endl;
    handle.destroy();
  }

  ReturnType result() const {
    const auto& promise = handle.promise();
    if(!promise.returned_value.has_value()){
      throw no_value_returned{};
    }
    return promise.returned_value.value();
  }

  base_promise_type& promise() override {
    return handle.promise();
  }

  const base_promise_type& promise() const override {
    return handle.promise();
  }

  virtual void resume() override {
    handle.resume();
  }

  template<CONVERTIBLE_TO(SendType) From>
  std::optional<YieldType> send(From&& input) {
    handle.promise().sender.send(input);
    return this->next();
  }
};

} // namespace asyncio
