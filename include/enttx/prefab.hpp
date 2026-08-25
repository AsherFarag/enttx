/*!
 * @file prefab.hpp
 * @brief TODO
 */

#pragma once
#include "core.hpp"
#include "entity_remap.hpp"
#include "hierarchy.hpp"
#include "stable_id.hpp"

#include <entt/core/type_info.hpp>
#include <entt/entity/registry.hpp>

#include <algorithm>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace enttx {

/*! @brief Type of identifier used to name prefab assets. */
using prefab_id = entt::id_type;

/*!
 * @brief Type of identifier used to reference persistent, stable nodes across
 * a prefab's inheritance chain.
 *
 * Node ids are generated once per logical node (via node_id_generator) and stay
 * stable for the node's lifetime, including across prefab variants/edits.
 * This is what allows derived prefabs to target a specific inherited node
 * for overrides.
 */
using node_id = basic_stable_id<std::uint32_t, struct node_id_tag>;

/*! @brief Generates unique identifiers for nodes in the prefab system. */
using node_id_generator = basic_monotonic_stable_id_generator<node_id>;

/*! @brief Provides component operations for a specific component type */
template <typename Registry> struct basic_component_ops {
private:
  using traits_type = entt::entt_traits<typename Registry::entity_type>;

public:
  /*! @brief Type of registry accepted by the handle. */
  using registry_type = Registry;
  /*! @brief Underlying entity identifier. */
  using entity_type = typename traits_type::value_type;
  /*! @brief Underlying version type. */
  using version_type = typename traits_type::version_type;

  using copy_fn = void(const registry_type &, entity_type, registry_type &,
                       entity_type);
  using remove_fn = void(registry_type &, entity_type);
  using remap_fn = void(registry_type &, entity_type,
                        const basic_entity_remap<entity_type> &);

  /*! @brief Deep copy operation for this component type from one
   * registry/entity to another. */
  copy_fn &copy;
  /*! @brief Safe removal operation for this component type from a
   * registry/entity. (Must check for existence first.) */
  remove_fn &remove;
  /*! @brief Optional remapping operation for this component type. */
  remap_fn *remap{nullptr};
};

/*! @brief Returns a basic_component_ops for a given component type T and
 * registry type Registry. */
template <typename T, typename Registry>
static basic_component_ops<Registry> get_component_ops() {
  using registry_type = Registry;
  using component_ops = basic_component_ops<registry_type>;
  using entity_type = typename component_ops::entity_type;
  using entity_remap = basic_entity_remap<entity_type>;

  static auto copy = +[](const registry_type &src, entity_type se,
                         registry_type &dst, entity_type de) {
    if constexpr (is_pageless<T, entity_type>) {
      dst.template emplace_or_replace<T>(de);
    } else {
      dst.template emplace_or_replace<T>(de, src.template get<T>(se));
    }
  };

  static auto remove = +[](registry_type &reg, entity_type e) {
    if (reg.template all_of<T>(e)) {
      reg.template remove<T>(e);
    }
  };

  component_ops ops{
      .copy = *copy,
      .remove = *remove,
  };

  if constexpr (is_remappable<T, Registry, entity_remap>) {
    ops.remap =
        +[](registry_type &reg, entity_type e, const entity_remap &remap) {
          remap_traits<T>::remap(reg, e, remap);
        };
  }

  return ops;
}

// -----------------------------------------------------------------------
// Authoring components for prefab definitions (def_reg)
// -----------------------------------------------------------------------

/*!
 * @brief Explicit removal tracking for a single authoring entity: which
 * component types this level removes from what it otherwise inherits.
 * Necessary because "component not present at this level" is ambiguous
 * between "not overridden" and "explicitly deleted".
 */
struct removed_components {
  std::unordered_set<entt::id_type> types;
};

/*!
 * @brief Explicit child-removal tracking for a single authoring entity:
 * which inherited children (by node_id) this level deletes entirely.
 */
struct removed_children {
  std::unordered_set<node_id> ids;
};

/*!
 * @brief Marks a node as an instance of another prefab nested inside this one,
 * rather than a plain node. When instantiated, the nested prefab's root node
 * will be instantiated as a child of this node.
 */
