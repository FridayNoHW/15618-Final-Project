#include "coarse_grain_list.h"
#include "lock_free_list.h"
#include "lock_free_list_no_reclaim.h"
#include <thread>
#include <vector>
#include <fstream>
#include <sstream>

std::ofstream result_file("benchmark_results.txt");

void log_result(const std::string &test_type, int threads, int duration) {
  if (result_file.is_open()) {
    result_file << test_type << "," << threads << "," << duration << "\n";
  }
}

static const int NUM_OPERATIONS = 150;
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
    log_result("CoarseGrainList_insert", num_threads, duration);
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
    log_result("CoarseGrainList_mixed", num_threads, duration);
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
    log_result("LockFreeList_insert", num_threads, duration);
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
    log_result("LockFreeList_mixed", num_threads, duration);
  }
}

void lock_free_no_reclaim_mixed_worker_all_delete(LockFreeListNoReclaim<int> &list, int thread_id) {
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

void lock_free_no_reclaim_insert_worker(LockFreeListNoReclaim<int> &list, int start) {
  for (int i = start; i < NUM_OPERATIONS; ++i) {
    list.insert(i);
  }
}

void benchmark_lock_free_no_reclaim() {
  std::cout << "Benchmarking LockFreeListNoReclaim insert only\n";

  for (int num_threads = 1; num_threads <= MAX_THREADS; num_threads *= 2) {
    LockFreeListNoReclaim<int> list;

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
      threads.emplace_back(lock_free_no_reclaim_insert_worker, std::ref(list), i);
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
    log_result("LockFreeListNoReclaim_insert", num_threads, duration);
  }

  std::cout << "Benchmarking LockFreeListNoReclaim mixed\n";

  for (int num_threads = 1; num_threads <= MAX_THREADS; num_threads *= 2) {
    LockFreeListNoReclaim<int> list;

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
      threads.emplace_back(lock_free_no_reclaim_mixed_worker_all_delete, std::ref(list),
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
    log_result("LockFreeListNoReclaim_mixed", num_threads, duration);
  }
}

int main() {
  benchmark_lock_free();
  benchmark_coarse_grain();
  benchmark_lock_free_no_reclaim();
  return 0;
}