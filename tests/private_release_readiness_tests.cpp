#define main original_private_fixture_main
#include "private_openxr_runtime_tests.cpp"
#undef main
#include "private_release_readiness_test.inc"
#include "private_two_credit_tests.inc"
#include "private_buffer_control_tests.inc"
int main(int argc,char** argv){
    if(argc>1 && std::string_view(argv[1])=="buffer") {
        live_buffer_control_test(false,true);live_buffer_control_test(false,false);
        live_buffer_control_test(true,true);live_buffer_control_test(true,false);
        queued_buffer_change_test(false);queued_buffer_change_test(true);buffer_margin_reset_test();return 0;
    }
    release_readiness_test(false);release_readiness_test(true);
    bounded_delivery_credit_test(3,true);bounded_delivery_credit_test(2,true);
    bounded_delivery_credit_test(3,false);bounded_delivery_credit_test(2,false);
    two_credit_stop_test();return 0;}
