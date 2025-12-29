//#include <gtest/gtest.h>
//
//#include <string>
//#include <cstring>
//#include <cstdio>     // std::snprintf
//
//#include "ThreadLogger.h"
//#include "Buffer.h"
//
//// --------------------------
//// Helpers
//// --------------------------
//static std::string view(Buffer& b) {
//    return std::string(b.getBuffer(), b.getSize());
//}
//
//static std::string fmt12g(double x) {
//    char tmp[32];
//    std::snprintf(tmp, sizeof(tmp), "%.12g", x);
//    return std::string(tmp);
//}
//
//// --------------------------
//// Fixture
//// --------------------------
//class ThreadLoggerTest : public ::testing::Test {
//protected:
//    ThreadLogger ls;
//};
//
//// ============================================================
//// General case (readable “pipeline” test)
//// ============================================================
//TEST_F(ThreadLoggerTest, General_MixedTypes_ChainingWorks) {
//    // Arrange
//    Buffer buf(256);
//    buf.reset();
//    ls.assignBuffer(&buf);
//
//    // Act
//    // Build a mixed message (int, space, bool, space, double, space, string, space, cstr, space, char)
//    ls << 42 << ' ' << true << ' ' << 3.5 << ' ' << std::string("hello") << ' ' << "world" << '!';
//
//    // Assert
//    // double uses "%.12g" in your implementation
//    std::string expected = std::string("42 true ") + fmt12g(3.5) + " hello world!";
//    EXPECT_EQ(view(buf), expected);
//}
//
//// ============================================================
//// Numbers
//// ============================================================
//TEST_F(ThreadLoggerTest, Int_NoWriteWhenRemainingLessThan32Bytes) {
//    // Arrange
//    Buffer buf(40);
//    buf.reset();
//    ls.assignBuffer(&buf);
//
//    // Fill 10 bytes => remaining 30 (<32), so convertInt should refuse to write
//    buf.add("0123456789", 10);
//    const auto before = view(buf);
//    const auto beforeSize = buf.getSize();
//
//    // Act
//    ls << 123456;
//
//    // Assert
//    EXPECT_EQ(buf.getSize(), beforeSize);
//    EXPECT_EQ(view(buf), before);
//}
//
//// ============================================================
//// Bool
//// ============================================================
//TEST_F(ThreadLoggerTest, Bool_WritesTrue) {
//    // Arrange
//    Buffer buf(64);
//    buf.reset();
//    ls.assignBuffer(&buf);
//
//    // Act
//    ls << true;
//
//    // Assert
//    EXPECT_EQ(view(buf), "true");
//}
//
//TEST_F(ThreadLoggerTest, Bool_WritesFalse) {
//    // Arrange
//    Buffer buf(64);
//    buf.reset();
//    ls.assignBuffer(&buf);
//
//    // Act
//    ls << false;
//
//    // Assert
//    EXPECT_EQ(view(buf), "false");
//}
//
//// ============================================================
//// Floating points
//// ============================================================
//TEST_F(ThreadLoggerTest, Double_Uses12gFormatting) {
//    // Arrange
//    Buffer buf(128);
//    buf.reset();
//    ls.assignBuffer(&buf);
//
//    const double x = 3.141592653589793;
//
//    // Act
//    ls << x;
//
//    // Assert
//    EXPECT_EQ(view(buf), fmt12g(x));
//}
//
//TEST_F(ThreadLoggerTest, Float_CastsToDoubleAndUses12g) {
//    // Arrange
//    Buffer buf(128);
//    buf.reset();
//    ls.assignBuffer(&buf);
//
//    const float x = 1.25f;
//
//    // Act
//    ls << x;
//
//    // Assert
//    EXPECT_EQ(view(buf), fmt12g(static_cast<double>(x)));
//}
//
//// ============================================================
//// Strings: std::string, const char*, char[]
//// ============================================================
//TEST_F(ThreadLoggerTest, StdString_WritesContent) {
//    // Arrange
//    Buffer buf(64);
//    buf.reset();
//    ls.assignBuffer(&buf);
//
//    const std::string s = "hello";
//
//    // Act
//    ls << s;
//
//    // Assert
//    EXPECT_EQ(view(buf), "hello");
//}
//
//TEST_F(ThreadLoggerTest, CStr_WritesContent) {
//    // Arrange
//    Buffer buf(64);
//    buf.reset();
//    ls.assignBuffer(&buf);
//
//    const char* s = "world";
//
//    // Act
//    ls << s;
//
//    // Assert
//    EXPECT_EQ(view(buf), "world");
//}
//
//TEST_F(ThreadLoggerTest, CharArray_WritesContent) {
//    // Arrange
//    Buffer buf(64);
//    buf.reset();
//    ls.assignBuffer(&buf);
//
//    char s[] = "abc";
//
//    // Act
//    ls << s;
//
//    // Assert
//    EXPECT_EQ(view(buf), "abc");
//}
//
//// ============================================================
//// Char
//// ============================================================
//TEST_F(ThreadLoggerTest, Char_WritesSingleChar) {
//    // Arrange
//    Buffer buf(16);
//    buf.reset();
//    ls.assignBuffer(&buf);
//
//    // Act
//    ls << 'X';
//
//    // Assert
//    EXPECT_EQ(view(buf), "X");
//}
//
//// ============================================================
//// Buffer-full cases (strings / char[] / char)
//// Expect: no change, no partial write
//// ============================================================
//TEST_F(ThreadLoggerTest, FullBuffer_NoWriteForStdString) {
//    // Arrange
//    Buffer buf(8);
//    buf.reset();
//    ls.assignBuffer(&buf);
//
//    buf.add("12345678", 8); // fill to capacity
//    ASSERT_EQ(buf.getSize(), 8);
//
//    const auto before = view(buf);
//    const auto beforeSize = buf.getSize();
//
//    // Act
//    ls << std::string("Z");
//
//    // Assert
//    EXPECT_EQ(buf.getSize(), beforeSize);
//    EXPECT_EQ(view(buf), before);
//}
//
//TEST_F(ThreadLoggerTest, FullBuffer_NoWriteForCharArray) {
//    // Arrange
//    Buffer buf(8);
//    buf.reset();
//    ls.assignBuffer(&buf);
//
//    buf.add("12345678", 8);
//    ASSERT_EQ(buf.getSize(), 8);
//
//    const auto before = view(buf);
//    const auto beforeSize = buf.getSize();
//
//    // Act
//    char s[] = "Y";
//    ls << s;
//
//    // Assert
//    EXPECT_EQ(buf.getSize(), beforeSize);
//    EXPECT_EQ(view(buf), before);
//}
//
//TEST_F(ThreadLoggerTest, FullBuffer_NoWriteForCStr) {
//    // Arrange
//    Buffer buf(8);
//    buf.reset();
//    ls.assignBuffer(&buf);
//
//    buf.add("12345678", 8);
//    ASSERT_EQ(buf.getSize(), 8);
//
//    const auto before = view(buf);
//    const auto beforeSize = buf.getSize();
//
//    // Act
//    ls << "W";
//
//    // Assert
//    EXPECT_EQ(buf.getSize(), beforeSize);
//    EXPECT_EQ(view(buf), before);
//}
//
//TEST_F(ThreadLoggerTest, FullBuffer_NoWriteForChar) {
//    // Arrange
//    Buffer buf(8);
//    buf.reset();
//    ls.assignBuffer(&buf);
//
//    buf.add("12345678", 8);
//    ASSERT_EQ(buf.getSize(), 8);
//
//    const auto before = view(buf);
//    const auto beforeSize = buf.getSize();
//
//    // Act
//    ls << 'Q';
//
//    // Assert
//    EXPECT_EQ(buf.getSize(), beforeSize);
//    EXPECT_EQ(view(buf), before);
//}