struct nested_prefab_ref {
  prefab_id prefab;
};

/*!
 * @brief Attached to authoring entities that exist purely to give a
 * structural path to a deeper override (copy-on-write ancestors) and have
 * no authored data of their own yet. Removed the moment the node is
 * actually authored (component emplaced/removed/child removed/nested).
 */
struct unauthored_tag {};

// -----------------------------------------------------------------------
// Runtime instantiation components
// -----------------------------------------------------------------------

/*!
 * @brief Attached to the root entity of every instantiated prefab so the
 * instance can be traced back to its source and refreshed later.
 */
struct prefab_instance_root {
  prefab_id source = entt::null;
};

/*!
 * @brief A registry for creating and managing prefab definitions and
 * instantiating them into a target registry.
 */
template <typename Registry, typename NodeHierarchy>
class basic_prefab_registry {
  using traits_type = entt::entt_traits<typename Registry::entity_type>;

public:
  /*! @brief Type of registry accepted by the handle. */
  using registry_type = Registry;
  /*! @brief Underlying entity identifier. */
  using entity_type = typename traits_type::value_type;
  /*! @brief Underlying version type. */
  using version_type = typename traits_type::version_type;
  /*! @brief Entity remap table used to fix up entity-valued components on
   * instantiate. */
  using entity_remap_type = basic_entity_remap<entity_type>;
  /*! @brief Component copy/remove/remap operation table. */
  using component_ops_type = basic_component_ops<registry_type>;
  /*! @brief Hierarchy type populated on instantiated entities */
  using node_hierarchy = NodeHierarchy;
  /*! @brief Hierarchy type used internally for def_reg authoring nodes. */
  using authoring_hierarchy = basic_hierarchy<
      registry_type,
      hierarchy_config{.deletion_policy = hierarchy_deletion_policy::unhandled},
      struct _prefab_authoring_hierarchy_tag>;
  /*! @brief Hierarchy type used to express prefab IsA relationships between
   * prefab asset entities in `def_reg`. */
  using isa_hierarchy =
      basic_hierarchy<registry_type,
                      hierarchy_config{
                          .deletion_policy =
                              hierarchy_deletion_policy::destroy_children},
                      struct _prefab_isa_hierarchy_tag>;

  /*! @brief Underlying registry used to store prefab definitions (authoring
   * deltas) in. */
  registry_type &def_reg{};

  /*! @brief Registered component operations, keyed by component type id. */
  std::unordered_map<entt::id_type, component_ops_type> component_ops;

  basic_prefab_registry(registry_type &reg) : def_reg{reg} {}
  basic_prefab_registry(const basic_prefab_registry &) = delete;
  basic_prefab_registry(basic_prefab_registry &&) = default;
  basic_prefab_registry &operator=(const basic_prefab_registry &) = delete;
  basic_prefab_registry &operator=(basic_prefab_registry &&) = default;
  ~basic_prefab_registry() = default;

  // ------------------------------------------------------------- Authoring

  /*!
   * @brief Declares a new prefab, optionally deriving ("IsA") from a base
   * prefab.
   * @param id The prefab_id to declare.
   * @param base The base prefab_id to derive from, if any.
   * @param root_hint A hint for the root node_id, if known.
   *                  If base is specified, the root_hint is ignored and the
   * base's root node_id is used instead.
   * @return The node_id of the root node of the prefab.
   */
  node_id create_prefab(const prefab_id id, const prefab_id base = entt::null,
                        const node_id root_hint = entt::null) {
    node_id root;
    entity_type base_entity = entt::null;

    if (base != entt::null) {
      // If a base prefab is specified, we need to find its root node_id so we
      // can share it with the new prefab.
      base_entity = get_prefab_entity(base);
      ENTTX_ASSERT(base_entity != entt::null, "Base prefab not found");
      ENTTX_ASSERT(def_reg.template all_of<node_id>(base_entity),
                   "Corrupted prefab: base prefab entity has no node_id");
      root = def_reg.template get<node_id>(base_entity);
    } else {
      root = (root_hint != entt::null) ? root_hint : id_gen_();
    }

    const entity_type e = ensure_authoring(id, root);
    def_reg.template emplace<prefab_id>(e, id);
    prefab_entities_[id] = e;

    if (base_entity != entt::null) {
      isa_hierarchy::push_back(def_reg, base_entity, e);
    }

    return root;
  }

