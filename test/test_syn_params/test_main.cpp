#include <unity.h>
#include "synth_params.h"
using namespace mt;
void setUp() {} void tearDown() {}
void test_shape_legacy_and_clamp() {
 SynParams p;p.shape[0]=.2f;p.shape[1]=.9f;p.mix=.3f;
 synApplyEnv(p,0,.4f);TEST_ASSERT_FLOAT_WITHIN(1e-6f,.6f,p.shape[0]);
 TEST_ASSERT_EQUAL_FLOAT(1,p.shape[1]);TEST_ASSERT_EQUAL_FLOAT(.3f,p.mix);
}
void test_each_normalized_destination_and_bipolar_depth() {
 for(int dest=1;dest<=3;++dest){SynParams p;p.fold=.8f;p.ring=.8f;p.mix=.8f;p.shape[0]=.2f;
 synApplyEnv(p,dest,-.5f);
 TEST_ASSERT_FLOAT_WITHIN(1e-6f,dest==1?.3f:.8f,p.fold);
 TEST_ASSERT_FLOAT_WITHIN(1e-6f,dest==2?.3f:.8f,p.ring);
 TEST_ASSERT_FLOAT_WITHIN(1e-6f,dest==3?.3f:.8f,p.mix);
 TEST_ASSERT_EQUAL_FLOAT(.2f,p.shape[0]);
 synApplyEnv(p,dest,2.f);TEST_ASSERT_EQUAL_FLOAT(1.f,dest==1?p.fold:dest==2?p.ring:p.mix);}
}
void test_pitch_destination_changes_only_osc2_pitch() {
 SynParams p;p.hz[0]=220;p.hz[1]=330;p.shape[0]=.2f;
 synApplyEnv(p,4,.5f);TEST_ASSERT_EQUAL_FLOAT(.2f,p.shape[0]);
 TEST_ASSERT_EQUAL_FLOAT(220,p.hz[0]);TEST_ASSERT_EQUAL_FLOAT(330,p.hz[1]);
 TEST_ASSERT_EQUAL_FLOAT(12,synEnvPitch2(4,.5f));TEST_ASSERT_EQUAL_FLOAT(-24,synEnvPitch2(4,-1));
 for(int i=0;i<4;++i)TEST_ASSERT_EQUAL_FLOAT(0,synEnvPitch2(i,1));
}
int main(){UNITY_BEGIN();RUN_TEST(test_shape_legacy_and_clamp);RUN_TEST(test_each_normalized_destination_and_bipolar_depth);RUN_TEST(test_pitch_destination_changes_only_osc2_pitch);return UNITY_END();}
