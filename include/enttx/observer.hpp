/*!
 * @file observer.hpp
 * @brief Observers track changes to components in an EnTT registry and can
 * produce commits that can be applied to other registries. This is useful for
 * networking, undo/redo systems, and other scenarios where you want to track
 * changes to a registry over time.
 *
 * @code{.cpp}
 * entt::registry reg_a;
 * entt::entity entity_a = reg_a.create();
 *
 * enttx::observers observers;
 * observers.emplace_back(enttx::observe<name>(reg_a));
 *
 * reg_a.emplace<name>(entity_a, "Player");
 * reg_a.patch<name>(entity_a, [](name &n) { n.value = "Hero"; });
 *
 * enttx::commit changes{};
 * for (auto &observer : observers) {
 *   observer->collect(changes);
 * }
 *
 * entt::registry reg_b;
 * entt::entity entity_b = reg_b.create();
 *
 * const auto remap = enttx::entity_remap{}.map(entity_a, entity_b);
 * changes.apply(reg_b, &remap);
 *
 * reg_b.get<name>(entity_b).value; // "Hero"
 * @endcode
 */

// TODO: Not all changes need to be invertible. Implement an api where users can
// specify if they want the changes to be invertible or not. For example, if
// commits are being sent over the network and inversions are not needed, then
// we can save some memory and bandwidth by not storing the old value for
// updates and destructs.

#pragma once
#include "core.hpp"
#include "entity_remap.hpp"

#include <entt/container/dense_map.hpp>
#include <entt/core/type_info.hpp>
#include <entt/entity/fwd.hpp>
#include <entt/entity/registry.hpp>
#include <entt/signal/fwd.hpp>
#include <entt/stl/memory.hpp>
#include <entt/stl/type_traits.hpp>
#include <entt/stl/vector.hpp>

#include <optional> // TODO: entt does not provide its own optional yet
#include <span>     // TODO: entt does not provide its own span yet
#include <variant>  // TODO: entt does not provide its own variant yet

