# Test executable — all test files + required source files (no UI/OpenGL deps)
file(GLOB TEST_SOURCES *.cpp)

# Source files needed by tests (non-UI logic only)
set(TESTABLE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/audio/ClipEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/util/FileIO.cpp
)

add_executable(yawn_tests ${TEST_SOURCES} ${TESTABLE_SOURCES})

target_include_directories(yawn_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(yawn_tests PRIVATE YAWN_TEST_BUILD)

target_link_libraries(yawn_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    sndfile
)

include(GoogleTest)
gtest_discover_tests(yawn_tests)
