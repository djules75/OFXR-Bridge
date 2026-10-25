#define main original_private_fixture_main
#include "private_openxr_runtime_tests.cpp"
#undef main
#include "private_release_readiness_test.inc"
int main(){release_readiness_test(false);release_readiness_test(true);return 0;}
