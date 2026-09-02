include(GoogleTest)

add_executable(NodeSpireMatchProtocolTests
    multiplayer/FixedTickClockTests.cpp
    multiplayer/LanMatchTransportTests.cpp
    multiplayer/LocalMatchHostTests.cpp
    multiplayer/LoopbackMatchSessionTests.cpp
    multiplayer/LoopbackTransportTests.cpp
    multiplayer/MatchSimulationTests.cpp
    multiplayer/MatchSnapshotBuilderTests.cpp
    multiplayer/MatchProtocolAdapterTests.cpp
    multiplayer/PlayerAccountsTests.cpp
    multiplayer/PlayLevelCombatControllerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/FixedTickClock.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/LanFramedConnection.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/LanMatchClient.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/LanMatchTransport.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/LoopbackMatchSession.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/LoopbackTransport.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/LocalMatchHost.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/MatchSimulation.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/MatchProtocolAdapter.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/MatchSnapshotBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/multiplayer/PlayerAccounts.cpp
    ${CMAKE_SOURCE_DIR}/src/scenes/PlayLevelCombatController.cpp
    ${CMAKE_SOURCE_DIR}/src/scenes/PlayLevelWaveController.cpp
)

target_include_directories(NodeSpireMatchProtocolTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(NodeSpireMatchProtocolTests PRIVATE
    GTest::gtest_main
    NodeSpireMatchProtocol
    glm::glm
    lua
    spdlog::spdlog
)

nodespire_stage_runtime_dependencies(NodeSpireMatchProtocolTests)
gtest_discover_tests(NodeSpireMatchProtocolTests)