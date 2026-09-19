#include <unity.h>

#include <string>

#include "../support/FakeByteStream.h"
#include "iridium/IridiumModem.h"

using meshsat::iridium::CommandOutcome;
using meshsat::iridium::IridiumModem;
using meshsat::test::FakeByteStream;
using meshsat::test::RecordingListener;
using meshsat::test::responseOf;

namespace {

constexpr uint32_t kTimeoutMs = 3000;

struct Fixture {
    FakeByteStream link;
    RecordingListener listener;
    IridiumModem modem{link};

    Fixture() {
        modem.setListener(&listener);
        modem.begin();
    }
};

}

void setUp() {}

void tearDown() {}

void test_sends_exactly_at_cr() {
    Fixture f;
    TEST_ASSERT_TRUE(f.modem.sendCommand("AT", kTimeoutMs, 0));
    TEST_ASSERT_EQUAL_STRING("AT\r", f.link.written.c_str());
    TEST_ASSERT_TRUE(f.modem.commandInFlight());
}

void test_ok_with_echo_completes_and_keeps_raw_reply() {
    Fixture f;
    f.modem.sendCommand("AT", kTimeoutMs, 0);
    f.link.feed("AT\r\r\nOK\r\n");
    f.modem.poll(25);
    TEST_ASSERT_FALSE(f.modem.commandInFlight());
    TEST_ASSERT_EQUAL(CommandOutcome::Ok, f.modem.lastOutcome());
    TEST_ASSERT_EQUAL(1, f.listener.completions);
    TEST_ASSERT_EQUAL_STRING("AT\r\r\nOK\r\n", responseOf(f.modem).c_str());
    TEST_ASSERT_EQUAL_UINT32(25, f.modem.lastDurationMs());
    TEST_ASSERT_TRUE(f.listener.idle.empty());
}

void test_lf_arriving_in_a_later_poll_stays_in_the_reply() {
    Fixture f;
    f.modem.sendCommand("AT", kTimeoutMs, 0);
    f.link.feed("\r\nOK\r");
    f.modem.poll(10);
    TEST_ASSERT_TRUE(f.modem.commandInFlight());
    f.link.feed("\n");
    f.modem.poll(11);
    TEST_ASSERT_FALSE(f.modem.commandInFlight());
    TEST_ASSERT_EQUAL(CommandOutcome::Ok, f.modem.lastOutcome());
    TEST_ASSERT_EQUAL_STRING("\r\nOK\r\n", responseOf(f.modem).c_str());
    TEST_ASSERT_EQUAL_UINT32(10, f.modem.lastDurationMs());
    TEST_ASSERT_TRUE(f.listener.idle.empty());
}

void test_missing_lf_completes_after_grace() {
    Fixture f;
    f.modem.sendCommand("AT", kTimeoutMs, 0);
    f.link.feed("OK\r");
    f.modem.poll(5);
    TEST_ASSERT_TRUE(f.modem.commandInFlight());
    f.modem.poll(5 + IridiumModem::kFinalLineGraceMs - 1);
    TEST_ASSERT_TRUE(f.modem.commandInFlight());
    f.modem.poll(5 + IridiumModem::kFinalLineGraceMs);
    TEST_ASSERT_EQUAL(CommandOutcome::Ok, f.modem.lastOutcome());
}

void test_bytes_after_the_reply_are_idle() {
    Fixture f;
    f.modem.sendCommand("AT", kTimeoutMs, 0);
    f.link.feed("OK\r\nSBDRING\r\n");
    f.modem.poll(5);
    TEST_ASSERT_EQUAL(CommandOutcome::Ok, f.modem.lastOutcome());
    TEST_ASSERT_EQUAL_STRING("OK\r\n", responseOf(f.modem).c_str());
    TEST_ASSERT_EQUAL_STRING("SBDRING\r\n", f.listener.idle.c_str());
}

void test_error_reply() {
    Fixture f;
    f.modem.sendCommand("AT+BAD", kTimeoutMs, 0);
    f.link.feed("AT+BAD\r\r\nERROR\r\n");
    f.modem.poll(5);
    TEST_ASSERT_EQUAL(CommandOutcome::Error, f.modem.lastOutcome());
    TEST_ASSERT_EQUAL(1, f.listener.completions);
}

