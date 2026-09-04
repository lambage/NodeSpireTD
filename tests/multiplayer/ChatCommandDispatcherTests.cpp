#include "multiplayer/ChatCommandDispatcher.hpp"

#include <gtest/gtest.h>

using multiplayer::ChatCommandDispatcher;

TEST(ChatCommandDispatcher, RelaysPlainTextUnchanged) {
    ChatCommandDispatcher dispatcher;
    const auto result = dispatcher.process("hello party", "Alice");
    ASSERT_TRUE(result.broadcast.has_value());
    EXPECT_EQ(result.broadcast->text, "hello party");
    EXPECT_FALSE(result.broadcast->isEmote);
    EXPECT_FALSE(result.localError.has_value());
}

TEST(ChatCommandDispatcher, RoarEmoteFormatsSpeakerName) {
    ChatCommandDispatcher dispatcher;
    const auto result = dispatcher.process("/roar", "Alice");
    ASSERT_TRUE(result.broadcast.has_value());
    EXPECT_EQ(result.broadcast->text, "Alice roars!");
    EXPECT_TRUE(result.broadcast->isEmote);
}

TEST(ChatCommandDispatcher, UnknownCommandProducesLocalErrorOnly) {
    ChatCommandDispatcher dispatcher;
    const auto result = dispatcher.process("/nonexistent", "Alice");
    EXPECT_FALSE(result.broadcast.has_value());
    ASSERT_TRUE(result.localError.has_value());
    EXPECT_EQ(*result.localError, "Unknown command: /nonexistent");
}

TEST(ChatCommandDispatcher, EmptyOrWhitespaceTextProducesNothing) {
    ChatCommandDispatcher dispatcher;
    const auto result = dispatcher.process("   ", "Alice");
    EXPECT_FALSE(result.broadcast.has_value());
    EXPECT_FALSE(result.localError.has_value());
}

TEST(ChatCommandDispatcher, StripsControlCharactersAndTrims) {
    ChatCommandDispatcher dispatcher;
    const auto result = dispatcher.process("  hi\x01 there\r\n  ", "Alice");
    ASSERT_TRUE(result.broadcast.has_value());
    EXPECT_EQ(result.broadcast->text, "hi there");
}

TEST(ChatCommandDispatcher, CommandNameIsCaseInsensitive) {
    ChatCommandDispatcher dispatcher;
    const auto result = dispatcher.process("/ROAR", "Bob");
    ASSERT_TRUE(result.broadcast.has_value());
    EXPECT_EQ(result.broadcast->text, "Bob roars!");
}

TEST(ChatCommandDispatcher, MeCommandAppendsArguments) {
    ChatCommandDispatcher dispatcher;
    const auto result = dispatcher.process("/me waves hello", "Alice");
    ASSERT_TRUE(result.broadcast.has_value());
    EXPECT_EQ(result.broadcast->text, "Alice waves hello");
    EXPECT_TRUE(result.broadcast->isEmote);
}
