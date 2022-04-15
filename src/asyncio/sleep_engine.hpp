#pragma once

#include "common.hpp"
#include "utils.hpp"
#include "coroutine.hpp"
#include <list>
#include <memory>
#include <limits>

namespace asyncio {

struct sleep_engine: public abstract_engine {
  using event_data = std::pair<msec, coroutine_handle<>>;

  std::list<event_data> events;  // A heap would be faster
  std::list<std::unique_ptr<abstract_task>> owned_tasks;
  timer tmer;

  sleep_engine() {}

  void schedule(coroutine_handle<> handle, msec tim) override {
    events.push_back(std::make_pair(tim, handle));
  }

  bool is_scheduled(coroutine_handle<> handle) const override {
    for(const auto &e: events) {
      if(e.second.address() == handle.address()) {
        return true;
      }
    }
    return false;
  }

  template<typename T>
  void schedule_task(task<T>& task, msec after) {
    task.set_engine(*this);
    schedule(task.handle, tmer.now() + after);
  }

  sleep_awaiter sleep(msec duration) {
    auto awake_at = tmer.now() + duration;
    return sleep_awaiter(this, awake_at);
  }

  void run_one_round() {
    // Get least sleep time
    msec least_await = std::numeric_limits<msec>::max();
    for(auto& e: events) {
      least_await = std::min(e.first, least_await);
    }
    // Sleep until the first executable task
    msec now = tmer.now();
    if(least_await > now) {
      tmer.sleep(least_await - now);
    }
    now = tmer.now();
    // Execute scheduled tasks
    for(auto it = events.begin(); it != events.end(); ){
      if(it->first <= now) {
        std::cout << "engine resumes " << it->second.address() << std::endl;
        it->second.resume();
        it = events.erase(it);
      } else {
        ++ it;
      }
    }
    // Remove finished owned tasks
    for(auto it = owned_tasks.begin(); it != owned_tasks.end(); ){
      if((*it)->is_done()) {
        std::cout << "engine removed a finished task" <<std::endl;
        it = owned_tasks.erase(it);
      } else {
        ++ it;
      }
    }
  }

  void run() {
    while(!events.empty()){
      run_one_round();
    }
  }

  void transfer_ownership(std::unique_ptr<abstract_task>&& task) {
    owned_tasks.push_back(std::move(task));
  }
};

} // namespace asyncio
