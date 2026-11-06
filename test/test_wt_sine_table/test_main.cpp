#include <cmath>
#include <cstring>
#include <unity.h>
#include "wt_sine_table.h"
#include "wt_mip.h"
using namespace mt;
void setUp() {}
void tearDown() {}
void test_flash_table_matches_original_runtime_double_sine_bits() {
  for (int i = 0; i < detail::kWtSineLen; ++i) {
    const float expected = static_cast<float>(sin(6.283185307179586 * i / 4096));
    uint32_t a, b;
    std::memcpy(&a, &expected, sizeof a);
    std::memcpy(&b, &detail::kWtSine[i], sizeof b);
    TEST_ASSERT_EQUAL_HEX32(a, b);
  }
}
void test_synthesis_stride_is_bit_exact_to_original_256_point_table() {
  for (int i = 0; i < kWtFrameLen; ++i) {
    const float expected = static_cast<float>(sin(6.283185307179586 * i / kWtFrameLen));
    const float actual = detail::kWtSine[i * (detail::kWtSineLen / kWtFrameLen)];
    TEST_ASSERT_EQUAL_MEMORY(&expected, &actual, sizeof expected);
  }
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_flash_table_matches_original_runtime_double_sine_bits);
  RUN_TEST(test_synthesis_stride_is_bit_exact_to_original_256_point_table);
  return UNITY_END();
}
