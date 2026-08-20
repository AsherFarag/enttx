/*!
 * @file observer.hpp
 * @brief Observer utility for enttx.
 * @dependencies change_mixin.hpp
 */

// TODO: Not all changes need to be invertible. Implement an api where users can specify if they want the changes to be invertible or not. 
// For example, if commits are being sent over the network and inversions are not needed,
// then we can save some memory and bandwidth by not storing the old value for updates and destructs. 

#pragma once
#include "core.hpp"
#include "entity_remap.hpp"

#include <entt/core/type_info.hpp>
#include <entt/entity/fwd.hpp>
#include <entt/signal/fwd.hpp>
#include <entt/container/dense_map.hpp>

#include <variant>
#include <vector>
#include <memory>
#include <optional>

namespace enttx
{

template<typename, typename = entt::entity>
struct change;

template<typename Registry>
class basic_observer_base;

using observer_base = basic_observer_base<entt::registry>;

template<typename Registry>
using basic_observers = std::vector<std::unique_ptr<basic_observer_base<Registry>>>;

using observers = basic_observers<entt::registry>;

template<typename>
class basic_commit;

using commit = basic_commit<entt::registry>;

template<typename Commit>
class basic_commit_snapshot;

using commit_snapshot = basic_commit_snapshot<commit>;

template<typename Commit>
class basic_commit_loader;

using commit_loader = basic_commit_loader<commit>;

/*!
 * @brief Represents a change to a component of type `T` associated with an entity of type `Entity`.
 * @tparam T Type of the component being changed.
 * @tparam Entity Type of the entity associated with the change.
 */
template<typename T, typename Entity>
struct change {
    struct construct { T value; };

    struct destruct { T value; };

    struct update { T old_value, new_value; };

    /*! @brief Underlying entity identifier. */
    using entity_type = Entity;
    /*! @brief Type of the change being tracked. */
	using variant_type = std::variant<construct, destruct, update>;

    entity_type entity;
    variant_type payload;

    [[nodiscard]] change invert() const {
		change inverted;
        inverted.entity = this->entity;
        std::visit([&inverted](const auto& change) { 
            using ChangeType = std::decay_t<decltype(change)>;
            if constexpr(std::is_same_v<ChangeType, construct>) {
                inverted.payload = destruct{change.value};
            } else if constexpr(std::is_same_v<ChangeType, destruct>) {
                inverted.payload = construct{change.value};
            } else if constexpr(std::is_same_v<ChangeType, update>) {
                inverted.payload = update{change.new_value, change.old_value};
            } else {
                static_assert(std::false_type::value, "Unknown change type");
            }
        }, payload);
        return inverted;
    }

    template<typename... Args>
    void apply_to(entt::storage_for_t<T, entity_type, Args...>& storage, const entity_type target = entt::null) const {
        const entity_type entity = target == entt::null ? this->entity : target;
        std::visit([&storage, entity] (const auto& change) { 
            using ChangeType = std::decay_t<decltype(change)>;
            if constexpr(std::is_same_v<ChangeType, construct>) {
                storage.emplace(entity, change.value);
            } else if constexpr(std::is_same_v<ChangeType, update>) {
                storage.patch(entity, [&change](T& value) { value = change.new_value; });
            } else if constexpr(std::is_same_v<ChangeType, destruct>) {
                storage.erase(entity);
            } else {
                static_assert(std::false_type::value, "Unknown change type");
            }
        }, payload);
    }
};

template<typename T, typename Entity>
requires is_pageless<T, Entity>
struct change<T, Entity> {
    struct construct {};

    struct destruct {};

    /*! @brief Underlying entity identifier. */
    using entity_type = Entity;
    /*! @brief Type of the change being tracked. */
	using variant_type = std::variant<construct, destruct>;

    entity_type entity;
    variant_type payload;

    [[nodiscard]] change invert() const {
		change inverted;
        inverted.entity = this->entity;
        if (std::holds_alternative<construct>(payload)) {
            inverted.payload = destruct{};
        } else /*if destruct*/ {
            inverted.payload = construct{};
        }
        return inverted;
    }

