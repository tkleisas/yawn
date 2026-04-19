# Test executable — all test files, linked against yawn_core (no UI/OpenGL deps)
file(GLOB TEST_SOURCES *.cpp)

add_executable(yawn_tests ${TEST_SOURCES})

target_include_directories(yawn_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(yawn_tests PRIVATE YAWN_TEST_BUILD)

# Expose the vendored tinygltf sample glb path so the M3DModel test can
# load a real fixture without us having to ship our own.
if(YAWN_HAS_MODEL3D)
    target_compile_definitions(yawn_tests PRIVATE
        YAWN_TINYGLTF_SAMPLE_DIR="${tinygltf_SOURCE_DIR}/models"
        YAWN_BUNDLED_MODELS_DIR="${CMAKE_SOURCE_DIR}/assets/examples/3d"
    )
endif()

target_link_libraries(yawn_tests PRIVATE
    yawn_core
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(yawn_tests)
