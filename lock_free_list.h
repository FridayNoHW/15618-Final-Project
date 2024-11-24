/**
 * @file lock_free_list.h
 * @author Sihan Zhuang (sihanzhu)
 * @brief This file contains the implementation of a lock-free linked list. The
 * reason the actual implementation is in the header file is because the
 * implementation is templated.
 * @bug ABA problem is not addressed in this implementation. For example, if we
 * load the next pointer of a node, and then the node is deleted and a new node
 * is inserted in its place, the next pointer will still be pointing to the old
 * node. By the time we dereference the next pointer, the memory region might
 * have been reused for a different node, ultimately leading to a segmentation
 * fault.
 */

#ifndef LOCK_FREE_LIST_H
#define LOCK_FREE_LIST_H

#include <assert.h>
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
using namespace std;

// #define DEBUG
// clear all the assertions if not in debug mode
#ifdef DEBUG
#define ASSERT(x) assert(x)
#else
#define ASSERT(x)
#endif

/**
 * @brief A class to manage hazard pointers for lock-free data structures
 *
 * We use an array of hazard pointers for each thread. Each thread can have
 * up to HP_PER_THREAD hazard pointers. Pointers stored in the array are
 * protected from being deleted. When a thread no longer need the reference to a
 * node, we can safely replace the pointer (stored in a specific index) with the
 * next one. The hazard pointers are used to protect nodes from being deleted
 * while they are still being accessed by other threads.
 *
 * @tparam T Type of the data structure
 */
template <typename T> class HazardPointer {
private:
  static constexpr int MAX_THREADS = 256;
  static constexpr int HP_PER_THREAD = 5;

  struct HPRec {
    atomic<thread::id> thread_id;
    array<atomic<T *>, HP_PER_THREAD> hp;

    HPRec() : thread_id(thread::id()) {
      for (auto &h : hp)
        h.store(nullptr);
    }
  };

  array<HPRec, MAX_THREADS> hp_list;

  /**
   * @brief Function to claim a hazard pointer record (spot in the array)
   *
   * @return HPRec* Pointer to the hazard pointer record
   */
  HPRec *acquire_hp_rec() {
    auto tid = this_thread::get_id();
    for (auto &rec : hp_list) {
      thread::id empty;
      if (rec.thread_id.compare_exchange_strong(empty, tid)) {
        return &rec;
      }
      if (rec.thread_id.load() == tid) {
        return &rec;
      }
    }
    throw runtime_error("No available hazard pointer records");
  }

public:
  /* below are helper functions to get/set states in the array */
  T *get_protected(int hp_index) {
    auto rec = acquire_hp_rec();
    return rec->hp[hp_index].load();
  }

  void protect(T *ptr, int hp_index) {
    auto rec = acquire_hp_rec();
    rec->hp[hp_index].store(ptr);
  }

  void clear(int hp_index) {
    auto rec = acquire_hp_rec();
    rec->hp[hp_index].store(nullptr);
  }

  bool is_protected(T *ptr) {
    for (const auto &rec : hp_list) {
      // record is not empty
      if (rec.thread_id.load() != thread::id()) {
        for (const auto &hp : rec.hp) {
          // found a match, the ptr is protected
          if (hp.load() == ptr)
            return true;
        }
      }
    }
    return false;
  }

  /**
   * @brief Retire a node and free the memory if possible
   *
   * @param ptr Pointer to the node to be retired
   */
  void retire_node(T *ptr) {
    const int DELETION_THRESHOLD = 50;

    static thread_local vector<T *> retired_list;
    static thread_local int retired_count = 0;

    retired_list.push_back(ptr);
    retired_count++;

    // Scan and free nodes that are safe to delete
    if (retired_count >= DELETION_THRESHOLD) {
      vector<T *> new_retired_list;
      for (auto node : retired_list) {
        if (!is_protected(node)) {
          // mark the memory before deleting
          node->deleted.store(true);
          delete node;
          retired_count--;
        } else {
          new_retired_list.push_back(node);
        }
      }
      retired_list = move(new_retired_list);
    }
  }
};

template <typename KeyType> struct LockFreeNode {
  KeyType key;
  atomic<LockFreeNode *> next;
  /* marked is used to indicate that the node is logically deleted, this
   * makes sure that other threads don't try to access the node before we have a
   * chance to physically delete them */
  /* TODO: extra memory usage, could use the last bit of next pointer instead
    because the LockFreeNode structure is more than 1 byte aligned */
  atomic<bool> marked;
  atomic<bool> deleted;
  LockFreeNode(KeyType key)
      : key(key), next(nullptr), marked(false), deleted(false) {}

  /**
   * @brief Helper function to check if the node is marked for deletion
   * @note The load() function is used to make sure that the value is read
   * with memory fence
   * @return Whether the node is marked for deletion
   */
  bool is_marked() { return marked.load(); }

  bool is_deleted() { return deleted.load(); }
};

