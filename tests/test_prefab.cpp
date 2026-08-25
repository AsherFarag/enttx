#include "doctest/doctest.h"

#include <enttx/hierarchy.hpp>
#include <enttx/prefab.hpp>

#include <entt/entity/registry.hpp>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

using namespace enttx;

namespace {

// A handful of simple component types used to exercise the prefab system.
// Kept deliberately small/POD-ish except where we specifically want to
// exercise string/vector-backed copy semantics or entity remapping.

struct position {
  float x{0.f};
  float y{0.f};

  friend bool operator==(const position &, const position &) = default;
};

struct health {
  int hp{0};

  friend bool operator==(const health &, const health &) = default;
};

struct name_tag {
  std::string name;

  friend bool operator==(const name_tag &, const name_tag &) = default;
};

struct tag_only {
  friend bool operator==(const tag_only &, const tag_only &) = default;
};

// An entity-valued component used to test basic_entity_remap / remap_traits.
// `target` is expected to hold the *authoring* entity of some other node at
// author time; after instantiate() it should be translated to point at that
// other node's freshly-collapsed target entity.
struct entity_ref {
  entt::entity target{entt::null};

  static void remap(entt::registry &reg, entt::entity e,
                    const enttx::entity_remap &remap) {
    auto &self = reg.get<entity_ref>(e);
    self.target = remap(self.target);
  }
};

// Small fixture bundling a definition registry + prefab registry + a
// target registry to instantiate into. Constructed fresh per TEST_CASE/
// SUBCASE so tests don't interfere with each other.
struct fixture {
  entt::registry def_reg;
  entt::registry target;
  prefab_registry reg{def_reg};
};

// Helper: returns true if `e` has component T with value `expected`.
template <typename T>
bool has_value(entt::registry &r, entt::entity e, const T &expected) {
  return r.all_of<T>(e) && r.get<T>(e) == expected;
}

} // namespace

// ===========================================================================
// Basic instantiate: single node, single/multiple components
// ===========================================================================

TEST_CASE("instantiate: single node with a single component") {
  fixture f;
  const prefab_id id = 1;
  const node_id root = f.reg.create_prefab(id);
  f.reg.emplace<position>(id, root, 1.f, 2.f);

  const entt::entity e = f.reg.instantiate(id, f.target);

  REQUIRE(e != entt::null);
  CHECK(f.target.valid(e));
  CHECK(has_value(f.target, e, position{1.f, 2.f}));
}

TEST_CASE("instantiate: multiple distinct component types on one node") {
  fixture f;
  const prefab_id id = 1;
  const node_id root = f.reg.create_prefab(id);
  f.reg.emplace<position>(id, root, 3.f, 4.f);
  f.reg.emplace<health>(id, root, 100);
  f.reg.emplace<name_tag>(id, root, name_tag{"hero"});

  const entt::entity e = f.reg.instantiate(id, f.target);

  CHECK(has_value(f.target, e, position{3.f, 4.f}));
  CHECK(has_value(f.target, e, health{100}));
  CHECK(has_value(f.target, e, name_tag{"hero"}));
}

TEST_CASE("instantiate: root entity is tagged with prefab_instance_root "
          "pointing at the source") {
  fixture f;
  const prefab_id id = 42;
  const node_id root = f.reg.create_prefab(id);
  f.reg.emplace<position>(id, root, 0.f, 0.f);

  const entt::entity e = f.reg.instantiate(id, f.target);

  REQUIRE(f.target.all_of<prefab_instance_root>(e));
  CHECK(f.target.get<prefab_instance_root>(e).source == id);
}

TEST_CASE("instantiate: unknown prefab id returns entt::null") {
  fixture f;
  const entt::entity e = f.reg.instantiate(/*prefab*/ 9999, f.target);
  CHECK(e == entt::null);
}

TEST_CASE(
    "instantiate: two instances of the same prefab are independent entities") {
  fixture f;
  const prefab_id id = 1;
  const node_id root = f.reg.create_prefab(id);
  f.reg.emplace<health>(id, root, 10);

  const entt::entity a = f.reg.instantiate(id, f.target);
  const entt::entity b = f.reg.instantiate(id, f.target);

  REQUIRE(a != b);
  f.target.get<health>(a).hp = 999;
  CHECK(f.target.get<health>(a).hp == 999);
  CHECK(f.target.get<health>(b).hp == 10); // unaffected
}

