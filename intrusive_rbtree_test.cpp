// g++ -std=c++23 -Wall -Wextra -g -fsanitize=address,undefined intrusive_rbtree_test.cpp && ./a.out
#include "intrusive_rbtree.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <set>

#define CHECK(c)                                                        \
  do {                                                                  \
    if (!(c)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
      std::abort();                                                     \
    }                                                                   \
  } while (0)

// RBTreeNode befriends every IntrusiveTree specialization, so this test-only
// specialization can read the private node links for CheckInvariants.
template <>
class IntrusiveTree<void, 0, void> {
public:
  template <class Node>
  static Node* Parent(const Node* node) { return node->parent(); }
  template <class Node>
  static Node* Left(const Node* node) { return node->left(); }
  template <class Node>
  static Node* Right(const Node* node) { return node->right(); }
  template <class Node>
  static bool Red(const Node* node) { return node->red(); }
};
using Links = IntrusiveTree<void, 0, void>;

struct CheckResult {
  bool ok;
  int black_height;
  std::size_t count;
};

// O(n). Verifies RB colors and black heights, BST order (equal keys allowed),
// parent/child links, linked state, size, and the sentinel's cached root/min.
template <class T, auto kLess, class Tag>
static bool CheckInvariants(const IntrusiveTree<T, kLess, Tag>& tree) {
  using Node = RBTreeNode<T, Tag>;
  const T* earliest = tree.Earliest();
  if (earliest == nullptr) return tree.Size() == 0;

  // Walk up from the cached minimum to the sentinel, the only node that is its
  // own right child.
  const Node* min = earliest;
  const Node* nil = min;
  for (std::size_t steps = 0; Links::Right(nil) != nil; ++steps) {
    if (steps == tree.Size()) return false;
    nil = Links::Parent(nil);
    if (nil == nullptr) return false;
  }
  const Node* root = Links::Parent(nil);
  if (Links::Red(nil) || nil->InTree() || root == nil || Links::Parent(root) != nil ||
      Links::Red(root)) {
    return false;
  }

  auto check_subtree = [&](this auto& self, const Node* node, const Node* parent,
                           const T* lower, const T* upper) -> CheckResult {
    if (node == nil) return {true, 0, 0};
    const CheckResult bad{false, 0, 0};
    if (node == nullptr || !node->InTree() || Links::Parent(node) != parent ||
        Links::Left(node) == nullptr || Links::Right(node) == nullptr) {
      return bad;
    }
    const T* elem = static_cast<const T*>(node);
    if (kLess(elem, elem) || (lower != nullptr && kLess(elem, lower)) ||
        (upper != nullptr && kLess(upper, elem))) {
      return bad;
    }
    if (Links::Red(node) && (Links::Red(Links::Left(node)) || Links::Red(Links::Right(node)))) {
      return bad;
    }
    CheckResult left = self(Links::Left(node), node, lower, elem);
    CheckResult right = self(Links::Right(node), node, elem, upper);
    if (!left.ok || !right.ok || left.black_height != right.black_height) return bad;
    return {true, left.black_height + (Links::Red(node) ? 0 : 1), left.count + right.count + 1};
  };
  CheckResult result = check_subtree(root, nil, nullptr, nullptr);
  if (!result.ok || result.count != tree.Size()) return false;

  const Node* leftmost = root;
  while (Links::Left(leftmost) != nil) leftmost = Links::Left(leftmost);
  return leftmost == min;
}

struct Item : RBTreeNode<Item> {
  int key = 0;
};

static bool ItemLess(const Item* x, const Item* y) { return x->key < y->key; }

using Tree = IntrusiveTree<Item, ItemLess>;

