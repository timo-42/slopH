#include "syntax_internal.h"

#include <string.h>

bool sloph_syntax_standard_transform(const char *name,
                                     bool *out_swap_branches) {
    if (name == NULL || out_swap_branches == NULL) return false;
    if (strcmp(name, "if") == 0) {
        *out_swap_branches = false;
        return true;
    }
    if (strcmp(name, "unless") == 0) {
        *out_swap_branches = true;
        return true;
    }
    return false;
}