template <typename KeyType> class LockFreeList {
private:
  LockFreeNode<KeyType> *head;
  LockFreeNode<KeyType> *tail;
  static HazardPointer<LockFreeNode<KeyType>> hp_manager;

  LockFreeNode<KeyType> *search(const KeyType key,
                                LockFreeNode<KeyType> **left_node);

public:
  /**
   * @brief Construct a new Lock Free List object
   */
  LockFreeList() {
    // use default constructor for KeyType
    head = new LockFreeNode<KeyType>(KeyType{});
    tail = new LockFreeNode<KeyType>(KeyType{});
    head->next.store(tail);
  }

  /**
   * @brief Destroy the Lock Free List object
   */
  ~LockFreeList() {
    LockFreeNode<KeyType> *curr = head;
    while (curr != nullptr) {
      LockFreeNode<KeyType> *next = curr->next.load();
      hp_manager.retire_node(curr);
      curr = next;
    }
  }

  bool get_marked(LockFreeNode<KeyType> *node) { return node->marked.load(); }
  KeyType get_key(LockFreeNode<KeyType> *node) { return node->key; }
  LockFreeNode<KeyType> *get_next(LockFreeNode<KeyType> *node) {
    return node->next.load();
  }

  /**
   * @brief Helper function to get the head of the list. The head is a sentinel
   * node
   *
   * @return LockFreeNode<KeyType>* The head of the list
   */
  LockFreeNode<KeyType> *get_head() { return head; }

  /**
   * @brief Helper function to get the front of the list. The front is the first
   * node after the head
   *
   * @return LockFreeNode<KeyType>* The front of the list
   */
  LockFreeNode<KeyType> *get_front() { return head->next.load(); }

  /**
   * @brief Helper function to get the tail of the list. The tail is a sentinel
   * node
   *
   * @return LockFreeNode<KeyType>* The tail of the list
   */
  LockFreeNode<KeyType> *get_tail() { return tail; }

  bool insert(const KeyType key);
  bool remove(const KeyType key);
  bool find(const KeyType search_key);
  void print_list();
  bool addr_valid(LockFreeNode<KeyType> *node) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(node);
    uintptr_t addr_high = addr >> 44;
    return ((addr & 0xf) == 0) && addr_high >= 0x5 && addr_high <= 0x7;
  }
};

/**
 * @brief Search for a spot to insert the key
 *
 * @param key Key to be inserted
 * @param left_node Pointer to be modified to point to the node before the
 * key
 * @note compare_exchange_weak is not used because although it's documented that
 * it's faster than spinning on compare_exchange_strong, the amount of extra
 * work involved in each iteration is not minimal
 * @return LockFreeNode<KeyType>* The node to the right where the key should be
 * inserted
 */
template <typename KeyType>
LockFreeNode<KeyType> *
LockFreeList<KeyType>::search(const KeyType key,
                              LockFreeNode<KeyType> **left_node) {
retry:
  // hp_manager.clear(0);
  // hp_manager.clear(1);
  // hp_manager.clear(2);

  LockFreeNode<KeyType> *left_node_next;
  LockFreeNode<KeyType> *right_node;
  while (true) {
    LockFreeNode<KeyType> *t = head;
    hp_manager.protect(t, 0);
    LockFreeNode<KeyType> *t_next = head->next.load();
    hp_manager.protect(t_next, 1);
    if (t->next.load() != t_next || t->is_deleted() || t_next->is_deleted()) {
      goto retry;
    }

    // 1. Find left_node and right_node (right node might be marked)
    do {
      ASSERT(hp_manager.is_protected(t));
      ASSERT(hp_manager.is_protected(t_next));
      /* unlike the paper, where the references are stored in the next
       * pointer, we already have a dedicated field for mark checking */
      if (!t->is_marked()) {
        ASSERT(hp_manager.is_protected(t));
        *left_node = t;
        hp_manager.protect(*left_node, 3);
        if (t->is_deleted()) {
          goto retry;
        }
        left_node_next = t_next;
      }

      // NOTE: unlike the paper, we don't need get_marked_reference()
      // because the pointer is not changed in this implementation
      t = t_next;
      if (t == tail) {
        break;
      }
      // t_next updated after the break statement because if we break here,
      // we no longer need to update left_node_next
      t_next = t->next.load();
      hp_manager.protect(t_next, 2);
      // NOTE: one of the potential races we are trying to solve here. t->next
      // could get physically deleted after we load
      if (t->next.load() != t_next || t->is_deleted() || t_next->is_deleted()) {
        goto retry;
      }

      // rotate the hazard pointers
      hp_manager.protect(t, 0);
      ASSERT(!t->is_deleted());
      hp_manager.protect(t_next, 1);
      ASSERT(!t_next->is_deleted());

      ASSERT(hp_manager.is_protected(t));
      ASSERT(hp_manager.is_protected(t_next));

      // keep looping until we find a right node that is not marked
    } while (t->is_marked() || t->key < key);

    right_node = t;
    ASSERT(hp_manager.is_protected(right_node));

    // 2. Check if the nodes are adjacent
    if (left_node_next == right_node) {
      // if right node is marked, search again
      if (right_node != tail && right_node->is_marked()) {
        continue;
      } else {
        // found the right node
        return right_node;
      }
    }

    // 3. Remove one or more marked nodes from left to right node
    // this is run many times until we find the right node
    ASSERT(hp_manager.is_protected(*left_node));
    ASSERT(hp_manager.is_protected(right_node));
    if ((*left_node)
            ->next.compare_exchange_strong(left_node_next, right_node)) {
      if (right_node != tail && right_node->is_marked()) {
        continue;
      } else {
        return right_node;
      }
    }
  }
}

