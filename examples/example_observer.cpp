// example_observer.cpp
//
// enttx::observer records what happened to a component (constructed, updated,
// destroyed) and lets you turn that history into a `commit` - a serializable,
// invertible batch of changes. This example walks through the two things a
// commit is good for:
//
//   1. Undo/redo within a single registry (invert() + apply()).
//   2. Syncing changes from one registry to another, e.g. server -> client
//      (commit_snapshot / commit_loader + apply() with an entity remap).
//
#include "archive.hpp"

#include <enttx/change_mixin.hpp>
#include <enttx/observer.hpp>
#include <enttx/stable_id.hpp>

#include <entt/entity/registry.hpp>

#include <format>
#include <iostream>

// Define a component type.
struct transform {
    float x{ 0.f }, y{ 0.f };
};

template<typename Archive>
void serialize(Archive& ar, transform& t) { ar( t.x, t.y ); }

// To track invertible changes to a component, you need to make its storage type a
// change_mixin of the underlying storage. This is done by specializing
// entt::storage_type for your component type. The change_mixin is an 
// entt::sigh_mixin with an extra on_pre_update() signal that is emitted before a component is updated. 
template<>
struct entt::storage_type<transform> {
    using type = enttx::change_mixin<entt::basic_storage<transform>>;
};

struct name {
    std::string value;
};

template<typename Archive>
void serialize(Archive& ar, name& n) { ar( n.value ); }

template<>
struct entt::storage_type<name> {
    using type = enttx::change_mixin<entt::basic_storage<name>>;
};

// Empty components (entt::component_traits<T>::page_size == 0) work too.
struct frozen {};

template<>
struct entt::storage_type<frozen> {
    using type = enttx::change_mixin<entt::basic_storage<frozen>>;
};

void print_state(entt::registry& registry, std::string_view label) {
    std::cout << "-- " << label << " --\n";
    registry.view<entt::entity>().each([&](entt::entity e) {
        std::cout << std::format(
            "  entity {} with name '{}': {}{}\n",
            entt::to_integral(e), 
            registry.any_of<name>(e) ? registry.get<name>(e).value : "<no name>",
            registry.any_of<transform>(e) 
                ? std::format("transform({}, {})", registry.get<transform>(e).x, registry.get<transform>(e).y) 
                : "<no transform>",
            registry.all_of<frozen>(e) ? ", frozen" : "");
    });
}

// -----------------------------------------------------------------------------
// Demo 1: undo, within a single registry.
//
// An observer<T> watches one component's storage; collect() drains whatever
// it saw since the last call into a commit. Because every change knows how to
// invert itself (construct <-> destroy, old_value <-> new_value), a whole
// commit can be inverted too - giving you a free undo.
// -----------------------------------------------------------------------------
void undo_demo() {
    std::cout << "=== Undo demo ===\n";

    entt::registry registry;
    entt::entity entity = registry.create();

	// Add a name before the observer is created, so that the observer doesn't see it.
	// You can also call observer->disconnect() to temporarily stop observing changes, then reconnect() later.
    registry.emplace<name>( entity, "Player" );

    enttx::observers observers;
    observers.emplace_back(enttx::observe<transform>(registry));
    observers.emplace_back(enttx::observe<name>(registry));
    observers.emplace_back(enttx::observe<frozen>(registry));

    registry.emplace<transform>(entity, 1.f, 2.f);
    registry.patch<transform>(entity, [](transform& t) { t.x = 5.f; });
    registry.patch<name>(entity, [](name& n) { n.value = "Hero"; });
    registry.emplace<frozen>(entity);

    print_state(registry, "after edits");

    enttx::commit changes{};
    for (auto& observer : observers) {
        observer->collect(changes);
    }

    enttx::commit undo = changes.invert();
    undo.apply(registry);

    print_state(registry, "after undo");
    std::cout << '\n';
}

// -----------------------------------------------------------------------------
// Demo 2: sync changes from one registry to another over the wire.
//
// Server and client entities aren't the same entt::entity - so the commit is
// serialized with a stable_id in place of the entity, and rebuilt on the
// client by mapping that id back to a local entity. This is entirely up to
// the caller: enttx never assumes how entities correspond across registries.
// -----------------------------------------------------------------------------
void network_sync_demo() {
    std::cout << "=== Network sync demo ===\n";

    using network_id = enttx::basic_stable_id<std::uint64_t, struct network_id_tag>;
    const network_id player_id{ 42 };

    std::string wire_data;

    // --- Server: make some changes, then serialize a commit describing them.
    {
        entt::registry server;
        entt::entity entity = server.create();
        server.emplace<network_id>(entity, player_id);

        enttx::observers observers;
        observers.emplace_back(enttx::observe<transform>(server));
        observers.emplace_back(enttx::observe<name>(server));

        server.emplace<transform>(entity, 1.f, 2.f);
        server.patch<transform>(entity, [](transform& t) { t.x = 3.f; });
        server.emplace<name>(entity, "Player");

        enttx::commit changes{};
        for (auto& observer : observers) {
            observer->collect(changes);
        }

        // The entity_handler here is what maps server entities to something
        // the client can understand: its stable network_id.
        output_archive ar{};

        const auto entity_handler = [&](entt::entity e) {
            return server.get<network_id>(e).value;
        };

        enttx::commit_snapshot{ changes }
            .get<transform>(ar, entity_handler)
            .get<name>(ar, entity_handler);

        wire_data = ar.stream.str();
    }

    // --- Client: already has its own entity for player_id; apply the commit
    //     by mapping the incoming network_id back to that local entity.
    {
        entt::registry client;
        entt::entity entity = client.create();
        client.emplace<network_id>(entity, player_id);

        std::unordered_map<network_id, entt::entity> id_to_entity{ { player_id, entity } };

        enttx::commit changes{};
        {
            input_archive ar{ wire_data };

            const auto entity_handler = [&](input_archive& ar) {
                network_id id{};
                ar(id.value);
                return id_to_entity.at(id);
            };

            enttx::commit_loader{ changes }
                .get<transform>(ar, entity_handler)
                .get<name>(ar, entity_handler);
        }

        changes.apply(client);
        print_state(client, "client after applying server commit");
    }
}

int main() {
    undo_demo();
    network_sync_demo();
}