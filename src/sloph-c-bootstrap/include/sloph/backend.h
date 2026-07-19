#ifndef SLOPH_BACKEND_H
#define SLOPH_BACKEND_H

#include "sloph/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Lower one validated Heartwood (Core) unit to a deterministic, standalone
 * C11 translation unit (Timber). The selected symbol is either printable data
 * or a process entry with the fixed ABI `sloph::Unit -> os::process::Exit`;
 * that ABI uses Unit::Unit and Exit::{Success,Failure}. Foreign calls use the
 * reviewed borrowed_bytes_write, page_map, and page_unmap adapter ABIs and
 * their documented Result/error constructors. These names are toolchain ABI,
 * not target- or project-specific conventions. The NUL-terminated result is
 * owned by the caller and must be released with free(3); out_length excludes
 * the NUL. */
SlophStatus sloph_heartwood_to_timber(SlophContext *context,
                                      SlophCoreUnit *unit,
                                      const char *symbol,
                                      char **out_text,
                                      size_t *out_length);

/* Compatibility spelling for embedders that name the target language. */
SlophStatus sloph_backend_emit_c11(SlophContext *context,
                                   SlophCoreUnit *unit,
                                   const char *symbol,
                                   char **out_text,
                                   size_t *out_length);

/* Read-only requirements used by native drivers when a Heartwood unit is
 * compiled without an already-loaded Source project. Pointers remain owned by
 * unit and are valid until sloph_core_free(unit). */
typedef struct SlophForeignRequirementView {
    const char *identity;
    const char *provider;
    const char *symbol;
    const char *header;
} SlophForeignRequirementView;

size_t sloph_heartwood_foreign_requirement_count(const SlophCoreUnit *unit);
SlophStatus sloph_heartwood_foreign_requirement(
    const SlophCoreUnit *unit, size_t index,
    SlophForeignRequirementView *out_requirement);

#ifdef __cplusplus
}
#endif

#endif
