#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>

#include <CUnit/Basic.h>
#include <CUnit/CUnit.h>

#include <libivee/libivee.h>

/*
 * Basic smoke test: create a vm, deploy a custom flat binary and call it
 */
static void raw_binary_smoke_test(void)
{
    int res = 0;
    ivee_t* ivee = NULL;

    res = ivee_create(0, &ivee);
    CU_ASSERT_TRUE(res == 0);

    res = ivee_load_executable(ivee, "smoke_test_payload.bin", IVEE_EXEC_BIN);
    CU_ASSERT_TRUE(res == 0);

    ivee_arch_state_t state = {
        .rax = 0,
        .rcx = 0xDEADF00Dul,
        .rdx = 0xCAFEBABEul,
    };

    res = ivee_call(ivee, &state);
    CU_ASSERT_TRUE(res == 0);
    CU_ASSERT_EQUAL(state.rax, 0xDEADF00Dul + 0xCAFEBABEul);

    ivee_destroy(ivee);
}

int main(int argc, char** argv)
{
    if (CUE_SUCCESS != CU_initialize_registry()) {
        return CU_get_error();
    }

    CU_pSuite suite = CU_add_suite("create_vm", NULL, NULL);
    if (NULL == suite) {
        CU_cleanup_registry();
        return CU_get_error();
    }

    CU_add_test(suite, "raw_binary_smoke_test", raw_binary_smoke_test);

    /* run tests */
    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    int res = CU_get_error() || CU_get_number_of_tests_failed();

    CU_cleanup_registry();
    return res;
}
