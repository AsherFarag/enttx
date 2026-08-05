#include "doctest/doctest.h"

#include <entt/entt.hpp>
#include <enttx/hierarchy.hpp>

TEST_CASE("Attach child creates hierarchy components")
{
    entt::registry registry;

    const auto parent = registry.create();
    const auto child = registry.create();

    enttx::hierarchy::push_front(registry, parent, child);

    REQUIRE(registry.all_of<enttx::hierarchy>(parent));
    REQUIRE(registry.all_of<enttx::hierarchy>(child));

    const auto& parentHierarchy = registry.get<enttx::hierarchy>(parent);
    const auto& childHierarchy = registry.get<enttx::hierarchy>(child);

    CHECK(parentHierarchy.first_child == child);
    CHECK(childHierarchy.parent == parent);
    CHECK(childHierarchy.next_sibling == entt::null);
    CHECK(childHierarchy.prev_sibling == entt::null);
}


TEST_CASE("Children are inserted at the front")
{
    entt::registry registry;

    auto parent = registry.create();
    auto child1 = registry.create();
    auto child2 = registry.create();
    auto child3 = registry.create();

    enttx::hierarchy::push_front(registry, parent, child1);
    enttx::hierarchy::push_front(registry, parent, child2);
    enttx::hierarchy::push_front(registry, parent, child3);

    std::vector<entt::entity> children;

    enttx::hierarchy::for_each_child(registry, parent,
        [&](auto child)
        {
            children.push_back(child);
        });

    REQUIRE(children.size() == 3);

    CHECK(children[0] == child3);
    CHECK(children[1] == child2);
    CHECK(children[2] == child1);
}


TEST_CASE("Sibling links are maintained")
{
    entt::registry registry;

    auto parent = registry.create();
    auto child1 = registry.create();
    auto child2 = registry.create();

    enttx::hierarchy::push_front(registry, parent, child1);
    enttx::hierarchy::push_front(registry, parent, child2);

    const auto& h1 = registry.get<enttx::hierarchy>(child1);
    const auto& h2 = registry.get<enttx::hierarchy>(child2);

    CHECK(h2.next_sibling == child1);
    CHECK(h1.prev_sibling == child2);
}


TEST_CASE("Detach removes child from hierarchy")
{
    entt::registry registry;

    auto parent = registry.create();
    auto child = registry.create();

    enttx::hierarchy::push_front(registry, parent, child);

    enttx::hierarchy::detach(registry, child);

    const auto& parentHierarchy = registry.get<enttx::hierarchy>(parent);
    const auto& childHierarchy = registry.get<enttx::hierarchy>(child);

    CHECK(parentHierarchy.first_child == entt::null);

    CHECK(childHierarchy.parent == entt::null);
    CHECK(childHierarchy.next_sibling == entt::null);
    CHECK(childHierarchy.prev_sibling == entt::null);
}


TEST_CASE("Reattaching moves child to new parent")
{
    entt::registry registry;

    auto parent1 = registry.create();
    auto parent2 = registry.create();
    auto child = registry.create();

    enttx::hierarchy::push_front(registry, parent1, child);

    CHECK(registry.get<enttx::hierarchy>(child).parent == parent1);

    enttx::hierarchy::push_front(registry, parent2, child);

    CHECK(registry.get<enttx::hierarchy>(child).parent == parent2);
    CHECK(registry.get<enttx::hierarchy>(parent1).first_child == entt::null);
    CHECK(registry.get<enttx::hierarchy>(parent2).first_child == child);
}


TEST_CASE("for_each_child visits children")
{
    entt::registry registry;

    auto parent = registry.create();
    auto child1 = registry.create();
    auto child2 = registry.create();

    enttx::hierarchy::push_front(registry, parent, child1);
    enttx::hierarchy::push_front(registry, parent, child2);

    size_t count = 0;

    enttx::hierarchy::for_each_child(registry, parent,
        [&](auto)
        {
            count++;
        });

    CHECK(count == 2);
}


TEST_CASE("for_each_descendant visits entire tree")
{
    entt::registry registry;

    auto root = registry.create();
    auto child = registry.create();
    auto grandchild = registry.create();

    enttx::hierarchy::push_front(registry, root, child);
    enttx::hierarchy::push_front(registry, child, grandchild);

    std::vector<entt::entity> result;

    enttx::hierarchy::for_each_descendant(registry, root,
        [&](auto entity)
        {
            result.push_back(entity);
        });

    REQUIRE(result.size() == 2);

    CHECK(result[0] == child);
    CHECK(result[1] == grandchild);
}


TEST_CASE("find_root returns root parent")
{
    entt::registry registry;

    auto root = registry.create();
    auto child = registry.create();
    auto grandchild = registry.create();

    enttx::hierarchy::push_front(registry, root, child);
    enttx::hierarchy::push_front(registry, child, grandchild);

    CHECK(enttx::hierarchy::find_root(registry, grandchild) == root);
}


TEST_CASE("for_each_child on entity without hierarchy does nothing")
{
    entt::registry registry;

    auto entity = registry.create();

    size_t count = 0;

    enttx::hierarchy::for_each_child(registry, entity,
        [&](auto)
        {
            count++;
        });

    CHECK(count == 0);
}

TEST_CASE("Child can be detached during iteration")
{
    entt::registry registry;

    auto parent = registry.create();
    auto child = registry.create();

    enttx::hierarchy::push_front(registry, parent, child);

    enttx::hierarchy::for_each_child(registry, parent,
        [&](auto entity)
        {
            enttx::hierarchy::detach(registry, entity);
        });

    CHECK(registry.get<enttx::hierarchy>(parent).first_child == entt::null);
}