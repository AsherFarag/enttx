find_program(ENTTX_CLANG_FORMAT
    NAMES clang-format
)

if(ENTTX_CLANG_FORMAT)
    message(STATUS "Found clang-format: ${ENTTX_CLANG_FORMAT}")

    file(GLOB_RECURSE ENTTX_FORMAT_FILES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp"
    )

    add_custom_target(EnTTx-format
        COMMAND ${ENTTX_CLANG_FORMAT}
            -i
            -style=file
            ${ENTTX_FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting EnTTx"
        VERBATIM
    )
    
    add_custom_target(EnTTx-format-check
        COMMAND ${ENTTX_CLANG_FORMAT}
            --dry-run
            --Werror
            -style=file
            ${ENTTX_FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking EnTTx formatting"
        VERBATIM
    )
else()
    message(WARNING "clang-format not found")
endif()