    template<typename... Args>
    void apply_to(entt::storage_for_t<T, entity_type, Args...>& storage, const entity_type target = entt::null) const {
        const entity_type entity = target == entt::null ? this->entity : target;
        if (std::holds_alternative<construct>(payload)) {
            storage.emplace(entity);
        } else /*if destruct*/ {
            storage.erase(entity);
        }
    }
};

template<typename Registry>
class basic_observer_base {
public:
    using commit_type = basic_commit<Registry>;
    
    virtual ~basic_observer_base() = default;

	virtual void connect() = 0;
	virtual void disconnect() = 0;
    virtual void collect(commit_type& commit) = 0;
};

template<typename Registry>
class basic_commit {
private:
    using traits_type = entt::entt_traits<typename Registry::entity_type>;

    friend class basic_commit_snapshot<basic_commit>;
    friend class basic_commit_loader<basic_commit>;

public:
    /*! @brief Type of registry */ 
    using registry_type = Registry;
    /*! @brief Underlying entity identifier. */
    using entity_type = typename traits_type::value_type;

    using entity_remap_type = basic_entity_remap<entity_type, entity_type>;

	[[nodiscard]]
    basic_commit invert() const {
        basic_commit inverted;
        for (const auto& [storage_id, segment] : segments) {
            inverted.segments[storage_id] = segment->invert();
        }
        return inverted;
    }

    void apply(Registry& registry, const entity_remap_type* remap = nullptr) const {
        for (const auto& [storage_id, segment] : segments) {
            segment->apply(registry, storage_id, remap);
        }
    }

    template<typename T>
    void append_segment(std::vector<change<T, entity_type>>&& changes, const entt::id_type storage_id = entt::type_hash<T>::value()) {
        auto s = std::make_unique<segment<T>>();
        s->changes = std::move(changes);
        segments[storage_id] = std::move(s);
    }

private:
    struct segment_base {
        virtual ~segment_base() = default;
        virtual std::unique_ptr<segment_base> invert() const = 0;
        virtual void apply(Registry& registry, const entt::id_type storage_id, const entity_remap_type* remap = nullptr) const = 0;
    };

    template<typename T>
    struct segment final: public segment_base {
        using change_list_type = std::vector<change<T, entity_type>>;

        change_list_type changes;

        std::unique_ptr<segment_base> invert() const override {
            auto inverted = std::make_unique<segment<T>>();
            inverted->changes.reserve(changes.size());
            for (auto rit = changes.rbegin(); rit != changes.rend(); ++rit) {
                inverted->changes.emplace_back(rit->invert());
            }
            return inverted;
        }

        void apply(Registry& registry, const entt::id_type storage_id, const entity_remap_type* remap = nullptr) const override {
            auto& storage = registry.template storage<T>(storage_id);
            if (remap) {
                for (const auto& change : changes) {
                    const auto remapped_entity = (*remap)(change.entity);
                    if (remapped_entity != entt::null) {
                        change.apply_to(storage, remapped_entity);
                    }
                }
            } else {
                for (const auto& change : changes) {
                    change.apply_to(storage);
                }
            }
        }
    };

    entt::dense_map<entt::id_type, std::unique_ptr<segment_base>> segments;
};

template<typename Commit>
class basic_commit_snapshot {
public:
    using commit_type = Commit;

    basic_commit_snapshot(const commit_type& commit) noexcept
        : commit{&commit} {}

    basic_commit_snapshot(const basic_commit_snapshot&) = delete;

    basic_commit_snapshot(basic_commit_snapshot&&) noexcept = default;

    ~basic_commit_snapshot() = default;

    basic_commit_snapshot& operator=(const basic_commit_snapshot&) = delete;

    basic_commit_snapshot& operator=(basic_commit_snapshot&&) noexcept = default;