  /*! @brief Adds a brand new child node under `parent`, local to `prefab`. */
  node_id add_child(const prefab_id prefab, const node_id parent) {
    const node_id child = id_gen_();
    node_parent_[child] = parent;
    ensure_authoring(prefab, child);
    return child;
  }

  /*!
   * @brief Begins (or continues) authoring an override for an *inherited* node
   * at this prefab level, without changing its position in the hierarchy.
   * @param node A node_id that already exists somewhere in `prefab`'s IsA
   * chain.
   * @return `node`, unchanged - provided for chaining symmetry with add_child.
   */
  node_id override_child(const prefab_id prefab, const node_id node) {
    ensure_authoring(prefab, node);
    return node;
  }

  /*! @brief Marks a nested prefab instance at `parent`, local to `prefab`. */
  node_id add_nested(const prefab_id prefab, const node_id parent,
                     const prefab_id nested_prefab) {
    const node_id child = id_gen_();
    node_parent_[child] = parent;
    const entity_type e = ensure_authoring(prefab, child);
    def_reg.template emplace<nested_prefab_ref>(
        e, nested_prefab_ref{nested_prefab});
    mark_authored(e);
    return child;
  }

  /*! @brief Emplaces (or replaces) an override component for `node` at this
   * prefab level. */
  template <typename T, typename... Args>
  T &emplace(const prefab_id prefab, const node_id node, Args &&...args) {
    register_ops<T>();
    const entity_type e = ensure_authoring(prefab, node);
    mark_authored(e);

    if (auto *rc = def_reg.template try_get<removed_components>(e)) {
      rc->types.erase(entt::type_hash<T>::value());
    }

    return def_reg.template emplace_or_replace<T>(e,
                                                  std::forward<Args>(args)...);
  }

  /*! @brief Explicitly removes (strips) an inherited or local component of type
   * T at this prefab level. */
  template <typename T>
  void remove(const prefab_id prefab, const node_id node) {
    register_ops<T>();
    const entity_type e = ensure_authoring(prefab, node);
    mark_authored(e);

    def_reg.template remove<T>(e);
    def_reg.template get_or_emplace<removed_components>(e).types.insert(
        entt::type_hash<T>::value());
  }

  /*! @brief Explicitly deletes an inherited child (and its subtree) at this
   * prefab level. */
  void remove_child(const prefab_id prefab, const node_id child) {
    const auto pit = node_parent_.find(child);
    if (pit == node_parent_.end()) {
      return; // child not found, nothing to do
    }

    const entity_type parent_ae = ensure_authoring(prefab, pit->second);
    mark_authored(parent_ae);
    def_reg.template get_or_emplace<removed_children>(parent_ae).ids.insert(
        child);
  }

  /*!
   * @brief Registers copy/remove/remap operations for T so it participates in
   * collapse. Called automatically the first time T is used via emplace/remove,
   * but can be called ahead of time (e.g. at startup, for editor tooling).
   */
  template <typename T>
  void register_ops(const entt::id_type id = entt::type_hash<T>::value()) {
    if (component_ops.find(id) == component_ops.end()) {
      component_ops.emplace(id, get_component_ops<T, registry_type>());
    }
  }

  /*! @brief Destroys all prefab definitions. */
  void clear() {
    for (const auto &[id, e] : prefab_entities_) {
      if (def_reg.valid(e)) {
        def_reg.destroy(e);
      }
    }

    prefab_entities_.clear();
    node_parent_.clear();
    node_authoring_.clear();
  }

