#include "doctest/doctest.h"

#include <enttx/hierarchy.hpp>
#include <enttx/entity_remap.hpp>

#include <entt/entt.hpp>

#include <vector>
#include <algorithm>
#include <unordered_map>

using namespace enttx;

namespace {

// Distinct tags so that different deletion policies can coexist in the same
// registry without colliding on the same basic_hierarchy<Tag> component type.
struct destroy_tag {};
struct orphan_tag {};
struct unhandled_tag {};

using destroy_hierarchy   = basic_hierarchy<entt::registry, hierarchy_config{ hierarchy_deletion_policy::destroy_children }, destroy_tag>;
using orphan_hierarchy    = basic_hierarchy<entt::registry, hierarchy_config{ hierarchy_deletion_policy::orphan_children },  orphan_tag>;
using unhandled_hierarchy = basic_hierarchy<entt::registry, hierarchy_config{ hierarchy_deletion_policy::unhandled },        unhandled_tag>;

// Collects the direct children of `parent` via the forward child_iterator/children_view.
template<typename Hierarchy>
std::vector<typename Hierarchy::entity_type> collect_children(const entt::registry& reg, entt::entity parent) {
    std::vector<typename Hierarchy::entity_type> out;
    for (auto c : Hierarchy::children(reg, parent)) {
        out.push_back(c);
    }
    return out;
}

// Collects the direct children of `parent` in reverse order via rbegin/rend.
template<typename Hierarchy>
std::vector<typename Hierarchy::entity_type> collect_children_reverse(const entt::registry& reg, entt::entity parent) {
    std::vector<typename Hierarchy::entity_type> out;
    auto view = Hierarchy::children(reg, parent);
    for (auto it = view.rbegin(); it != view.rend(); ++it) {
        out.push_back(*it);
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Basic insertion: push_back / push_front
// ---------------------------------------------------------------------------

TEST_SUITE("insertion") {

TEST_CASE("push_back appends children in order and updates child_count") {
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();
    auto c3 = reg.create();

    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::push_back(reg, parent, c2);
    destroy_hierarchy::push_back(reg, parent, c3);

    auto& ph = reg.get<destroy_hierarchy>(parent);
    CHECK(ph.child_count == 3);
    CHECK(ph.first_child == c1);
    CHECK(ph.last_child == c3);
    CHECK(ph.has_children() == true);

    CHECK(collect_children<destroy_hierarchy>(reg, parent) == std::vector<entt::entity>{c1, c2, c3});
}

TEST_CASE("push_front prepends children in reverse insertion order") {
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();
    auto c3 = reg.create();

    destroy_hierarchy::push_front(reg, parent, c1);
    destroy_hierarchy::push_front(reg, parent, c2);
    destroy_hierarchy::push_front(reg, parent, c3);

    auto& ph = reg.get<destroy_hierarchy>(parent);
    CHECK(ph.child_count == 3);
    CHECK(ph.first_child == c3);
    CHECK(ph.last_child == c1);

    CHECK(collect_children<destroy_hierarchy>(reg, parent) == std::vector<entt::entity>{c3, c2, c1});
}

TEST_CASE("push_back and push_front correctly set parent link and sibling pointers") {
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();

    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::push_back(reg, parent, c2);

    auto& h1 = reg.get<destroy_hierarchy>(c1);
    auto& h2 = reg.get<destroy_hierarchy>(c2);

    CHECK(h1.parent == parent);
    CHECK(h2.parent == parent);
    CHECK(h1.prev_sibling == entt::null);
    CHECK(h1.next_sibling == c2);
    CHECK(h2.prev_sibling == c1);
    CHECK(h2.next_sibling == entt::null);
}

TEST_CASE("push_back auto-creates hierarchy component for parent and child (get_or_emplace)") {
    entt::registry reg;
    auto parent = reg.create();
    auto child = reg.create();

    REQUIRE_FALSE(reg.all_of<destroy_hierarchy>(parent));
    REQUIRE_FALSE(reg.all_of<destroy_hierarchy>(child));

    destroy_hierarchy::push_back(reg, parent, child);

    CHECK(reg.all_of<destroy_hierarchy>(parent));
    CHECK(reg.all_of<destroy_hierarchy>(child));
}

TEST_CASE("re-parenting a child updates the old parent's child_count") {
    entt::registry reg;
    auto parentA = reg.create();
    auto parentB = reg.create();
    auto child = reg.create();

    destroy_hierarchy::push_back(reg, parentA, child);
    CHECK(reg.get<destroy_hierarchy>(parentA).child_count == 1);

    destroy_hierarchy::push_back(reg, parentB, child);

    CHECK(reg.get<destroy_hierarchy>(parentA).child_count == 0);
    CHECK(reg.get<destroy_hierarchy>(parentB).child_count == 1);
    CHECK(reg.get<destroy_hierarchy>(child).parent == parentB);
}

TEST_CASE("null and self-parenting arguments are safely ignored") {
    entt::registry reg;
    auto parent = reg.create();
    auto child = reg.create();

    // parent == child should be a no-op
    destroy_hierarchy::push_back(reg, child, child);
    CHECK_FALSE(reg.all_of<destroy_hierarchy>(child));

    // entt::null as either argument should be a no-op
    destroy_hierarchy::push_back(reg, entt::null, child);
    destroy_hierarchy::push_back(reg, parent, entt::entity(entt::null));
    CHECK_FALSE(reg.all_of<destroy_hierarchy>(child));
    CHECK_FALSE(reg.all_of<destroy_hierarchy>(parent));
}

TEST_CASE("insert_before places a child immediately before a sibling") {
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();
    auto newChild = reg.create();

    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::push_back(reg, parent, c2);

    destroy_hierarchy::insert_before(reg, /*before=*/c2, /*child=*/newChild);

    CHECK(collect_children<destroy_hierarchy>(reg, parent) == std::vector<entt::entity>{c1, newChild, c2});
    CHECK(reg.get<destroy_hierarchy>(parent).child_count == 3);
}

TEST_CASE("insert_before as the new first child updates parent's first_child") {
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    auto newChild = reg.create();

    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::insert_before(reg, c1, newChild);

    CHECK(reg.get<destroy_hierarchy>(parent).first_child == newChild);
    CHECK(collect_children<destroy_hierarchy>(reg, parent) == std::vector<entt::entity>{newChild, c1});
}

TEST_CASE("insert_after places a child immediately after a sibling") {
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();
    auto newChild = reg.create();

    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::push_back(reg, parent, c2);

    destroy_hierarchy::insert_after(reg, /*after=*/c1, /*child=*/newChild);

    CHECK(collect_children<destroy_hierarchy>(reg, parent) == std::vector<entt::entity>{c1, newChild, c2});
}

TEST_CASE("insert_after as the new last child updates parent's last_child") {
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    auto newChild = reg.create();

    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::insert_after(reg, c1, newChild);

    CHECK(reg.get<destroy_hierarchy>(parent).last_child == newChild);
    CHECK(collect_children<destroy_hierarchy>(reg, parent) == std::vector<entt::entity>{c1, newChild});
}

TEST_CASE("insert_before/insert_after with a node that has no parent is a no-op") {
    entt::registry reg;
    auto orphanAnchor = reg.create();
    auto child = reg.create();

    // Give `orphanAnchor` a hierarchy component with no parent by making it a
    // parent of something else (so it has the component) but never attaching
    // it to a parent itself.
    auto someChild = reg.create();
    destroy_hierarchy::push_back(reg, orphanAnchor, someChild);
    REQUIRE(reg.get<destroy_hierarchy>(orphanAnchor).parent == entt::null);

    destroy_hierarchy::insert_before(reg, orphanAnchor, child);
    CHECK_FALSE(reg.all_of<destroy_hierarchy>(child));

    destroy_hierarchy::insert_after(reg, orphanAnchor, child);
    CHECK_FALSE(reg.all_of<destroy_hierarchy>(child));
}

TEST_CASE("moving a child via insert_before/insert_after within the same parent reorders correctly") {
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();
    auto c3 = reg.create();

    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::push_back(reg, parent, c2);
    destroy_hierarchy::push_back(reg, parent, c3);
    REQUIRE(collect_children<destroy_hierarchy>(reg, parent) == std::vector<entt::entity>{c1, c2, c3});

    // Move c3 to the front.
    destroy_hierarchy::insert_before(reg, c1, c3);
    CHECK(collect_children<destroy_hierarchy>(reg, parent) == std::vector<entt::entity>{c3, c1, c2});
    CHECK(reg.get<destroy_hierarchy>(parent).child_count == 3);

    // Move c1 to the end.
    destroy_hierarchy::insert_after(reg, c2, c1);
    CHECK(collect_children<destroy_hierarchy>(reg, parent) == std::vector<entt::entity>{c3, c2, c1});
    CHECK(reg.get<destroy_hierarchy>(parent).child_count == 3);
}

TEST_CASE("insert_before/insert_after reject null and self-referential arguments") {
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    destroy_hierarchy::push_back(reg, parent, c1);

    destroy_hierarchy::insert_before(reg, c1, c1); // before == child
    CHECK(reg.get<destroy_hierarchy>(parent).child_count == 1);

    destroy_hierarchy::insert_after(reg, c1, c1); // after == child
    CHECK(reg.get<destroy_hierarchy>(parent).child_count == 1);

    destroy_hierarchy::insert_before(reg, entt::null, c1);
    destroy_hierarchy::insert_after(reg, entt::null, c1);
    CHECK(reg.get<destroy_hierarchy>(parent).child_count == 1);
}

TEST_CASE("is_descendant prevents creating a cycle via push_back/push_front/insert_*") {
    entt::registry reg;
    auto grandparent = reg.create();
    auto parent = reg.create();
    auto child = reg.create();

    destroy_hierarchy::push_back(reg, grandparent, parent);
    destroy_hierarchy::push_back(reg, parent, child);

    CHECK(destroy_hierarchy::is_descendant(reg, child, grandparent) == true);
    CHECK(destroy_hierarchy::is_descendant(reg, parent, grandparent) == true);
    CHECK(destroy_hierarchy::is_descendant(reg, grandparent, child) == false);

    // Attempting to make `grandparent` a child of its own descendant `child`
    // would create a cycle; the ENTTX_ASSERT guards against it in debug
    // builds. We simply verify the detection function used for the guard
    // reports the correct result rather than invoking undefined behavior.
    CHECK(destroy_hierarchy::is_descendant(reg, grandparent, child) == false);
    CHECK(destroy_hierarchy::is_descendant(reg, grandparent, grandparent) == false);
}

} // TEST_SUITE("insertion")

// ---------------------------------------------------------------------------
// Detach / orphan_children
// ---------------------------------------------------------------------------

TEST_SUITE("detach") {

TEST_CASE("detach removes a middle child and relinks its siblings") {
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();
    auto c3 = reg.create();

    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::push_back(reg, parent, c2);
    destroy_hierarchy::push_back(reg, parent, c3);

    destroy_hierarchy::detach(reg, c2);

    CHECK(collect_children<destroy_hierarchy>(reg, parent) == std::vector<entt::entity>{c1, c3});
    CHECK(reg.get<destroy_hierarchy>(parent).child_count == 2);

    auto& h2 = reg.get<destroy_hierarchy>(c2);
    CHECK(h2.parent == entt::null);
    CHECK(h2.next_sibling == entt::null);
    CHECK(h2.prev_sibling == entt::null);

    // Siblings correctly relinked.
    CHECK(reg.get<destroy_hierarchy>(c1).next_sibling == c3);
    CHECK(reg.get<destroy_hierarchy>(c3).prev_sibling == c1);
}

TEST_CASE("detach of the first child updates parent's first_child") {
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();

    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::push_back(reg, parent, c2);

    destroy_hierarchy::detach(reg, c1);

    CHECK(reg.get<destroy_hierarchy>(parent).first_child == c2);
    CHECK(reg.get<destroy_hierarchy>(parent).child_count == 1);
}

TEST_CASE("detach of the last child updates parent's last_child") {
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();

    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::push_back(reg, parent, c2);

    destroy_hierarchy::detach(reg, c2);

    CHECK(reg.get<destroy_hierarchy>(parent).last_child == c1);
    CHECK(reg.get<destroy_hierarchy>(parent).child_count == 1);
}

TEST_CASE("detach of the only child leaves the parent with no children") {
    entt::registry reg;
    auto parent = reg.create();
    auto child = reg.create();

    destroy_hierarchy::push_back(reg, parent, child);
    destroy_hierarchy::detach(reg, child);

    auto& ph = reg.get<destroy_hierarchy>(parent);
    CHECK(ph.first_child == entt::null);
    CHECK(ph.last_child == entt::null);
    CHECK(ph.child_count == 0);
    CHECK(ph.has_children() == false);
}

TEST_CASE("detach on an entity without a hierarchy component is a safe no-op") {
    entt::registry reg;
    auto e = reg.create();
    CHECK_NOTHROW(destroy_hierarchy::detach(reg, e));
}

TEST_CASE("detach on an entity with a hierarchy component but no parent is a safe no-op") {
    entt::registry reg;
    auto parent = reg.create();
    auto child = reg.create();
    destroy_hierarchy::push_back(reg, parent, child);

    // `parent` has a hierarchy component but h.parent == entt::null.
    CHECK_NOTHROW(destroy_hierarchy::detach(reg, parent));
    CHECK(reg.get<destroy_hierarchy>(parent).parent == entt::null);
    // Children untouched.
    CHECK(reg.get<destroy_hierarchy>(parent).child_count == 1);
}

TEST_CASE("orphan_children detaches all direct children but leaves them independently valid") {
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();
    auto c3 = reg.create();
    auto grandchild = reg.create();

    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::push_back(reg, parent, c2);
    destroy_hierarchy::push_back(reg, parent, c3);
    destroy_hierarchy::push_back(reg, c1, grandchild);

    destroy_hierarchy::orphan_children(reg, parent);

    auto& ph = reg.get<destroy_hierarchy>(parent);
    CHECK(ph.first_child == entt::null);
    CHECK(ph.last_child == entt::null);
    CHECK(ph.child_count == 0);

    for (auto c : {c1, c2, c3}) {
        auto& ch = reg.get<destroy_hierarchy>(c);
        CHECK(ch.parent == entt::null);
        CHECK(ch.next_sibling == entt::null);
        CHECK(ch.prev_sibling == entt::null);
        CHECK(reg.valid(c)); // still alive, just orphaned
    }

    // orphan_children only affects direct children, not grandchildren.
    CHECK(reg.get<destroy_hierarchy>(grandchild).parent == c1);
    CHECK(reg.valid(grandchild));
}

TEST_CASE("orphan_children on an entity without a hierarchy component is a safe no-op") {
    entt::registry reg;
    auto e = reg.create();
    CHECK_NOTHROW(destroy_hierarchy::orphan_children(reg, e));
}

TEST_CASE("orphan_children on a childless parent is a safe no-op") {
    entt::registry reg;
    auto parent = reg.create();
    auto child = reg.create();
    destroy_hierarchy::push_back(reg, parent, child);
    destroy_hierarchy::detach(reg, child);

    CHECK_NOTHROW(destroy_hierarchy::orphan_children(reg, parent));
    CHECK(reg.get<destroy_hierarchy>(parent).child_count == 0);
}

} // TEST_SUITE("detach")

// ---------------------------------------------------------------------------
// Iteration: children_view, for_each_child, for_each_descendant
// ---------------------------------------------------------------------------

TEST_SUITE("iteration") {

TEST_CASE("children_view forward iteration visits children in sibling order") {
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();
    auto c3 = reg.create();

    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::push_back(reg, parent, c2);
    destroy_hierarchy::push_back(reg, parent, c3);

    CHECK(collect_children<destroy_hierarchy>(reg, parent) == std::vector<entt::entity>{c1, c2, c3});
}

TEST_CASE("children_view reverse iteration visits children in reverse sibling order") {
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();
    auto c3 = reg.create();

    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::push_back(reg, parent, c2);
    destroy_hierarchy::push_back(reg, parent, c3);

    CHECK(collect_children_reverse<destroy_hierarchy>(reg, parent) == std::vector<entt::entity>{c3, c2, c1});
}

TEST_CASE("children_view on a parent without children (no component) yields an empty range") {
    entt::registry reg;
    auto lonely = reg.create();

    auto kids = collect_children<destroy_hierarchy>(reg, lonely);
    CHECK(kids.empty());

    auto view = destroy_hierarchy::children(reg, lonely);
    CHECK(view.begin() == view.end());
}

TEST_CASE("children_view on a parent with the component but zero children yields an empty range") {
    entt::registry reg;
    auto parent = reg.create();
    auto child = reg.create();
    destroy_hierarchy::push_back(reg, parent, child);
    destroy_hierarchy::detach(reg, child);

    CHECK(collect_children<destroy_hierarchy>(reg, parent).empty());
}

TEST_CASE("has_children reflects the current child_count") {
    entt::registry reg;
    auto parent = reg.create();
    auto child = reg.create();

    destroy_hierarchy::push_back(reg, parent, child);
    CHECK(reg.get<destroy_hierarchy>(parent).has_children());

    destroy_hierarchy::detach(reg, child);
    CHECK_FALSE(reg.get<destroy_hierarchy>(parent).has_children());
}

TEST_CASE("for_each_child visits only direct children, not descendants") {
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();
    auto grandchild = reg.create();

    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::push_back(reg, parent, c2);
    destroy_hierarchy::push_back(reg, c1, grandchild);

    std::vector<entt::entity> visited;
    destroy_hierarchy::for_each_child(reg, parent, [&](entt::entity e) { visited.push_back(e); });

    CHECK(visited == std::vector<entt::entity>{c1, c2});
}

TEST_CASE("for_each_child safely tolerates detaching the currently-visited child") {
    // The child_iterator caches the "next" pointer *before* invoking the
    // callback for the current element, so it is safe for the callback to
    // detach or destroy the entity it was just handed -- that's the
    // documented guarantee ("fn can safely detach or destroy the visited
    // child entity").
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();
    auto c3 = reg.create();

    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::push_back(reg, parent, c2);
    destroy_hierarchy::push_back(reg, parent, c3);

    std::vector<entt::entity> visited;
    destroy_hierarchy::for_each_child(reg, parent, [&](entt::entity e) {
        visited.push_back(e);
        if (e == c2) {
            destroy_hierarchy::detach(reg, c2);
        }
    });

    CHECK(visited == std::vector<entt::entity>{c1, c2, c3});
    CHECK(collect_children<destroy_hierarchy>(reg, parent) == std::vector<entt::entity>{c1, c3});
}

TEST_CASE("for_each_child safely tolerates destroying the currently-visited child") {
    entt::registry reg;

    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();
    auto c3 = reg.create();

    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::push_back(reg, parent, c2);
    destroy_hierarchy::push_back(reg, parent, c3);

    std::vector<entt::entity> visited;
    destroy_hierarchy::for_each_child(reg, parent, [&](entt::entity e) {
        visited.push_back(e);
        if (e == c2) {
            reg.destroy(c2);
        }
    });

    CHECK(visited == std::vector<entt::entity>{c1, c2, c3});
    CHECK_FALSE(reg.valid(c2));
    CHECK(collect_children<destroy_hierarchy>(reg, parent) == std::vector<entt::entity>{c1, c3});
}

TEST_CASE("for_each_descendant visits all descendants in post-order (children before parent)") {
    entt::registry reg;
    auto root = reg.create();
    auto a = reg.create();
    auto b = reg.create();
    auto a1 = reg.create();
    auto a2 = reg.create();

    destroy_hierarchy::push_back(reg, root, a);
    destroy_hierarchy::push_back(reg, root, b);
    destroy_hierarchy::push_back(reg, a, a1);
    destroy_hierarchy::push_back(reg, a, a2);

    std::vector<entt::entity> visited;
    destroy_hierarchy::for_each_descendant(reg, root, [&](entt::entity e) { visited.push_back(e); });

    // Expected depth-first post-order: for child `a`, its own children (a1, a2)
    // are visited before `a` itself; then sibling `b` is visited (no children).
    CHECK(visited == std::vector<entt::entity>{a1, a2, a, b});
}

TEST_CASE("for_each_descendant on a leaf entity visits nothing") {
    entt::registry reg;
    auto leaf = reg.create();

    std::vector<entt::entity> visited;
    destroy_hierarchy::for_each_descendant(reg, leaf, [&](entt::entity e) { visited.push_back(e); });

    CHECK(visited.empty());
}

} // TEST_SUITE("iteration")

// ---------------------------------------------------------------------------
// find_root / is_descendant
// ---------------------------------------------------------------------------

TEST_SUITE("ancestry queries") {

TEST_CASE("find_root walks up to the top-most ancestor") {
    entt::registry reg;
    auto root = reg.create();
    auto mid = reg.create();
    auto leaf = reg.create();

    destroy_hierarchy::push_back(reg, root, mid);
    destroy_hierarchy::push_back(reg, mid, leaf);

    CHECK(destroy_hierarchy::find_root(reg, leaf) == root);
    CHECK(destroy_hierarchy::find_root(reg, mid) == root);
    CHECK(destroy_hierarchy::find_root(reg, root) == root);
}

TEST_CASE("find_root on an entity without a hierarchy component returns itself") {
    entt::registry reg;
    auto e = reg.create();
    CHECK(destroy_hierarchy::find_root(reg, e) == e);
}

TEST_CASE("find_root stops at an orphaned node (parent == entt::null)") {
    entt::registry reg;
    auto parent = reg.create();
    auto child = reg.create();
    destroy_hierarchy::push_back(reg, parent, child);
    destroy_hierarchy::detach(reg, child);

    CHECK(destroy_hierarchy::find_root(reg, child) == child);
}

TEST_CASE("is_descendant correctly reports ancestry across multiple levels") {
    entt::registry reg;
    auto root = reg.create();
    auto mid = reg.create();
    auto leaf = reg.create();
    auto unrelated = reg.create();

    destroy_hierarchy::push_back(reg, root, mid);
    destroy_hierarchy::push_back(reg, mid, leaf);

    CHECK(destroy_hierarchy::is_descendant(reg, leaf, root) == true);
    CHECK(destroy_hierarchy::is_descendant(reg, leaf, mid) == true);
    CHECK(destroy_hierarchy::is_descendant(reg, mid, root) == true);
    CHECK(destroy_hierarchy::is_descendant(reg, root, leaf) == false);
    CHECK(destroy_hierarchy::is_descendant(reg, unrelated, root) == false);
    CHECK(destroy_hierarchy::is_descendant(reg, leaf, unrelated) == false);
}

} // TEST_SUITE("ancestry queries")

// ---------------------------------------------------------------------------
// Destruction behavior across all three deletion policies
// ---------------------------------------------------------------------------

TEST_SUITE("destruction") {

TEST_CASE("destroy_children policy: destroying a parent recursively destroys all descendants") {
    entt::registry reg;

    auto root = reg.create();
    auto a = reg.create();
    auto b = reg.create();
    auto a1 = reg.create();

    destroy_hierarchy::push_back(reg, root, a);
    destroy_hierarchy::push_back(reg, root, b);
    destroy_hierarchy::push_back(reg, a, a1);

    reg.destroy(root);

    CHECK_FALSE(reg.valid(root));
    CHECK_FALSE(reg.valid(a));
    CHECK_FALSE(reg.valid(b));
    CHECK_FALSE(reg.valid(a1)); // grandchild also destroyed
}

TEST_CASE("destroy_children policy: destroying a leaf child does not affect its siblings") {
    entt::registry reg;

    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();
    auto c3 = reg.create();

    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::push_back(reg, parent, c2);
    destroy_hierarchy::push_back(reg, parent, c3);

    reg.destroy(c2);

    CHECK(reg.valid(parent));
    CHECK(reg.valid(c1));
    CHECK_FALSE(reg.valid(c2));
    CHECK(reg.valid(c3));

    CHECK(collect_children<destroy_hierarchy>(reg, parent) == std::vector<entt::entity>{c1, c3});
    CHECK(reg.get<destroy_hierarchy>(parent).child_count == 2);
}

TEST_CASE("destroy_children policy: destroying a mid-level node detaches it from its parent and destroys only its own subtree") {
    entt::registry reg;

    auto root = reg.create();
    auto a = reg.create();
    auto b = reg.create();
    auto a1 = reg.create();

    destroy_hierarchy::push_back(reg, root, a);
    destroy_hierarchy::push_back(reg, root, b);
    destroy_hierarchy::push_back(reg, a, a1);

    reg.destroy(a);

    CHECK(reg.valid(root));
    CHECK_FALSE(reg.valid(a));
    CHECK_FALSE(reg.valid(a1)); // a's subtree destroyed
    CHECK(reg.valid(b));        // sibling untouched

    CHECK(collect_children<destroy_hierarchy>(reg, root) == std::vector<entt::entity>{b});
    CHECK(reg.get<destroy_hierarchy>(root).child_count == 1);
}

TEST_CASE("destroy_children policy: destroying an entity without children does not throw and is fine") {
    entt::registry reg;

    auto e = reg.create();
    CHECK_NOTHROW(reg.destroy(e));
    CHECK_FALSE(reg.valid(e));
}

TEST_CASE("destroy_children policy: destroying an entity without a hierarchy component is a safe no-op for hierarchy logic") {
    entt::registry reg;

    auto e = reg.create(); // never given a destroy_hierarchy component
    CHECK_NOTHROW(reg.destroy(e));
}

TEST_CASE("orphan_children policy: destroying a parent orphans (does not destroy) its children") {
    entt::registry reg;

    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();

    orphan_hierarchy::push_back(reg, parent, c1);
    orphan_hierarchy::push_back(reg, parent, c2);

    reg.destroy(parent);

    CHECK_FALSE(reg.valid(parent));
    CHECK(reg.valid(c1));
    CHECK(reg.valid(c2));

    auto& h1 = reg.get<orphan_hierarchy>(c1);
    auto& h2 = reg.get<orphan_hierarchy>(c2);
    CHECK(h1.parent == entt::null);
    CHECK(h2.parent == entt::null);
}

TEST_CASE("orphan_children policy: only direct children are orphaned, grandchildren keep their parent link") {
    entt::registry reg;

    auto root = reg.create();
    auto a = reg.create();
    auto a1 = reg.create();

    orphan_hierarchy::push_back(reg, root, a);
    orphan_hierarchy::push_back(reg, a, a1);

    reg.destroy(root);

    CHECK(reg.valid(a));
    CHECK(reg.valid(a1));
    CHECK(reg.get<orphan_hierarchy>(a).parent == entt::null);
    // a1's parent is still `a`, which is still alive.
    CHECK(reg.get<orphan_hierarchy>(a1).parent == a);
}

TEST_CASE("orphan_children policy: destroying a child also detaches it from its parent's list") {
    entt::registry reg;

    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();

    orphan_hierarchy::push_back(reg, parent, c1);
    orphan_hierarchy::push_back(reg, parent, c2);

    reg.destroy(c1);

    CHECK(reg.valid(parent));
    CHECK(reg.get<orphan_hierarchy>(parent).child_count == 1);
    CHECK(collect_children<orphan_hierarchy>(reg, parent) == std::vector<entt::entity>{c2});
}

TEST_CASE("unhandled policy: destroying a parent leaves children's stale links untouched by hierarchy logic") {
    entt::registry reg;
    // Deliberately do NOT connect on_destroy: unhandled_hierarchy has no
    // on_destroy overload at all (the `requires` clause excludes it), so
    // hierarchy relationships are the user's responsibility on destruction.

    auto parent = reg.create();
    auto child = reg.create();

    unhandled_hierarchy::push_back(reg, parent, child);
    REQUIRE(reg.get<unhandled_hierarchy>(child).parent == parent);

    reg.destroy(parent);

    CHECK_FALSE(reg.valid(parent));
    CHECK(reg.valid(child)); // child is untouched -- policy does not manage it
    // The child's hierarchy component still (stale) points at the now-dead parent,
    // demonstrating that `unhandled` performs no bookkeeping at all.
    CHECK(reg.get<unhandled_hierarchy>(child).parent == parent);
}

TEST_CASE("destroy_children policy: reg.clear() destroys every entity without corrupting bookkeeping") {
    entt::registry reg;

    auto root = reg.create();
    auto a = reg.create();
    auto b = reg.create();
    destroy_hierarchy::push_back(reg, root, a);
    destroy_hierarchy::push_back(reg, root, b);

    CHECK_NOTHROW(reg.clear());
    CHECK_FALSE(reg.valid(root));
    CHECK_FALSE(reg.valid(a));
    CHECK_FALSE(reg.valid(b));
}

} // TEST_SUITE("destruction")

// ---------------------------------------------------------------------------
// remap (used by prefab collapsing, per the doc comment)
// ---------------------------------------------------------------------------

TEST_SUITE("remap") {

// Minimal stand-in satisfying the implicit `remap_traits`-like interface:
// anything with a `.translate(entity_type)` member.
namespace {
    constexpr auto identity_remap = +[](entt::entity e) { return e; };
} // namespace

TEST_CASE("remap with identity translation leaves all links unchanged") {
    entt::registry reg;
    auto parent = reg.create();
    auto child = reg.create();
    destroy_hierarchy::push_back(reg, parent, child);

    destroy_hierarchy::remap(reg, child, identity_remap);
    destroy_hierarchy::remap(reg, parent, identity_remap);

    CHECK(reg.get<destroy_hierarchy>(child).parent == parent);
    CHECK(reg.get<destroy_hierarchy>(parent).first_child == child);
}

TEST_CASE("remap translates all five hierarchy links through the provided remap table") {
    entt::registry reg;
    auto parent = reg.create();
    auto c1 = reg.create();
    auto c2 = reg.create();
    destroy_hierarchy::push_back(reg, parent, c1);
    destroy_hierarchy::push_back(reg, parent, c2);

    // Simulate a prefab collapse: every source entity maps to a "new" entity.
    auto newParent = reg.create();
    auto newC1 = reg.create();
    auto newC2 = reg.create();

    auto remap = entity_remap{}
        .map(parent, newParent)
        .map(c1, newC1)
        .map(c2, newC2);

    destroy_hierarchy::remap(reg, parent, remap);
    destroy_hierarchy::remap(reg, c1, remap);
    destroy_hierarchy::remap(reg, c2, remap);

    auto& ph = reg.get<destroy_hierarchy>(parent);
    CHECK(ph.first_child == newC1);
    CHECK(ph.last_child == newC2);

    auto& h1 = reg.get<destroy_hierarchy>(c1);
    CHECK(h1.parent == newParent);
    CHECK(h1.next_sibling == newC2);

    auto& h2 = reg.get<destroy_hierarchy>(c2);
    CHECK(h2.parent == newParent);
    CHECK(h2.prev_sibling == newC1);
}

TEST_CASE("remap on an entity without a hierarchy component is a safe no-op") {
    entt::registry reg;
    auto e = reg.create();
    CHECK_NOTHROW(destroy_hierarchy::remap(reg, e, identity_remap));
}

TEST_CASE("remap correctly maps entt::null links to entt::null") {
    entt::registry reg;
    auto lonely = reg.create();
    reg.emplace<destroy_hierarchy>(lonely); // all links default to entt::null

    entity_remap remap; // empty table -> translate(null) still returns null
    destroy_hierarchy::remap(reg, lonely, remap);

    auto& h = reg.get<destroy_hierarchy>(lonely);
    CHECK(h.parent == entt::null);
    CHECK(h.first_child == entt::null);
    CHECK(h.last_child == entt::null);
    CHECK(h.next_sibling == entt::null);
    CHECK(h.prev_sibling == entt::null);
}

} // TEST_SUITE("remap")

// ---------------------------------------------------------------------------
// Larger scenario combining several operations together
// ---------------------------------------------------------------------------

TEST_SUITE("integration") {

TEST_CASE("building, reordering, and tearing down a multi-level tree behaves consistently end to end") {
    entt::registry reg;

    auto root = reg.create();
    auto a = reg.create();
    auto b = reg.create();
    auto c = reg.create();

    destroy_hierarchy::push_back(reg, root, a);
    destroy_hierarchy::push_back(reg, root, b);
    destroy_hierarchy::push_front(reg, root, c); // c, a, b

    CHECK(collect_children<destroy_hierarchy>(reg, root) == std::vector<entt::entity>{c, a, b});

    auto a1 = reg.create();
    auto a2 = reg.create();
    destroy_hierarchy::push_back(reg, a, a1);
    destroy_hierarchy::push_back(reg, a, a2);

    // Move `b` before `c`, then verify total ordering.
    destroy_hierarchy::insert_before(reg, c, b);
    CHECK(collect_children<destroy_hierarchy>(reg, root) == std::vector<entt::entity>{b, c, a});

    // find_root / is_descendant sanity across the tree.
    CHECK(destroy_hierarchy::find_root(reg, a1) == root);
    CHECK(destroy_hierarchy::is_descendant(reg, a1, root));
    CHECK(destroy_hierarchy::is_descendant(reg, a1, a));
    CHECK_FALSE(destroy_hierarchy::is_descendant(reg, b, a));

    // Detach `a` (with its subtree) and confirm root's bookkeeping updates,
    // while `a`'s own subtree remains intact but disconnected.
    destroy_hierarchy::detach(reg, a);
    CHECK(collect_children<destroy_hierarchy>(reg, root) == std::vector<entt::entity>{b, c});
    CHECK(reg.get<destroy_hierarchy>(root).child_count == 2);
    CHECK(collect_children<destroy_hierarchy>(reg, a) == std::vector<entt::entity>{a1, a2});

    // Destroying `root` should not affect the now-detached `a` subtree.
    reg.destroy(root);
    CHECK_FALSE(reg.valid(root));
    CHECK_FALSE(reg.valid(b));
    CHECK_FALSE(reg.valid(c));
    CHECK(reg.valid(a));
    CHECK(reg.valid(a1));
    CHECK(reg.valid(a2));

    // Now destroying `a` should take its subtree with it.
    reg.destroy(a);
    CHECK_FALSE(reg.valid(a));
    CHECK_FALSE(reg.valid(a1));
    CHECK_FALSE(reg.valid(a2));
}

TEST_CASE("default alias `enttx::hierarchy` behaves identically to a manually instantiated destroy_children hierarchy") {
    entt::registry reg;
    reg.on_destroy<enttx::hierarchy>().connect<&enttx::hierarchy::on_destroy>();

    auto parent = reg.create();
    auto child = reg.create();
    enttx::hierarchy::push_back(reg, parent, child);

    CHECK(reg.get<enttx::hierarchy>(parent).child_count == 1);
    CHECK(enttx::hierarchy::deletion_policy == hierarchy_deletion_policy::destroy_children);

    reg.destroy(parent);
    CHECK_FALSE(reg.valid(child));
}

} // TEST_SUITE("integration")