    template<typename T, typename Archive>
    const basic_commit_snapshot& get(Archive& archive, const entt::id_type storage_id = entt::type_hash<T>::value()) const {
        using segment_type = typename commit_type::template segment<T>;
        const auto it = commit->segments.find(storage_id);

        if (it == commit->segments.end()) {
            archive(std::size_t{0}); // No changes for this storage_id, serialize as empty
            return *this;
        }

        const auto& changes = static_cast<const segment_type&>(*it->second).changes;
        archive(changes.size());

        for (const auto& change : changes) {
            using change_type = std::decay_t<decltype(change)>;
            using entity_type = typename change_type::entity_type;
            archive(change.entity); // TODO: We should use a remapper here. For example, mapping the entt::entity to a network_id etc.
            archive(static_cast<std::uint8_t>(change.payload.index()));

            if constexpr(!is_pageless<T, entity_type>) {
                std::visit([&archive](const auto& c) {
                    if constexpr(std::is_same_v<std::decay_t<decltype(c)>, typename change_type::update>) {
                        archive(c.old_value, c.new_value);
                    } else {
                        archive(c.value); // construct / destroy
                    }
                }, change.payload);
            }
        }

        return *this;
    }

private:
    const commit_type* commit;
};

template<typename Commit>
struct basic_commit_loader {
public:
    using commit_type = Commit;

    basic_commit_loader(commit_type& commit) noexcept
        : commit{&commit} {}

    basic_commit_loader(const basic_commit_loader&) = delete;

    basic_commit_loader(basic_commit_loader&&) noexcept = default;

    ~basic_commit_loader() = default;

    basic_commit_loader& operator=(const basic_commit_loader&) = delete;

    basic_commit_loader& operator=(basic_commit_loader&&) noexcept = default;

    template<typename T, typename Archive>
    const basic_commit_loader& get(Archive& archive, const entt::id_type storage_id = entt::type_hash<T>::value()) const {
        using entity_type = typename commit_type::entity_type;
        using change_type = change<T, entity_type>;

        std::size_t size{};
        archive(size);

        if (size == 0u) {
            return *this;
        }

        std::vector<change_type> changes;
        changes.reserve(size);

        for (std::size_t i = 0; i < size; ++i) {
            entity_type entity{};
            archive(entity);

            std::uint8_t index{};
            archive(index);

            typename change_type::variant_type payload;

            // Index order must match change<T>::variant_type: construct(0), destroy(1), update(2).
            switch (index) {
            case 0: {
                using construct = typename change_type::construct;
                if constexpr(is_pageless<T, entity_type>) {
                    payload = construct{};
                } else {
                    T value{};
                    archive(value);
                    payload = construct{std::move(value)};
                }
                break;
            }
            case 1: {
                using destruct = typename change_type::destruct;
                if constexpr(is_pageless<T, entity_type>) {
                    payload = destruct{};
                } else {
                    T value{};
                    archive(value);
                    payload = destruct{std::move(value)};
                }
                break;
            }
            case 2: {
                if constexpr(is_pageless<T, entity_type>) {
                    ENTTX_ASSERT(false, "update_change encountered for pageless type during deserialization");
                } else {
                    using update_change = typename change_type::update;
                    T old_value{}, new_value{};
                    archive(old_value, new_value);
                    payload = update_change{std::move(old_value), std::move(new_value)};
                }
                break;
            }
            default:
                ENTTX_ASSERT(false, "Unknown change payload index during deserialization");
                break;
            }

            changes.push_back(change_type{entity, std::move(payload)});
        }

        commit->template append_segment<T>(std::move(changes), storage_id);

        return *this;
    }

private:
    commit_type* commit;
};

namespace internal 
{

template<typename Storage>
concept is_change_observer_storage = requires (Storage& s) {
    typename Storage::value_type;
    typename Storage::entity_type;
    { s.on_construct() };
    { s.on_destroy() };
}
&& (
    is_pageless<typename Storage::value_type, typename Storage::entity_type>
    ||
    requires (Storage& s) {
        { s.on_pre_update() };
        { s.on_update() };
    }
);

/*! @brief Tracks changes to components of type `T` within the given `Registry`. */
template<is_change_observer_storage Storage, typename Allocator = std::allocator<change<typename Storage::value_type, typename Storage::entity_type>>>
class basic_observer final: public basic_observer_base<typename Storage::registry_type> {
public:
    /*! @brief Storage for the component type being observed. */
    using storage_type = Storage;
    /*! @brief Underlying entity identifier. */
    using entity_type = typename storage_type::entity_type;
    /*! @brief Value type of the component being observed. */
    using value_type = typename storage_type::value_type;
    /*! @brief Allocator type for the changes vector. */
    using allocator_type = Allocator;
    /*! @brief Type of the change being tracked. */
    using change_type = change<value_type, entity_type>;
    /*! @brief Container for the changes being tracked. */
    using change_list_type = std::vector<change_type, allocator_type>;
    /*! @brief Base type for commits. */
    using commit_base_type = basic_commit<typename storage_type::registry_type>;

