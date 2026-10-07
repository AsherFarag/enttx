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

# Run each example as a test, so a crash or failed assert fails CI.

if(ENTTX_BUILD_TESTING)
    foreach(ENTTX_EXAMPLE
        hierarchy_example
        prefab_example
        stable_id_example
        observer_example
    )
        add_test(NAME ${ENTTX_EXAMPLE} COMMAND ${ENTTX_EXAMPLE})
    endforeach()
endif()
