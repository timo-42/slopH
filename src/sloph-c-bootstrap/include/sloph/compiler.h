#ifndef SLOPH_COMPILER_H
#define SLOPH_COMPILER_H

#include "sloph/core.h"
#include "sloph/project.h"

#ifdef __cplusplus
extern "C" {
#endif

SlophStatus sloph_project_elaborate(SlophContext *context,
                                    const SlophProject *project,
                                    SlophCoreUnit **out_unit);

#ifdef __cplusplus
}
#endif
#endif
