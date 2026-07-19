#ifndef SLOPH_PROJECT_H
#define SLOPH_PROJECT_H

#include "sloph/host.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlophProject SlophProject;
typedef struct SlophSyntaxModule SlophSyntaxModule;

typedef enum SlophOperatingSystem {
    SLOPH_OS_LINUX = 0,
    SLOPH_OS_DARWIN = 1
} SlophOperatingSystem;

typedef enum SlophArchitecture {
    SLOPH_ARCH_AMD64 = 0,
    SLOPH_ARCH_ARM64 = 1
} SlophArchitecture;

typedef struct SlophCompilerTarget {
    SlophOperatingSystem operating_system;
    SlophArchitecture architecture;
} SlophCompilerTarget;

typedef struct SlophProjectOptions {
    const SlophHost *host;
    const char *libraries_root;
    unsigned int source_version;
    SlophCompilerTarget target;
    int target_is_set;
} SlophProjectOptions;

typedef struct SlophProjectManifestView {
    const char *path;
    const char *package;
    const char *source_root;
    const char *entry;
    size_t dependency_count;
    const char *const *dependencies;
} SlophProjectManifestView;

typedef struct SlophProjectModuleView {
    const char *name;
    const char *path;
    const SlophSyntaxModule *syntax;
    size_t import_count;
    const char *const *imports;
    int bundled;
} SlophProjectModuleView;

typedef struct SlophProviderSourceView {
    const char *module;
    const char *root;
    const char *bindings_path;
    size_t source_count;
    const char *const *sources;
} SlophProviderSourceView;

SlophProjectOptions sloph_project_options_default(void);
SlophStatus sloph_compiler_target_host(const SlophHost *host,
                                       SlophCompilerTarget *out_target);
const char *sloph_operating_system_name(SlophOperatingSystem value);
const char *sloph_architecture_name(SlophArchitecture value);

SlophStatus sloph_project_load(SlophContext *context, const char *path,
                               const SlophProjectOptions *options,
                               SlophProject **out_project);
void sloph_project_free(SlophProject *project);

SlophStatus sloph_project_manifest(const SlophProject *project,
                                   SlophProjectManifestView *out_manifest);
size_t sloph_project_module_count(const SlophProject *project);
SlophStatus sloph_project_module(const SlophProject *project, size_t index,
                                 SlophProjectModuleView *out_module);
SlophStatus sloph_project_find_module(const SlophProject *project,
                                      const char *name,
                                      SlophProjectModuleView *out_module);
size_t sloph_project_provider_count(const SlophProject *project);
SlophStatus sloph_project_provider(const SlophProject *project, size_t index,
                                   SlophProviderSourceView *out_provider);

#ifdef __cplusplus
}
#endif

#endif
