include(${PROJECT_SOURCE_DIR}/cmake/doctest/doctest.cmake)

add_executable(EnTTxTests
    main.cpp
    test_observer.cpp
    test_hierarchy.cpp
    test_prefab.cpp
    test_stable_id.cpp
)

target_link_libraries(EnTTxTests
    PRIVATE
        EnTTx
        EnTT::EnTT
)

# Register each doctest test case as its own CTest test.
doctest_discover_tests(EnTTxTests)