  /*!
   * @brief Rebuilds the internal cache maps.
   * Must be called after modifying the prefab definitions without using the
   * prefab registry's modification methods. For example, after loading a prefab
   * registry from disk, or after manually modifying the def_reg directly.
   */
  void rebuild_cache() {
    prefab_entities_.clear();
    node_parent_.clear();
    node_authoring_.clear();

    // Rebuild prefab_entities_ lookup
    for (auto [e, pid] : def_reg.template view<enttx::prefab_id>().each()) {
      prefab_entities_[pid] = e;
    }

    // Rebuild node_authoring_ and node_parent_
    for (auto [root_entity, pid] :
         def_reg.template view<enttx::prefab_id>().each()) {

      auto traverse_and_rebuild = [&](entt::entity curr, auto &self) -> void {
        enttx::node_id curr_id = def_reg.template get<enttx::node_id>(curr);
        node_authoring_[pid][curr_id] = curr;

        // Rebuild node_parent_
        if (const auto *auth =
                def_reg.template try_get<authoring_hierarchy>(curr)) {
          if (auth->parent != entt::null) {
            enttx::node_id parent_id =
                def_reg.template get<enttx::node_id>(auth->parent);
            node_parent_[curr_id] = parent_id;
          }
        }

        // TODO: Possible to run out of stack here if prefab is very deep, move
        // to temp alloc buffers instead
        authoring_hierarchy::for_each_child(
            def_reg, curr, [&](entt::entity child) { self(child, self); });
      };

      traverse_and_rebuild(root_entity, traverse_and_rebuild);
    }
  }

  // ------------------------------------------------------------- Instantiate

  /*! @brief Collapses `prefab`'s full IsA chain and node tree into fresh
   * entities in `target`. */
  // TODO: Since it is so heavy to instantiate a prefab, should probably call
  // this bake instead so users will prefer to copy a baked prefab instead of
  // re-instantiating it every time?
  entity_type instantiate(const prefab_id prefab, registry_type &target) {
    return instantiate_into(prefab, target, entt::null);
  }

  // TODO: Add method to `refresh` an existing instance, like in unity how a
  // prefab instance will update when the prefab asset changes, but only for the
  // parts that haven't been overridden in the instance.

  /*! @brief Removes the book-keeping components from an instantiated prefab
   * instance, leaving it as a plain entity tree. */
  void unpack(registry_type &target, const entity_type instance_root) {
    if (!target.template all_of<prefab_instance_root>(instance_root)) {
      return; // not a prefab instance root, nothing to do
    }

    target.template remove<prefab_instance_root>(instance_root);
  }

  // ------------------------------------------------------------- Introspection

  /*! @brief */
  template <typename Underlying> class prefab_id_iterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = prefab_id;
    using difference_type = std::ptrdiff_t;
    using pointer = const prefab_id *;
    using reference = prefab_id;

    constexpr prefab_id_iterator() noexcept = default;
    prefab_id_iterator(const registry_type *reg, Underlying it) noexcept
        : reg_{reg}, it_{it} {}

    [[nodiscard]] reference operator*() const {
      return reg_->template get<prefab_id>(*it_);
    }

    prefab_id_iterator &operator++() {
      ++it_;
      return *this;
    }
    prefab_id_iterator operator++(int) {
      auto tmp{*this};
      ++(*this);
      return tmp;
    }

    [[nodiscard]] friend bool
    operator==(const prefab_id_iterator &lhs,
               const prefab_id_iterator &rhs) noexcept {
      return lhs.it_ == rhs.it_;
    }

