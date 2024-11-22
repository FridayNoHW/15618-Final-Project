#include <mutex>
#include <iostream>
#include <memory>
using namespace std;

template <typename T>
struct Node {
  T key;
  std::shared_ptr<Node<T>> next;
  Node(const T &key) : key(key), next(nullptr) {}
  Node() : next(nullptr) {}
};

template <typename T>
class CoarseGrainList {
private:
  // sentinel nodes
  shared_ptr<Node<T>> head;
  shared_ptr<Node<T>> tail;
  mutable mutex list_mutex;

public:
  CoarseGrainList() {
    head = make_shared<Node<T>>();
    tail = make_shared<Node<T>>();
    head->next = tail;
  }

  ~CoarseGrainList() {
    head->next = nullptr;
  }

  Node<T>* get_head() {
    lock_guard<mutex> lock(list_mutex);
    return head.get();
  }

  Node<T>* get_tail() {
    lock_guard<mutex> lock(list_mutex);
    return tail.get();
  }

  Node<T>* get_front() {
    lock_guard<mutex> lock(list_mutex);
    return head->next.get();
  }

  Node<T>* get_next(Node<T>* current) {
    lock_guard<mutex> lock(list_mutex);
    return current->next.get();
  }

  bool insert(const T key) {
    lock_guard<mutex> lock(list_mutex);
    shared_ptr<Node<T>> new_node = make_shared<Node<T>>(key);
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
