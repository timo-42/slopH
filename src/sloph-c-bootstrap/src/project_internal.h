#ifndef SLOPH_PROJECT_INTERNAL_H
#define SLOPH_PROJECT_INTERNAL_H

#include "internal.h"
#include "sloph/project.h"

typedef struct SlophProjectManifest {
    char *path;
    char *package;
    char *source_root;
    char *entry;
    size_t dependency_count;
    char **dependencies;
} SlophProjectManifest;

typedef struct SlophProjectModule {
    char *name;
    char *path;
    SlophSyntaxModule *syntax;
    size_t import_count;
    char **imports;
    unsigned char *source;
    size_t source_length;
    int bundled;
    int visit_state;
    int reachable;
} SlophProjectModule;

typedef struct SlophProjectProvider {
    char *module;
    char *root;
    char *bindings_path;
    size_t source_count;
    char **sources;
} SlophProjectProvider;

struct SlophProject {
    SlophContext *context;
    SlophArena arena;
    SlophProjectManifest manifest;
    SlophCompilerTarget target;
    SlophProjectModule *modules;
    size_t module_count;
    size_t module_capacity;
    SlophProjectProvider *providers;
    size_t provider_count;
    size_t provider_capacity;
    size_t source_bytes;
};

SlophStatus sloph_project_load_manifest(SlophProject *project,
                                        const SlophHost *host,
                                        const char *supplied_path);
SlophStatus sloph_project_load_sources(SlophProject *project,
                                       const SlophProjectOptions *options);
SlophStatus sloph_project_add_source_file(SlophProject *project,
                                          const SlophProjectOptions *options,
                                          const char *source_path,
                                          const char *expected_name,
                                          int bundled);
SlophStatus sloph_project_require_available(SlophProject *project,
                                            SlophProjectModule *module);
SlophStatus sloph_project_load_packages(SlophProject *project,
                                        const SlophProjectOptions *options);
SlophStatus sloph_project_order_modules(SlophProject *project);
SlophStatus sloph_project_load_providers(SlophProject *project,
                                         const SlophProjectOptions *options);
SlophStatus sloph_project_validate_target(SlophContext *context,
                                          SlophCompilerTarget target);

SlophStatus sloph_project_diag(SlophContext *context, SlophStatus status,
                               const char *code, const char *phase,
                               const char *message, const char *details);
SlophStatus sloph_project_arena_string(SlophProject *project,
                                       const char *text, char **out);
SlophStatus sloph_project_join_path(SlophProject *project, const char *left,
                                    const char *right, char **out);
int sloph_project_regular_file(const char *path);
int sloph_project_directory(const char *path);
int sloph_project_lower_segment(const char *text);
int sloph_project_global(const char *text);

#endif