/**
 * @brief Insert a key into the list sorted by key
 *
 * @param key Key to be inserted
 * @return true If the key is successfully inserted, false otherwise
 */
template <typename KeyType>
bool LockFreeList<KeyType>::insert(const KeyType key) {
  LockFreeNode<KeyType> *new_node = new LockFreeNode<KeyType>(key);
  LockFreeNode<KeyType> *left_node, *right_node;

  while (true) {
    right_node = search(key, &left_node);
    ASSERT(hp_manager.is_protected(right_node));

    // duplicate key, release allocated memory
    if (right_node != tail && right_node->key == key) {
      return false;
    }

    new_node->next.store(right_node);

    ASSERT(hp_manager.is_protected(left_node));
    ASSERT(hp_manager.is_protected(right_node));

    // loop back until we get a chance to insert the new node
    if (left_node->next.compare_exchange_strong(right_node, new_node)) {
      return true;
    }
  }
}

/**
 * @brief Remove a key from the list
 *
 * @param search_key Key to be removed
 * @return true If the key is successfully removed, false if the key is not
 * found
 */
template <typename KeyType>
bool LockFreeList<KeyType>::remove(const KeyType search_key) {
  LockFreeNode<KeyType> *right_node, *right_node_next, *left_node;

  while (true) {
    right_node = search(search_key, &left_node);

    ASSERT(hp_manager.is_protected(right_node));
    // if the key is not found, return false
    if (right_node == tail || right_node->key != search_key) {
      return false;
    }

    right_node_next = right_node->next.load();
    hp_manager.protect(right_node_next, 4);
    if (right_node->next.load() != right_node_next ||
        right_node->is_deleted()) {
      continue;
    }
    ASSERT(hp_manager.is_protected(right_node));
    bool expected = false;
    // Try to mark the node using compare_exchange
    if (right_node->marked.compare_exchange_strong(expected, true)) {
      break;
    }
  }
  ASSERT(hp_manager.is_protected(left_node));
  // physically remove the node if possible
  if (left_node->next.compare_exchange_strong(right_node, right_node_next)) {
    hp_manager.retire_node(right_node);
  }

  // hp_manager.clear(0);
  // hp_manager.clear(1);
  // hp_manager.clear(2);
  return true;
}

/**
 * @brief Find a key in the list
 *
 * @param search_key Key to be searched
 * @return true If the key is found, false otherwise
 */
template <typename KeyType>
bool LockFreeList<KeyType>::find(const KeyType search_key) {
  LockFreeNode<KeyType> *right_node, *left_node;
  right_node = search(search_key, &left_node);
  bool result = right_node != tail && right_node->key == search_key;
  return result;
}

/**
 * @brief A helper function to print the list after operations
 * @note Not thread-safe
 */
template <typename KeyType> void LockFreeList<KeyType>::print_list() {
  LockFreeNode<KeyType> *current = get_front();
  while (current != tail) {
    if (!current->is_marked()) {
      std::cout << current->key << " -> ";
    }
    current = current->next.load();
  }
  std::cout << "NULL\n";
}

template <typename KeyType>
HazardPointer<LockFreeNode<KeyType>> LockFreeList<KeyType>::hp_manager;

#endif // LOCK_FREE_LIST_H