// ===========================================================================
// IsA inheritance: inherited values, overrides, additive merges across levels
// ===========================================================================

TEST_CASE("IsA: derived prefab inherits a component untouched from its base") {
  fixture f;
  const prefab_id base = 1, derived = 2;
  const node_id root = f.reg.create_prefab(base);
  f.reg.emplace<position>(base, root, 5.f, 5.f);
  f.reg.create_prefab(derived, base); // no overrides

  const entt::entity e = f.reg.instantiate(derived, f.target);

  CHECK(has_value(f.target, e, position{5.f, 5.f}));
}

TEST_CASE("IsA: derived prefab overrides a component value from its base") {
  fixture f;
  const prefab_id base = 1, derived = 2;
  const node_id root = f.reg.create_prefab(base);
  f.reg.emplace<position>(base, root, 5.f, 5.f);

  f.reg.create_prefab(derived, base);
  f.reg.emplace<position>(derived, root, 9.f, 9.f);

  const entt::entity e = f.reg.instantiate(derived, f.target);

  CHECK(has_value(f.target, e, position{9.f, 9.f}));

  // Base prefab itself must still instantiate with its own, un-mutated value.
  const entt::entity be = f.reg.instantiate(base, f.target);
  CHECK(has_value(f.target, be, position{5.f, 5.f}));
}

TEST_CASE("IsA: components from different levels of the chain are additive") {
  fixture f;
  const prefab_id base = 1, derived = 2;
  const node_id root = f.reg.create_prefab(base);
  f.reg.emplace<position>(base, root, 1.f, 1.f);

  f.reg.create_prefab(derived, base);
  f.reg.emplace<health>(derived, root, 50); // doesn't touch position

  const entt::entity e = f.reg.instantiate(derived, f.target);

  CHECK(has_value(f.target, e, position{1.f, 1.f})); // inherited
  CHECK(has_value(f.target, e, health{50}));         // own
}

TEST_CASE("IsA: three-level chain merges deltas from every level, most-derived "
          "wins") {
  fixture f;
  const prefab_id grandparent = 1, parent = 2, child = 3;

  const node_id root = f.reg.create_prefab(grandparent);
  f.reg.emplace<position>(grandparent, root, 1.f, 1.f);
  f.reg.emplace<health>(grandparent, root, 10);

  f.reg.create_prefab(parent, grandparent);
  f.reg.emplace<health>(parent, root, 20); // override health only

  f.reg.create_prefab(child, parent);
  f.reg.emplace<name_tag>(child, root, name_tag{"leaf"}); // additive only

  const entt::entity e = f.reg.instantiate(child, f.target);

  CHECK(has_value(f.target, e, position{1.f, 1.f})); // from grandparent
  CHECK(has_value(f.target, e,
                  health{20})); // from parent, overriding grandparent
  CHECK(has_value(f.target, e, name_tag{"leaf"})); // from child
}

TEST_CASE("IsA: is_a / get_base introspection") {
  fixture f;
  const prefab_id a = 1, b = 2, c = 3, unrelated = 4;
  f.reg.create_prefab(a);
  f.reg.create_prefab(b, a);
  f.reg.create_prefab(c, b);
  f.reg.create_prefab(unrelated);

  CHECK(f.reg.get_base(a) == entt::null);
  CHECK(f.reg.get_base(b) == a);
  CHECK(f.reg.get_base(c) == b);

  CHECK(f.reg.is_a(c, a)); // transitive
  CHECK(f.reg.is_a(c, b));
  CHECK(f.reg.is_a(c, c)); // reflexive
  CHECK_FALSE(f.reg.is_a(a, c));
  CHECK_FALSE(f.reg.is_a(c, unrelated));
}

