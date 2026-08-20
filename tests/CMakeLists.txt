include(FetchContent)

FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v4.0.0
)

FetchContent_MakeAvailable(EnTT)

add_executable(EnTTxTests
    main.cpp
    test_observer.cpp
    test_hierarchy.cpp
    test_prefab.cpp
)

target_link_libraries(EnTTxTests
    PRIVATE
        EnTTx
        EnTT::EnTT
)

enable_testing()

add_test(NAME EnTTxTests COMMAND EnTTxTests)