#include <stdio.h>
#include "sfmm.h"

int main(int argc, char const *argv[]) {
    sf_errno = 0;
    size_t sz_w = 8, sz_x = 200, sz_y = 300, sz_z = 4;
    /* void *w = */ sf_malloc(sz_w);
    void *x = sf_malloc(sz_x);
    void *y = sf_malloc(sz_y);
    /* void *z = */ sf_malloc(sz_z);

    sf_free(y);
    sf_free(x);
    return EXIT_SUCCESS;
}