void test_timeout_is_exact() {
    Fixture f;
    f.modem.sendCommand("AT", kTimeoutMs, 100);
    f.modem.poll(100 + kTimeoutMs - 1);
    TEST_ASSERT_TRUE(f.modem.commandInFlight());
    f.modem.poll(100 + kTimeoutMs);
    TEST_ASSERT_FALSE(f.modem.commandInFlight());
    TEST_ASSERT_EQUAL(CommandOutcome::Timeout, f.modem.lastOutcome());
    TEST_ASSERT_EQUAL_UINT32(kTimeoutMs, f.modem.lastDurationMs());
    TEST_ASSERT_EQUAL(0, static_cast<int>(f.modem.lastResponseLength()));
}

void test_timeout_across_millis_wraparound() {
    Fixture f;
    const uint32_t start = 0xFFFFFF00u;
    f.modem.sendCommand("AT", kTimeoutMs, start);
    f.modem.poll(start + 0x100u);
    TEST_ASSERT_TRUE(f.modem.commandInFlight());
    f.modem.poll(start + kTimeoutMs);
    TEST_ASSERT_EQUAL(CommandOutcome::Timeout, f.modem.lastOutcome());
}

void test_garbage_without_ok_times_out() {
    Fixture f;
    f.modem.sendCommand("AT", kTimeoutMs, 0);
    f.link.feed("\xF0\x80\xFF garbage\r\n");
    f.modem.poll(1);
    f.modem.poll(kTimeoutMs);
    TEST_ASSERT_EQUAL(CommandOutcome::Timeout, f.modem.lastOutcome());
    TEST_ASSERT_EQUAL(13, static_cast<int>(f.modem.lastResponseLength()));
}

void test_busy_while_command_in_flight() {
    Fixture f;
    TEST_ASSERT_TRUE(f.modem.sendCommand("AT", kTimeoutMs, 0));
    TEST_ASSERT_FALSE(f.modem.sendCommand("AT", kTimeoutMs, 1));
    TEST_ASSERT_EQUAL_STRING("AT\r", f.link.written.c_str());
}

void test_rejects_bad_commands() {
    Fixture f;
    TEST_ASSERT_FALSE(f.modem.sendCommand("", kTimeoutMs, 0));
    TEST_ASSERT_FALSE(f.modem.sendCommand(nullptr, kTimeoutMs, 0));
    TEST_ASSERT_FALSE(f.modem.sendCommand("AT\r", kTimeoutMs, 0));
    TEST_ASSERT_FALSE(f.modem.sendCommand("AT\n", kTimeoutMs, 0));
    const std::string tooLong(IridiumModem::kMaxCommandLength + 1, 'A');
    TEST_ASSERT_FALSE(f.modem.sendCommand(tooLong.c_str(), kTimeoutMs, 0));
    const std::string longest(IridiumModem::kMaxCommandLength, 'A');
    TEST_ASSERT_TRUE(f.modem.sendCommand(longest.c_str(), kTimeoutMs, 0));
    TEST_ASSERT_EQUAL_STRING((longest + "\r").c_str(), f.link.written.c_str());
}

void test_stale_input_is_drained_before_sending() {
    Fixture f;
    f.link.feed("stale\r\n");
    TEST_ASSERT_TRUE(f.modem.sendCommand("AT", kTimeoutMs, 0));
    TEST_ASSERT_EQUAL_STRING("stale\r\n", f.listener.idle.c_str());
    f.link.feed("OK\r\n");
    f.modem.poll(1);
    TEST_ASSERT_EQUAL_STRING("OK\r\n", responseOf(f.modem).c_str());
}

void test_idle_bytes_go_to_listener() {
    Fixture f;
    f.link.feed("SBDRING\r\n");
    f.modem.poll(0);
    TEST_ASSERT_EQUAL_STRING("SBDRING\r\n", f.listener.idle.c_str());
    TEST_ASSERT_EQUAL(0, f.listener.completions);
}

void test_partial_writes_finish_over_polls() {
    Fixture f;
    f.link.maxWritePerCall = 1;
    f.link.refuseWrites = true;
    TEST_ASSERT_TRUE(f.modem.sendCommand("AT", kTimeoutMs, 0));
    TEST_ASSERT_TRUE(f.modem.transmitPending());
    TEST_ASSERT_EQUAL_STRING("", f.link.written.c_str());
    f.link.refuseWrites = false;
    f.modem.poll(1);
    TEST_ASSERT_EQUAL_STRING("AT\r", f.link.written.c_str());
    TEST_ASSERT_FALSE(f.modem.transmitPending());
}

