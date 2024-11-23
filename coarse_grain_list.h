#ifndef COARSE_GRAIN_LIST_H
#define COARSE_GRAIN_LIST_H

#include <mutex>
#include <iostream>
#include <memory>
using namespace std;

template <typename T>
struct CoarseGrainNode {
  T key;
  std::shared_ptr<CoarseGrainNode<T>> next;
  CoarseGrainNode(const T &key) : key(key), next(nullptr) {}
  CoarseGrainNode() : next(nullptr) {}
};

template <typename T>
class CoarseGrainList {
private:
  // sentinel nodes
  shared_ptr<CoarseGrainNode<T>> head;
  shared_ptr<CoarseGrainNode<T>> tail;
  mutable mutex list_mutex;

public:
  CoarseGrainList() {
    head = make_shared<CoarseGrainNode<T>>();
    tail = make_shared<CoarseGrainNode<T>>();
    head->next = tail;
  }

  ~CoarseGrainList() {
    head->next = nullptr;
  }

  CoarseGrainNode<T>* get_head() {
    lock_guard<mutex> lock(list_mutex);
    return head.get();
  }

  CoarseGrainNode<T>* get_tail() {
    lock_guard<mutex> lock(list_mutex);
    return tail.get();
  }

  CoarseGrainNode<T>* get_front() {
    lock_guard<mutex> lock(list_mutex);
    return head->next.get();
  }

  CoarseGrainNode<T>* get_next(CoarseGrainNode<T>* current) {
    lock_guard<mutex> lock(list_mutex);
    return current->next.get();
  }

  bool insert(const T key) {
    lock_guard<mutex> lock(list_mutex);
    shared_ptr<CoarseGrainNode<T>> new_node = make_shared<CoarseGrainNode<T>>(key);
    auto current = head;

    while (current->next != tail && current->next->key < key) {
      current = current->next;
    }

    // check for duplicates
    if (current->next != tail && current->next->key == key) {
      return false;
    }

    new_node->next = current->next;
    current->next = new_node;

    return true;
  }

  bool remove(const T key) {
    lock_guard<mutex> lock(list_mutex);
    auto current = head;

    while (current->next != tail && current->next->key < key) {
      current = current->next;
    }

    if (current->next != tail && current->next->key == key) {
      current->next = current->next->next;
      return true;
    }

    return false;
  }

  bool find(const T search_key) {
    lock_guard<mutex> lock(list_mutex);
    auto current = head->next;

    while (current != tail) {
      if (current->key == search_key) {
        return true;
      }
      current = current->next;
    }

    return false;
  }

  void print_list() {
    auto current = head->next;

    while (current != tail) {
      cout << current->key << " -> ";
      current = current->next;
    }
    cout << "NULL\n";
  }
};

#endif // COARSE_GRAIN_LIST_H
