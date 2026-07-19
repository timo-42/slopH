#include "project_internal.h"
#include "sloph/syntax.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_module(const void *left, const void *right) {
    return strcmp(((const SlophProjectModule *)left)->name,
                  ((const SlophProjectModule *)right)->name);
}

static SlophProjectModule *find_internal(SlophProject *project,
                                         const char *name) {
    size_t low = 0u, high = project->module_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        int order = strcmp(project->modules[middle].name, name);
        if (order == 0) return &project->modules[middle];
        if (order < 0) low = middle + 1u; else high = middle;
    }
    return NULL;
}

static void mark_reachable(SlophProject *project, SlophProjectModule *module) {
    size_t index;
    if (module == NULL || module->reachable) return;
    module->reachable = 1;
    for (index = 0u; index < module->import_count; ++index)
        mark_reachable(project, find_internal(project, module->imports[index]));
}

static int imports(const SlophProjectModule *module, const char *name) {
    size_t index;
    for (index = 0u; index < module->import_count; ++index)
        if (strcmp(module->imports[index], name) == 0) return 1;
    return 0;
}

typedef struct CycleState {
    SlophProject *project;
    SlophProjectModule **stack;
    size_t stack_count;
} CycleState;

static SlophStatus cycle_visit(CycleState *state, SlophProjectModule *module) {
    size_t index;
    module->visit_state = 1;
    state->stack[state->stack_count++] = module;
    /* Scan targets in module-name order, matching the Python oracle. */
    for (index = 0u; index < state->project->module_count; ++index) {
        SlophProjectModule *target = &state->project->modules[index];
        if (target->visit_state == 3 || !target->reachable ||
            !imports(module, target->name)) continue;
        if (target->visit_state == 0) {
            SlophStatus status = cycle_visit(state, target);
            if (status != SLOPH_STATUS_OK) return status;
        } else if (target->visit_state == 1) {
            char details[4096] = "{\"modules\":[";
            size_t start = 0u, used = strlen(details), cursor;
            while (state->stack[start] != target) ++start;
            /* Cycle members are emitted lexicographically, not traversal order. */
            for (cursor = 0u; cursor < state->project->module_count; ++cursor) {
                SlophProjectModule *candidate = &state->project->modules[cursor];
                size_t member;
                int included = 0;
                for (member = start; member < state->stack_count; ++member)
                    if (state->stack[member] == candidate) { included = 1; break; }
                if (included) {
                    int count = snprintf(details + used, sizeof(details) - used,
                        "%s\"%s\"", used == strlen("{\"modules\":[") ? "" : ",",
                        candidate->name);
                    if (count > 0) used += (size_t)count;
                }
            }
            (void)snprintf(details + used, sizeof(details) - used, "]}");
            return sloph_project_diag(state->project->context,
                SLOPH_STATUS_INVALID_ARGUMENT, "project.import.cycle", "resolve",
                "module imports must be acyclic", details);
        }
    }
    --state->stack_count;
    module->visit_state = 2;
    return SLOPH_STATUS_OK;
}

static SlophStatus report_cycle(SlophProject *project) {
    const SlophAllocator *allocator = sloph_context_allocator(project->context);
    SlophProjectModule **stack = allocator->allocate(allocator->user_data,
        project->module_count * sizeof(*stack));
    CycleState state;
    SlophStatus status = SLOPH_STATUS_INTERNAL_ERROR;
    size_t index;
    if (stack == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    state.project = project; state.stack = stack; state.stack_count = 0u;
    for (index = 0u; index < project->module_count; ++index)
        if (project->modules[index].visit_state != 3)
            project->modules[index].visit_state = 0;
    for (index = 0u; index < project->module_count; ++index) {
        if (project->modules[index].reachable &&
            project->modules[index].visit_state == 0) {
            status = cycle_visit(&state, &project->modules[index]);
            if (status != SLOPH_STATUS_OK) break;
        }
    }
    allocator->deallocate(allocator->user_data, stack,
                          project->module_count * sizeof(*stack));
    return status;
}

SlophStatus sloph_project_order_modules(SlophProject *project) {
    const SlophAllocator *allocator = sloph_context_allocator(project->context);
    SlophProjectModule *ordered;
    size_t reachable_count = 0u, output_count = 0u, index;
    SlophStatus status = SLOPH_STATUS_OK;
    qsort(project->modules, project->module_count, sizeof(*project->modules),
          compare_module);
    for (index = 0u; index < project->module_count; ++index) {
        SlophProjectModule *module = &project->modules[index];
        if (!module->bundled || strcmp(module->name, "prelude") == 0 ||
            strcmp(module->name, "core") == 0 ||
            strncmp(module->name, "core::", 6u) == 0)
            mark_reachable(project, module);
    }
    for (index = 0u; index < project->module_count; ++index) {
        SlophProjectModule *module = &project->modules[index];
        size_t imported;
        if (!module->reachable) continue;
        status = sloph_project_require_available(project, module);
        if (status != SLOPH_STATUS_OK) return status;
        ++reachable_count;
        module->visit_state = (int)module->import_count; /* Kahn indegree. */
        for (imported = 0u; imported < module->import_count; ++imported) {
            if (find_internal(project, module->imports[imported]) == NULL) {
                char details[512];
                char message[768];
                (void)snprintf(details, sizeof(details),
                    "{\"imported\":\"%s\",\"module\":\"%s\"}",
                    module->imports[imported], module->name);
                (void)snprintf(message, sizeof(message),
                    "module '%s' imports missing module '%s'", module->name,
                    module->imports[imported]);
                return sloph_project_diag(project->context,
                    SLOPH_STATUS_INVALID_ARGUMENT, "project.import.missing",
                    "resolve", message, details);
            }
        }
    }
    ordered = allocator->allocate(allocator->user_data,
                                   project->module_capacity * sizeof(*ordered));
    if (ordered == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    while (output_count < reachable_count) {
        SlophProjectModule *ready = NULL;
        for (index = 0u; index < project->module_count; ++index) {
            SlophProjectModule *candidate = &project->modules[index];
            if (candidate->reachable && candidate->visit_state == 0) {
                ready = candidate; break;
            }
        }
        if (ready == NULL) { status = report_cycle(project); break; }
        ordered[output_count++] = *ready;
        ready->visit_state = 3;
        for (index = 0u; index < project->module_count; ++index) {
            SlophProjectModule *dependent = &project->modules[index];
            if (dependent->reachable && dependent->visit_state != 3 &&
                imports(dependent, ready->name)) --dependent->visit_state;
        }
    }
    if (status != SLOPH_STATUS_OK) {
        allocator->deallocate(allocator->user_data, ordered,
                              project->module_capacity * sizeof(*ordered));
        return status;
    }
    for (index = 0u; index < project->module_count; ++index)
        if (!project->modules[index].reachable)
            sloph_syntax_module_free(project->modules[index].syntax);
    allocator->deallocate(allocator->user_data, project->modules,
                          project->module_capacity * sizeof(*project->modules));
    project->modules = ordered; project->module_count = output_count;
    return SLOPH_STATUS_OK;
}
