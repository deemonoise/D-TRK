#include <unity.h>
#include "wt_editor.h"
#include <cstring>
using namespace mt;
static int16_t live[kWtSrcSamples], backup[kWtSrcSamples], original[kWtSrcSamples];
static WtEditor doc;
void setUp() { doc.attach(live, backup); }
void tearDown() { doc.detach(); }
static void load() {
  for (int f=0; f<kWtFrames; ++f) for (int i=0;i<kWtFrameLen;++i)
    live[f*kWtFrameLen+i] = int16_t(f*401+i*3-15000);
  memcpy(original,live,sizeof(live)); doc.loaded();
}
void test_loaded_preserves_all_samples() { load(); TEST_ASSERT_EQUAL_MEMORY(original,live,sizeof(live)); for(int f=0;f<64;++f) TEST_ASSERT_TRUE(doc.anchor(f)); TEST_ASSERT_FALSE(doc.dirty()); }
void test_stroke_local_and_single_undo() { load(); doc.select(31); TEST_ASSERT_TRUE(doc.beginStroke()); doc.drawLine(0,123,4,456); doc.drawLine(4,456,8,789); doc.endStroke(); TEST_ASSERT_EQUAL_MEMORY(original,live,31*256*2); TEST_ASSERT_EQUAL_MEMORY(original+32*256,live+32*256,32*256*2); TEST_ASSERT_TRUE(doc.undo()); TEST_ASSERT_EQUAL_MEMORY(original,live,sizeof(live)); TEST_ASSERT_EQUAL(31,doc.selected()); TEST_ASSERT_FALSE(doc.dirty()); TEST_ASSERT_FALSE(doc.canUndo()); }
void test_wave_stroke_all_frames() { doc.newWave(); doc.beginStroke(); doc.drawLine(10,-32768,12,32767); doc.endStroke(); for(int f=1;f<64;++f) TEST_ASSERT_EQUAL_MEMORY(live,live+256*f,512); TEST_ASSERT_EQUAL_INT16(-32768,live[10]); TEST_ASSERT_EQUAL_INT16(32767,live[12]); }
void test_flatten_undo_restores_loaded_table() { load(); doc.select(17); TEST_ASSERT_TRUE(doc.setMode(WtEditMode::Wave)); for(int f=0;f<64;++f) TEST_ASSERT_EQUAL_MEMORY(original+17*256,live+256*f,512); TEST_ASSERT_TRUE(doc.undo()); TEST_ASSERT_EQUAL_MEMORY(original,live,sizeof(live)); TEST_ASSERT_TRUE(doc.mode()==WtEditMode::Morph); TEST_ASSERT_EQUAL(17,doc.selected()); }
void test_morph_interpolates_and_undo_anchor_delete() { doc.newWave(); doc.setMode(WtEditMode::Morph); doc.select(63); doc.beginStroke(); doc.drawLine(0,6300,255,6300); doc.endStroke(); doc.select(0); doc.beginStroke(); doc.drawLine(0,0,255,0); doc.endStroke(); TEST_ASSERT_EQUAL_INT16(3100,doc.frame(31)[50]); doc.select(31); TEST_ASSERT_TRUE(doc.addAnchor()); TEST_ASSERT_TRUE(doc.anchor(31)); TEST_ASSERT_TRUE(doc.deleteAnchor()); TEST_ASSERT_FALSE(doc.anchor(31)); TEST_ASSERT_TRUE(doc.undo()); TEST_ASSERT_TRUE(doc.anchor(31)); }
void test_invalid_operations_preserve_undo() { load(); doc.select(4); doc.shape(WtShape::Sqr); TEST_ASSERT_FALSE(doc.copyAnchor(4,5)); TEST_ASSERT_FALSE(doc.select(64)); doc.select(0); TEST_ASSERT_FALSE(doc.deleteAnchor()); TEST_ASSERT_TRUE(doc.undo()); TEST_ASSERT_EQUAL_MEMORY(original,live,sizeof(live)); }
void test_copy_empty_target_and_smooth_undo() { doc.newWave(); doc.setMode(WtEditMode::Morph); TEST_ASSERT_TRUE(doc.copyAnchor(0,20)); TEST_ASSERT_TRUE(doc.anchor(20)); doc.select(20); doc.shape(WtShape::Sqr); memcpy(original,live,sizeof(live)); TEST_ASSERT_TRUE(doc.smooth()); TEST_ASSERT_NOT_EQUAL(original[20*256],live[20*256]); TEST_ASSERT_TRUE(doc.undo()); TEST_ASSERT_EQUAL_MEMORY(original,live,sizeof(live)); }
void test_empty_stroke_preserves_previous_undo() { load(); doc.shape(WtShape::Sqr); TEST_ASSERT_TRUE(doc.beginStroke()); doc.endStroke(); TEST_ASSERT_TRUE(doc.undo()); TEST_ASSERT_EQUAL_MEMORY(original,live,sizeof(live)); }
void test_identical_stroke_preserves_previous_undo() { load(); doc.select(31); doc.shape(WtShape::Sqr); TEST_ASSERT_TRUE(doc.beginStroke()); doc.drawLine(1,30000,5,30000); doc.endStroke(); TEST_ASSERT_TRUE(doc.undo()); TEST_ASSERT_EQUAL_MEMORY(original,live,sizeof(live)); }
void test_extreme_interpolation_and_reverse_stroke() { doc.newWave(); doc.setMode(WtEditMode::Morph); doc.select(0); doc.beginStroke(); doc.drawLine(255,-32768,0,-32768); doc.endStroke(); doc.select(63); doc.beginStroke(); doc.drawLine(255,32767,0,32767); doc.endStroke(); TEST_ASSERT_EQUAL_INT16(-32768,doc.frame(0)[0]); TEST_ASSERT_EQUAL_INT16(32767,doc.frame(63)[255]); TEST_ASSERT_EQUAL_INT16(-521,doc.frame(31)[128]); TEST_ASSERT_EQUAL_INT16(519,doc.frame(32)[128]); }
void test_detached_safe() { doc.detach(); TEST_ASSERT_FALSE(doc.beginStroke()); TEST_ASSERT_FALSE(doc.shape(WtShape::Sin)); TEST_ASSERT_FALSE(doc.undo()); TEST_ASSERT_NULL(doc.frame()); }
int main() { UNITY_BEGIN(); RUN_TEST(test_loaded_preserves_all_samples); RUN_TEST(test_stroke_local_and_single_undo); RUN_TEST(test_wave_stroke_all_frames); RUN_TEST(test_flatten_undo_restores_loaded_table); RUN_TEST(test_morph_interpolates_and_undo_anchor_delete); RUN_TEST(test_invalid_operations_preserve_undo); RUN_TEST(test_copy_empty_target_and_smooth_undo); RUN_TEST(test_empty_stroke_preserves_previous_undo); RUN_TEST(test_identical_stroke_preserves_previous_undo); RUN_TEST(test_extreme_interpolation_and_reverse_stroke); RUN_TEST(test_detached_safe); return UNITY_END(); }
