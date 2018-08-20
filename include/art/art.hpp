/**
 * @file adaptive radix tree header.
 * @author Rafael Kallis <rk@rafaelkallis.com>
 */

#ifndef ART_ART_HPP
#define ART_ART_HPP

#include "node.hpp"
#include "node_0.hpp"
#include "node_4.hpp"
#include "tree_it.hpp"
#include <algorithm>
#include <iostream>
#include <stack>
#include <string>

namespace art {

using std::memcpy;
using std::memmove;
using std::min;
using std::string;
using std::stack;

template <class T> class art {
public:
  ~art();

  /**
   * Finds the value associated with the given key.
   *
   * @param key - The key to find.
   * @return the value associated with the key or a nullptr.
   */
  T *get(const string &key) const;
  T *get(uint8_t *key, int key_len) const;

  /**
   * Associates the given key with the given value.
   * If another value is already associated with the given key,
   * since the method consumer is the resource owner.
   *
   * @param key - The key to associate with the value.
   * @param value - The value to be associated with the key.
   * @return a nullptr if no other value is associated with they or the
   * previously associated value.
   */
  T *set(const string &key, T *value);
  T *set(uint8_t *key, int key_len, T *value);

  /**
   * Deletes the given key and returns it's associated value.
   * The associated value is returned,
   * since the method consumer is the resource owner.
   * If no value is associated with the given key, nullptr is returned.
   *
   * @param key - The key to delete.
   * @return the values assciated with they key or a nullptr otherwise.
   */
  T *del(const string &key);
  T *del(uint8_t *key, int key_len);

  /**
   * Forward iterator that traverses the tree in lexicographic order.
   */
  tree_it<T> begin();

  /**
   * Forward iterator that traverses the tree in lexicographic order starting
   * from the provided key.
   */
  tree_it<T> begin(const string &key);

  /**
   * Iterator to the end of the lexicographic order.
   */
  tree_it<T> end();

private:
  node<T> *root_ = nullptr;
};

template <class T> art<T>::~art() {
  if (root_ == nullptr) {
    return;
  }
  stack<node<T> *> node_stack;
  node_stack.push(root_);
  while (!node_stack.empty()) {
    auto cur = node_stack.top();
    node_stack.pop();
    auto child_it = cur->begin();
    auto child_it_end = cur->end();
    for (; child_it != child_it_end; ++child_it) {
      node_stack.push(*(*cur).find_child(*child_it));
    }
    if (cur->prefix_ != nullptr) {
      delete[] cur->prefix_;
    }
    delete cur;
  }
}

template <class T> T *art<T>::get(const string &key) const {
  return get((uint8_t *)key.c_str(), key.length());
}

template <class T> T *art<T>::get(uint8_t *key, int key_len) const {
  node<T> *cur = root_, **child;
  int depth = 0;
  while (cur != nullptr) {
    /* prefix mismatch */
    if (cur->prefix_len_ != cur->check_prefix(key + depth, key_len - depth)) {
      /* prefix mismatch */
      return nullptr;
    }
    if (cur->prefix_len_ == key_len - depth) {
      /* exact match */
      return cur->value_;
    }
    depth += cur->prefix_len_;
    child = cur->find_child(key[depth]);
    cur = child != nullptr ? *child : nullptr;
    ++depth;
  }
  return nullptr;
}

template <class T> T *art<T>::set(const string &key, T *value) {
  return set((uint8_t *)key.c_str(), key.length(), value);
}

template <class T> T *art<T>::set(uint8_t *key, int key_len, T *value) {
  if (root_ == nullptr) {
    root_ = new node_0<T>();
    root_->prefix_ = new uint8_t[key_len];
    memcpy(root_->prefix_, key, key_len);
    root_->prefix_len_ = key_len;
    root_->value_ = value;
    return nullptr;
  }

  node<T> **cur = &root_, **child;
  uint8_t cur_partial_key, child_partial_key;
  int depth = 0, prefix_match_len;
  bool is_prefix_match;

  while (true) {
    /* number of bytes of the current node's prefix that match the key */
    prefix_match_len = (**cur).check_prefix(key + depth, key_len - depth);

    /* true if the current node's prefix matches with a part of the key */
    is_prefix_match =
        (min<int>((**cur).prefix_len_, key_len - depth)) == prefix_match_len;

    if (is_prefix_match && (**cur).prefix_len_ == key_len - depth) {
      /* exact match:
       * => "replace"
       * => replace value of current node.
       * => return old value to caller to handle.
       *        _                             _
       *        |                             |
       *       (aa)->Ø                       (aa)->Ø
       *    a /    \ b     +[aaaaa,v3]    a /    \ b
       *     /      \      ==========>     /      \
       * *(aa)->v1  ()->v2             *(aa)->v3  ()->v2
       *  /|\       /|\                 /|\       /|\
       */

      T *old_value = (**cur).value_;
      (**cur).value_ = value;
      return old_value;
    }

    if (is_prefix_match && (**cur).prefix_len_ > key_len - depth) {
      /* new key is a prefix of the current node's key:
       * => "expand"
       * => create new node with value to insert.
       * => new node becomes parent of current node.
       *        _                           _
       *        |                           |
       *       (aa)->Ø                     (aa)->Ø
       *    a /    \ b     +[aaaa,v3]   a /    \ b
       *     /      \      =========>    /      \
       * *(aa)->v1  ()->v2            +(a)->v3  ()->v2
       *  /|\       /|\             a /         /|\
       *                             /
       *                           *()->v1
       *                            /|\
       */

      auto new_node = new node_4<T>();
      new_node->value_ = value;
      new_node->prefix_ = new uint8_t[prefix_match_len];
      memcpy(new_node->prefix_, (**cur).prefix_, prefix_match_len);
      new_node->prefix_len_ = prefix_match_len;
      new_node->set_child((**cur).prefix_[prefix_match_len], *cur);

      // TODO(rafaelkallis): shrink?
      memmove((**cur).prefix_, (**cur).prefix_ + prefix_match_len + 1,
              (**cur).prefix_len_ - prefix_match_len - 1);
      (**cur).prefix_len_ -= prefix_match_len + 1;

      *cur = new_node;
      return nullptr;
    }

    if (!is_prefix_match) {
      /* prefix mismatch:
       * => new parent node with common prefix and no associated value.
       * => new node with value to insert.
       * => current and new node become children of new parent node.
       *
       *        |                        |
       *      *(aa)->Ø                 +(a)->Ø
       *    a /    \ b     +[ab,v3]  a /   \ b
       *     /      \      =======>   /     \
       *  (aa)->v1  ()->v2          *()->Ø +()->v3
       *  /|\       /|\           a /   \ b
       *                           /     \
       *                        (aa)->v1 ()->v2
       *                        /|\      /|\
       */

      auto new_parent = new node_4<T>();
      new_parent->prefix_ = new uint8_t[prefix_match_len];
      memcpy(new_parent->prefix_, (**cur).prefix_, prefix_match_len);
      new_parent->prefix_len_ = prefix_match_len;
      new_parent->set_child((**cur).prefix_[prefix_match_len], *cur);

      // TODO(rafaelkallis): shrink?
      memmove((**cur).prefix_, (**cur).prefix_ + prefix_match_len + 1,
              (**cur).prefix_len_ - prefix_match_len - 1);
      (**cur).prefix_len_ -= prefix_match_len + 1;

      auto new_node = new node_0<T>();
      new_node->prefix_ = new uint8_t[key_len - depth - prefix_match_len - 1];
      memcpy(new_node->prefix_, key + depth + prefix_match_len + 1,
             key_len - depth - prefix_match_len - 1);
      new_node->prefix_len_ = key_len - depth - prefix_match_len - 1;
      new_node->value_ = value;
      new_parent->set_child(key[depth + prefix_match_len], new_node);

      *cur = new_parent;
      return nullptr;
    }

    child_partial_key = key[depth + (**cur).prefix_len_];
    child = (**cur).find_child(child_partial_key);

    if (child == nullptr) {
      /*
       * no child associated with the next partial key.
       * => create new node with value to insert.
       * => new node becomes current node's child.
       *
       *      *(aa)->Ø              *(aa)->Ø
       *    a /        +[aab,v2]  a /    \ b
       *     /         ========>   /      \
       *   (a)->v1               (a)->v1 +()->v2
       */

      if ((**cur).is_full()) {
        *cur = (**cur).grow();
      }

      auto new_node = new node_0<T>();
      new_node->prefix_ =
          new uint8_t[key_len - depth - (**cur).prefix_len_ - 1];
      memcpy(new_node->prefix_, key + depth + (**cur).prefix_len_ + 1,
             key_len - depth - (**cur).prefix_len_ - 1);
      new_node->prefix_len_ = key_len - depth - (**cur).prefix_len_ - 1;
      new_node->value_ = value;
      (**cur).set_child(child_partial_key, new_node);
      return nullptr;
    }

    /* propagate down and repeat:
     *
     *     *(aa)->Ø                   (aa)->Ø
     *   a /    \ b    +[aaba,v3]  a /    \ b     repeat
     *    /      \     =========>   /      \     ========>  ...
     *  (a)->v1  ()->v2           (a)->v1 *()->v2
     */

    depth += (**cur).prefix_len_ + 1;
    cur = child;
  }
}

template <class T> T *art<T>::del(const string &key) {
  return del((uint8_t *)key.c_str(), key.length());
}

template <class T> T *art<T>::del(uint8_t *key, int key_len) {
  if (root_ == nullptr) {
    return nullptr;
  }

  /* pointer to parent, current and child node */
  node<T> **par = nullptr, **cur = &root_;

  /* partial key of current and child node */
  uint8_t cur_partial_key;

  /* current key depth */
  int depth = 0;

  while (cur != nullptr) {
    if ((**cur).prefix_len_ !=
        (**cur).check_prefix(key + depth, key_len - depth)) {
      /* prefix mismatch => key doesn't exist */

      return nullptr;
    }

    if (key_len == depth + (**cur).prefix_len_) {
      /* exact match */

      T *value = (**cur).value_;
      (**cur).value_ = nullptr;
      const int n_children = (**cur).n_children();
      const int n_siblings = par != nullptr ? (**par).n_children() - 1 : 0;

      if (n_children == 0 && n_siblings == 0) {
        /*
         * => delete leaf node
         *
         *     |                 |
         *    (aa)->v1          (aa)->v1
         *     | a     -[aaaaa]
         *     |       =======>
         *   *(aa)->v2
         */

        if (par != nullptr) {
          (**par).del_child(cur_partial_key);
        }
        if ((**cur).prefix_ != nullptr) {
          delete[](**cur).prefix_;
        }
        delete (*cur);

      } else if (n_children == 0 && n_siblings == 1 &&
                 (**par).value_ == nullptr) {
        /* => delete leaf node
         * => replace parent with sibling
         *
         *        |a                         |a
         *        |                          |
         *       (aa)->Ø     -"aaaaabaa"     |
         *    a /    \ b     ==========>    /
         *     /      \                    /
         *  (aa)->v1 *()->v2             (aaaaa)->v1
         *  /|\
         */

        /* find sibling */
        auto sibling_partial_key = (**par).next_partial_key(0);
        if (sibling_partial_key == cur_partial_key) {
          sibling_partial_key = (**par).next_partial_key(cur_partial_key + 1);
        }
        auto sibling = *(**par).find_child(sibling_partial_key);

        auto old_prefix = sibling->prefix_;
        auto old_prefix_len = sibling->prefix_len_;

        sibling->prefix_ =
            new uint8_t[(**par).prefix_len_ + 1 + old_prefix_len];
        sibling->prefix_len_ = (**par).prefix_len_ + 1 + old_prefix_len;
        memcpy(sibling->prefix_, (**par).prefix_, (**par).prefix_len_);
        sibling->prefix_[(**par).prefix_len_ + 1] = sibling_partial_key;
        memcpy(sibling->prefix_ + (**par).prefix_len_ + 1, old_prefix,
               old_prefix_len);
        delete[] old_prefix;

        if ((**cur).prefix_ != nullptr) {
          delete[](**cur).prefix_;
        }
        delete (*cur);
        *cur = nullptr;

        if ((**par).prefix_ != nullptr) {
          delete[](**par).prefix_;
        }
        delete (*par);
        *par = sibling;

      } else if (n_children == 0 && n_siblings > 1) {
        /* => delete leaf node
         *
         *        |a                         |a
         *        |                          |
         *       (aa)->Ø     -"aaaaabaa"    (aa)->Ø
         *    a / |  \ b     ==========> a / |
         *     /  |   \                   /  |
         *           *()->v1
         */

        if ((**cur).prefix_ != nullptr) {
          delete[](**cur).prefix_;
        }
        delete (*cur);
        *cur = nullptr;

        (**par).del_child(cur_partial_key);

      } else if (n_children == 1) {
        /* node with 1 child
         * => replace node with child
         *
         *       (aa)->v1            (aa)->v1
         *        |a                  |a
         *        |        -[aaaaa]   |
         *      *(aa)->v2  =======>   |
         *        |a                  |
         *        |                   |
         *       (aa)->v3            (aaaaa)->v3
         */

        auto child_partial_key = (**cur).next_partial_key(0);
        auto child = *(**cur).find_child(child_partial_key);

        auto old_prefix = child->prefix_;
        auto old_prefix_len = child->prefix_len_;

        child->prefix_ = new uint8_t[(**cur).prefix_len_ + 1 + old_prefix_len];
        child->prefix_len_ = (**cur).prefix_len_ + 1 + old_prefix_len;
        memcpy(child->prefix_, (**cur).prefix_, (**cur).prefix_len_);
        memcpy(child->prefix_ + (**cur).prefix_len_, &child_partial_key, 1);
        memcpy(child->prefix_ + (**cur).prefix_len_ + 1, old_prefix,
               old_prefix_len);
        delete[] old_prefix;

        if ((**cur).prefix_ != nullptr) {
          delete[](**cur).prefix_;
        }
        delete (*cur);
        *cur = child;
      }

      if (par != nullptr && (**par).is_underfull()) {
        *par = (**par).shrink();
      }
      return value;
    }

    /* propagate down and repeat */
    cur_partial_key = key[depth + (**cur).prefix_len_];
    depth += (**cur).prefix_len_ + 1;
    cur = (**cur).find_child(cur_partial_key);
  }
  return nullptr;
}

template <class T> tree_it<T> art<T>::begin() { return tree_it<T>(root_); }

template <class T> tree_it<T> art<T>::begin(const string &key) {
  return tree_it<T>::greater_equal(root_, key);
}

template <class T> tree_it<T> art<T>::end() { return tree_it<T>(); }

} // namespace art

#endif
