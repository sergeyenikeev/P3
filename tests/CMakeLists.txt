add_executable(unit_tests
    unit/test_main.cpp
)
target_link_libraries(unit_tests PRIVATE uploader_core)
add_test(NAME unit COMMAND unit_tests)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
    NAME integration
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_integration.py
            --uploader $<TARGET_FILE:uploader>
)
set_tests_properties(integration PROPERTIES
    ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/integration"
)

add_test(
    NAME e2e
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_e2e.py
            --uploader $<TARGET_FILE:uploader>
)
set_tests_properties(e2e PROPERTIES
    ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/integration"
)
