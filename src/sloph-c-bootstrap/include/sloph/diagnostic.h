#ifndef SLOPH_DIAGNOSTIC_H
#define SLOPH_DIAGNOSTIC_H

#include "sloph/base.h"

typedef enum SlophSeverity {
    SLOPH_SEVERITY_ERROR = 0,
    SLOPH_SEVERITY_WARNING,
    SLOPH_SEVERITY_NOTE
} SlophSeverity;

typedef struct SlophDiagnosticView {
    const char *code;
    const char *phase;
    const char *message;
    const char *details_json;
    SlophSpan span;
    SlophSeverity severity;
} SlophDiagnosticView;

const char *sloph_severity_name(SlophSeverity severity);

#endif