TEST_CASE(
    "IsA: derived/derived_count/has_derived report direct children only") {
  fixture f;
  const prefab_id base = 1, d1 = 2, d2 = 3, grandchild = 4;
  f.reg.create_prefab(base);
  f.reg.create_prefab(d1, base);
  f.reg.create_prefab(d2, base);
  f.reg.create_prefab(grandchild, d1);

  CHECK(f.reg.has_derived(base));
  CHECK(f.reg.has_derived(d1));
  CHECK_FALSE(f.reg.has_derived(d2));
  CHECK_FALSE(f.reg.has_derived(grandchild));

  std::vector<prefab_id> kids;
  for (const prefab_id p : f.reg.derived(base))
    kids.push_back(p);
  CHECK(kids.size() == 2);
  CHECK(std::find(kids.begin(), kids.end(), d1) != kids.end());
  CHECK(std::find(kids.begin(), kids.end(), d2) != kids.end());
}

// ===========================================================================
// Explicit component removal
// ===========================================================================

TEST_CASE("remove<T>: strips an inherited component at instantiate time") {
  fixture f;
  const prefab_id base = 1, derived = 2;
  const node_id root = f.reg.create_prefab(base);
  f.reg.emplace<health>(base, root, 10);
  f.reg.emplace<position>(base, root, 1.f, 1.f);

  f.reg.create_prefab(derived, base);
  f.reg.remove<health>(derived, root);

  const entt::entity e = f.reg.instantiate(derived, f.target);

  CHECK_FALSE(f.target.all_of<health>(e));
  CHECK(has_value(f.target, e,
                  position{1.f, 1.f})); // untouched component survives

  // Base prefab is unaffected by the derived-level removal.
  const entt::entity be = f.reg.instantiate(base, f.target);
  CHECK(f.target.all_of<health>(be));
}

TEST_CASE("remove<T>: a later (more-derived) level can re-add what a mid level "
          "removed") {
  fixture f;
  const prefab_id base = 1, mid = 2, leaf = 3;
  const node_id root = f.reg.create_prefab(base);
  f.reg.emplace<health>(base, root, 5);

  f.reg.create_prefab(mid, base);
  f.reg.remove<health>(mid, root);

  f.reg.create_prefab(leaf, mid);
  f.reg.emplace<health>(leaf, root, 9);

  const entt::entity e = f.reg.instantiate(leaf, f.target);

  REQUIRE(f.target.all_of<health>(e));
  CHECK(f.target.get<health>(e).hp == 9);
}

TEST_CASE("remove<T>: without a re-add, the component stays gone through the "
          "rest of the chain") {
  fixture f;
  const prefab_id base = 1, mid = 2, leaf = 3;
  const node_id root = f.reg.create_prefab(base);
  f.reg.emplace<health>(base, root, 5);

  f.reg.create_prefab(mid, base);
  f.reg.remove<health>(mid, root);

  f.reg.create_prefab(leaf, mid); // no re-add

  const entt::entity e = f.reg.instantiate(leaf, f.target);
  CHECK_FALSE(f.target.all_of<health>(e));
}

TEST_CASE("emplace after remove at the same level clears the removal "
          "(component reappears)") {
  fixture f;
  const prefab_id id = 1;
  const node_id root = f.reg.create_prefab(id);
  f.reg.emplace<health>(id, root, 5);
  f.reg.remove<health>(id, root);
  f.reg.emplace<health>(id, root, 7); // same level, re-added

  const entt::entity e = f.reg.instantiate(id, f.target);
  REQUIRE(f.target.all_of<health>(e));
  CHECK(f.target.get<health>(e).hp == 7);
}

// ===========================================================================
// Hierarchy / children
// ===========================================================================

