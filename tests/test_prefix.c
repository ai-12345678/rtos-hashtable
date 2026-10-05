#include "dict.h"
#include "sds.h"
#include <assert.h>
#include <stdio.h>

extern int prefix_roundtrip(void);
extern int RTOS_SYMBOL(prefix_roundtrip)(void);
int main(void) {
    SDS_ALLOCATOR_T allocator = {0};
    SDS_T string = SDS_NEW("demo", NULL);
    (void)allocator;
    assert(string && SDS_LEN(string) == 4);
    assert(prefix_roundtrip() == 0);
    assert(RTOS_SYMBOL(prefix_roundtrip)() == 0);
    SDS_FREE(string, NULL);
    puts("Default and demo_ namespaces coexist; cross-TU symbols resolve.");
    return 0;
}
