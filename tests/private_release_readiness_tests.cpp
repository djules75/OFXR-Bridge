#define main original_private_fixture_main
#include "private_openxr_runtime_tests.cpp"
#undef main
#include "private_release_readiness_test.inc"
#include "private_two_credit_tests.inc"
int main(){release_readiness_test(false);release_readiness_test(true);
    bounded_delivery_credit_test(3,true);bounded_delivery_credit_test(2,true);
    bounded_delivery_credit_test(3,false);bounded_delivery_credit_test(2,false);
    two_credit_stop_test();return 0;}
