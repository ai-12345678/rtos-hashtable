#include "fht.h"
int fht_other_translation_unit(void) {
    fht h;
    fht_config c = fht_config_default(fht_hash_string, fht_equal_string);
    if (fht_init(&h, &c) != FHT_OK) return 1;
    return fht_destroy(&h) == FHT_OK ? 0 : 1;
}
