#include "dict.h"
#include "sds.h"
#include <assert.h>
#include <stdio.h>

extern int prefix_roundtrip(void);
extern int RTOS_SYMBOL(prefix_roundtrip)(void);
int main(void) {
    RTOS_SYMBOL(sds_allocator) allocator = {0};
    RTOS_SYMBOL(sds) string = SDS_NEW("demo", NULL);
    (void)allocator;
    assert(string && SDS_LEN(string) == 4);
    assert(prefix_roundtrip() == 0);
    assert(RTOS_SYMBOL(prefix_roundtrip)() == 0);
    SDS_FREE(string, NULL);
    puts("Default and demo_ namespaces coexist; cross-TU symbols resolve.");
    return 0;
}
