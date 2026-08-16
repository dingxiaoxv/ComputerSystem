#include <algorithm>
#include <coroutine>
#include <deque>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

class Scheduler {
public:
  class SleepAwaiter {
  public:
    SleepAwaiter(Scheduler &scheduler, int ticks) : scheduler_(scheduler), ticks_(ticks) {}

    bool await_ready() const noexcept { return ticks_ <= 0; }

    void await_suspend(std::coroutine_handle<> handle) const {
      scheduler_.schedule_after(handle, ticks_);
    }

    void await_resume() const noexcept {}

  private:
    Scheduler &scheduler_;
    int ticks_;
  };

  int now() const { return tick_; }

  SleepAwaiter sleep_for(int ticks) { return SleepAwaiter(*this, ticks); }

  void spawn(std::coroutine_handle<> handle) { ready_.push_back(handle); }

  void schedule_after(std::coroutine_handle<> handle, int ticks) {
    timers_.push_back(Timer{tick_ + ticks, handle});
  }

  void run() {
    while (!ready_.empty() || !timers_.empty()) {
      wake_due_timers();

      if (ready_.empty()) {
        tick_ = next_timer_tick();
        wake_due_timers();
      }

      std::coroutine_handle<> handle = ready_.front();
      ready_.pop_front();

      if (!handle.done()) {
        handle.resume();
      }
    }
  }

private:
  struct Timer {
    int wake_tick;
    std::coroutine_handle<> handle;
  };

  void wake_due_timers() {
    auto it = timers_.begin();
    while (it != timers_.end()) {
      if (it->wake_tick <= tick_) {
        ready_.push_back(it->handle);
        it = timers_.erase(it);
      } else {
        ++it;
      }
    }
  }

  int next_timer_tick() const {
    int next = std::numeric_limits<int>::max();
    for (const Timer &timer : timers_) {
      next = std::min(next, timer.wake_tick);
    }
    return next;
  }

  int tick_ = 0;
  std::deque<std::coroutine_handle<>> ready_;
  std::vector<Timer> timers_;
};

class Task {
public:
  struct promise_type {
    Task get_return_object() {
      return Task(std::coroutine_handle<promise_type>::from_promise(*this));
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    std::suspend_always final_suspend() noexcept { return {}; }

    void return_void() noexcept {}

    void unhandled_exception() { std::terminate(); }
  };

  using Handle = std::coroutine_handle<promise_type>;

  explicit Task(Handle handle) : handle_(handle) {}

  Task(Task &&other) noexcept : handle_(std::exchange(other.handle_, {})) {}

  Task &operator=(Task &&other) noexcept {
    if (this != &other) {
      if (handle_) {
        handle_.destroy();
      }
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }

  Task(const Task &) = delete;
  Task &operator=(const Task &) = delete;

  ~Task() {
    if (handle_) {
      handle_.destroy();
    }
  }

  void start(Scheduler &scheduler) const { scheduler.spawn(handle_); }

private:
  Handle handle_;
};

Task handle_request(Scheduler &scheduler, std::string name, int read_delay, int write_delay) {
  std::cout << "[tick " << scheduler.now() << "] " << name << ": start read, await " << read_delay
            << " ticks\n";

  co_await scheduler.sleep_for(read_delay);

  std::string request = "request-from-" + name;
  std::cout << "[tick " << scheduler.now() << "] " << name << ": read complete: " << request
            << "\n";
  std::cout << "[tick " << scheduler.now() << "] " << name << ": start write, await " << write_delay
            << " ticks\n";

  co_await scheduler.sleep_for(write_delay);

  std::string response = "echo(" + request + ")";
  std::cout << "[tick " << scheduler.now() << "] " << name << ": write complete: " << response
            << "\n";
}

int main() {
  Scheduler scheduler;

  std::vector<Task> tasks;
  tasks.push_back(handle_request(scheduler, "coroutine-A", 3, 2));
  tasks.push_back(handle_request(scheduler, "coroutine-B", 1, 1));
  tasks.push_back(handle_request(scheduler, "coroutine-C", 2, 1));

  for (const Task &task : tasks) {
    task.start(scheduler);
  }

  scheduler.run();
  return 0;
}
