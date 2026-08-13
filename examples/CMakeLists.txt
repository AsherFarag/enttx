include(FetchContent)

FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v4.0.0
)

FetchContent_MakeAvailable(EnTT)

# Hierarchy example

add_executable(hierarchy_example
    example_hierarchy.cpp
)

target_link_libraries(hierarchy_example
    PRIVATE
        EnTTx
        EnTT::EnTT
)

# Prefab example

add_executable(prefab_example
    example_prefab.cpp
)

target_link_libraries(prefab_example
    PRIVATE
        EnTTx
        EnTT::EnTT
)