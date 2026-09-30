#include "multiplayer/PlayerProfileStore.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

std::filesystem::path makeTempProfilePath() {
    return std::filesystem::temp_directory_path() /
           ("nodespire_profile_test_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ".json");
}

} // namespace

TEST(PlayerProfileStore, GeneratesUuidAndDefaultNameWhenFileMissing) {
    const auto path = makeTempProfilePath();
    std::filesystem::remove(path);

    multiplayer::PlayerProfileStore store(path);
    EXPECT_FALSE(store.profile().playerUuid.empty());
    EXPECT_EQ(store.profile().displayName, "Player");
    EXPECT_TRUE(store.profile().lastPlayedLevelId.empty());

    std::filesystem::remove(path);
}

TEST(PlayerProfileStore, PersistsUuidAcrossReload) {
    const auto path = makeTempProfilePath();
    std::filesystem::remove(path);

    const std::string firstUuid = [&] {
        multiplayer::PlayerProfileStore store(path);
        return store.profile().playerUuid;
    }();

    multiplayer::PlayerProfileStore reloaded(path);
    EXPECT_EQ(reloaded.profile().playerUuid, firstUuid);

    std::filesystem::remove(path);
}

TEST(PlayerProfileStore, SetDisplayNameTrimsAndPersists) {
    const auto path = makeTempProfilePath();
    std::filesystem::remove(path);

    multiplayer::PlayerProfileStore store(path);
    EXPECT_TRUE(store.setDisplayName("  Skywalker  "));
    EXPECT_EQ(store.profile().displayName, "Skywalker");

    multiplayer::PlayerProfileStore reloaded(path);
    EXPECT_EQ(reloaded.profile().displayName, "Skywalker");

    std::filesystem::remove(path);
}

TEST(PlayerProfileStore, SetDisplayNameRejectsEmptyOrTooLong) {
    const auto path = makeTempProfilePath();
    std::filesystem::remove(path);

    multiplayer::PlayerProfileStore store(path);
    EXPECT_FALSE(store.setDisplayName("   "));
    EXPECT_FALSE(store.setDisplayName(std::string(64, 'x')));

    std::filesystem::remove(path);
}

TEST(PlayerProfileStore, SetLastPlayedLevelIdPersists) {
    const auto path = makeTempProfilePath();
    std::filesystem::remove(path);

    multiplayer::PlayerProfileStore store(path);
    EXPECT_TRUE(store.setLastPlayedLevelId("mesa_canyon"));
    EXPECT_EQ(store.profile().lastPlayedLevelId, "mesa_canyon");

    multiplayer::PlayerProfileStore reloaded(path);
    EXPECT_EQ(reloaded.profile().lastPlayedLevelId, "mesa_canyon");

    std::filesystem::remove(path);
}

TEST(PlayerProfileStore, SetLastPlayedLevelIdRejectsEmpty) {
    const auto path = makeTempProfilePath();
    std::filesystem::remove(path);

    multiplayer::PlayerProfileStore store(path);
    EXPECT_FALSE(store.setLastPlayedLevelId(""));
    EXPECT_TRUE(store.profile().lastPlayedLevelId.empty());

    std::filesystem::remove(path);
}