  private:
    const registry_type *reg_{nullptr};
    Underlying it_{};
  };

  /*! @brief */
  struct derived_view {
    using iterator = prefab_id_iterator<typename isa_hierarchy::child_iterator>;
    using reverse_iterator =
        prefab_id_iterator<typename isa_hierarchy::reverse_child_iterator>;

    const registry_type *reg;
    entity_type base_entity;

    [[nodiscard]] iterator begin() const {
      return iterator{reg, isa_hierarchy::children(*reg, base_entity).begin()};
    }
    [[nodiscard]] iterator end() const {
      return iterator{reg, isa_hierarchy::children(*reg, base_entity).end()};
    }
    [[nodiscard]] reverse_iterator rbegin() const {
      return reverse_iterator{
          reg, isa_hierarchy::children(*reg, base_entity).rbegin()};
    }
    [[nodiscard]] reverse_iterator rend() const {
      return reverse_iterator{
          reg, isa_hierarchy::children(*reg, base_entity).rend()};
    }
  };

  /*! @brief Returns the asset entity representing `prefab` in the IsA
   * hierarchy, or entt::null if `prefab` hasn't been created. */
  [[nodiscard]]
  entity_type get_prefab_entity(const prefab_id prefab) const {
    const auto it = prefab_entities_.find(prefab);
    return it != prefab_entities_.end() ? it->second : entt::null;
  }

  /*! @brief Returns the immediate base of `prefab`, or entt::null if it has
   * none. */
  [[nodiscard]]
  prefab_id get_base(const prefab_id prefab) const {
    const entity_type e = get_prefab_entity(prefab);
    if (e == entt::null || !def_reg.template all_of<isa_hierarchy>(e)) {
      return entt::null;
    }
    const entity_type p = def_reg.template get<isa_hierarchy>(e).parent;
    if (p == entt::null) {
      return entt::null;
    }

    ENTTX_ASSERT(def_reg.template all_of<prefab_id>(p),
                 "Corrupted prefab: parent entity has no prefab_id component");
    return def_reg.template get<prefab_id>(p);
  }

  /*! @brief Checks if a prefab is the `same as` or `derived` from base. */
  [[nodiscard]]
  bool is_a(const prefab_id derived, const prefab_id base) const {
    const entity_type de = get_prefab_entity(derived);
    const entity_type be = get_prefab_entity(base);
    return de == be || isa_hierarchy::is_descendant(def_reg, de, be);
  }

  /*! @brief Returns a range over `base`'s direct derived prefabs. */
  [[nodiscard]]
  derived_view derived(const prefab_id base) const {
    return derived_view{&def_reg, get_prefab_entity(base)};
  }

  /*! @brief Whether `base` has any derived prefabs. */
  [[nodiscard]]
  bool has_derived(const prefab_id base) const {
    const entity_type be = get_prefab_entity(base);
    const auto *h = def_reg.template try_get<isa_hierarchy>(be);
    return h && h->has_children();
  }

  /*!
   * @brief Gets the entity in `def_reg` that authoring `node` at prefab level
   * `prefab` corresponds to.
   * @param prefab The prefab level to look in.
   * @param node The logical node_id to look for.
   * @return The entity in `def_reg` that authoring `node` at prefab level
   * `prefab` corresponds to, or entt::null if no such authoring exists.
   */
  [[nodiscard]]
  entity_type get_node_entity(const prefab_id prefab,
                              const node_id node) const {
    const auto pit = node_authoring_.find(prefab);
    if (pit == node_authoring_.end()) {
      return entt::null;
    }
    const auto nit = pit->second.find(node);
    return nit != pit->second.end() ? nit->second : entt::null;
  }

  /*! @brief Gets the root node_id of a prefab, or entt::null if the prefab
   * doesn't exist. */
  [[nodiscard]]
  node_id get_root_node(const prefab_id prefab) const {
    const entity_type e = get_prefab_entity(prefab);
    if (e == entt::null) {
      return entt::null;
    }
    return def_reg.template get<node_id>(e);
  }

  /*! @brief Gets the entity in `def_reg` that corresponds to the root node of a
   * prefab, or entt::null if the prefab doesn't exist. */
  [[nodiscard]]
  entity_type get_root_node_entity(const prefab_id prefab) const {
    return get_node_entity(prefab, get_root_node(prefab));
  }