TEST_CASE(
    "hierarchy: children are instantiated with correct parent/child links") {
  fixture f;
  const prefab_id id = 1;
  const node_id root = f.reg.create_prefab(id);
  f.reg.emplace<name_tag>(id, root, name_tag{"root"});

  prefab_builder builder{f.reg, id, root};
  node_id child_a_id{}, child_b_id{};
  builder
      .add_child([&](prefab_builder &c) {
        child_a_id = c.node_;
        c.emplace<name_tag>(name_tag{"a"});
      })
      .add_child([&](prefab_builder &c) {
        child_b_id = c.node_;
        c.emplace<name_tag>(name_tag{"b"});
      });

  const entt::entity e = f.reg.instantiate(id, f.target);

  REQUIRE(f.target.all_of<hierarchy>(e));
  const auto &h = f.target.get<hierarchy>(e);
  CHECK(h.child_count == 2);

  std::vector<std::string> names;
  hierarchy::for_each_child(f.target, e, [&](entt::entity c) {
    REQUIRE(f.target.all_of<name_tag>(c));
    names.push_back(f.target.get<name_tag>(c).name);
    CHECK(f.target.get<hierarchy>(c).parent == e);
  });

  REQUIRE(names.size() == 2);
  CHECK(names[0] == "a");
  CHECK(names[1] == "b");
}

TEST_CASE("hierarchy: grandchildren are instantiated and linked transitively") {
  fixture f;
  const prefab_id id = 1;
  const node_id root = f.reg.create_prefab(id);

  prefab_builder builder{f.reg, id, root};
  builder.add_child([&](prefab_builder &c) {
    c.emplace<name_tag>(name_tag{"child"});
    c.add_child([&](prefab_builder &gc) {
      gc.emplace<name_tag>(name_tag{"grandchild"});
    });
  });

  const entt::entity e = f.reg.instantiate(id, f.target);
  const auto &h = f.target.get<hierarchy>(e);
  REQUIRE(h.child_count == 1);

  const entt::entity child = h.first_child;
  REQUIRE(f.target.all_of<hierarchy>(child));
  const auto &ch = f.target.get<hierarchy>(child);
  CHECK(ch.child_count == 1);
  CHECK(f.target.get<name_tag>(ch.first_child).name == "grandchild");
  CHECK(f.target.get<hierarchy>(ch.first_child).parent == child);
}

TEST_CASE("hierarchy: overriding a deep child at a derived level doesn't "
          "pollute intermediate nodes") {
  fixture f;
  const prefab_id base = 1, derived = 2;
  const node_id root = f.reg.create_prefab(base);

  node_id mid_id{}, leaf_id{};
  prefab_builder builder{f.reg, base, root};
  builder.add_child([&](prefab_builder &mid) {
    mid_id = mid.node_;
    mid.emplace<name_tag>(name_tag{"mid"});
    mid.add_child([&](prefab_builder &leaf) {
      leaf_id = leaf.node_;
      leaf.emplace<health>(health{1});
    });
  });

  f.reg.create_prefab(derived, base);
  prefab_builder dbuilder{f.reg, derived, root};
  dbuilder.override_child(
      leaf_id, [&](prefab_builder &leaf) { leaf.emplace<health>(health{99}); });

  const entt::entity e = f.reg.instantiate(derived, f.target);
  const entt::entity mid = f.target.get<hierarchy>(e).first_child;
  REQUIRE(f.target.valid(mid));
  CHECK(f.target.get<name_tag>(mid).name == "mid"); // inherited, untouched
  CHECK_FALSE(
      f.target.all_of<health>(mid)); // no leakage onto the skeleton ancestor

  const entt::entity leaf = f.target.get<hierarchy>(mid).first_child;
  REQUIRE(f.target.valid(leaf));
  CHECK(f.target.get<health>(leaf).hp == 99); // override took effect
}

TEST_CASE("hierarchy: remove_child deletes an inherited child and its whole "
          "subtree") {
  fixture f;
  const prefab_id base = 1, derived = 2;
  const node_id root = f.reg.create_prefab(base);

  node_id doomed_id{};
  prefab_builder builder{f.reg, base, root};
  builder
      .add_child([&](prefab_builder &survivor) {
        survivor.emplace<name_tag>(name_tag{"survivor"});
      })
      .add_child([&](prefab_builder &doomed) {
        doomed_id = doomed.node_;
        doomed.emplace<name_tag>(name_tag{"doomed"});
        doomed.add_child([&](prefab_builder &gc) {
          gc.emplace<name_tag>(name_tag{"doomed_child"});
        });
      });

  f.reg.create_prefab(derived, base);
  prefab_builder dbuilder{f.reg, derived, root};
  dbuilder.remove_child(doomed_id);

  const entt::entity e = f.reg.instantiate(derived, f.target);
  const auto &h = f.target.get<hierarchy>(e);
  CHECK(h.child_count == 1);

  hierarchy::for_each_child(f.target, e, [&](entt::entity c) {
    CHECK(f.target.get<name_tag>(c).name == "survivor");
  });

  // Base prefab (un-derived) still has both children.
  const entt::entity be = f.reg.instantiate(base, f.target);
  CHECK(f.target.get<hierarchy>(be).child_count == 2);
}

