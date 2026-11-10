#include <unity.h>
#include <cstring>
#include "file_rules.h"
using namespace mt;
void setUp() {} void tearDown() {}

void test_list_move() {
  TEST_ASSERT_EQUAL(1, listMove(0, 1, 5));
  TEST_ASSERT_EQUAL(0, listMove(4, 1, 5));   // wraps to the top
  TEST_ASSERT_EQUAL(4, listMove(0, -1, 5));  // and to the end
  TEST_ASSERT_EQUAL(2, listMove(4, 8, 5));
  TEST_ASSERT_EQUAL(0, listMove(3, 1, 0));   // empty
  TEST_ASSERT_EQUAL(0, listMove(0, 3, 1));
}

void test_list_page() {
  TEST_ASSERT_EQUAL(10, listPage(0, 1, 10, 30));
  TEST_ASSERT_EQUAL(29, listPage(25, 1, 10, 30));  // to the end, no wrap
  TEST_ASSERT_EQUAL(0, listPage(5, -1, 10, 30));
  TEST_ASSERT_EQUAL(0, listPage(29, 1, 10, 30));   // at the end: wraps to the top
  TEST_ASSERT_EQUAL(29, listPage(0, -1, 10, 30));  // at the top: to the end
  TEST_ASSERT_EQUAL(0, listPage(0, 1, 10, 0));
}

void test_list_visible() {
  TEST_ASSERT_EQUAL(0, listKeepVisible(3, 0, 10, 30));
  TEST_ASSERT_EQUAL(1, listKeepVisible(10, 0, 10, 30));
  TEST_ASSERT_EQUAL(4, listKeepVisible(4, 8, 10, 30));
  TEST_ASSERT_EQUAL(20, listKeepVisible(29, 25, 10, 30));  // past the end: pulled back
  TEST_ASSERT_EQUAL(0, listKeepVisible(2, 5, 10, 4));
  TEST_ASSERT_EQUAL(0, listClampTop(-3, 10, 30));
  TEST_ASSERT_EQUAL(20, listClampTop(25, 10, 30));
  TEST_ASSERT_EQUAL(0, listClampTop(2, 10, 8));
}

void test_fit_name() {
  char out[32];
  fitName("kick", 8, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("kick", out);
  fitName("abcdefgh", 8, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("abcdefgh", out);
  fitName("abcdefghi", 8, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("abcdefg~", out);
  fitName("abcdefghi", 0, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("", out);
  fitName("abcdefghi", 8, out, 5);  // the buffer limits too
  TEST_ASSERT_EQUAL_STRING("abc~", out);
}

void test_fit_tail() {
  char out[32];
  fitTail("/samples", 10, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("/samples", out);
  fitTail("/samples/drums/909", 10, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("...ums/909", out);
  fitTail("/samples/drums/909", 3, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("909", out);
  fitTail("/abc", 0, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("", out);
}

void test_browse_into() {
  char dir[24] = "/samples";
  TEST_ASSERT_EQUAL(0, browseDepth(dir, "/samples"));
  TEST_ASSERT_EQUAL(int(BrowseResult::Ok), int(browseInto(dir, sizeof dir, "/samples", "a", 2)));
  TEST_ASSERT_EQUAL_STRING("/samples/a", dir);
  TEST_ASSERT_EQUAL(1, browseDepth(dir, "/samples"));
  TEST_ASSERT_EQUAL(int(BrowseResult::Ok), int(browseInto(dir, sizeof dir, "/samples", "b", 2)));
  TEST_ASSERT_EQUAL(int(BrowseResult::TooDeep), int(browseInto(dir, sizeof dir, "/samples", "c", 2)));
  TEST_ASSERT_EQUAL_STRING("/samples/a/b", dir);
  TEST_ASSERT_EQUAL(int(BrowseResult::TooLong), int(browseInto(dir, sizeof dir, "/samples", "0123456789ab", 4)));
  TEST_ASSERT_EQUAL_STRING("/samples/a/b", dir);
  TEST_ASSERT_EQUAL(int(BrowseResult::Ok), int(browseInto(dir, sizeof dir, "/samples", "0123456789", 4)));  // 23 chars
  TEST_ASSERT_EQUAL(-1, browseDepth("/midi/x", "/samples"));
}

int main(){ UNITY_BEGIN();
  RUN_TEST(test_list_move);
  RUN_TEST(test_list_page);
  RUN_TEST(test_list_visible);
  RUN_TEST(test_fit_name);
  RUN_TEST(test_fit_tail);
  RUN_TEST(test_browse_into);
return UNITY_END();}