protected:
  node_id_generator id_gen_;

  /*! @brief prefab_id -> its asset entity in def_reg. */
  std::unordered_map<prefab_id, entity_type> prefab_entities_;

  /*! @brief node_id -> its (single, level-independent) logical parent node_id.
   */
  std::unordered_map<node_id, node_id> node_parent_;

  /*! @brief prefab_id -> (node_id -> authoring entity in def_reg) for that
   * level. */
  std::unordered_map<prefab_id, std::unordered_map<node_id, entity_type>>
      node_authoring_;

  void mark_authored(const entity_type e) {
    def_reg.template remove<unauthored_tag>(e);
  }

  /*! @brief Gets or creates the authoring entity in def_reg for a given prefab
   * and node_id. */
  entity_type ensure_authoring(const prefab_id prefab, const node_id node) {
    auto &level_map = node_authoring_[prefab];
    if (const auto it = level_map.find(node); it != level_map.end()) {
      return it->second;
    }

    const entity_type e = def_reg.create();
    def_reg.template emplace<node_id>(e, node);
    def_reg.template emplace<unauthored_tag>(e);
    level_map.emplace(node, e);

    if (const auto pit = node_parent_.find(node); pit != node_parent_.end()) {
      const entity_type parent_ae = ensure_authoring(prefab, pit->second);
      authoring_hierarchy::push_back(def_reg, parent_ae, e);
    }

    return e;
  }

  /*! @brief Builds the IsA chain of prefabs from the given leaf up to the root.
   */
  [[nodiscard]]
  std::vector<prefab_id> build_chain(const prefab_id leaf) const {
    std::vector<prefab_id> chain;
    entity_type cur = get_prefab_entity(leaf);
    while (cur != entt::null) {
      ENTTX_ASSERT(def_reg.template all_of<prefab_id>(cur),
                   "Corrupted prefab: entity in IsA chain has no prefab_id");
      chain.push_back(def_reg.template get<prefab_id>(cur));
      const auto *isa = def_reg.template try_get<isa_hierarchy>(cur);
      cur = isa ? isa->parent : entt::null;
    }
    return chain;
  }

  void
  apply_remap(registry_type &target, const entity_remap_type &remap,
              std::span<const std::pair<entt::id_type, entity_type>> touched) {
    // TODO: Currently an entity can get remapped multiple times from overrides.
    // For now its fine but could be optimized.
    for (const auto &[id, te] : touched) {
      if (const auto it = component_ops.find(id);
          it != component_ops.end() && it->second.remap) {
        it->second.remap(target, te, remap);
      }
    }
  }

  entity_type instantiate_into(const prefab_id prefab, registry_type &target,
                               const entity_type reuse) {
    const entity_type prefab_entity = get_prefab_entity(prefab);
    if (prefab_entity == entt::null) {
      return entt::null;
    }

    const node_id root_node = def_reg.template get<node_id>(prefab_entity);

    // TODO: Use pmr for this temp allocated containers?
    const std::vector<prefab_id> chain = build_chain(prefab);
    entity_remap_type remap;
    std::vector<std::pair<entt::id_type, entity_type>> touched;

    const entity_type root = collapse_node(root_node, chain, target, entt::null,
                                           reuse, remap, touched);
    apply_remap(target, remap, touched);

    target.template emplace_or_replace<prefab_instance_root>(
        root, prefab_instance_root{prefab});
    return root;
  }

  entity_type
  collapse_node(const node_id logical_node, std::span<const prefab_id> chain,
                registry_type &target, const entity_type target_parent,
                const entity_type reuse, entity_remap_type &remap,
                std::vector<std::pair<entt::id_type, entity_type>> &touched) {
    const entity_type te = (reuse != entt::null) ? reuse : target.create();

    if (target_parent != entt::null) {
      node_hierarchy::push_back(target, target_parent, te);
    }

    prefab_id nested = entt::null;

    // chain is derived-to-base, but we want to apply overrides from
    // base-to-derived, so iterate in reverse.
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
      const prefab_id level = *it;
      const entity_type ae = get_node_entity(level, logical_node);
      if (ae == entt::null) {
        continue; // no authoring at this level, skip to next.
      }

      remap.entt_map[ae] = te;

      // Explicit removals authored at this level strip whatever was copied
      // in from earlier (more base) levels.
      if (const auto *rc = def_reg.template try_get<removed_components>(ae)) {
        for (const entt::id_type tid : rc->types) {
          if (const auto it = component_ops.find(tid);
              it != component_ops.end()) {
            it->second.remove(target, te);
          }
        }
      }

      // Copy every component this level explicitly authored on the node.
      for (auto &&curr : def_reg.storage()) {
        auto &&[id, storage] = curr;
        if (!storage.contains(ae)) {
          continue; // this level doesn't author this component, skip.
        }
        const auto it = component_ops.find(id);
        if (it == component_ops.end()) {
          continue; // unregistered (internal bookkeeping type), skip.
        }

        it->second.copy(def_reg, ae, target, te);
        touched.emplace_back(id, te);
      }

      if (def_reg.template all_of<nested_prefab_ref>(ae)) {
        nested = def_reg.template get<nested_prefab_ref>(ae)
                     .prefab; // most-derived wins.
      }
    }

    if (nested != entt::null) {
      instantiate_into(nested, target, te);
    }

    // TODO: This is both allocation heavy and like O(children^2).
    // Figure out a bter way to do this like using std::pmr and unordered_sets?

    // Build a list of all children authored at any level of the chain, in order
    // from most-derived to most-base, skipping duplicates.

    std::vector<node_id> ordered_children;
    std::vector<node_id> removed_ids;

    // We want to iterate from most-derived to most-base so that the
    // most-derived authored children are first in the list.
    for (const prefab_id level : chain) {
      const entity_type ae = get_node_entity(level, logical_node);
      if (ae == entt::null) {
        continue; // no authoring at this level, skip to next.
      }

      // Track any children that were explicitly removed at this level so we can
      // filter them out of the final child list.
      if (const auto *rc = def_reg.template try_get<removed_children>(ae)) {
        for (const node_id rid : rc->ids) {
          removed_ids.push_back(rid);
        }
      }

      // Add any children authored at this level to the ordered list, skipping
      // duplicates.
      authoring_hierarchy::for_each_child(
          def_reg, ae, [&](const entity_type child_e) {
            const node_id cid = def_reg.template get<node_id>(child_e);
            if (std::find(ordered_children.begin(), ordered_children.end(),
                          cid) == ordered_children.end()) {
              ordered_children.push_back(cid);
            }
          });
    }

    // Filter out any children that were explicitly removed at any level of the
    // chain.
    if (!removed_ids.empty()) {
      std::erase_if(ordered_children, [&](node_id id) {
        return std::find(removed_ids.begin(), removed_ids.end(), id) !=
               removed_ids.end();
      });
    }

    for (const node_id child : ordered_children) {
      collapse_node(child, chain, target, te, entt::null, remap, touched);
    }

    return te;
  }
};