TEST_CASE("hierarchy: overriding one inherited child de-duplicates it (no "
          "double entity) but can reorder it") {
  // Documents current, known behavior: children are merged by node_id
  // (deduplicated) but not stably re-sorted, so overriding a child at a
  // derived level can move it to the front of the child list rather than
  // preserving its original position. See project notes re: "hierarchy
  // child merging across the IsA chain" as an open design question.
  fixture f;
  const prefab_id base = 1, derived = 2;
  const node_id root = f.reg.create_prefab(base);

  node_id a_id{}, b_id{};
  prefab_builder builder{f.reg, base, root};
  builder
      .add_child([&](prefab_builder &a) {
        a_id = a.node_;
        a.emplace<name_tag>(name_tag{"a"});
      })
      .add_child([&](prefab_builder &b) {
        b_id = b.node_;
        b.emplace<name_tag>(name_tag{"b"});
      });

  f.reg.create_prefab(derived, base);
  prefab_builder dbuilder{f.reg, derived, root};
  dbuilder.override_child(b_id, [&](prefab_builder &b) {
    b.emplace<name_tag>(name_tag{"b-overridden"});
  });

  const entt::entity e = f.reg.instantiate(derived, f.target);
  const auto &h = f.target.get<hierarchy>(e);

  // Still exactly two children -- not three -- confirming de-duplication by
  // node_id.
  CHECK(h.child_count == 2);

  std::vector<std::string> names;
  hierarchy::for_each_child(f.target, e, [&](entt::entity c) {
    names.push_back(f.target.get<name_tag>(c).name);
  });
  REQUIRE(names.size() == 2);

  // Both original children are present in some order, and the overridden
  // value for "b" won -- but callers should not rely on original ordering
  // being preserved across an override.
  CHECK(std::find(names.begin(), names.end(), "a") != names.end());
  CHECK(std::find(names.begin(), names.end(), "b-overridden") != names.end());
  CHECK(std::find(names.begin(), names.end(), "b") == names.end());
}

// ===========================================================================
// Nested prefabs
// ===========================================================================

TEST_CASE("nested prefab: instantiated as a child of the referencing node") {
  fixture f;
  const prefab_id weapon = 1, character = 2;

  const node_id wroot = f.reg.create_prefab(weapon);
  f.reg.emplace<name_tag>(weapon, wroot, name_tag{"sword"});
  f.reg.emplace<health>(weapon, wroot, 3); // "durability"

  const node_id croot = f.reg.create_prefab(character);
  f.reg.emplace<name_tag>(character, croot, name_tag{"hero"});
  prefab_builder cbuilder{f.reg, character, croot};
  cbuilder.add_nested(weapon);

  const entt::entity e = f.reg.instantiate(character, f.target);
  CHECK(f.target.get<name_tag>(e).name == "hero");

  const auto &h = f.target.get<hierarchy>(e);
  REQUIRE(h.child_count == 1);
  const entt::entity nested = h.first_child;
  CHECK(f.target.get<name_tag>(nested).name == "sword");
  CHECK(f.target.get<health>(nested).hp == 3);
  REQUIRE(f.target.all_of<prefab_instance_root>(nested));
  CHECK(f.target.get<prefab_instance_root>(nested).source == weapon);
}