    entt::id_type storage_id;
    storage_type& storage;
    change_list_type changes;

    basic_observer(entt::id_type storage_id, storage_type& storage, const allocator_type& allocator = allocator_type{})
        : storage_id{storage_id}, storage{storage}, changes{allocator} {
        connect();
    }

    ~basic_observer() {
        disconnect();
    }

    void connect() override {
        storage.on_construct().template connect<&basic_observer::on_construct>(*this);
        if constexpr(!is_pageless<value_type, entity_type>) {
            storage.on_pre_update().template connect<&basic_observer::on_pre_update>(*this);
            storage.on_update().template connect<&basic_observer::on_update>(*this);
        }
        storage.on_destroy().template connect<&basic_observer::on_destroy>(*this);
    }

    void disconnect() override {
        storage.on_construct().template disconnect<&basic_observer::on_construct>(*this);
        if constexpr(!is_pageless<value_type, entity_type>) {
            storage.on_pre_update().template disconnect<&basic_observer::on_pre_update>(*this);
            storage.on_update().template disconnect<&basic_observer::on_update>(*this);
        }
        storage.on_destroy().template disconnect<&basic_observer::on_destroy>(*this);
    }

    void collect(commit_base_type& commit) override {
        // TODO: Add an option to flatten changes into a single net change for networking?
        if (!changes.empty()) {
            commit.template append_segment<value_type>(std::move(changes), storage_id);
            changes.clear();
        }
    }

private:
    // TODO: See comment in on_update 
    std::optional<std::conditional_t<!is_pageless<value_type, entity_type>, value_type, std::monostate>> pre_update_value;

    void on_construct(const entity_type entity) {
        if constexpr(is_pageless<value_type, entity_type>) {
            changes.emplace_back(entity, typename change_type::construct{});
        } else {
            changes.emplace_back(entity, typename change_type::construct{storage.get(entity)});
        }
    }

    void on_pre_update(const entity_type entity) requires(!is_pageless<value_type, entity_type>) {
        ENTTX_ASSERT(!pre_update_value.has_value(), "Pre-update value already set for entity");
        pre_update_value = storage.get(entity);
    }

    void on_update(const entity_type entity) requires(!is_pageless<value_type, entity_type>) {
        // TODO: Figure out if this is safe and the right way to do it.
        // Need to check if its legal that patch cannot modify the registry in a way that would invalidate this.
        changes.emplace_back(entity, typename change_type::update{std::move(pre_update_value).value(), storage.get(entity)});
        pre_update_value.reset();
    }

    void on_destroy(const entity_type entity) {
        if constexpr(is_pageless<value_type, entity_type>) {
            changes.emplace_back(entity, typename change_type::destruct{});
        } else {
            changes.emplace_back(entity, typename change_type::destruct{storage.get(entity)});
        }
    }
};
} // namespace internal

template<typename T, typename Registry>
[[nodiscard]] std::unique_ptr<basic_observer_base<Registry>> 
observe(Registry& registry, entt::id_type storage_id = entt::type_hash<T>::value()) {
    auto& storage = registry.template storage<T>(storage_id);
    using storage_type = std::remove_reference_t<decltype(storage)>;
    return std::make_unique<internal::basic_observer<storage_type>>(storage_id, storage);
}

} // namespace enttx