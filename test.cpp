#include "lock_free_list.h"

/**
 * @brief A simpler test case for the lock-free list where operations are done
 * sequentially
 *
 * This test is just to make sure that the code can run and operations can be
 * performed in a simple setting. After all the operations, the list should
 * contain 5, 20, 25.
 *
 * @return int 0 if the test passes, -1 otherwise
 */
int test_sequential() {
  int ret = 0;
  LockFreeList<int> list;

  list.insert(10);
  list.insert(20);
  list.insert(15);

  list.remove(15);

  list.insert(25);
  list.insert(5);

  list.remove(10);

  list.print_list();
  // list should contain 5, 20, 25
  Node<int> *curr = list.get_front();
  if (curr->key != 5) {
    cout << "First element is " << curr->key << " while it should be 5\n";
    ret = -1;
  }
  curr = list.get_next(curr);
  if (curr->key != 20) {
    cout << "Second element is " << curr->key << " while it should be 20\n";
    ret = -1;
  }
  curr = list.get_next(curr);
  if (curr->key != 25) {
    cout << "Third element is " << curr->key << " while it should be 25\n";
    ret = -1;
  }

  return ret;
}

/**
 * @brief Number of operations to be performed by each worker
 */
const int NUM_OPERATIONS = 100;

/**
 * @brief Worker function to insert elements into the list
 *
 * @param list LockFreeList object
 * @param start Start index for the worker
 * @param end End index for the worker
 */
void insert_worker(LockFreeList<int> &list, int start, int end) {
  for (int i = start; i < end; ++i) {
    list.insert(i);
  }
}

/**
 * @brief Worker function to remove elements from the list with exponential
 * backoff
 *
 * @note Yielding wouldn't work here because we'd just be yielding to a
 * different thread that's also spinning
 * @param list LockFreeList object
 * @param start Start index for the worker
 * @param end End index for the worker
 */
void remove_worker(LockFreeList<int> &list, int start, int end) {
  for (int i = start; i < end; ++i) {
    for (int attempt = 0; attempt < 3; ++attempt) {
      if (list.remove(i))
        break;
      this_thread::sleep_for(chrono::milliseconds(1 << attempt));
    }
  }
}

/**
 * @brief Worker function to insert and remove elements from the list
 *
 * We insert even numbers and try to remove odd numbers. This is to test that
 * remove() calls that don't actually remove anything don't cause any issues.
 *
 * @param list LockFreeList object
 * @param id Thread ID
 */
void mixed_worker_no_delete(LockFreeList<int> &list, int id) {
  for (int i = 0; i < NUM_OPERATIONS; ++i) {
    if (i % 2 == 0) {
      list.insert(i + id * NUM_OPERATIONS);
    } else {
      for (int attempt = 0; attempt < 1; ++attempt) {
        if (list.remove(i))
          break;
        this_thread::sleep_for(chrono::milliseconds(1 << attempt));
      }
    }
  }
}

/**
 * @brief Worker function to insert and remove elements from the list
 *
 * We insert even numbers and each thread tries to remove them. This is to test
 * that remove() calls that actually remove something don't cause any issues. We
 * check that the list is empty at the end.
 *
 * @param list LockFreeList object
 * @param thread_id Thread ID
 */