namespace enttx {

template <typename, typename> struct basic_change;

template <typename> class basic_observer_base;

template <typename Registry,
          typename Allocator =
              stl::allocator<stl::unique_ptr<basic_observer_base<Registry>>>>
using basic_observers =
    stl::vector<stl::unique_ptr<basic_observer_base<Registry>>, Allocator>;

template <typename Registry,
          typename Allocator = typename Registry::allocator_type>
class basic_commit;

template <typename> class basic_commit_snapshot;

template <typename> class basic_commit_loader;

/*! @brief Type of a list of changes for a specific component type. */
template <typename T, typename Entity,
          typename Allocator = stl::allocator<basic_change<T, Entity>>>
using basic_change_list = stl::vector<basic_change<T, Entity>, Allocator>;

/*! @brief Alias declaration for the most common use case. */
template <typename T> using change = basic_change<T, entt::entity>;

/*! @brief Alias declaration for the most common use case. */
template <typename T,
          typename Allocator = stl::allocator<basic_change<T, entt::entity>>>
using change_list = basic_change_list<T, entt::entity, Allocator>;

/*! @brief Alias declaration for the most common use case. */
using observer_base = basic_observer_base<entt::registry>;

/*! @brief Alias declaration for the most common use case. */
using observers = basic_observers<entt::registry>;

/*! @brief Alias declaration for the most common use case. */
using commit = basic_commit<entt::registry>;

/*! @brief Alias declaration for the most common use case. */
using commit_snapshot = basic_commit_snapshot<commit>;

/*! @brief Alias declaration for the most common use case. */
using commit_loader = basic_commit_loader<commit>;

/*!
 * @brief Base class for observers that track changes in a registry.
 * @tparam Registry Type of the registry being observed.
 */
template <typename Registry> class basic_observer_base {
private:
  using traits_type = entt::entt_traits<typename Registry::entity_type>;

public:
  /*! @brief Type of registry */
  using registry_type = Registry;
  /*! @brief Underlying entity identifier. */
  using entity_type = typename traits_type::value_type;
  /*! @brief Commit type associated with this observer. */
  using commit_type = basic_commit<Registry>;

  /*! @brief Default constructor. `connect()` is expected to be called within
   * the derived class constructor. */
  basic_observer_base() = default;
  /*! @brief Virtual destructor. `disconnect()` is expected to be called within
   * the derived class destructor. */
  virtual ~basic_observer_base() = default;

  /*! @brief Connects the observer events to the registry. */
  virtual void connect() = 0;

  /*! @brief Disconnects the observer events from the registry. */
  virtual void disconnect() = 0;

  /*! @brief Collects the changes observed since the last collection and stores
   * them in the provided commit. */
  virtual void collect(commit_type &commit) = 0;
};

/*!
 * @brief Represents a change to a component of type `T` associated with an
 * entity of type `Entity`.
 * @tparam T Type of the component being changed.
 * @tparam Entity Type of the entity associated with the change.
 */
template <typename T, typename Entity> struct basic_change {
  struct construct {
    T value;
  };

  struct destruct {
    T value;
  };

  struct update {
    T old_value, new_value;
  };

  /*! @brief Underlying entity identifier. */
  using entity_type = Entity;
  /*! @brief Type of the change being tracked. */
  using variant_type = std::variant<construct, destruct, update>;

  entity_type entity;
  variant_type payload;

  [[nodiscard]] basic_change invert() const {
    basic_change inverted;
    inverted.entity = this->entity;
    std::visit(
        [&inverted](const auto &change) {
          using ChangeType = stl::decay_t<decltype(change)>;
          if constexpr (stl::is_same_v<ChangeType, construct>) {
            inverted.payload = destruct{change.value};
          } else if constexpr (stl::is_same_v<ChangeType, destruct>) {
            inverted.payload = construct{change.value};
          } else if constexpr (stl::is_same_v<ChangeType, update>) {
            inverted.payload = update{change.new_value, change.old_value};
          } else {
            static_assert(stl::false_type::value, "Unknown change type");
          }
        },
        payload);
    return inverted;
  }

  template <typename... Args>
  void apply_to(entt::storage_for_t<T, entity_type, Args...> &storage,
                const entity_type target = entt::null) const {
    const entity_type entity = target == entt::null ? this->entity : target;
    std::visit(
        [&storage, entity](const auto &change) {
          using ChangeType = stl::decay_t<decltype(change)>;
          if constexpr (stl::is_same_v<ChangeType, construct>) {
            storage.emplace(entity, change.value);
          } else if constexpr (stl::is_same_v<ChangeType, update>) {
            storage.patch(entity,
                          [&change](T &value) { value = change.new_value; });
          } else if constexpr (stl::is_same_v<ChangeType, destruct>) {
            storage.erase(entity);
          } else {
            static_assert(stl::false_type::value, "Unknown change type");
          }
        },
        payload);
  }
};

/*! @brief Specialization of `change` for pageless components, which do not have
 * a value associated with them. */
template <typename T, typename Entity>
  requires is_pageless<T, Entity>
struct basic_change<T, Entity> {
  struct construct {};

  struct destruct {};

  /*! @brief Underlying entity identifier. */
  using entity_type = Entity;
  /*! @brief Type of the change being tracked. */
  using variant_type = std::variant<construct, destruct>;

  entity_type entity;
  variant_type payload;

  [[nodiscard]] basic_change invert() const {
    basic_change inverted;
    inverted.entity = entity;
    if (std::holds_alternative<construct>(payload)) {
      inverted.payload = destruct{};
    } else /*if destruct*/ {
      inverted.payload = construct{};
    }
    return inverted;
  }