TEST_CASE(
    "nested prefab: nested subtree (its own children) is fully instantiated") {
  fixture f;
  const prefab_id part = 1, whole = 2;

  const node_id proot = f.reg.create_prefab(part);
  prefab_builder pbuilder{f.reg, part, proot};
  pbuilder.emplace<name_tag>(name_tag{"part-root"})
      .add_child([&](prefab_builder &c) {
        c.emplace<name_tag>(name_tag{"part-child"});
      });

  const node_id wroot = f.reg.create_prefab(whole);
  prefab_builder wbuilder{f.reg, whole, wroot};
  wbuilder.add_nested(part);

  const entt::entity e = f.reg.instantiate(whole, f.target);
  const entt::entity nested = f.target.get<hierarchy>(e).first_child;
  CHECK(f.target.get<name_tag>(nested).name == "part-root");
  const entt::entity nested_child = f.target.get<hierarchy>(nested).first_child;
  CHECK(f.target.get<name_tag>(nested_child).name == "part-child");
}

TEST_CASE("nested prefab: most-derived override of nested_prefab_ref wins") {
  fixture f;
  const prefab_id sword = 1, axe = 2, character = 3, elite_character = 4;

  const node_id sroot = f.reg.create_prefab(sword);
  f.reg.emplace<name_tag>(sword, sroot, name_tag{"sword"});

  const node_id aroot = f.reg.create_prefab(axe);
  f.reg.emplace<name_tag>(axe, aroot, name_tag{"axe"});

  const node_id croot = f.reg.create_prefab(character);
  prefab_builder cbuilder{f.reg, character, croot};
  node_id weapon_slot{};
  cbuilder.add_nested(sword); // add_nested returns nothing usable directly;
                              // capture via add_child semantics below instead.

  // add_nested creates a brand new child node id but doesn't expose it via
  // the builder chain, so fetch it by walking the freshly authored tree.
  hierarchy::size_type unused = 0;
  (void)unused;
  entt::entity croot_ae = f.reg.get_node_entity(character, croot);
  REQUIRE(croot_ae != entt::null);
  // Grab the sole child's node_id straight off the authoring hierarchy.
  using authoring_hierarchy = prefab_registry::authoring_hierarchy;
  entt::entity slot_ae = entt::null;
  authoring_hierarchy::for_each_child(f.def_reg, croot_ae,
                                      [&](entt::entity c) { slot_ae = c; });
  REQUIRE(slot_ae != entt::null);
  weapon_slot = f.def_reg.get<node_id>(slot_ae);

  f.reg.create_prefab(elite_character, character);
  prefab_builder ebuilder{f.reg, elite_character, croot};
  ebuilder.override_child(weapon_slot, [&](prefab_builder &slot) {
    slot.add_nested(axe); // most-derived nested ref should win over "sword"
  });

  const entt::entity base_instance = f.reg.instantiate(character, f.target);
  const entt::entity base_weapon =
      f.target.get<hierarchy>(base_instance).first_child;
  CHECK(f.target.get<name_tag>(base_weapon).name == "sword");

  const entt::entity elite_instance =
      f.reg.instantiate(elite_character, f.target);
  // The slot node has no authored components of its own (only a nested ref),
  // so its instantiated entity's single child is the nested weapon.
  bool found_axe = false;
  hierarchy::for_each_child(
      f.target, elite_instance, [&](entt::entity slot_entity) {
        if (f.target.all_of<hierarchy>(slot_entity) &&
            f.target.get<hierarchy>(slot_entity).child_count == 1) {
          const entt::entity weapon =
              f.target.get<hierarchy>(slot_entity).first_child;
          if (f.target.all_of<name_tag>(weapon) &&
              f.target.get<name_tag>(weapon).name == "axe") {
            found_axe = true;
          }
        }
      });
  CHECK(found_axe);
}

// ===========================================================================
// Entity remap (entity-valued components)
// ===========================================================================

