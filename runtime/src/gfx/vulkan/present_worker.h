#pragma once
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace gfxvk {
// One outstanding operation, including an unconsumed result. The operation
// never runs under the mailbox mutex; polling cannot wait for the operation.
// Used only with a dedicated VkQueue, never concurrently on the graphics queue.
template<class Result> class PresentWorker {
 public:
  PresentWorker(): thread_([this] { run(); }) {}
  PresentWorker(const PresentWorker&)=delete;
  PresentWorker& operator=(const PresentWorker&)=delete;
  ~PresentWorker() {
    { std::lock_guard lock(mutex_); stopping_=true; }
    changed_.notify_one();thread_.join();
  }
  void submit(std::function<Result()> task) {
    std::lock_guard lock(mutex_);
    if(busy_ || ready_ || stopping_)throw std::logic_error("Presentation worker is occupied");
    task_=std::move(task);busy_=true;changed_.notify_one();
  }
  std::optional<Result> poll() {
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock() || !ready_)return {};
    return consume();
  }
  Result wait() {
    std::unique_lock lock(mutex_);
    if(!busy_ && !ready_)throw std::logic_error("No pending presentation");
    changed_.wait(lock,[this] { return ready_; });
    return std::move(*consume());
  }
 private:
  std::optional<Result> consume() {
    ready_=false;
    auto failure=std::exchange(failure_,{});
    auto result=std::move(result_);result_.reset();
    if(failure)std::rethrow_exception(failure);
    return result;
  }
  void run() {
    std::unique_lock lock(mutex_);
    for(;;) {
      changed_.wait(lock,[this] { return stopping_ || bool(task_); });
      if(!task_)return;
      auto task=std::move(task_);task_={};lock.unlock();
      std::optional<Result> result;std::exception_ptr failure;
      try { result=task(); } catch(...) { failure=std::current_exception(); }
      lock.lock();result_=std::move(result);failure_=failure;busy_=false;ready_=true;
      changed_.notify_all();
    }
  }
  std::mutex mutex_;
  std::condition_variable changed_;
  std::function<Result()> task_;
  std::optional<Result> result_;
  std::exception_ptr failure_;
  bool busy_=false,ready_=false,stopping_=false;
  std::thread thread_;
};
}
