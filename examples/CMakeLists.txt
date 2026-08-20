include(FetchContent)

FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v4.0.0
)

FetchContent_MakeAvailable(EnTT)

# hierarchy_example

add_executable(hierarchy_example
    example_hierarchy.cpp
)

target_link_libraries(hierarchy_example
    PRIVATE
        EnTTx
        EnTT::EnTT
)

# prefab_example

add_executable(prefab_example
    example_prefab.cpp
)

target_link_libraries(prefab_example
    PRIVATE
        EnTTx
        EnTT::EnTT
)

# stable_id_example

add_executable(stable_id_example
    example_stable_id.cpp
)

target_link_libraries(stable_id_example
    PRIVATE
        EnTTx
        EnTT::EnTT
)

# observer_example

add_executable(observer_example
    example_observer.cpp
)

target_link_libraries(observer_example
    PRIVATE
        EnTTx
        EnTT::EnTT
)