/**
 * @file lock_free_list.h
 * @author Sihan Zhuang (sihanzhu)
 * @brief This file contains the implementation of a lock-free linked list. The
 * reason the actual implementation is in the header file is because the
 * implementation is templated.
 * @bug Should address the ABA problem if time permits
 */

#ifndef LOCK_FREE_LIST_H
#define LOCK_FREE_LIST_H

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
using namespace std;

#define TAG_BITS 3

template <typename KeyType> struct Node {
  KeyType key;
  atomic<Node *> next;
  /* is_deleted is used to indicate that the node is logically deleted, this
   * makes sure that other threads don't try to access the node before we have a
   * chance to physically delete them */
  /* TODO: extra memory usage, could use the last bit of next pointer instead
    because the Node structure is more than 1 byte aligned */
  atomic<bool> is_deleted;
  Node(KeyType key) : key(key), next(nullptr), is_deleted(false) {}

  /**
   * @brief Helper function to check if the node is marked for deletion
   * @note The load() function is used to make sure that the value is read
   * with memory fence
   * @return Whether the node is marked for deletion
   */
  bool is_marked() { return is_deleted.load(); }
};

template <typename KeyType>
Node<KeyType>* pack_ptr_with_tag(Node<KeyType>* ptr, uint32_t tag) {
  uintptr_t raw_ptr = reinterpret_cast<uintptr_t>(ptr);
  uintptr_t packed = (raw_ptr & ~((1 << TAG_BITS) - 1)) | (tag & ((1 << TAG_BITS) - 1));
  return reinterpret_cast<Node<KeyType>*>(packed);
}

template <typename KeyType>
Node<KeyType>* unpack_pointer(Node<KeyType>* packed_ptr) {
  uintptr_t raw_ptr = reinterpret_cast<uintptr_t>(packed_ptr);
  return reinterpret_cast<Node<KeyType>*>(raw_ptr & ~((1 << TAG_BITS) - 1));
}

template <typename KeyType>
uint32_t unpack_tag(Node<KeyType>* packed_ptr) {
  uintptr_t raw_ptr = reinterpret_cast<uintptr_t>(packed_ptr);
  return static_cast<uint32_t>(raw_ptr & ((1 << TAG_BITS) - 1));
}


