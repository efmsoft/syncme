#include <thread>
#include <random>
#include <vector>
#include <chrono>
#include <iostream>
#include <atomic>
#include <mutex>

#include <gtest/gtest.h>

#include <Syncme/Sync.h>
#include <Syncme/ThreadPool/Pool.h>

using namespace Syncme;
using namespace Syncme::ThreadPool;

constexpr size_t kNumMeasuringThreads = 100;
constexpr int kNumIterationsPerThread = 50;
constexpr int kMinSleep = 0;
constexpr int kMaxSleep = 100;

struct Stats
{
  std::atomic<size_t> count{ 0 };
  std::atomic<uint64_t> overheadSum{ 0 };
};

static uint32_t RandomSleepMs()
{
  static thread_local std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<int> dist(kMinSleep, kMaxSleep);
  return dist(rng);
}

static void DirectThreadWorker(Stats& stats)
{
  for (int i = 0; i < kNumIterationsPerThread; ++i)
  {
    uint64_t sleepMs = RandomSleepMs();
    auto start = std::chrono::steady_clock::now();

    auto t0 = std::chrono::steady_clock::now();
    std::thread t([sleepMs]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    });

    t.join();
    auto end = std::chrono::steady_clock::now();

    uint64_t elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - t0).count();
    uint64_t overhead = (elapsed > sleepMs * 1000) ? (elapsed - sleepMs * 1000) : 0;

    stats.count++;
    stats.overheadSum += overhead;
  }
}

static void PoolThreadWorker(Pool& pool, Stats& stats)
{
  for (int i = 0; i < kNumIterationsPerThread; ++i)
  {
    uint64_t sleepMs = RandomSleepMs();

    auto start = std::chrono::steady_clock::now();
    HEvent ev = pool.Run([sleepMs]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    });

    auto t0 = std::chrono::steady_clock::now();
    WaitForSingleObject(ev);
    auto end = std::chrono::steady_clock::now();

    uint64_t elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - t0).count();
    uint64_t overhead = (elapsed > sleepMs * 1000) ? (elapsed - sleepMs * 1000) : 0;

    stats.count++;
    stats.overheadSum += overhead;
  }
}

static void RunTest()
{
  Stats directStats;
  Stats poolStats;

  // Direct thread test
  {
    std::cout << "Running direct thread test..." << std::endl;
    std::vector<std::thread> threads;
    for (size_t i = 0; i < kNumMeasuringThreads; ++i)
    {
      threads.emplace_back(DirectThreadWorker, std::ref(directStats));
    }

    for (auto& t : threads)
      t.join();
  }

  // Thread pool test
  {
    std::cout << "Running thread pool test..." << std::endl;

    Pool pool;
    pool.SetMaxThreads(100);
    std::vector<std::thread> threads;
    for (size_t i = 0; i < kNumMeasuringThreads; ++i)
    {
      threads.emplace_back(PoolThreadWorker, std::ref(pool), std::ref(poolStats));
    }

    for (auto& t : threads)
      t.join();

    pool.Stop();
  }

  auto avgDirectOverhead = directStats.overheadSum / directStats.count;
  auto avgPoolOverhead = poolStats.overheadSum / poolStats.count;

  std::cout << "\n=== Results ===\n";

  std::cout << "Direct threads: count = " << directStats.count
    << ", avg overhead = " << avgDirectOverhead << " µs\n";

  std::cout << "Thread pool   : count = " << poolStats.count
    << ", avg overhead = " << avgPoolOverhead << " µs\n";
}

TEST(Pool, performance)
{
  RunTest();
}