#include <unity.h>

#include <string>

#include "iridium/AtResponseParser.h"

using meshsat::iridium::AtResponseParser;
using meshsat::iridium::FinalResult;

namespace {

// Feeds `bytes` and returns the first non-None result together with the
// index of the byte that produced it (-1 if none).
FinalResult feedAll(AtResponseParser& parser, const std::string& bytes, int* atIndex = nullptr) {
    for (size_t i = 0; i < bytes.size(); ++i) {
        const FinalResult result = parser.feed(static_cast<uint8_t>(bytes[i]));
        if (result != FinalResult::None) {
            if (atIndex != nullptr) {
                *atIndex = static_cast<int>(i);
            }
            return result;
        }
    }
    if (atIndex != nullptr) {
        *atIndex = -1;
    }
    return FinalResult::None;
}

}

void setUp() {}

void tearDown() {}

void test_ok_after_echo() {
    AtResponseParser parser;
    int index = 0;
    const std::string reply = "AT\r\r\nOK\r\n";
    TEST_ASSERT_EQUAL(FinalResult::Ok, feedAll(parser, reply, &index));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(reply.find("OK") + 2), index);
}

void test_ok_without_echo() {
    AtResponseParser parser;
    TEST_ASSERT_EQUAL(FinalResult::Ok, feedAll(parser, "\r\nOK\r\n"));
}

void test_error() {
    AtResponseParser parser;
    TEST_ASSERT_EQUAL(FinalResult::Error, feedAll(parser, "AT+XYZ\r\r\nERROR\r\n"));
}

void test_lf_only_line_endings() {
    AtResponseParser parser;
    TEST_ASSERT_EQUAL(FinalResult::Ok, feedAll(parser, "\nOK\n"));
}

void test_ok_must_be_whole_line() {
    AtResponseParser parser;
    TEST_ASSERT_EQUAL(FinalResult::None, feedAll(parser, "BOOK\r\nOKAY\r\n OK\r\nOK \r\n"));
}

void test_unterminated_ok_is_not_final() {
    AtResponseParser parser;
    TEST_ASSERT_EQUAL(FinalResult::None, feedAll(parser, "\r\nOK"));
    TEST_ASSERT_EQUAL(FinalResult::Ok, feedAll(parser, "\r"));
}

void test_numeric_status_line_is_not_final() {
    AtResponseParser parser;
    TEST_ASSERT_EQUAL(FinalResult::None, feedAll(parser, "0\r\n"));
    TEST_ASSERT_EQUAL(FinalResult::Ok, feedAll(parser, "\r\nOK\r\n"));
}

void test_overlong_line_is_skipped() {
    AtResponseParser parser;
    const std::string longLine(AtResponseParser::kMaxLineLength + 10, 'X');
    TEST_ASSERT_EQUAL(FinalResult::None, feedAll(parser, longLine + "OK\r\n"));
    TEST_ASSERT_EQUAL(FinalResult::Ok, feedAll(parser, "OK\r\n"));
}

void test_reset_drops_partial_line() {
    AtResponseParser parser;
    feedAll(parser, "O");
    parser.reset();
    TEST_ASSERT_EQUAL(FinalResult::None, feedAll(parser, "K\r\n"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ok_after_echo);
    RUN_TEST(test_ok_without_echo);
    RUN_TEST(test_error);
    RUN_TEST(test_lf_only_line_endings);
    RUN_TEST(test_ok_must_be_whole_line);
    RUN_TEST(test_unterminated_ok_is_not_final);
    RUN_TEST(test_numeric_status_line_is_not_final);
    RUN_TEST(test_overlong_line_is_skipped);
    RUN_TEST(test_reset_drops_partial_line);
    return UNITY_END();
}
