#include "sloph/compiler.h"
#include "../src/project_internal.h"
#include "../src/syntax_internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    SlophContext *context = NULL;
    SlophProject project;
    SlophProjectModule project_module;
    SlophSyntaxModule module;
    SlophSyntaxType int_type;
    SlophSyntaxExpr literal;
    SlophSyntaxBlock block;
    SlophSyntaxValue value;
    SlophCoreUnit *unit = NULL;
    char *printed = NULL;
    size_t printed_length = 0u;
    memset(&project, 0, sizeof(project));
    memset(&project_module, 0, sizeof(project_module));
    memset(&module, 0, sizeof(module));
    memset(&int_type, 0, sizeof(int_type));
    memset(&literal, 0, sizeof(literal));
    memset(&block, 0, sizeof(block));
    memset(&value, 0, sizeof(value));
    assert(sloph_context_create(NULL, &context) == SLOPH_STATUS_OK);
    int_type.kind = SLOPH_SYNTAX_TYPE_INT;
    literal.kind = SLOPH_SYNTAX_EXPR_INT;
    literal.as.integer = "42";
    block.result = &literal;
    value.name = "main";
    value.type = &int_type;
    value.value = &block;
    module.values = &value;
    module.value_count = 1u;
    project_module.name = "demo::main";
    project_module.syntax = &module;
    project.modules = &project_module;
    project.module_count = 1u;
    project.manifest.entry = "demo::main::main";
    assert(sloph_project_elaborate(context, &project, &unit) == SLOPH_STATUS_OK);
    assert(sloph_core_print(context, unit, &printed, &printed_length) == SLOPH_STATUS_OK);
    assert(strstr(printed, "demo::main::main") != NULL);
    assert(strstr(printed, "(int 42)") != NULL);
    free(printed);
    sloph_core_free(unit);
    sloph_context_destroy(context);
    return 0;
}
