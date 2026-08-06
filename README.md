# EnTTx

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

EnTTx is a lightweight, header-only extension library for [EnTT](https://github.com/skypjack/entt) that adds game development features such as entity hierarchies and prefab inheritance while remaining fully compatible with EnTT's ECS philosophy.

## Why EnTTx?

EnTT provides a powerful low-level ECS foundation, but many games require higher-level systems such as entity hierarchies and prefab workflows. EnTTx provides these utilities while preserving EnTT's lightweight design and registry model.

## Design Goals

- Non-intrusive extension to EnTT.
- Remain header-only.
- Keep APIs idiomatic to EnTT.
- Optimized for real-time applications such as games.

## Features

- Entity hierarchies
- Unity-style Prefabs
- Delta-based prefab serialization
- Header-only integration with EnTT

## Requirements

- [EnTT](https://github.com/skypjack/entt) v4.0.0+
- C++20+

For building examples and tests:
- CMake 3.28+

## Installation

Since EnTTx is header-only, copy the contents of `include/enttx` into your project.

### Building Examples and Tests

| CMake option | Default |
|---|---|
| `ENTTX_BUILD_EXAMPLES` | OFF
| `ENTTX_BUILD_TESTS` | OFF

```bash
git clone https://github.com/AsherFarag/enttx.git
cd enttx
cmake -B build -S . -DENTTX_BUILD_EXAMPLES=ON
cmake --build build
# example .exe files will be in `build/examples/Debug`
```

## Usage

*See the `examples` folder for more detailed usage examples.*

### Hierarchies `hierarchy.hpp`

```cpp
#include <cassert>
#include <enttx/hierarchy.hpp>
#include <entt/entity/registry.hpp>

int main()
{
    entt::registry reg;
    entt::entity player = reg.create();
    entt::entity camera = reg.create();

    // Automatically adds enttx::hierarchy to `player` and `camera`
    enttx::hierarchy::attach_child(reg, player, camera);

    // `camera` is now a child of `player`
    // player
    // └── camera

    assert(reg.get<enttx::hierarchy>(camera).parent == player);

    enttx::hierarchy::detach(reg, camera);

    // `player` and `camera` are now no longer related
    // player
    // camera

    assert(reg.get<enttx::hierarchy>(camera).parent == entt::null);
}
```

- **Intrusive hierarchy component:** Represents parent/child relationships using a lightweight intrusive double-linked structure.
- **Constant-time reparenting:** Attach, detach, and reorder entities without rebuilding the hierarchy.
- **Cache-friendly traversal:** Iterate children efficiently without external tree allocations.
- **Tag support:** Multiple independent hierarchies can exist on the same entity using hierarchy tags.
- **EnTT native:** Integrates directly with `entt::basic_registry` and entities without requiring a custom scene graph.

### Prefabs `prefab.hpp`

```
Goblin
├── Weapon
└── Campfire

Goblin Chief (inherits Goblin)
├── Weapon (overridden)
├── War Banner
└── Campfire
```

- **Prefab inheritance:** Create variants of existing prefabs using `'Is A'` relationships. Derived prefabs inherit components and children from their base and can override or remove them.
- **Component overrides:** Change only the data that differs from the base prefab while keeping inherited values intact.
- **Child overrides:** Modify inherited child nodes without recreating the hierarchy.
- **Nested prefabs:** Compose larger prefabs by embedding other prefabs as children.
- **Runtime instantiation:** Convert prefab definitions into normal EnTT entities inside any registry.
- **Prefab introspection:** Query relationships with `is_a()`, `get_base()`, and `derived()`.
- **Detach instances:** Use `unpack()` to remove prefab tracking and turn an instance into an independent entity hierarchy.
- **EnTT native:** Built on top of `entt::registry` and works with normal EnTT components.

## Roadmap
- [x] Entity hierarchies
- [x] Prefab inheritance
- [x] Nested prefabs
- [ ] Documentation
- [ ] More advanced examples like serialization and prefab editors
- [ ] Support for per-field delta with prefabs

## Contributing

Contributions are welcome!

* Open an issue to discuss changes or ideas
* Keep code consistent with the existing style (EnTT)
* Add and ensure tests (if applicable) pass

## License

EnTTx is licensed under the **MIT License** - see the [LICENSE](https://github.com/AsherFarag/enttx/blob/main/LICENSE) file for details.