void test_timeout_drops_unsent_command_bytes() {
    Fixture f;
    f.link.refuseWrites = true;
    f.modem.sendCommand("AT", kTimeoutMs, 0);
    f.modem.poll(kTimeoutMs);
    TEST_ASSERT_EQUAL(CommandOutcome::Timeout, f.modem.lastOutcome());
    TEST_ASSERT_FALSE(f.modem.transmitPending());
    f.link.refuseWrites = false;
    f.modem.poll(kTimeoutMs + 1);
    TEST_ASSERT_EQUAL_STRING("", f.link.written.c_str());
}

void test_write_raw_passes_bytes_verbatim() {
    Fixture f;
    const uint8_t bytes[] = {'A', 'T', '&', 'K', '0', '\r'};
    TEST_ASSERT_EQUAL(sizeof(bytes), f.modem.writeRaw(bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL_STRING("AT&K0\r", f.link.written.c_str());
    f.link.feed("AT&K0\r\r\nOK\r\n");
    f.modem.poll(1);
    TEST_ASSERT_EQUAL_STRING("AT&K0\r\r\nOK\r\n", f.listener.idle.c_str());
    TEST_ASSERT_EQUAL(0, f.listener.completions);
}

void test_write_raw_refused_during_command() {
    Fixture f;
    f.modem.sendCommand("AT", kTimeoutMs, 0);
    const uint8_t byte = 'X';
    TEST_ASSERT_EQUAL(0, static_cast<int>(f.modem.writeRaw(&byte, 1)));
}

void test_send_command_refused_while_raw_bytes_queued() {
    Fixture f;
    f.link.refuseWrites = true;
    const uint8_t byte = 'X';
    TEST_ASSERT_EQUAL(1, static_cast<int>(f.modem.writeRaw(&byte, 1)));
    TEST_ASSERT_FALSE(f.modem.sendCommand("AT", kTimeoutMs, 0));
}

void test_long_reply_is_truncated_but_still_completes() {
    Fixture f;
    f.modem.sendCommand("AT", kTimeoutMs, 0);
    const std::string filler(IridiumModem::kResponseCapacity, 'x');
    f.link.feed(filler + "\r\nOK\r\n");
    for (uint32_t t = 1; t < 20 && f.modem.commandInFlight(); ++t) {
        f.modem.poll(t);
    }
    TEST_ASSERT_EQUAL(CommandOutcome::Ok, f.modem.lastOutcome());
    TEST_ASSERT_TRUE(f.modem.lastResponseTruncated());
    TEST_ASSERT_EQUAL(static_cast<int>(IridiumModem::kResponseCapacity),
                      static_cast<int>(f.modem.lastResponseLength()));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sends_exactly_at_cr);
    RUN_TEST(test_ok_with_echo_completes_and_keeps_raw_reply);
    RUN_TEST(test_lf_arriving_in_a_later_poll_stays_in_the_reply);
    RUN_TEST(test_missing_lf_completes_after_grace);
    RUN_TEST(test_bytes_after_the_reply_are_idle);
    RUN_TEST(test_error_reply);
    RUN_TEST(test_timeout_is_exact);
    RUN_TEST(test_timeout_across_millis_wraparound);
    RUN_TEST(test_garbage_without_ok_times_out);
    RUN_TEST(test_busy_while_command_in_flight);
    RUN_TEST(test_rejects_bad_commands);
    RUN_TEST(test_stale_input_is_drained_before_sending);
    RUN_TEST(test_idle_bytes_go_to_listener);
    RUN_TEST(test_partial_writes_finish_over_polls);
    RUN_TEST(test_timeout_drops_unsent_command_bytes);
    RUN_TEST(test_write_raw_passes_bytes_verbatim);
    RUN_TEST(test_write_raw_refused_during_command);
    RUN_TEST(test_send_command_refused_while_raw_bytes_queued);
    RUN_TEST(test_long_reply_is_truncated_but_still_completes);
    return UNITY_END();
}
