include(GoogleTest)

add_executable(NodeSpireMatchProtocolTests
    multiplayer/MatchProtocolAdapterTests.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/LocalMatchHost.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/MatchProtocolAdapter.cpp
)

target_include_directories(NodeSpireMatchProtocolTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(NodeSpireMatchProtocolTests PRIVATE GTest::gtest_main NodeSpireMatchProtocol)

nodespire_stage_runtime_dependencies(NodeSpireMatchProtocolTests)
gtest_discover_tests(NodeSpireMatchProtocolTests)