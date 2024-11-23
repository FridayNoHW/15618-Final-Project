#include "coarse_grain_list.h"
#include "lock_free_list.h"
#include <thread>
#include <vector>

static const int NUM_OPERATIONS = 200;
static const int MAX_THREADS = 128;

void coarse_grain_mixed_worker_all_delete(CoarseGrainList<int> &list,
                                          int thread_id) {
  int base = thread_id * NUM_OPERATIONS;
  for (int i = 0; i < NUM_OPERATIONS; ++i) {
    if (i % 2 == 0) {
      list.insert(base + i);
    } else {
      for (int attempt = 0; attempt < 5; ++attempt) {
        if (list.remove(base + i - 1))
          break;
        this_thread::sleep_for(chrono::milliseconds(1 << attempt));
      }
    }
  }
}

void coarse_grain_insert_worker(CoarseGrainList<int> &list, int start) {
  for (int i = start; i < NUM_OPERATIONS; ++i) {
    list.insert(i);
  }
}

void benchmark_coarse_grain() {
  std::cout << "Benchmarking CoarseGrainList insert only\n";

  for (int num_threads = 1; num_threads <= MAX_THREADS; num_threads *= 2) {
    CoarseGrainList<int> list;

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
      threads.emplace_back(coarse_grain_insert_worker, std::ref(list), i);
    }

    for (auto &t : threads) {
      t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time)
                        .count();

    std::cout << "Threads: " << num_threads << " | Time: " << duration
              << " ms\n";
  }

  std::cout << "Benchmarking CoarseGrainList mixed\n";

  for (int num_threads = 1; num_threads <= MAX_THREADS; num_threads *= 2) {
    CoarseGrainList<int> list;

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
      threads.emplace_back(coarse_grain_mixed_worker_all_delete, std::ref(list),
                           i);
    }

    for (auto &t : threads) {
      t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time)
                        .count();

    std::cout << "Threads: " << num_threads << " | Time: " << duration
              << " ms\n";
  }
}

void lock_free_mixed_worker_all_delete(LockFreeList<int> &list, int thread_id) {
  int base = thread_id * NUM_OPERATIONS;
  for (int i = 0; i < NUM_OPERATIONS; ++i) {
    if (i % 2 == 0) {
      list.insert(base + i);
    } else {
      for (int attempt = 0; attempt < 3; ++attempt) {
        if (list.remove(base + i - 1))
          break;
        this_thread::sleep_for(chrono::milliseconds(1 << attempt));
      }
    }
  }
}

void lock_free_insert_worker(LockFreeList<int> &list, int start) {
  for (int i = start; i < NUM_OPERATIONS; ++i) {
    list.insert(i);
  }
}

// Benchmark for LockFreeList
void benchmark_lock_free() {
  std::cout << "Benchmarking LockFreeList insert only\n";

  for (int num_threads = 1; num_threads <= MAX_THREADS; num_threads *= 2) {
    LockFreeList<int> list;

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
      threads.emplace_back(lock_free_insert_worker, std::ref(list), i);
    }

    for (auto &t : threads) {
      t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time)
                        .count();

    std::cout << "Threads: " << num_threads << " | Time: " << duration
              << " ms\n";
  }

  std::cout << "Benchmarking LockFreeList mixed\n";

  for (int num_threads = 1; num_threads <= MAX_THREADS; num_threads *= 2) {
    LockFreeList<int> list;

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
      threads.emplace_back(lock_free_mixed_worker_all_delete, std::ref(list),
                           i);
    }

    for (auto &t : threads) {
      t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time)
                        .count();

    std::cout << "Threads: " << num_threads << " | Time: " << duration
              << " ms\n";
  }
}

int main() {
  benchmark_coarse_grain();
  benchmark_lock_free();
  return 0;
}