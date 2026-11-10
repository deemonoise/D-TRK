#include <cstring>
#include <unity.h>
#include "fx_info.h"
#include "automation.h"
using namespace mt;
void setUp(){}
void tearDown(){}
static Fx matrixOne(){for(int i=1;i<int(Fx::Count);++i)if(strcmp(fxName(Fx(i)),"M01")==0)return Fx(i);return Fx::None;}
void test_matrix_depth_is_signed_and_available(){auto f=matrixOne();TEST_ASSERT_NOT_EQUAL(int(Fx::None),int(f));TEST_ASSERT_TRUE(fxSynthOnly(f));TEST_ASSERT_EQUAL_INT(-64,fxSigned(fxStep(f,0,-100)));TEST_ASSERT_EQUAL_INT(63,fxSigned(fxStep(f,0,100)));}
void test_matrix_automation_interpolates_through_zero(){Step s[3];const auto f=matrixOne();TEST_ASSERT_TRUE(automationRamp(s,3,f,0,2,uint8_t(-32),32));TEST_ASSERT_EQUAL_INT(0,automationGet(s[1],f));TEST_ASSERT_EQUAL_INT(-32,fxSigned(uint8_t(automationGet(s[0],f))));}
void test_reserved_accent_never_offered(){TEST_ASSERT_FALSE(fxAllowed(Fx::AC2));Fx f=Fx::None;for(int i=0;i<int(Fx::Count)*2;++i){f=fxNextCmd(f,1);TEST_ASSERT_NOT_EQUAL(int(Fx::AC2),int(f));}TEST_ASSERT_EQUAL(int(Fx::SAT)+1,int(Fx::AC2));TEST_ASSERT_EQUAL(int(Fx::AC2)+1,int(Fx::M01));}
int main(){UNITY_BEGIN();RUN_TEST(test_reserved_accent_never_offered);RUN_TEST(test_matrix_depth_is_signed_and_available);RUN_TEST(test_matrix_automation_interpolates_through_zero);return UNITY_END();}
