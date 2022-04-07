#pragma once

#include <optional>
#include <exception>
#include <memory>
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

/** @brief Common interface of coroutine and generator.
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

  constexpr chainable_promise():
    nested(nullptr), error(), yielded_value(std::nullopt), done(false), no_yield_finish(false),
    sender()
  {}

  constexpr auto initial_suspend() {
    return suspend_always();
  }

  constexpr auto final_suspend() noexcept {
    return suspend_always();
  }

  void unhandled_exception() {
    error = std::current_exception();
  }

  void chain(abstract_fiber<YieldType>* inner_fiber) {
    if(nested){
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

  template<CONVERTIBLE_TO(YieldType) From>
  auto& yield_value(From&& value) {
    yielded_value = std::forward<From>(value);
    // To imitate Python's send(), replace SendAwaitable with a user-defined sender
    return sender;
  }
};

template<typename YieldType, typename SendAwaitable = suspend_always>
struct void_promise: public chainable_promise<YieldType, SendAwaitable> {
  void return_void() {
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
    auto nested_yield = promise.wait_nested();
    if(nested_yield.has_value()) {
      return nested_yield.value();
    }
    promise.no_yield_finish = false;
    do {
      resume();
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

  bool await_ready() const noexcept {
    return false;
  }

  template<CONVERTIBLE_TO(base_promise_type) outer_promise_type>
  void await_suspend(coroutine_handle<outer_promise_type> outer) {
    base_promise_type& outer_promise = outer.promise();
    outer_promise.chain(this);
    outer_promise.yielded_value = next();
    if(promise().done) {
      outer_promise.no_yield_finish = true;
    }
  }

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

  coroutine_handle<promise_type> handle;

  void await_resume() const {
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

  coroutine_handle<promise_type> handle;

  ReturnType result() const {
    const auto& promise = handle.promise();
    if(!promise.returned_value.has_value()){
      throw no_value_returned{};
    }
    return promise.returned_value.value();
  }

  ReturnType await_resume() const {
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
    *value = std::forward<From>(input);
  }

  SendType await_resume() const noexcept {
    return value->value();
  }
};

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

  void await_resume() const {
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


  ReturnType result() const {
    const auto& promise = handle.promise();
    if(!promise.returned_value.has_value()){
      throw no_value_returned{};
    }
    return promise.returned_value.value();
  }

  ReturnType await_resume() const {
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

  template<CONVERTIBLE_TO(SendType) From>
  std::optional<YieldType> send(From&& input) {
    handle.promise().sender.send(input);
    return this->next();
  }
};

} // namespace asyncio
