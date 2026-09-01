include(GoogleTest)

add_executable(NodeSpireMatchProtocolTests
    multiplayer/FixedTickClockTests.cpp
    multiplayer/MatchSimulationTests.cpp
    multiplayer/MatchProtocolAdapterTests.cpp
    multiplayer/PlayerAccountsTests.cpp
    multiplayer/PlayLevelCombatControllerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/FixedTickClock.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/LocalMatchHost.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/MatchSimulation.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/MatchProtocolAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/PlayerAccounts.cpp
    ${CMAKE_SOURCE_DIR}/src/scenes/PlayLevelCombatController.cpp
)

target_include_directories(NodeSpireMatchProtocolTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(NodeSpireMatchProtocolTests PRIVATE GTest::gtest_main NodeSpireMatchProtocol glm::glm)

nodespire_stage_runtime_dependencies(NodeSpireMatchProtocolTests)
gtest_discover_tests(NodeSpireMatchProtocolTests)