void mixed_worker_all_delete(LockFreeList<int> &list, int thread_id) {
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

void aba_all_delete(LockFreeList<int> &list) {
  for (int i = 0; i < NUM_OPERATIONS; ++i) {
    if (i % 2 == 0) {
      list.insert(i);
    } else {
      for (int attempt = 0; attempt < 3; ++attempt) {
        if (list.remove(i-1)) break;
        this_thread::sleep_for(chrono::milliseconds(1 << attempt));
      }
    }
  }
}

/**
 * @brief Helper function to check that all the elements are in the list
 * are reomved (can't be found).
 *
 * @param list LockFreeList object
 * @return true If the list contains all the elements, false otherwise
 */
bool check_separate_workers(LockFreeList<int> &list) {
  for (int i = 0; i < NUM_OPERATIONS; ++i) {
    if (list.find(i)) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Helper function to check that the list contains the expected elements (even numbers)
 * after mixed operations without actual deletions. We also check that the length of the list
 * is as expected.
 *
 * @param list LockFreeList object
 * @param num_threads Number of threads
 * @return true If the list contains the expected elements, false otherwise
 */
bool check_mixed_worker_no_delete(LockFreeList<int> &list, int num_threads) {
  Node<int> *curr = list.get_front();
  for (int i = 0; i < NUM_OPERATIONS * num_threads; i += 2) {
    // even number should be in the list
    if (curr->key != i) {
      cout << "Expected " << i << " but got " << curr->key << endl;
      return false;
    }
    // odd numbers should be deleted
    if (list.find(i + 1)) {
      cout << "Expected " << i << " to be deleted but it's still in the list\n";
      return false;
    }
    curr = list.get_next(curr);
  }

  // make sure the list is not longer than expected
  if (curr != list.get_tail()) {
    cout << "List is longer than expected\n";
    return false;
  }

  return true;
}

/**
 * @brief Main test function for mixed operations
 *
 * This test creates multiple threads that insert and remove elements from the
 * list. The test checks that the list contains the expected elements after all
 * the operations.
 *
 * @return int 0 if the test passes, -1 otherwise
 */
int test_mixed() {
  int ret = 0;

  LockFreeList<int> list;

  int num_threads = 8;
  vector<thread> separate_work_threads;
  vector<thread> mixed_work_threads;

  cout << "---------- Testing separate but concurrent operations ----------\n";
  for (int i = 0; i < num_threads; ++i) {
    separate_work_threads.push_back(thread(insert_worker, ref(list),
                                           i * NUM_OPERATIONS,
                                           (i + 1) * NUM_OPERATIONS));
  }

  for (int i = 0; i < num_threads; ++i) {
    separate_work_threads.push_back(thread(remove_worker, ref(list),
                                           i * NUM_OPERATIONS,
                                           (i + 1) * NUM_OPERATIONS));
  }

  for (auto &t : separate_work_threads) {
    t.join();
  }

  cout << "State of the list after insertion and removal:\n";
  list.print_list();
  if (!check_separate_workers(list)) {
    cout << "Separate operations failed\n";
    ret = -1;
  } else {
    cout << "Separate operations passed\n";
  }

  cout << "---------- Testing mixed operations without actual deletions "
          "----------\n";
  for (int i = 0; i < num_threads; ++i) {
    mixed_work_threads.push_back(thread(mixed_worker_no_delete, ref(list), i));
  }

  for (auto &t : mixed_work_threads) {
    t.join();
  }

  cout
      << "State of the list after mixed operations without actual deletions:\n";
  list.print_list();
  if (!check_mixed_worker_no_delete(list, num_threads)) {
    cout << "Mixed operations without actual deletions failed\n";
    ret = -1;
  } else {
    cout << "Mixed operations without actual deletions passed\n";
  }

  cout << "---------- Testing mixed operations with all deletions ----------\n";
  mixed_work_threads.clear();
  for (int i = 0; i < num_threads; ++i) {
    mixed_work_threads.push_back(thread(mixed_worker_all_delete, ref(list), i));
  }
  for (auto &t : mixed_work_threads) {
    t.join();
  }
  cout << "State of the list after mixed operations with all deletions:\n";
  list.print_list();
  // check that the list is empty
  if (list.get_front() != list.get_tail()) {
    cout << "Mixed operations with all deletions failed\n";
    ret = -1;
  } else {
    cout << "Mixed operations with all deletions passed\n";
  }

  cout << "---------- Testing mixed operations with all deletions ----------\n";
  mixed_work_threads.clear();
  for (int i = 0; i < num_threads; ++i) {
    mixed_work_threads.push_back(thread(aba_all_delete, ref(list)));
  }
  for (auto &t : mixed_work_threads) {
    t.join();
  }
  cout << "State of the list after mixed operations with all deletions:\n";
  list.print_list();
  // check that the list is empty
  if (list.get_front() != list.get_tail()) {
    cout << "Mixed operations with all deletions failed\n";
    ret = -1;
  } else {
    cout << "Mixed operations with all deletions passed\n";
  }

  return ret;
}

/**
 * @brief Entry point. Run the tests and print the results.
 * 
 * @return int 0 if program finishes
 */
int main() {
  bool success = true;

  cout << "======================= Testing sequential operations "
          "=======================\n";
  if (test_sequential() != 0) {
    cout << "Test sequential failed\n";
    success = false;
  }
  if (success) {
    cout << "Sequential test passed\n";
  }

  cout << "======================= Testing mixed operations "
          "=======================\n";
  if (test_mixed() != 0) {
    cout << "Test mixed failed\n";
    success = false;
  }
  if (success) {
    cout << "Mixed test passed\n";
  }

  if (success) {
    cout << "All tests passed\n";
  }
  return 0;
}