#include <unity.h>

#include <cstring>

#include "core/ByteRing.h"

using meshsat::core::ByteRing;

void setUp() {}

void tearDown() {}

void test_push_pop_in_order() {
    ByteRing<8> ring;
    const uint8_t data[] = {1, 2, 3};
    TEST_ASSERT_EQUAL(3, static_cast<int>(ring.push(data, 3)));
    TEST_ASSERT_EQUAL(1, ring.peek());
    TEST_ASSERT_EQUAL(1, ring.pop());
    TEST_ASSERT_EQUAL(2, ring.pop());
    TEST_ASSERT_EQUAL(3, ring.pop());
    TEST_ASSERT_EQUAL(-1, ring.pop());
    TEST_ASSERT_TRUE(ring.empty());
}

void test_push_stops_when_full() {
    ByteRing<4> ring;
    const uint8_t data[] = {1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL(4, static_cast<int>(ring.push(data, 6)));
    TEST_ASSERT_EQUAL(0, static_cast<int>(ring.space()));
    TEST_ASSERT_EQUAL(0, static_cast<int>(ring.push(data, 1)));
}

void test_wraps_around() {
    ByteRing<4> ring;
    const uint8_t first[] = {1, 2, 3};
    const uint8_t second[] = {4, 5, 6};
    ring.push(first, 3);
    ring.drop(2);
    TEST_ASSERT_EQUAL(3, static_cast<int>(ring.push(second, 3)));
    uint8_t out[4] = {};
    TEST_ASSERT_EQUAL(4, static_cast<int>(ring.copyFront(out, sizeof(out))));
    const uint8_t expected[] = {3, 4, 5, 6};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 4);
}

void test_copy_front_does_not_consume() {
    ByteRing<8> ring;
    const uint8_t data[] = {7, 8, 9};
    ring.push(data, 3);
    uint8_t out[2] = {};
    TEST_ASSERT_EQUAL(2, static_cast<int>(ring.copyFront(out, 2)));
    TEST_ASSERT_EQUAL(3, static_cast<int>(ring.size()));
    ring.drop(10);
    TEST_ASSERT_TRUE(ring.empty());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_push_pop_in_order);
    RUN_TEST(test_push_stops_when_full);
    RUN_TEST(test_wraps_around);
    RUN_TEST(test_copy_front_does_not_consume);
    return UNITY_END();
}