// Random Insert/Erase checked against a std::multiset. With unique == false,
// keys repeat often.
static void TestRandomOps(bool unique) {
  constexpr int kN = 2000;
  const int scale = unique ? kN : 1;
  auto items = std::make_unique<Item[]>(kN);
  std::mt19937 rng(12345);
  std::multiset<int> keys;

  Tree tree;
  for (int op = 0; op < 200000; ++op) {
    const int i = static_cast<int>(rng() % kN);
    Item* it = &items[i];
    if (it->InTree()) {
      CHECK(tree.Erase(it));
      keys.erase(keys.find(it->key));
      CHECK(!it->InTree() && !tree.Erase(it));
    } else {
      it->key = static_cast<int>(rng() % 500) * scale + (unique ? i : 0);
      CHECK(tree.Insert(it));
      keys.insert(it->key);
      CHECK(!tree.Insert(it));
    }
    CHECK(tree.Size() == keys.size());
    CHECK(keys.empty() ? tree.Earliest() == nullptr : tree.Earliest()->key == *keys.begin());
    if (op % 997 == 0) CHECK(CheckInvariants(tree));
  }
  CHECK(CheckInvariants(tree));

  auto popped = tree.ExtractPrefix([=](const Item* e) { return e->key < 100 * scale; });
  CHECK(std::is_sorted(popped.begin(), popped.end(), ItemLess));
  for (Item* e : popped) {
    CHECK(!e->InTree() && e->key < 100 * scale);
    keys.erase(keys.find(e->key));
  }
  CHECK(tree.Size() == keys.size());
  CHECK(tree.Empty() || tree.Earliest()->key >= 100 * scale);
  CHECK(CheckInvariants(tree));

  int last = -1;
  std::size_t seen = 0;
  std::size_t n = tree.PopWhile([=](const Item* e) { return e->key < 200 * scale; },
                                [&](Item* e) {
                                  CHECK(!e->InTree() && e->key >= last);
                                  last = e->key;
                                  keys.erase(keys.find(e->key));
                                  ++seen;
                                });
  CHECK(n == seen && tree.Size() == keys.size());
  CHECK(CheckInvariants(tree));

  tree.Clear();
  CHECK(tree.Empty() && tree.Earliest() == nullptr && CheckInvariants(tree));
  for (int i = 0; i < kN; ++i) CHECK(!items[i].InTree());
}

static void TestDestructorUnlinks() {
  constexpr int kN = 100;
  auto items = std::make_unique<Item[]>(kN);
  {
    Tree tree;
    for (int i = 0; i < kN; ++i) {
      items[i].key = i;
      CHECK(tree.Insert(&items[i]));
    }
  }
  for (int i = 0; i < kN; ++i) CHECK(!items[i].InTree());
}

struct TagA {};
struct TagB {};
struct Multi : RBTreeNode<Multi, TagA>, RBTreeNode<Multi, TagB> {
  int a = 0;
  int b = 0;
};
using HookA = RBTreeNode<Multi, TagA>;
using HookB = RBTreeNode<Multi, TagB>;

static void TestMultipleHooks() {
  constexpr auto kLessA = [](const Multi* x, const Multi* y) { return x->a < y->a; };
  constexpr auto kLessB = [](const Multi* x, const Multi* y) { return x->b < y->b; };
  constexpr int kN = 100;
  auto items = std::make_unique<Multi[]>(kN);
  IntrusiveTree<Multi, kLessA, TagA> by_a;
  IntrusiveTree<Multi, kLessB, TagB> by_b;
  for (int i = 0; i < kN; ++i) {
    items[i].a = i;
    items[i].b = kN - 1 - i;
    CHECK(by_a.Insert(&items[i]) && by_b.Insert(&items[i]));
  }
  CHECK(by_a.Earliest() == &items[0] && by_b.Earliest() == &items[kN - 1]);

  CHECK(by_a.Erase(&items[0]));
  CHECK(!static_cast<HookA&>(items[0]).InTree());
  CHECK(static_cast<HookB&>(items[0]).InTree());
  CHECK(by_a.Size() + 1 == by_b.Size());
  CHECK(CheckInvariants(by_a) && CheckInvariants(by_b));
}

// Equal keys are legal: Insert sends them right and rotations may move them
// to either side, so CheckInvariants must accept them.
static void TestEqualKeys() {
  Item a, b, c;
  a.key = b.key = c.key = 1;
  Tree tree;
  CHECK(tree.Insert(&a) && tree.Insert(&b) && tree.Insert(&c));
  CHECK(CheckInvariants(tree));
}

// Mutating a linked key breaks the kLess contract on purpose: CheckInvariants
// must reject exactly the mutations that leave the in-order sequence unsorted.
static void TestCheckInvariantsDetectsDisorder() {
  constexpr int kN = 100;
  auto items = std::make_unique<Item[]>(kN);
  Tree tree;
  for (int i = 0; i < kN; ++i) {
    items[i].key = i;
    CHECK(tree.Insert(&items[i]));
  }
  for (int i = 0; i < kN; ++i) {
    for (int key : {i - 2, i - 1, i + 1, i + 2, -1000, 1000}) {
      items[i].key = key;
      const bool sorted = (i == 0 || key >= i - 1) && (i == kN - 1 || key <= i + 1);
      CHECK(CheckInvariants(tree) == sorted);
    }
    items[i].key = i;
  }
  CHECK(CheckInvariants(tree));
}

int main() {
  TestRandomOps(/*unique=*/false);
  TestRandomOps(/*unique=*/true);
  TestDestructorUnlinks();
  TestMultipleHooks();
  TestEqualKeys();
  TestCheckInvariantsDetectsDisorder();
  std::puts("all tests passed");
}