template <typename KeyType> class LockFreeList {
private:
  Node<KeyType> *head;
  Node<KeyType> *tail;

  Node<KeyType> *search(const KeyType key, Node<KeyType> **left_node);

public:
  /**
   * @brief Construct a new Lock Free List object
   */
  LockFreeList() {
    // use default constructor for KeyType
    head = new Node<KeyType>(KeyType{});
    tail = new Node<KeyType>(KeyType{});
    head->next.store(tail);

    // make sure head and tail both have a tag of 0
    head = pack_ptr_with_tag(head, 0);
    tail = pack_ptr_with_tag(tail, 0);
  }

  /**
   * @brief Destroy the Lock Free List object
   */
  ~LockFreeList() {
    Node<KeyType> *curr = head;
    while (curr != nullptr) {
      Node<KeyType> *next = get_next(curr);
      delete unpack_pointer(curr);
      curr = next;
    }
  }

  Node<KeyType> *get_next(Node<KeyType> *node) { return unpack_pointer(node->next.load()); }

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
  Node<KeyType> *get_front() { return get_next(head); }

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
  Node<KeyType> *left_node_next;
  Node<KeyType> *right_node;

  while (true) {
    Node<KeyType> *t_packed = head;
    Node<KeyType> *t_next_packed = head->next.load();

    // 1. Find left_node and right_node (right node might be marked)
    do {
      Node<KeyType>* t_next = unpack_pointer(t_next_packed);

      /* unlike the paper, where the references are stored in the next
       * pointer, we already have a dedicated field for mark checking */
      if (!unpack_pointer(t_packed)->is_marked()) {
        *left_node = t_packed;
        left_node_next = t_next;
      }

      // NOTE: unlike the paper, we don't need get_marked_reference()
      // because the pointer is not changed in this implementation
      t_packed = t_next;

      if (t_packed == tail) {
        break;
      }

      // t_next updated after the break statement because if we break here,
      // we no longer need to update left_node_next
      t_next_packed = t_packed->next.load();

      // keep looping until we find a right node that is not marked
    } while (unpack_pointer(t_packed)->is_marked() || unpack_pointer(t_packed)->key < key);
    
    right_node = t_packed;

    printf("search: --------------------\n");
    printf("right_node is %p with tag %d\n", t_packed, unpack_tag(t_packed));
    // 2. Check if the nodes are adjacent, check should be done with tags
    if (left_node_next == right_node) {
      printf("right node is adjacent to left node\n");
      // if right node is marked, search again
      if (right_node != tail && unpack_pointer(right_node)->is_marked()) {
        printf("but right node is marked\n");
        continue;
      } else {
        // found the right node
        printf("right node is not marked\n");
        return right_node;
      }
    }

    // 3. Remove one or more marked nodes from left to right node
    // this is run many times until we find the right node
    Node<KeyType>* left_node_next_packed = pack_ptr_with_tag(left_node_next, unpack_tag(t_next_packed));
    Node<KeyType>* new_right_node = pack_ptr_with_tag(right_node, unpack_tag(left_node_next_packed) + 1);
    if ((*left_node)
            ->next.compare_exchange_strong(left_node_next_packed, new_right_node)) {

      // TODO: free the marked nodes
      // Node<KeyType> *curr = left_node_next;
      // while (curr != right_node) {
      //   Node<KeyType> *next = curr->next.load();
      //   delete curr;
      //   curr = next;
      // }

      if (right_node != tail && unpack_pointer(right_node)->is_marked()) {
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
  Node<KeyType> *new_node = new Node<KeyType>(key);
  Node<KeyType> *left_node, *right_node;

  while (true) {
    printf("insert: --------------------\n");
    // search returns a packed pointer
    right_node = search(key, &left_node);

    // duplicate key, release allocated memory
    if (right_node != tail && right_node->key == key) {
      // TODO:
      // delete new_node;
      printf("insert: key %d already exists\n", key);
      return false;
    }

    Node<KeyType>* right_node_packed = pack_ptr_with_tag(right_node, unpack_tag(right_node) + 1);
    new_node->next.store(right_node_packed);
    Node<KeyType>* new_node_packed = pack_ptr_with_tag(new_node, unpack_tag(right_node) + 1);
    printf("insert: left_node->next is %p with tag %d\n", unpack_pointer(left_node->next.load()), unpack_tag(left_node->next.load()));
    printf("insert: new_node is %p with tag %d\n", new_node, unpack_tag(new_node_packed));
    printf("insert: new_node->next is %p with tag %d\n", unpack_pointer(new_node->next.load()), unpack_tag(new_node->next.load()));
    printf("insert: right_node is %p with tag %d\n", right_node, unpack_tag(right_node));

    // loop back until we get a chance to insert the new node
    if (left_node->next.compare_exchange_strong(right_node_packed, new_node_packed)) {
      printf("inserted: left_node->next is %p with tag %d\n", unpack_pointer(left_node->next.load()), unpack_tag(left_node->next.load()));
      printf("inserted: new_node is %p with tag %d\n", new_node, unpack_tag(new_node_packed));
      printf("inserted: new_node->next is %p with tag %d\n", unpack_pointer(new_node->next.load()), unpack_tag(new_node->next.load()));
      printf("inserted: right_node is %p with tag %d\n", right_node, unpack_tag(right_node));
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

  while (true) {
    right_node = search(search_key, &left_node);
    // if the key is not found, return false
    if (right_node == tail || right_node->key != search_key) {
      printf("remove: key %d not found\n", search_key);
      return false;
    }

    right_node_next = right_node->next.load();
    bool expected = false;
    // Try to mark the node using compare_exchange
    printf("remove: --------------------\n");
    printf("remove: right_node->next is %p with tag %d\n", unpack_pointer(right_node_next), unpack_tag(right_node_next));
    printf("remove: right_node is %p with tag %d\n", right_node, unpack_tag(right_node));
    if (unpack_pointer(right_node)->is_deleted.compare_exchange_strong(expected, true)) {
      break;
    }
  }

  Node<KeyType>* desired_packed = pack_ptr_with_tag(unpack_pointer(right_node_next), unpack_tag(right_node) + 1);

  // physically remove the node if possible
  printf("remove: --------------------\n");
  printf("remove: left_node->next is %p with tag %d\n", unpack_pointer(left_node->next.load()), unpack_tag(left_node->next.load()));
  printf("remove: right_node is %p with tag %d\n", right_node, unpack_tag(right_node));
  printf("remove: right_node_next is %p with tag %d\n", unpack_pointer(right_node_next), unpack_tag(right_node_next));
  printf("remove: desired_packed is %p with tag %d\n", unpack_pointer(desired_packed), unpack_tag(desired_packed));
  if (left_node->next.compare_exchange_strong(right_node, right_node_next)) {
    // TODO:
    // delete right_node;
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
bool LockFreeList<KeyType>::find(const KeyType search_key) {
  Node<KeyType> *right_node, *left_node;
  right_node = search(search_key, &left_node);
  return right_node != tail && right_node->key == search_key;
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
    current = get_next(current);
  }
  std::cout << "NULL\n";
}

#endif // LOCK_FREE_LIST_H