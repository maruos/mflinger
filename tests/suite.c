#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#include "../src/mclient/util.h"

static void test_argb8888_get_alpha() {
    /* ARGB8888 is from MSB to LSB */
    uint32_t cases[][2] = {
        { 0xff0011bb, 0xff },
        { 0x12345678, 0x12 }
    };

    int i;
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        assert(cases[i][1] == argb8888_get_alpha(cases[i][0]));
    }
}

int main() {
    test_argb8888_get_alpha();

    printf("All tests passed.\n");
    return 0;
}
