/**
 * @file lock_free_list.h
 * @author Sihan Zhuang (sihanzhu)
 * @brief This file contains the implementation of a lock-free linked list. The
 * reason the actual implementation is in the header file is because the
 * implementation is templated.
 * @bug Should address the ABA problem for memory reuse if time permits
 */

#ifndef LOCK_FREE_LIST_H
#define LOCK_FREE_LIST_H

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
#include <sstream>
#include <iostream>
#include <assert.h>
#include <cstring>
using namespace std;

#define DEBUG

#ifdef DEBUG
#define ASSERT(x) assert(x)
#else
#define ASSERT(x)
#endif

template <typename T> class HazardPointer {
private:
  static constexpr int MAX_THREADS = 128;
  static constexpr int HP_PER_THREAD = 6;

  struct HPRec {
    atomic<thread::id> thread_id;
    array<atomic<T *>, HP_PER_THREAD> hp;

    HPRec() : thread_id(thread::id()) {
      for (auto &h : hp)
        h.store(nullptr);
    }
  };

  array<HPRec, MAX_THREADS> hp_list;

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

  void retire_node(T *ptr) {
    static thread_local vector<T *> retired_list;
    retired_list.push_back(ptr);

    // Scan and free nodes that are safe to delete
    vector<T *> new_retired_list;
    for (auto node : retired_list) {
      if (!is_protected(node)) {
        // clear the memory before deleting
        node->deleted.store(true);
        delete node;
        // printf("Node %p deleted\n", node);
      } else {
        new_retired_list.push_back(node);
      }
    }
    retired_list = move(new_retired_list);
  }
};

template <typename KeyType> struct Node {
  KeyType key;
  atomic<Node *> next;
  /* marked is used to indicate that the node is logically deleted, this
   * makes sure that other threads don't try to access the node before we have a
   * chance to physically delete them */
  /* TODO: extra memory usage, could use the last bit of next pointer instead
    because the Node structure is more than 1 byte aligned */
  atomic<bool> marked;
  atomic<bool> deleted;
  Node(KeyType key) : key(key), next(nullptr), marked(false), deleted(false) {}

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
  Node<KeyType> *head;
  Node<KeyType> *tail;
  static HazardPointer<Node<KeyType>> hp_manager;

  Node<KeyType> *search(const KeyType key, Node<KeyType> **left_node);

public:
  /**
   * @brief Construct a new Lock Free List object
   */
  LockFreeList() {
    // use default constructor for KeyType
    head = new Node<KeyType>(KeyType{});
    tail = new Node<KeyType>(KeyType{});
    // printf("Head address: %p\n", head);
    // printf("Tail address: %p\n", tail);
    head->next.store(tail);
  }

  /**
   * @brief Destroy the Lock Free List object
   */
  ~LockFreeList() {
    Node<KeyType> *curr = head;
    while (curr != nullptr) {
      Node<KeyType> *next = curr->next.load();
      hp_manager.retire_node(curr);
      curr = next;
    }
  }

  bool get_marked(Node<KeyType> *node) { return node->marked.load(); }
  KeyType get_key(Node<KeyType> *node) { return node->key; }
  Node<KeyType> *get_next(Node<KeyType> *node) { return node->next.load(); }

  /**
   * @brief Helper function to get the head of the list. The head is a sentinel
   * node
   *
   * @return Node<KeyType>* The head of the list
   */
  Node<KeyType> *get_head() { return head; }

  /**
   * @brief Helper function to get the front of the list. The front is the first
   * node after the head
   *
   * @return Node<KeyType>* The front of the list
   */
  Node<KeyType> *get_front() { return head->next.load(); }

  /**
   * @brief Helper function to get the tail of the list. The tail is a sentinel
   * node
   *
   * @return Node<KeyType>* The tail of the list
   */
  Node<KeyType> *get_tail() { return tail; }

  bool insert(const KeyType key);
  bool remove(const KeyType key);
  bool find(const KeyType search_key);
  void print_list();
  bool addr_valid(Node<KeyType> *node) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(node);
    uintptr_t addr_high = addr >> 44;
    return ((addr & 0xf) == 0) && addr_high >= 0x5 && addr_high <= 0x7;
  }
};

template <typename KeyType>
/**
 * @brief Search for a spot to insert the key
 *
 * @param key Key to be inserted
 * @param left_node Pointer to be modified to point to the node before the
 * key
 * @note compare_exchange_weak is not used because although it's documented that
 * it's faster than spinning on compare_exchange_strong, the amount of extra
 * work involved in each iteration is not minimal
 * @return Node<KeyType>* The node to the right where the key should be inserted
 */
