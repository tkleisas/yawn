#include <gtest/gtest.h>
#include "util/DroppedFilePath.h"
#include <fstream>

TEST(DroppedFilePath, AcceptsLocalFilenameAndFileUriWithSpaces) {
    const auto file = std::filesystem::temp_directory_path() / "yawn kick drop.wav";
    { std::ofstream create(file); create << "sample"; }
    const auto path = file.generic_u8string();
    EXPECT_EQ(yawn::droppedFilePath(path), path);
    EXPECT_EQ(yawn::droppedFilePath(" \"" + path + "\"\r\n"), path);
    std::string encoded;
    for (const auto c : path) encoded += c == ' ' ? "%20" : std::string(1, c);
#ifdef _WIN32
    const auto uri = "file:///" + encoded;
#else
    const auto uri = "file://" + encoded;
#endif
    EXPECT_EQ(yawn::droppedFilePath(uri), path);
    std::filesystem::remove(file);
}

TEST(DroppedFilePath, RejectsTextRemoteUrisAndInvalidEscapes) {
    EXPECT_TRUE(yawn::droppedFilePath("kick drum").empty());
    EXPECT_TRUE(yawn::droppedFilePath("https://example.com/kick.wav").empty());
    EXPECT_TRUE(yawn::droppedFilePath("file://remote/kick.wav").empty());
    EXPECT_TRUE(yawn::droppedFilePath("file:///kick%00.wav").empty());
    EXPECT_TRUE(yawn::droppedFilePath("file:///kick%XX.wav").empty());
    EXPECT_TRUE(yawn::droppedFilePath("file:///kick%").empty());
    EXPECT_TRUE(yawn::droppedFilePath("first.wav\nsecond.wav").empty());
    EXPECT_TRUE(yawn::droppedFilePath("").empty());
}