TEST_CASE("entity_ref: entity-valued components are remapped from authoring "
          "entities to instance entities") {
  fixture f;
  const prefab_id id = 1;
  const node_id root = f.reg.create_prefab(id);

  node_id target_child_id{};
  prefab_builder builder{f.reg, id, root};
  builder.add_child([&](prefab_builder &target_child) {
    target_child_id = target_child.node_;
    target_child.emplace<name_tag>(name_tag{"target"});
  });

  // Author entity_ref on the root, pointing at the *authoring* entity for
  // target_child_id. remap_traits should translate this to the target
  // registry entity for that node when instantiated.
  const entt::entity target_child_ae =
      f.reg.get_node_entity(id, target_child_id);
  REQUIRE(target_child_ae != entt::null);
  f.reg.emplace<entity_ref>(id, root, entity_ref{target_child_ae});

  const entt::entity e = f.reg.instantiate(id, f.target);
  REQUIRE(f.target.all_of<entity_ref>(e));

  const entt::entity expected_target = f.target.get<hierarchy>(e).first_child;
  CHECK(f.target.get<entity_ref>(e).target == expected_target);
  CHECK(f.target.get<name_tag>(expected_target).name == "target");
}

TEST_CASE("entity_ref: a reference to a node outside the instantiated chain "
          "translates to null") {
  fixture f;
  const prefab_id id = 1;
  const node_id root = f.reg.create_prefab(id);

  // A bogus/unassociated authoring entity that never gets touched.
  const entt::entity stray_ae = f.def_reg.create();
  f.reg.emplace<entity_ref>(id, root, entity_ref{stray_ae});

  const entt::entity e = f.reg.instantiate(id, f.target);
  REQUIRE(f.target.all_of<entity_ref>(e));
  CHECK(f.target.get<entity_ref>(e).target == entt::null);
}

// ===========================================================================
// register_ops
// ===========================================================================

TEST_CASE(
    "register_ops: can be called ahead of time without affecting behavior") {
  fixture f;
  f.reg.register_ops<position>();
  f.reg.register_ops<position>(); // idempotent, second call is a no-op

  const prefab_id id = 1;
  const node_id root = f.reg.create_prefab(id);
  f.reg.emplace<position>(id, root, 2.f, 3.f);

  const entt::entity e = f.reg.instantiate(id, f.target);
  CHECK(has_value(f.target, e, position{2.f, 3.f}));
}

// ===========================================================================
// Introspection helpers
// ===========================================================================

TEST_CASE(
    "introspection: get_root_node / get_root_node_entity / get_prefab_entity") {
  fixture f;
  const prefab_id id = 1;
  const node_id root = f.reg.create_prefab(id);
  f.reg.emplace<position>(id, root, 0.f, 0.f);

  CHECK(f.reg.get_root_node(id) == root);
  CHECK(f.reg.get_root_node(9999) ==
        node_id{}); // unknown prefab -> null-ish root

  const entt::entity prefab_entity = f.reg.get_prefab_entity(id);
  CHECK(prefab_entity != entt::null);
  CHECK(f.reg.get_prefab_entity(9999) == entt::null);

  const entt::entity root_entity = f.reg.get_root_node_entity(id);
  CHECK(root_entity != entt::null);
  CHECK(f.def_reg.get<node_id>(root_entity) == root);
}

TEST_CASE("introspection: get_node_entity returns entt::null for a node never "
          "authored in that prefab") {
  fixture f;
  const prefab_id id = 1;
  const node_id root = f.reg.create_prefab(id);

  static_assert(std::is_same_v<node_id_generator,
                               basic_monotonic_stable_id_generator<node_id>>,
                "This test assumes node_id is a monotonic stable id generator, "
                "so we can predict the next id value.");
  const node_id unrelated = node_id{2}; // never created in this prefab

  CHECK(f.reg.get_node_entity(id, unrelated) == entt::null);
  CHECK(f.reg.get_node_entity(id, root) != entt::null);
}

// ===========================================================================
// node_id basics
// ===========================================================================

TEST_CASE("node_id: generator never produces the null id, and ids are (almost "
          "certainly) unique") {
  node_id_generator gen;
  std::unordered_set<std::uint64_t> seen;
  for (int i = 0; i < 1000; ++i) {
    const node_id id = gen();
    CHECK_FALSE(id == entt::null);
    CHECK(seen.insert(id.value).second); // no duplicate within this run
  }
}

TEST_CASE(
    "node_id: comparisons against entt::null work in both operand orders") {
  node_id n{};
  CHECK(n == entt::null);
  CHECK(entt::null == n);

  node_id nz{42};
  CHECK_FALSE(nz == entt::null);
  CHECK_FALSE(entt::null == nz);
}
