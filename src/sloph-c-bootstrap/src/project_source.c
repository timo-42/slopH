#include "project_internal.h"
#include "sloph/syntax.h"
#include "syntax_internal.h"

#include <stdio.h>
#include <string.h>

static SlophStatus grow_modules(SlophProject *project) {
    const SlophAllocator *allocator = sloph_context_allocator(project->context);
    size_t capacity = project->module_capacity == 0u
        ? 16u : project->module_capacity * 2u;
    size_t old_size = project->module_capacity * sizeof(*project->modules);
    SlophProjectModule *grown = allocator->resize(allocator->user_data,
        project->modules, old_size, capacity * sizeof(*project->modules));
    if (grown == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    project->modules = grown; project->module_capacity = capacity;
    return SLOPH_STATUS_OK;
}

static int pattern_constant(const SlophSyntaxPattern *pattern,
                            const char *expected) {
    return pattern != NULL && pattern->kind == SLOPH_SYNTAX_PATTERN_CONSTANT &&
           strcmp(pattern->as.constant, expected) == 0;
}

static int pattern_matches(const SlophSyntaxPattern *pattern,
                           SlophCompilerTarget target, const char *selector) {
    if (strcmp(selector, "compiler::target::arch") == 0)
        return pattern_constant(pattern, target.architecture == SLOPH_ARCH_AMD64
            ? "arch::amd64" : "arch::arm64");
    if (strcmp(selector, "compiler::target::platform") == 0 && pattern != NULL &&
        pattern->kind == SLOPH_SYNTAX_PATTERN_TUPLE &&
        pattern->as.tuple.count == 2u) {
        return pattern_constant(pattern->as.tuple.items[0],
                target.operating_system == SLOPH_OS_LINUX
                    ? "os::linux" : "os::darwin") &&
               pattern_constant(pattern->as.tuple.items[1],
                target.architecture == SLOPH_ARCH_AMD64
                    ? "arch::amd64" : "arch::arm64");
    }
    return 0;
}

static SlophStatus select_imports(SlophProject *project,
                                  SlophSyntaxModule *syntax,
                                  SlophProjectModule *module) {
    size_t index;
    if (syntax->import_count != 0u) {
        SlophStatus status = sloph_arena_allocate(&project->arena,
            syntax->import_count * sizeof(char *), _Alignof(char *),
            (void **)&module->imports);
        if (status != SLOPH_STATUS_OK) return status;
    }
    for (index = 0u; index < syntax->import_count; ++index) {
        SlophSyntaxImport *item = &syntax->imports[index];
        SlophSyntaxDirectImport direct;
        size_t match_count = 0u;
        size_t alternative;
        size_t previous;
        if (item->kind == SLOPH_SYNTAX_IMPORT_DIRECT) direct = item->as.direct;
        else {
            memset(&direct, 0, sizeof(direct));
            for (alternative = 0u;
                 alternative < item->as.conditional.alternative_count;
                 ++alternative) {
                SlophSyntaxConditionalAlternative *candidate =
                    &item->as.conditional.alternatives[alternative];
                if (pattern_matches(candidate->pattern, project->target,
                                    item->as.conditional.selector)) {
                    direct = candidate->import_;
                    ++match_count;
                }
            }
            if (match_count != 1u)
                return sloph_project_diag(project->context,
                    SLOPH_STATUS_INVALID_ARGUMENT, "project.target.no_match",
                    "resolve", "conditional import has no branch for compiler target",
                    "{}");
            item->kind = SLOPH_SYNTAX_IMPORT_DIRECT;
            item->as.direct = direct;
        }
        for (previous = 0u; previous < module->import_count; ++previous)
            if (strcmp(module->imports[previous], direct.module) == 0)
                return sloph_project_diag(project->context,
                    SLOPH_STATUS_INVALID_ARGUMENT, "project.import.duplicate",
                    "resolve", "module imports a module more than once", "{}");
        module->imports[module->import_count++] = direct.module;
    }
    return SLOPH_STATUS_OK;
}

SlophStatus sloph_project_require_available(SlophProject *project,
                                            SlophProjectModule *module) {
    SlophSyntaxModule *syntax = module->syntax;
    char details[512];
    if (syntax->availability == NULL) return SLOPH_STATUS_OK;
    if (pattern_matches(syntax->availability->pattern, project->target,
                        syntax->availability->selector)) return SLOPH_STATUS_OK;
    (void)snprintf(details, sizeof(details), "{\"module\":\"%s\"}",
                   syntax->name);
    {
        SlophStatus added = sloph_context_add_diagnostic_full(
            project->context, "project.module.unavailable", "resolve",
            "module is unavailable for compiler target", details,
            syntax->availability->span, SLOPH_SEVERITY_ERROR);
        return added == SLOPH_STATUS_OK ? SLOPH_STATUS_INVALID_ARGUMENT : added;
    }
}

SlophStatus sloph_project_add_source_file(SlophProject *project,
                                          const SlophProjectOptions *options,
                                          const char *source_path,
                                          const char *expected_name,
                                          int bundled) {
    SlophOwnedBytes bytes;
    SlophSyntaxModule *syntax = NULL;
    SlophProjectModule module;
    SlophStatus status;
    size_t index;
    char details[768];
    status = options->host->read_file(options->host->user_data, project->context,
        source_path, sloph_context_limits(project->context)->input_bytes + 1u,
        &bytes);
    if (status != SLOPH_STATUS_OK) return status;
    if (bytes.length > sloph_context_limits(project->context)->input_bytes) {
        options->host->release_bytes(options->host->user_data, project->context,
                                     &bytes);
        return sloph_project_diag(project->context, SLOPH_STATUS_LIMIT_EXCEEDED,
            "project.source.limit", "project",
            "source file exceeds the configured input limit", "{}");
    }
    if (!bundled && bytes.length >
        sloph_context_limits(project->context)->project_bytes -
        project->source_bytes) {
        options->host->release_bytes(options->host->user_data, project->context,
                                     &bytes);
        return sloph_project_diag(project->context, SLOPH_STATUS_LIMIT_EXCEEDED,
            "project.bytes.limit", "project",
            "project source-byte limit exceeded", "{}");
    }
    if (!bundled) project->source_bytes += bytes.length;
    status = sloph_syntax_parse(project->context, bytes.data, bytes.length,
                                options->source_version, &syntax);
    options->host->release_bytes(options->host->user_data, project->context,
                                 &bytes);
    if (status != SLOPH_STATUS_OK) return status;
    if (strcmp(syntax->name, expected_name) != 0) {
        (void)snprintf(details, sizeof(details),
            "{\"actual\":\"%s\",\"expected\":\"%s\",\"path\":\"%s\"}",
            syntax->name, expected_name, source_path);
        sloph_syntax_module_free(syntax);
        return sloph_project_diag(project->context, SLOPH_STATUS_INVALID_ARGUMENT,
            "project.module.path_mismatch", "resolve",
            "module declaration does not match its path identity", details);
    }
    for (index = 0u; index < project->module_count; ++index) {
        if (strcmp(project->modules[index].name, syntax->name) == 0) {
            sloph_syntax_module_free(syntax);
            return sloph_project_diag(project->context,
                SLOPH_STATUS_INVALID_ARGUMENT, "project.module.duplicate",
                "resolve", "duplicate module", "{}");
        }
    }
    memset(&module, 0, sizeof(module));
    status = sloph_project_arena_string(project, syntax->name, &module.name);
    if (status == SLOPH_STATUS_OK)
        status = sloph_project_arena_string(project, source_path, &module.path);
    module.syntax = syntax; module.bundled = bundled;
    if (status == SLOPH_STATUS_OK) status = select_imports(project, syntax, &module);
    if (status == SLOPH_STATUS_OK && project->module_count == project->module_capacity)
        status = grow_modules(project);
    if (status != SLOPH_STATUS_OK) { sloph_syntax_module_free(syntax); return status; }
    project->modules[project->module_count++] = module;
    return SLOPH_STATUS_OK;
}
