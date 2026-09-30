#include "AppSettings.hpp"

#include <gtest/gtest.h>

TEST(SettingsStartupTests, ResolveStartupWindowConfigHonorsSavedFullscreenMode) {
    AppSettings settings;
    settings.fullscreen = true;
    settings.exclusiveFullscreen = false;
    settings.displayWidth = 1600;
    settings.displayHeight = 1024;
    settings.refreshRate = 60;

    const auto startupWindow = resolveStartupWindowConfig(settings);

    EXPECT_TRUE(startupWindow.fullscreen);
    EXPECT_EQ(startupWindow.width, 1600);
    EXPECT_EQ(startupWindow.height, 1024);
    EXPECT_EQ(startupWindow.refreshRate, 60);
}
