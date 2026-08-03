include(FetchContent)

FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v4.0.0
)

FetchContent_MakeAvailable(EnTT)


add_executable(hierarchy_example
    example_hierarchy.cpp
)

target_link_libraries(hierarchy_example
    PRIVATE
        EnTTx
        EnTT::EnTT
)