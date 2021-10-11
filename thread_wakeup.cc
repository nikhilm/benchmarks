#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

#include <sys/eventfd.h>
#include <unistd.h>

#include <benchmark/benchmark.h>

/*
 * These benchmarks compare the cost of waking up another thread using eventfd
 * vs using a condition variable. In both cases, the subsidiary thread uses a CV
 * to wake up the main thread. Effectively each iteration of the benchmark
 * causes 2 wakeups, so the cost of a wakeup is roughly half.
 *
 * It is recommended to use `taskset -c 0` to pin the benchmark to a single CPU
 * because due to its nature each thread is going to block on the other, and
 * uses state that is likely to be in the cache. So it won't benefit from
 * multiple CPUs and will have the overhead of cache synchronization if run on
 * multiple CPUs. Typically on my machine, pinning has results of ~2us, while
 * not pinning is ~12us.
 *
 * In practice on my machine, it seems like eventfd is slightly faster.
 */

static void BM_EventFdWakeup(benchmark::State &state) {
  std::atomic_bool running{true};

  int efd = eventfd(0, EFD_CLOEXEC);
  if (efd < 0) {
    state.SkipWithError("Could not create eventfd\n");
    return;
  }

  std::mutex mutex_main;
  std::condition_variable cv_main;
  bool main_wakeup{false};

  std::thread thread([&]() {
    while (running.load(std::memory_order_acquire)) {
      {
        uint64_t value;
        int r = read(efd, &value, sizeof(value));
        if (r != sizeof(value)) {
          std::cerr << "read(efd) failure " << errno << "\n";
          abort();
        }
      }
      {
        std::unique_lock<std::mutex> lock(mutex_main);
        main_wakeup = true;
        cv_main.notify_one();
      }
    }
  });

  uint64_t value = 1;
  for (auto _ : state) {
    if (write(efd, &value, sizeof(value)) != sizeof(value)) {
      state.SkipWithError("Failed to write to efd\n");
      return;
    }
    {
      std::unique_lock<std::mutex> lock(mutex_main);
      cv_main.wait(lock, [&] { return main_wakeup; });
      main_wakeup = false;
    }
  }
  running.store(false, std::memory_order_release);
  // Cause progress so the thread can read the atomic change.
  if (write(efd, &value, sizeof(value)) != sizeof(value)) {
    state.SkipWithError("Failed to write to efd while exiting\n");
    return;
  }
  thread.join();
}

BENCHMARK(BM_EventFdWakeup)->Unit(benchmark::kMicrosecond);

static void BM_CondVarWakeup(benchmark::State &state) {
  std::atomic_bool running{true};

  std::mutex mutex;
  std::condition_variable cv;
  bool thread_wakeup{false};

  std::mutex mutex_main;
  std::condition_variable cv_main;
  bool main_wakeup{false};

  std::thread thread([&]() {
    while (running.load(std::memory_order_acquire)) {
      {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return thread_wakeup; });
        thread_wakeup = false;
      }
      {
        std::unique_lock<std::mutex> lock(mutex_main);
        main_wakeup = true;
        cv_main.notify_one();
      }
    }
  });

  for (auto _ : state) {
    {
      std::unique_lock<std::mutex> lock(mutex);
      thread_wakeup = true;
      cv.notify_one();
    }
    {
      std::unique_lock<std::mutex> lock(mutex_main);
      cv_main.wait(lock, [&] { return main_wakeup; });
      main_wakeup = false;
    }
  }
  running.store(false, std::memory_order_release);
  // Cause progress so the thread can read the atomic change.
  thread_wakeup = true;
  cv.notify_one();
  thread.join();
}

BENCHMARK(BM_CondVarWakeup)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
