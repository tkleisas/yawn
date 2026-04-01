# Test executable — all test files, linked against yawn_core (no UI/OpenGL deps)
file(GLOB TEST_SOURCES *.cpp)

add_executable(yawn_tests ${TEST_SOURCES})

target_include_directories(yawn_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(yawn_tests PRIVATE YAWN_TEST_BUILD)

target_link_libraries(yawn_tests PRIVATE
    yawn_core
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(yawn_tests)