Node<KeyType> *LockFreeList<KeyType>::search(const KeyType key,
                                             Node<KeyType> **left_node) {
  retry:
  std::ostringstream oss;
  oss << std::hex << std::this_thread::get_id();
  string thread_id = oss.str();
  // hp_manager.clear(0);
  // hp_manager.clear(1);
  // hp_manager.clear(2);

  Node<KeyType> *left_node_next;
  Node<KeyType> *right_node;
  // printf("thread %s ------ Searching for %d\n",thread_id.c_str(), key);
  while (true) {
    Node<KeyType> *t = head;
    // printf("search %d: thread_id %s: stop protecting initial t %p\n", key, thread_id.c_str(), hp_manager.get_protected(0));
    hp_manager.protect(t, 0);
    // printf("search %d: thread_id %s: now protecting initial t %p\n", key, thread_id.c_str(), t);
    Node<KeyType> *t_next = head->next.load();
    // printf("search %d: thread id: %s: initial t_next %p\n", key, thread_id.c_str(), t_next);
    hp_manager.protect(t_next, 1);
    // printf("search %d: thread id: %s: now protecting initial t_next %p\n", key, thread_id.c_str(), t_next);
    if (t->next.load() != t_next || t->is_deleted()) {
      goto retry;
    }

    // 1. Find left_node and right_node (right node might be marked)
    do {
      // printf("search %d: thread id: %s: start: current node address: %p, next node address: %p\n", key, thread_id.c_str(), t, t_next);
      ASSERT(hp_manager.is_protected(t));
      ASSERT(hp_manager.is_protected(t_next));
      /* unlike the paper, where the references are stored in the next
       * pointer, we already have a dedicated field for mark checking */
      if (!t->is_marked()) {
        ASSERT(hp_manager.is_protected(t));
        *left_node = t;
        // printf("search %d: thread_id %s: stop protecting left_node at index 3 %p\n", key, thread_id.c_str(), hp_manager.get_protected(2));
        hp_manager.protect(*left_node, 3);
        // printf("search %d: thread_id %s: now protecting left_node at index 3 %p\n", key, thread_id.c_str(), *left_node);
        left_node_next = t_next;
      }

      ASSERT(hp_manager.is_protected(t_next));

      // printf("search %d: thread id: %s: before update t: current node address: %p, next node address: %p\n", key, thread_id.c_str(), t, t_next);
      // NOTE: unlike the paper, we don't need get_marked_reference()
      // because the pointer is not changed in this implementation
      t = t_next;
      // printf("search %d: thread id: %s: after update t: current node address: %p, next node address: %p\n", key, thread_id.c_str(), t, t_next);
      if (t == tail) {
        break;
      }
      // t_next updated after the break statement because if we break here,
      // we no longer need to update left_node_next
      if (!addr_valid(t)) {
        // printf("search %d: t bad addr %p\n", key, t);
        std::ostringstream oss;
        oss << hex << std::this_thread::get_id(); // Convert thread ID to string
        // printf("search %d: thread id: %s: t node address: %p\n", key, oss.str().c_str(), t);
      }
      // printf("search %d: thread id: %s: before loading t->next: current node address: %p, next node address: %p\n", key, thread_id.c_str(), t, t_next);

      ASSERT(hp_manager.is_protected(t));
      t_next = t->next.load();
      // NOTE: end of potential race, t->next gets physically deleted after we load
      // printf("search %d: thread_id %s: stop protecting t_next at index 2 %p\n", key, thread_id.c_str(), hp_manager.get_protected(2));
      hp_manager.protect(t_next, 2);
      if (t->next.load() != t_next || t->is_deleted()) {
        // printf("search %d: current node changed, restarting search\n", key);
        goto retry;
      }
      // printf("search %d: thread_id %s: now protecting t_next at index 2 %p\n", key, thread_id.c_str(), t_next);
      // printf("search %d: thread id: %s: after loading t->next: current node address: %p, next node address: %p\n", key, thread_id.c_str(), t, t_next);
      if (!addr_valid(t_next)) {
        // printf("search %d: t_next bad addr %p retrieved from t %p\n", key, t_next, t);
        std::ostringstream oss;
        oss << hex << std::this_thread::get_id(); // Convert thread ID to string
        // printf("search %d: thread id: %s: t_next node address: %p\n", key, oss.str().c_str(), t_next);
      }

      // rotate the hazard pointers
      // printf("search %d: thread id: %s: stop protecting t %p\n", key, thread_id.c_str(), hp_manager.get_protected(0));
      hp_manager.protect(t, 0);
      // printf("search %d: thread id: %s: now protecting t %p\n", key, thread_id.c_str(), t);
      // printf("search %d: thread id: %s: stop protecting t_next %p\n", key, thread_id.c_str(), hp_manager.get_protected(1));
      hp_manager.protect(t_next, 1);
      // printf("search %d: thread id: %s: now protecting t_next %p\n", key, thread_id.c_str(), t_next);

      ASSERT(hp_manager.is_protected(t));
      ASSERT(hp_manager.is_protected(t_next));

      // keep looping until we find a right node that is not marked
    } while (t->is_marked() || t->key < key);

    right_node = t;
    ASSERT(hp_manager.is_protected(right_node));
    // printf("search %d: candidate right node address: %p\n", key, right_node);

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
      if (!addr_valid(right_node)) {
        // printf("search %d: right node bad addr %p\n", key, right_node);
        std::ostringstream oss;
        oss << hex << std::this_thread::get_id(); // Convert thread ID to string
        // printf("search %d: thread id: %s: right node address: %p\n", key, oss.str().c_str(), right_node);
      }
      if (right_node != tail && right_node->is_marked()) {
        continue;
      } else {
        return right_node;
      }
    }
  }
}

