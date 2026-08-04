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
- CMake 3.20+

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

- **Intrusive hierarchy component:** Represents parent/child relationships using a lightweight intrusive linked structure.
- **Constant-time reparenting:** Attach, detach, and reorder entities without rebuilding the hierarchy.
- **Cache-friendly traversal:** Iterate children efficiently without external tree allocations.
- **Tag support:** Multiple independent hierarchies can exist on the same entity using hierarchy tags.
- **EnTT native:** Integrates directly with `entt::basic_registry` and entities without requiring a custom scene graph.

### Prefabs `prefab.hpp`

```
Character Prefab
    ├── Enemy Prefab
    │   ├── Slime Prefab
    │   └── Boss Prefab
    │       └── Slime King Prefab
    ├── Player Prefab
    └── Shop Merchant Prefab
```

- **'Is A' relationships:** Prefabs implement a variant system similar to Unity's prefabs, allowing derived prefabs to inherit from a base while selectively overriding data.
- **Delta-based serialization:** Derived prefabs store only authored changes instead of duplicating inherited data.
- **Nested prefabs:** TODO

## Roadmap
- [x] Entity hierarchies
- [ ] Prefab inheritance
- [ ] Nested prefabs
- [ ] Documentation

## Contributing

Contributions are welcome!

* Open an issue to discuss changes or ideas
* Keep code consistent with the existing style (EnTT)
* Add and ensure tests (if applicable) pass

## License

EnTTx is licensed under the **MIT License** - see the [LICENSE](https://github.com/AsherFarag/enttx/blob/main/LICENSE) file for details.