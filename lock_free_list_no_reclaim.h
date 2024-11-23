/**
 * @file lock_free_list_no_reclaim.h
 * @author Sihan Zhuang (sihanzhu)
 * @brief This file contains the implementation of a lock-free linked list. The
 * reason the actual implementation is in the header file is because the
 * implementation is templated.
 * @bug Should address the ABA problem if time permits
 */

#ifndef LOCK_FREE_LIST_NO_RECLAIM_H
#define LOCK_FREE_LIST_NO_RECLAIM_H

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
using namespace std;

template <typename KeyType> struct LockFreeNoReclaimNode {
  KeyType key;
  atomic<LockFreeNoReclaimNode *> next;
  /* is_deleted is used to indicate that the node is logically deleted, this
   * makes sure that other threads don't try to access the node before we have a
   * chance to physically delete them */
  /* TODO: extra memory usage, could use the last bit of next pointer instead
    because the LockFreeNoReclaimNode structure is more than 1 byte aligned */
  atomic<bool> is_deleted;
  LockFreeNoReclaimNode(KeyType key) : key(key), next(nullptr), is_deleted(false) {}

  /**
   * @brief Helper function to check if the node is marked for deletion
   * @note The load() function is used to make sure that the value is read
   * with memory fence
   * @return Whether the node is marked for deletion
   */
  bool is_marked() { return is_deleted.load(); }
};

template <typename KeyType> class LockFreeListNoReclaim {
private:
  LockFreeNoReclaimNode<KeyType> *head;
  LockFreeNoReclaimNode<KeyType> *tail;

  LockFreeNoReclaimNode<KeyType> *search(const KeyType key, LockFreeNoReclaimNode<KeyType> **left_node);

public:
  /**
   * @brief Construct a new Lock Free List object
   */
  LockFreeListNoReclaim() {
    // use default constructor for KeyType
    head = new LockFreeNoReclaimNode<KeyType>(KeyType{});
    tail = new LockFreeNoReclaimNode<KeyType>(KeyType{});
    head->next.store(tail);
  }

  /**
   * @brief Destroy the Lock Free List object
   */
  ~LockFreeListNoReclaim() {
    LockFreeNoReclaimNode<KeyType> *curr = head;
    while (curr != nullptr) {
      LockFreeNoReclaimNode<KeyType> *next = curr->next.load();
      delete curr;
      curr = next;
    }
  }

  /**
   * @brief Helper function to get the head of the list. The head is a sentinel
   * node
   *
   * @return LockFreeNoReclaimNode<KeyType>* The head of the list
   */
  LockFreeNoReclaimNode<KeyType> *get_head() { return head; }

  /**
   * @brief Helper function to get the front of the list. The front is the first
   * node after the head
   *
   * @return LockFreeNoReclaimNode<KeyType>* The front of the list
   */
  LockFreeNoReclaimNode<KeyType> *get_front() { return head->next.load(); }

  /**
   * @brief Helper function to get the tail of the list. The tail is a sentinel
   * node
   *
   * @return LockFreeNoReclaimNode<KeyType>* The tail of the list
   */
  LockFreeNoReclaimNode<KeyType> *get_tail() { return tail; }

  bool insert(const KeyType key);
  bool remove(const KeyType key);
  bool find(const KeyType search_key);
  void print_list();
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
 * @return LockFreeNoReclaimNode<KeyType>* The node to the right where the key should be inserted
 */
LockFreeNoReclaimNode<KeyType> *LockFreeListNoReclaim<KeyType>::search(const KeyType key,
                                             LockFreeNoReclaimNode<KeyType> **left_node) {
  LockFreeNoReclaimNode<KeyType> *left_node_next;
  LockFreeNoReclaimNode<KeyType> *right_node;

  while (true) {
    LockFreeNoReclaimNode<KeyType> *t = head;
    LockFreeNoReclaimNode<KeyType> *t_next = head->next.load();

    // 1. Find left_node and right_node (right node might be marked)
    do {
      /* unlike the paper, where the references are stored in the next
       * pointer, we already have a dedicated field for mark checking */
      if (!t->is_marked()) {
        *left_node = t;
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

      // keep looping until we find a right node that is not marked
    } while (t->is_marked() || t->key < key);

    right_node = t;

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

template <typename KeyType>
/**
 * @brief Insert a key into the list sorted by key
 *
 * @param key Key to be inserted
 * @return true If the key is successfully inserted, false otherwise
 */
bool LockFreeListNoReclaim<KeyType>::insert(const KeyType key) {
  LockFreeNoReclaimNode<KeyType> *new_node = new LockFreeNoReclaimNode<KeyType>(key);
  LockFreeNoReclaimNode<KeyType> *left_node, *right_node;

  while (true) {
    right_node = search(key, &left_node);

    // duplicate key, release allocated memory
    if (right_node != tail && right_node->key == key) {
      return false;
    }

    new_node->next.store(right_node);

    // loop back until we get a chance to insert the new node
    if (left_node->next.compare_exchange_strong(right_node, new_node)) {
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
bool LockFreeListNoReclaim<KeyType>::remove(const KeyType search_key) {
  LockFreeNoReclaimNode<KeyType> *right_node, *right_node_next, *left_node;

  while (true) {
    right_node = search(search_key, &left_node);
    // if the key is not found, return false
    if (right_node == tail || right_node->key != search_key) {
      return false;
    }

    right_node_next = right_node->next.load();
    bool expected = false;
    // Try to mark the node using compare_exchange
    if (right_node->is_deleted.compare_exchange_strong(expected, true)) {
      break;
    }
  }

  // physically remove the node if possible
  if (left_node->next.compare_exchange_strong(right_node, right_node_next)) {
  }

  return true;
}

template <typename KeyType>
/**
 * @brief Find a key in the list
 *
 * @param search_key Key to be searched
 * @return true If the key is found, false otherwise
 */
bool LockFreeListNoReclaim<KeyType>::find(const KeyType search_key) {
  LockFreeNoReclaimNode<KeyType> *right_node, *left_node;
  right_node = search(search_key, &left_node);
  return right_node != tail && right_node->key == search_key;
}

template <typename KeyType>
/**
 * @brief A helper function to print the list after operations
 * @note Not thread-safe
 */
void LockFreeListNoReclaim<KeyType>::print_list() {
  LockFreeNoReclaimNode<KeyType> *current = get_front();
  while (current != tail) {
    if (!current->is_marked()) {
      std::cout << current->key << " -> ";
    }
    current = current->next.load();
  }
  std::cout << "NULL\n";
}

#endif // LOCK_FREE_LIST_NO_RECLAIM_H