template <typename KeyType>
/**
 * @brief Insert a key into the list sorted by key
 *
 * @param key Key to be inserted
 * @return true If the key is successfully inserted, false otherwise
 */
bool LockFreeList<KeyType>::insert(const KeyType key) {
  // printf("------- Inserting %d\n", key);
  Node<KeyType> *new_node = new Node<KeyType>(key);
  // printf("insert %d: allocated new node address: %p\n", key, new_node);
  // printf("insert %d: stop protecting %p\n", key, hp_manager.get_protected(4));
  hp_manager.protect(new_node, 4);
  // printf("insert %d: now protecting %p\n", key, new_node);
  Node<KeyType> *left_node, *right_node;

  while (true) {
    right_node = search(key, &left_node);
    // printf("insert %d: right node address: %p\n", key, right_node);
    ASSERT(hp_manager.is_protected(right_node));

    // duplicate key, release allocated memory
    if (right_node != tail && right_node->key == key) {
      return false;
    }

    new_node->next.store(right_node);

    ASSERT(hp_manager.is_protected(left_node));
    ASSERT(hp_manager.is_protected(right_node));
    ASSERT(hp_manager.is_protected(new_node));

    // loop back until we get a chance to insert the new node
    if (left_node->next.compare_exchange_strong(right_node, new_node)) {
      // printf("insert %d: inserted node address: %p\n", key, new_node);
      if (!addr_valid(new_node)) {
        // printf("insert %d: new node bad addr %p\n", key, new_node);
        std::ostringstream oss;
        oss << hex << std::this_thread::get_id(); // Convert thread ID to string
        // printf("insert %d: thread id: %s: new node address: %p\n", key, oss.str().c_str(), new_node);
      }
      if (!addr_valid(right_node)) {
        // printf("insert %d: right node bad addr %p\n", key, right_node);
        std::ostringstream oss;
        oss << hex << std::this_thread::get_id(); // Convert thread ID to string
        // printf("insert %d: thread id: %s: right node address: %p\n", key, oss.str().c_str(), right_node);
      }
      return true;
    }
  }
}

template <typename KeyType>
/**
 * @brief Remove a key from the list
 *
 * @param search_key Key to be removed
 * @return true If the key is successfully removed, false if the key is not
 * found
 */
bool LockFreeList<KeyType>::remove(const KeyType search_key) {
  Node<KeyType> *right_node, *right_node_next, *left_node;
  // printf("------- Removing %d\n", search_key);

  while (true) {
    right_node = search(search_key, &left_node);

    ASSERT(hp_manager.is_protected(right_node));
    // if the key is not found, return false
    if (right_node == tail || right_node->key != search_key) {
      return false;
    }


    right_node_next = right_node->next.load();
    // printf("remove %d: stop protecting %p\n", search_key, hp_manager.get_protected(5));
    hp_manager.protect(right_node_next, 5);
    if (right_node->next.load() != right_node_next) {
      // printf("remove %d: right node next changed, restarting search\n", search_key);
      continue;
    }
    // printf("remove %d: now protecting %p\n", search_key, right_node_next);
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
    // printf("remove %d: retiring node address: %p, key: %d\n", search_key, right_node, right_node->key);
    // printf("remove %d: after removal: left node %p -> right node %p\n", search_key, left_node, right_node_next);
    if (!addr_valid(right_node_next)) {
      // printf("remove %d: right node next bad addr %p\n", search_key, right_node_next);
      std::ostringstream oss;
      oss << hex << std::this_thread::get_id(); // Convert thread ID to string
      // printf("remove %d: thread id: %s: right node next address: %p\n", search_key, oss.str().c_str(), right_node_next);
    }
    hp_manager.retire_node(right_node);
  }

  // hp_manager.clear(0);
  // hp_manager.clear(1);
  // hp_manager.clear(2);
  return true;
}

template <typename KeyType>
/**
 * @brief Find a key in the list
 *
 * @param search_key Key to be searched
 * @return true If the key is found, false otherwise
 */
bool LockFreeList<KeyType>::find(const KeyType search_key) {
  Node<KeyType> *right_node, *left_node;
  right_node = search(search_key, &left_node);
  bool result = right_node != tail && right_node->key == search_key;
  return result;
}

template <typename KeyType>
/**
 * @brief A helper function to print the list after operations
 * @note Not thread-safe
 */
void LockFreeList<KeyType>::print_list() {
  Node<KeyType> *current = get_front();
  while (current != tail) {
    if (!current->is_marked()) {
      std::cout << current->key << " -> ";
    }
    current = current->next.load();
  }
  std::cout << "NULL\n";
}


template <typename KeyType>
HazardPointer<Node<KeyType>> LockFreeList<KeyType>::hp_manager;

#endif // LOCK_FREE_LIST_H