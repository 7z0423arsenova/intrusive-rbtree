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
    if (op % 997 == 0) CHECK(tree.CheckInvariants());
  }
  CHECK(tree.CheckInvariants());

  auto popped = tree.ExtractPrefix([=](const Item* e) { return e->key < 100 * scale; });
  CHECK(std::is_sorted(popped.begin(), popped.end(), ItemLess));
  for (Item* e : popped) {
    CHECK(!e->InTree() && e->key < 100 * scale);
    keys.erase(keys.find(e->key));
  }
  CHECK(tree.Size() == keys.size());
  CHECK(tree.Empty() || tree.Earliest()->key >= 100 * scale);
  CHECK(tree.CheckInvariants());

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
  CHECK(tree.CheckInvariants());

  tree.Clear();
  CHECK(tree.Empty() && tree.Earliest() == nullptr && tree.CheckInvariants());
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
  CHECK(by_a.CheckInvariants() && by_b.CheckInvariants());
}

// Equal keys are legal: Insert sends them right and rotations may move them
// to either side, so CheckInvariants must accept them.
static void TestEqualKeys() {
  Item a, b, c;
  a.key = b.key = c.key = 1;
  Tree tree;
  CHECK(tree.Insert(&a) && tree.Insert(&b) && tree.Insert(&c));
  CHECK(tree.CheckInvariants());
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
      CHECK(tree.CheckInvariants() == sorted);
    }
    items[i].key = i;
  }
  CHECK(tree.CheckInvariants());
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