  template <typename... Args>
  void apply_to(entt::storage_for_t<T, entity_type, Args...> &storage,
                const entity_type target = entt::null) const {
    const entity_type entity = target == entt::null ? this->entity : target;
    if (std::holds_alternative<construct>(payload)) {
      storage.emplace(entity);
    } else /*if destruct*/ {
      storage.erase(entity);
    }
  }
};

/*!
 * @brief Represents a collection of changes (a commit) that can be applied to a
 * registry.
 * @tparam Registry Type of the registry to which the commit can be applied.
 */
template <typename Registry, typename Allocator> class basic_commit {
private:
  using traits_type = entt::entt_traits<typename Registry::entity_type>;
  using alloc_traits = stl::allocator_traits<Allocator>;

  template <typename T>
  auto &get_or_emplace_segment(
      const entt::id_type storage_id = entt::type_hash<T>::value()) {
    auto it = segments.find(storage_id);
    if (it == segments.end()) {
      // TODO: Make the segment allocation use the allocator.
      it = segments
               .emplace(storage_id,
                        stl::make_unique<segment<T>>(get_allocator()))
               .first;
    }
    return static_cast<segment<T> &>(*it->second);
  }

public:
  /*! @brief Allocator type used for the commit. */
  using allocator_type = Allocator;
  /*! @brief Type of registry */
  using registry_type = Registry;
  /*! @brief Underlying entity identifier. */
  using entity_type = typename traits_type::value_type;
  /*! @brief Type used for remapping entities during commit application. */
  using entity_remap_type = basic_entity_remap<entity_type, entity_type>;
  /*! @brief Allocator type used for the list of changes for a specific
   * component type. */
  template <typename T>
  using change_allocator_type = typename alloc_traits::template rebind_alloc<
      basic_change<T, entity_type>>;
  /*! @brief Type of the list of changes for a specific component type. */
  template <typename T>
  using change_list_type =
      basic_change_list<T, entity_type, change_allocator_type<T>>;
  /*! @brief Type of the view of the list of changes for a specific component
   * type. */
  template <typename T>
  using change_list_view_type = std::span<const basic_change<T, entity_type>>;

  /*!
   * @brief Constructs an empty commit with a given allocator.
   * @param allocator The allocator to use.
   */
  basic_commit(const allocator_type &allocator = {}) : segments(allocator) {}

  /*! @brief Default copy constructor, deleted on purpose. */
  basic_commit(const basic_commit &) = delete;

  /*!
   * @brief Move constructor.
   * @param other The instance to move from.
   */
  basic_commit(basic_commit &&other) noexcept
      : segments(stl::move(other.segments)) {}

  /*! @brief Default destructor. */
  ~basic_commit() = default;

  /*! @brief Default copy assignment operator, deleted on purpose. */
  basic_commit &operator=(const basic_commit &) = delete;

  /*!
   * @brief Move assignment operator.
   * @param other The instance to move from.
   * @return Reference to this instance after the move.
   */
  basic_commit &operator=(basic_commit &&other) noexcept {
    segments = stl::move(other.segments);
    return *this;
  }

  /*!
   * @brief Swaps the contents of this commit with another commit.
   * @param other The commit to swap with.
   */
  void swap(basic_commit &other) noexcept {
    stl::swap(segments, other.segments);
  }

  /*!
   * @brief Returns the associated allocator.
   * @return The associated allocator.
   */
  [[nodiscard]] constexpr allocator_type get_allocator() const noexcept {
    return segments.get_allocator();
  }

  /*! @brief Inverts the commit, producing a new commit that can undo the
   * changes of this commit. */
  [[nodiscard]] basic_commit invert() const {
    basic_commit inverted{get_allocator()};
    for (const auto &[storage_id, segment] : segments) {
      inverted.segments[storage_id] = segment->invert();
    }
    return inverted;
  }

  /*!
   * @brief Applies the commit to the given registry.
   * @param registry The registry to which the commit will be applied.
   * @param remap Optional pointer to an entity remap that can be used to remap
   * entities during the application of the commit.
   * @remark If a remap is provided, entities in the commit will be remapped
   * according to the remap before being applied to the registry. If no remap is
   * provided, entities will be applied as-is.
   */
  void apply(Registry &registry,
             const entity_remap_type *remap = nullptr) const {
    for (const auto &[storage_id, segment] : segments) {
      segment->apply(registry, storage_id, remap);
    }
  }

  /*!
   * @brief Checks if there are any changes for a specific component type.
   * @param storage_id The storage identifier for the component type.
   * @return True if there are changes for the specified component type, false
   * otherwise.
   */
  [[nodiscard]] bool has_changes(const entt::id_type storage_id) const {
    return segments.find(storage_id) != segments.end();
  }

  /*!
   * @brief Checks if there are any changes for a specific component type.
   * @tparam T The component type for which the changes are being checked.
   * @param storage_id The storage identifier for the component type. Defaults
   * to the type hash of T.
   * @return True if there are changes for the specified component type, false
   * otherwise.
   */
  template <typename T>
  [[nodiscard]] bool has_changes(
      const entt::id_type storage_id = entt::type_hash<T>::value()) const {
    return has_changes(storage_id);
  }

  /*!
   * @brief Gets or emplaces the list of changes for a specific component type.
   * @tparam T The component type for which the changes are being retrieved.
   * @param storage_id The storage identifier for the component type. Defaults
   * to the type hash of T.
   * @return A reference to the list of changes for the specified component
   * type.
   * @remark If no segment exists for the given storage_id, a new empty segment
   * will be created.
   */
  template <typename T>
  [[nodiscard]] change_list_type<T> &
  changes(const entt::id_type storage_id = entt::type_hash<T>::value()) {
    return get_or_emplace_segment<T>(storage_id).changes;
  }

  /*!
   * @brief Gets a view of the list of changes for a specific component type.
   * @tparam T The component type for which the changes are being retrieved.
   * @param storage_id The storage identifier for the component type. Defaults
   * to the type hash of T.
   * @return A view of the list of changes for the specified component type.
   * @remark If no segment exists for the given storage_id, an empty view will
   * be returned.
   */
  template <typename T>
  [[nodiscard]] change_list_view_type<T> view_changes(
      const entt::id_type storage_id = entt::type_hash<T>::value()) const {
    if (const auto it = segments.find(storage_id); it != segments.end()) {
      return static_cast<const segment<T> &>(*it->second).changes;
    }
    return {};
  }

  /*!
   * @brief Appends a list of changes for a specific component type to the
   * commit.
   * @tparam T The component type for which the changes are being appended.
   * @tparam Allocator The allocator type used for the list of changes.
   * @param changes The list of changes to append.
   * @param storage_id The storage identifier for the component type. Defaults
   * to the type hash of T.
   * @remark If a segment for the given storage_id already exists, the changes
   * will be appended to it. Otherwise, a new segment will be created.
   */
  template <typename T, typename Allocator>
  void append(basic_change_list<T, entity_type, Allocator> &&to_append,
              const entt::id_type storage_id = entt::type_hash<T>::value()) {
    if (to_append.empty()) {
      return; // No changes to append, exit early.
    }

    auto &c = changes<T>(storage_id);
    c.insert(c.end(), std::make_move_iterator(to_append.begin()),
             std::make_move_iterator(to_append.end()));
  }

  /*!
   * @brief Appends the changes from another commit into the end of this commit.
   * @param other The commit from which to merge changes.
   * @remark After the merge, the other commit will be empty.
   */
  void merge_from(basic_commit &&other) {
    for (auto &&[storage_id, other_segment] : other.segments) {
      if (const auto it = segments.find(storage_id); it != segments.end()) {
        it->second->merge_from(*other_segment);
      } else {
        segments[storage_id] = stl::move(other_segment);
      }
    }
    other.segments.clear();
  }

  /*! @brief True if no segments have been recorded. */
  [[nodiscard]] bool empty() const noexcept { return segments.empty(); }

private:
  struct segment_base {
    virtual ~segment_base() = default;
    virtual stl::unique_ptr<segment_base> invert() const = 0;
    virtual void apply(Registry &registry, const entt::id_type storage_id,
                       const entity_remap_type *remap = nullptr) const = 0;
    virtual void merge_from(segment_base &other) = 0;
  };

  template <typename T> struct segment final : public segment_base {
    change_list_type<T> changes;

    segment(const change_allocator_type<T> &allocator = {})
        : changes(allocator) {}

    stl::unique_ptr<segment_base> invert() const override {
      auto inverted = stl::make_unique<segment<T>>(changes.get_allocator());
      inverted->changes.reserve(changes.size());
      for (auto rit = changes.rbegin(); rit != changes.rend(); ++rit) {
        inverted->changes.emplace_back(rit->invert());
      }
      return inverted;
    }

    void apply(Registry &registry, const entt::id_type storage_id,
               const entity_remap_type *remap = nullptr) const override {
      auto &storage = registry.template storage<T>(storage_id);
      if (remap) {
        for (const auto &change : changes) {
          const auto remapped_entity = (*remap)(change.entity);
          if (remapped_entity != entt::null) {
            change.apply_to(storage, remapped_entity);
          }
        }
      } else {
        for (const auto &change : changes) {
          change.apply_to(storage);
        }
      }
    }

    void merge_from(segment_base &other) override {
      auto &other_changes = static_cast<segment &>(other).changes;
      changes.insert(changes.end(),
                     std::make_move_iterator(other_changes.begin()),
                     std::make_move_iterator(other_changes.end()));
      other_changes.clear();
    }
  };

  using segment_storage_type =
      entt::dense_map<entt::id_type, stl::unique_ptr<segment_base>,
                      stl::hash<entt::id_type>, std::equal_to<>,
                      typename alloc_traits::template rebind_alloc<std::pair<
                          const entt::id_type, stl::unique_ptr<segment_base>>>>;

  segment_storage_type segments;
};

/*!
 * @brief Utility class to serialize a commit into an archive, with optional
 * entity remapping.
 * @tparam Commit Type of the commit to be serialized.
 */
template <typename Commit> class basic_commit_snapshot {
  static_assert(!stl::is_const_v<Commit>, "Non-const commit type required");

public:
  /*! @brief Basic commit type. */
  using commit_type = Commit;
  /*! @brief Underlying entity identifier. */
  using entity_type = typename commit_type::entity_type;

  /*!
   * @brief Constructs an instance that is bound to a given commit.
   * @param commit A valid reference to a commit.
   */
  basic_commit_snapshot(const commit_type &commit) noexcept : commit{&commit} {}

  /*! @brief Default copy constructor, deleted on purpose. */
  basic_commit_snapshot(const basic_commit_snapshot &) = delete;

  /*! @brief Default move constructor. */
  basic_commit_snapshot(basic_commit_snapshot &&) noexcept = default;

  /*! @brief Default destructor. */
  ~basic_commit_snapshot() = default;

  /**
   * @brief Default copy assignment operator, deleted on purpose.
   * @return This snapshot.
   */
  basic_commit_snapshot &operator=(const basic_commit_snapshot &) = delete;

  /**
   * @brief Default move assignment operator.
   * @return This snapshot.
   */
  basic_commit_snapshot &operator=(basic_commit_snapshot &&) noexcept = default;

  /*!
   * @brief Serializes the changes for a specific component type.
   *
   * @tparam T The component type.
   * @tparam Archive The archive type.
   * @tparam EntityHandler A callable that maps entities to a serializable form.
   * E.g., entt::entity -> network_id.
   * @param archive The archive to serialize to.
   * @param entity_handler The entity handler callable.
   * @param storage_id The storage identifier, defaults to the type hash of T.
   * @return A const reference to this snapshot.
   *
   * @remark The entity_handler is expected to be a callable that takes an
   * entity of type `entity_type` and returns a serializable representation,
   * which can be anything, even just the entity itself. But it must produce a
   * value the load-side can use to remap the entity back to the correct entity
   * in the registry.
   */
  template <typename T, typename Archive,
            stl::invocable<entity_type> EntityHandler>
  const basic_commit_snapshot &
  get(Archive &archive, EntityHandler &&entity_handler,
      const entt::id_type storage_id = entt::type_hash<T>::value()) const {
    auto changes = commit->view_changes<T>(storage_id);
    archive(static_cast<stl::size_t>(changes.size()));

    for (const auto &change : changes) {
      using change_type = stl::decay_t<decltype(change)>;

      archive(entity_handler(change.entity));
      archive(static_cast<stl::uint8_t>(change.payload.index()));

      if constexpr (!is_pageless<T, entity_type>) {
        std::visit(
            [&archive](const auto &c) {
              if constexpr (stl::is_same_v<stl::decay_t<decltype(c)>,
                                           typename change_type::update>) {
                archive(c.old_value, c.new_value);
              } else {
                archive(c.value); // construct / destroy
              }
            },
            change.payload);
      }
    }

    return *this;
  }

  template <typename T, typename Archive>
  const basic_commit_snapshot &
  get(Archive &archive,
      const entt::id_type storage_id = entt::type_hash<T>::value()) const {
    return get<T>(
        archive, [](const entity_type entity) { return entity; }, storage_id);
  }

private:
  const commit_type *commit;
};

template <typename Commit>
basic_commit_snapshot(const Commit &commit) -> basic_commit_snapshot<Commit>;

/*!
 * @brief Utility class to deserialize a commit from an archive, with optional
 * entity remapping.
 * @tparam Commit Type of the commit to be deserialized.
 */
template <typename Commit> struct basic_commit_loader {
public:
  /*! @brief Basic commit type. */
  using commit_type = Commit;
  /*! @brief Underlying entity identifier. */
  using entity_type = typename commit_type::entity_type;

  /*!
   * @brief Constructs an instance that is bound to a given commit.
   * @param commit A valid reference to a commit.
   */
  basic_commit_loader(commit_type &commit) noexcept : commit{&commit} {}

  /*! @brief Default copy constructor, deleted on purpose. */
  basic_commit_loader(const basic_commit_loader &) = delete;

  /*! @brief Default move constructor. */
  basic_commit_loader(basic_commit_loader &&) noexcept = default;

  /*! @brief Default destructor. */
  ~basic_commit_loader() = default;

  /**
   * @brief Default copy assignment operator, deleted on purpose.
   * @return This loader.
   */
  basic_commit_loader &operator=(const basic_commit_loader &) = delete;

  /**
   * @brief Default move assignment operator.
   * @return This loader.
   */
  basic_commit_loader &operator=(basic_commit_loader &&) noexcept = default;

  /*!
   * @brief Deserializes the changes for a specific component type.
   * @tparam T Component type.
   * @tparam Archive Archive type.
   * @tparam EntityHandler Callable type to handle entity remapping.
   * @param archive Archive to deserialize from.
   * @param entity_handler Callable to handle entity remapping.
   * @param storage_id Optional storage identifier.
   * @return This loader.
   */
  template <typename T, typename Archive, typename EntityHandler>
    requires(stl::is_invocable_r_v<typename commit_type::entity_type,
                                   EntityHandler, Archive &>)
  const basic_commit_loader &
  get(Archive &archive, EntityHandler &&entity_handler,
      const entt::id_type storage_id = entt::type_hash<T>::value()) const {
    using change_type = basic_change<T, entity_type>;
    using change_list_type = basic_change_list<T, entity_type>;

    stl::size_t size{};
    archive(size);

    if (size == 0u) {
      return *this;
    }

    change_list_type changes{};
    changes.reserve(size);

    for (stl::size_t i = 0; i < size; ++i) {
      entity_type entity = entity_handler(archive);

      stl::uint8_t index{};
      archive(index);

      typename change_type::variant_type payload;

      // Index order must match change<T>::variant_type: construct(0),
      // destroy(1), update(2).
      switch (index) {
      case 0: {
        using construct = typename change_type::construct;
        if constexpr (is_pageless<T, entity_type>) {
          payload = construct{};
        } else {
          T value{};
          archive(value);
          payload = construct{stl::move(value)};
        }
        break;
      }
      case 1: {
        using destruct = typename change_type::destruct;
        if constexpr (is_pageless<T, entity_type>) {
          payload = destruct{};
        } else {
          T value{};
          archive(value);
          payload = destruct{stl::move(value)};
        }
        break;
      }
      case 2: {
        if constexpr (is_pageless<T, entity_type>) {
          ENTTX_ASSERT(false, "update_change encountered for pageless type "
                              "during deserialization");
          return *this;
        } else {
          using update_change = typename change_type::update;
          T old_value{}, new_value{};
          archive(old_value, new_value);
          payload = update_change{stl::move(old_value), stl::move(new_value)};
        }
        break;
      }
      default:
        ENTTX_ASSERT(false,
                     "Unknown change payload index during deserialization");
        return *this;
      }

      changes.emplace_back(entity, stl::move(payload));
    }

    commit->template append<T>(stl::move(changes), storage_id);

    return *this;
  }

  /*!
   * @brief Deserializes the changes for a specific component type, using the
   * default entity handler that returns the entity as-is.
   * @tparam T Component type.
   * @tparam Archive Archive type.
   * @param archive Archive to deserialize from.
   * @param storage_id Optional storage identifier.
   * @return This loader.
   */
  template <typename T, typename Archive>
  const basic_commit_loader &
  get(Archive &archive,
      const entt::id_type storage_id = entt::type_hash<T>::value()) const {
    return get<T>(
        archive,
        [](Archive &archive) {
          entity_type entity{};
          archive(entity);
          return entity;
        },
        storage_id);
  }

private:
  commit_type *commit;
};

template <typename Commit>
basic_commit_loader(Commit &commit) -> basic_commit_loader<Commit>;

namespace internal {

template <typename Storage>
concept is_change_observer_storage =
    requires(Storage &s) {
      typename Storage::value_type;
      typename Storage::entity_type;
      { s.on_construct() };
      { s.on_destroy() };
    } &&
    (is_pageless<typename Storage::value_type, typename Storage::entity_type> ||
     requires(Storage &s) {
       { s.on_pre_update() };
       { s.on_update() };
     });

/*! @brief Tracks changes to components of type `T` within the given `Registry`.
 */
template <is_change_observer_storage Storage,
          typename Allocator = stl::allocator<basic_change<
              typename Storage::element_type, typename Storage::entity_type>>>
class basic_observer final
    : public basic_observer_base<typename Storage::registry_type> {
public:
  /*! @brief Storage for the component type being observed. */
  using storage_type = Storage;
  /*! @brief Underlying entity identifier. */
  using entity_type = typename storage_type::entity_type;
  /*! @brief Type of the component being observed. */
  using element_type = typename storage_type::element_type;
  /*! @brief Allocator type for the changes vector. */
  using allocator_type = Allocator;
  /*! @brief Type of the change being tracked. */
  using change_type = basic_change<element_type, entity_type>;
  /*! @brief Container for the changes being tracked. */
  using change_list_type =
      basic_change_list<element_type, entity_type, allocator_type>;
  /*! @brief Type of the commit being used. */
  using commit_type = basic_commit<typename storage_type::registry_type>;

  entt::id_type storage_id;
  storage_type &storage;
  change_list_type changes;

  basic_observer(entt::id_type storage_id, storage_type &storage,
                 const allocator_type &allocator = allocator_type{})
      : storage_id{storage_id}, storage{storage}, changes{allocator} {
    connect();
  }

  ~basic_observer() { disconnect(); }

  void connect() override {
    storage.on_construct().template connect<&basic_observer::on_construct>(
        *this);
    if constexpr (!is_pageless<element_type, entity_type>) {
      storage.on_pre_update().template connect<&basic_observer::on_pre_update>(
          *this);
      storage.on_update().template connect<&basic_observer::on_update>(*this);
    }
    storage.on_destroy().template connect<&basic_observer::on_destroy>(*this);
  }

  void disconnect() override {
    storage.on_construct().template disconnect<&basic_observer::on_construct>(
        *this);
    if constexpr (!is_pageless<element_type, entity_type>) {
      storage.on_pre_update()
          .template disconnect<&basic_observer::on_pre_update>(*this);
      storage.on_update().template disconnect<&basic_observer::on_update>(
          *this);
    }
    storage.on_destroy().template disconnect<&basic_observer::on_destroy>(
        *this);
  }

  void collect(commit_type &commit) override {
    // TODO: Add an option to flatten changes into a single net change for
    // networking?
    if (!changes.empty()) {
      commit.template append<element_type>(stl::move(changes), storage_id);
      changes.clear();
    }
  }

private:
  // TODO: See comment in on_update
  [[no_unique_address]]
  stl::conditional_t<!is_pageless<element_type, entity_type>,
                     std::optional<element_type>, std::monostate>
      pre_update_value;

  void on_construct(const entity_type entity) {
    if constexpr (is_pageless<element_type, entity_type>) {
      changes.emplace_back(entity, typename change_type::construct{});
    } else {
      changes.emplace_back(
          entity, typename change_type::construct{storage.get(entity)});
    }
  }

  void on_pre_update(const entity_type entity)
    requires(!is_pageless<element_type, entity_type>)
  {
    ENTTX_ASSERT(!pre_update_value.has_value(),
                 "Pre-update value already set for entity");
    pre_update_value = storage.get(entity);
  }

  void on_update(const entity_type entity)
    requires(!is_pageless<element_type, entity_type>)
  {
    // TODO: Figure out if this is safe and the right way to do it.
    // Need to check if its legal that patch cannot modify the registry in a way
    // that would invalidate this.
    changes.emplace_back(
        entity, typename change_type::update{
                    stl::move(pre_update_value).value(), storage.get(entity)});
    pre_update_value.reset();
  }

  void on_destroy(const entity_type entity) {
    if constexpr (is_pageless<element_type, entity_type>) {
      changes.emplace_back(entity, typename change_type::destruct{});
    } else {
      changes.emplace_back(entity,
                           typename change_type::destruct{storage.get(entity)});
    }
  }
};
} // namespace internal

/*!
 * @brief Creates an observer for a specific component type.
 * @tparam T Component type.
 * @tparam Registry Registry type.
 * @param registry Registry to observe.
 * @param storage_id Optional storage identifier.
 * @return A unique pointer to the created observer.
 */
template <typename T, typename Registry>
[[nodiscard]] stl::unique_ptr<basic_observer_base<Registry>>
observe(Registry &registry,
        entt::id_type storage_id = entt::type_hash<T>::value()) {
  auto &storage = registry.template storage<T>(storage_id);
  using storage_type = stl::remove_reference_t<decltype(storage)>;
  return stl::make_unique<internal::basic_observer<storage_type>>(storage_id,
                                                                  storage);
}

} // namespace enttx