// TODO: Clean this up or remove it
template <typename PrefabRegistry> class basic_prefab_builder {
public:
  /*! @brief  */
  using prefab_registry_type = PrefabRegistry;
  using node_id_type = node_id;

  prefab_registry_type &reg_;
  const prefab_id prefab_;
  const node_id node_;

  basic_prefab_builder(prefab_registry_type &reg, const prefab_id prefab,
                       const node_id node)
      : reg_{reg}, prefab_{prefab}, node_{node} {}

  basic_prefab_builder(const basic_prefab_builder &) = delete;
  basic_prefab_builder &operator=(const basic_prefab_builder &) = delete;

  template <typename BuilderFn>
    requires(std::invocable<BuilderFn, basic_prefab_builder &>)
  basic_prefab_builder &add_child(BuilderFn &&fn) {
    const node_id new_child = reg_.add_child(prefab_, node_);
    basic_prefab_builder child_builder{reg_, prefab_, new_child};
    std::invoke(std::forward<BuilderFn>(fn), child_builder);
    return *this;
  }

  template <typename BuilderFn>
    requires(std::invocable<BuilderFn, basic_prefab_builder &>)
  basic_prefab_builder &override_child(node_id child, BuilderFn &&fn) {
    reg_.override_child(prefab_, child);
    basic_prefab_builder child_builder{reg_, prefab_, child};
    std::invoke(std::forward<BuilderFn>(fn), child_builder);
    return *this;
  }

  basic_prefab_builder &remove_child(node_id child) {
    reg_.remove_child(prefab_, child);
    return *this;
  }

  basic_prefab_builder &add_nested(prefab_id nested_prefab) {
    reg_.add_nested(prefab_, node_, nested_prefab);
    return *this;
  }

  template <typename T, typename... Args>
  basic_prefab_builder &emplace(Args &&...args) {
    reg_.template emplace<T>(prefab_, node_, std::forward<Args>(args)...);
    return *this;
  }

  template <typename T> basic_prefab_builder &remove() {
    reg_.template remove<T>(prefab_, node_);
    return *this;
  }
};

/*! @brief Alias declaration for the most common use case. */
using component_ops = basic_component_ops<entt::registry>;

/*! @brief Alias declaration for the most common use case. */
using prefab_registry = basic_prefab_registry<entt::registry, hierarchy>;

/*! @brief Alias declaration for the most common use case. */
using prefab_builder = basic_prefab_builder<prefab_registry>;

} // namespace enttx