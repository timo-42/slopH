#include "sloph/context.h"
#include "sloph/project.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_text(const char *path, const char *text) {
    FILE *stream = fopen(path, "wb");
    assert(stream != NULL);
    assert(fwrite(text, 1u, strlen(text), stream) == strlen(text));
    assert(fclose(stream) == 0);
}

static void join(char *output, size_t size, const char *left,
                 const char *right) {
    int count = snprintf(output, size, "%s/%s", left, right);
    assert(count > 0 && (size_t)count < size);
}

static char *make_temp_dir(char *path) {
    int descriptor = mkstemp(path);
    assert(descriptor >= 0);
    assert(close(descriptor) == 0);
    assert(unlink(path) == 0);
    assert(mkdir(path, 0700) == 0);
    return path;
}

static const char *last_code(SlophContext *context) {
    SlophDiagnosticView diagnostic;
    size_t count = sloph_context_diagnostic_count(context);
    assert(count != 0u);
    assert(sloph_context_diagnostic(context, count - 1u, &diagnostic) ==
           SLOPH_STATUS_OK);
    return diagnostic.code;
}

static void test_manifest_and_order(void) {
    char template[] = "/tmp/sloph-project-XXXXXX";
    char *root = make_temp_dir(template);
    char src[512], main_path[512], math_path[512], manifest_path[512];
    SlophContext *context = NULL;
    SlophProject *project = NULL;
    SlophProjectOptions options = sloph_project_options_default();
    SlophProjectManifestView manifest;
    SlophProjectModuleView module;
    assert(root != NULL);
    join(src, sizeof(src), root, "src"); assert(mkdir(src, 0700) == 0);
    join(main_path, sizeof(main_path), src, "main.sloph");
    join(math_path, sizeof(math_path), src, "math.sloph");
    join(manifest_path, sizeof(manifest_path), root, "sloph.json");
    write_text(manifest_path,
        "{\"format\":1,\"package\":\"demo\",\"source-root\":\"src\","
        "\"entry\":\"demo::main::main\"}");
    write_text(main_path,
        "module demo::main; import demo::math::{answer}; "
        "const main: Int { answer }");
    write_text(math_path,
        "module demo::math; public const answer: Int { 42 }");
    assert(sloph_context_create(NULL, &context) == SLOPH_STATUS_OK);
    options.target_is_set = 1;
    options.target.operating_system = SLOPH_OS_LINUX;
    options.target.architecture = SLOPH_ARCH_AMD64;
    assert(sloph_project_load(context, root, &options, &project) ==
           SLOPH_STATUS_OK);
    assert(sloph_project_manifest(project, &manifest) == SLOPH_STATUS_OK);
    assert(strcmp(manifest.package, "demo") == 0);
    assert(manifest.dependency_count == 0u);
    assert(sloph_project_module_count(project) == 2u);
    assert(sloph_project_module(project, 0u, &module) == SLOPH_STATUS_OK);
    assert(strcmp(module.name, "demo::math") == 0);
    assert(sloph_project_module(project, 1u, &module) == SLOPH_STATUS_OK);
    assert(strcmp(module.name, "demo::main") == 0);
    sloph_project_free(project); sloph_context_destroy(context);
    assert(unlink(main_path) == 0); assert(unlink(math_path) == 0);
    assert(unlink(manifest_path) == 0); assert(rmdir(src) == 0);
    assert(rmdir(root) == 0);
}

static void test_duplicate_manifest_key(void) {
    char template[] = "/tmp/sloph-manifest-XXXXXX";
    char *root = make_temp_dir(template);
    char src[512], manifest_path[512];
    SlophContext *context = NULL;
    SlophProject *project = NULL;
    SlophProjectOptions options = sloph_project_options_default();
    assert(root != NULL);
    join(src, sizeof(src), root, "src"); assert(mkdir(src, 0700) == 0);
    join(manifest_path, sizeof(manifest_path), root, "sloph.json");
    write_text(manifest_path,
        "{\"format\":1,\"format\":1,\"package\":\"demo\","
        "\"source-root\":\"src\",\"entry\":\"demo::main::main\"}");
    assert(sloph_context_create(NULL, &context) == SLOPH_STATUS_OK);
    options.target_is_set = 1;
    options.target.operating_system = SLOPH_OS_LINUX;
    options.target.architecture = SLOPH_ARCH_AMD64;
    assert(sloph_project_load(context, root, &options, &project) ==
           SLOPH_STATUS_INVALID_ARGUMENT);
    assert(strcmp(last_code(context), "project.manifest.duplicate") == 0);
    sloph_context_destroy(context);
    assert(unlink(manifest_path) == 0); assert(rmdir(src) == 0);
    assert(rmdir(root) == 0);
}

static void test_portable_manifest_diagnostics(void) {
    static const struct {
        const char *path;
        const char *code;
    } cases[] = {
        {"../../tests/v1/check/manifest-duplicate-key/project",
         "project.manifest.duplicate"},
        {"../../tests/v1/check/manifest-malformed-json/project",
         "project.manifest.syntax"},
        {"../../tests/v1/check/manifest-missing-key/project",
         "project.manifest.missing"},
        {"../../tests/v1/check/manifest-source-root-escape/project",
         "project.manifest.source_root"},
        {"../../tests/v1/check/manifest-unknown-key/project",
         "project.manifest.unknown"},
        {"../../tests/v1/check/manifest-unsupported-format/project",
         "project.manifest.format"},
        {"../../tests/v1/check/manifest-wrong-shape/project",
         "project.manifest.shape"},
        {"../../tests/v1/check/manifest-wrong-type/project",
         "project.manifest.field_type"}
    };
    size_t index;
    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        SlophContext *context = NULL;
        SlophProject *project = NULL;
        SlophProjectOptions options = sloph_project_options_default();
        assert(sloph_context_create(NULL, &context) == SLOPH_STATUS_OK);
        options.target_is_set = 1;
        options.target.operating_system = SLOPH_OS_LINUX;
        options.target.architecture = SLOPH_ARCH_AMD64;
        assert(sloph_project_load(context, cases[index].path, &options,
                                  &project) != SLOPH_STATUS_OK);
        assert(strcmp(last_code(context), cases[index].code) == 0);
        sloph_context_destroy(context);
    }
}

static void create_library(const char *libraries, const char *name,
                           const char *dependencies) {
    char root[512], src[512], manifest[512], text[1024];
    join(root, sizeof(root), libraries, name); assert(mkdir(root, 0700) == 0);
    join(src, sizeof(src), root, "src"); assert(mkdir(src, 0700) == 0);
    join(manifest, sizeof(manifest), root, "library.json");
    assert(snprintf(text, sizeof(text),
        "{\"dependencies\":%s,\"format\":0,\"package\":\"%s\"}",
        dependencies, name) > 0);
    write_text(manifest, text);
}

static void remove_library(const char *libraries, const char *name) {
    char root[512], src[512], manifest[512];
    join(root, sizeof(root), libraries, name);
    join(src, sizeof(src), root, "src");
    join(manifest, sizeof(manifest), root, "library.json");
    assert(unlink(manifest) == 0); assert(rmdir(src) == 0);
    assert(rmdir(root) == 0);
}

static void create_minimal_project(const char *root, char *src, char *source,
                                   char *manifest) {
    join(src, 512u, root, "src"); assert(mkdir(src, 0700) == 0);
    join(source, 512u, src, "main.sloph");
    join(manifest, 512u, root, "sloph.json");
    write_text(source, "module demo::main; const main: Int { 0 }");
    write_text(manifest,
        "{\"format\":1,\"package\":\"demo\",\"source-root\":\"src\","
        "\"entry\":\"demo::main::main\"}");
}

static void test_unreachable_module_availability(void) {
    char template[] = "/tmp/sloph-availability-XXXXXX";
    char *root = make_temp_dir(template);
    char src[512], source[512], manifest_path[512];
    SlophContext *context = NULL;
    SlophProject *project = NULL;
    SlophProjectOptions options = sloph_project_options_default();
    create_minimal_project(root, src, source, manifest_path);
    write_text(manifest_path,
        "{\"format\":1,\"package\":\"demo\",\"source-root\":\"src\","
        "\"entry\":\"demo::main::main\",\"dependencies\":[\"cpu\"]}");
    assert(sloph_context_create(NULL, &context) == SLOPH_STATUS_OK);
    options.libraries_root = "../libraries";
    options.target_is_set = 1;
    options.target.operating_system = SLOPH_OS_DARWIN;
    options.target.architecture = SLOPH_ARCH_ARM64;
    assert(sloph_project_load(context, root, &options, &project) ==
           SLOPH_STATUS_OK);
    assert(sloph_project_find_module(project, "cpu::amd64",
           &(SlophProjectModuleView){0}) == SLOPH_STATUS_INVALID_ARGUMENT);
    sloph_project_free(project); project = NULL;
    write_text(source,
        "module demo::main; import cpu::amd64::{avx512}; "
        "const main: Int { 0 }");
    assert(sloph_project_load(context, root, &options, &project) ==
           SLOPH_STATUS_INVALID_ARGUMENT);
    assert(strcmp(last_code(context), "project.module.unavailable") == 0);
    sloph_context_destroy(context);
    assert(unlink(source) == 0); assert(unlink(manifest_path) == 0);
    assert(rmdir(src) == 0); assert(rmdir(root) == 0);
}

static void test_library_dependency_validation_and_growth(void) {
    char template[] = "/tmp/sloph-packages-XXXXXX";
    char *root = make_temp_dir(template);
    char libraries[512], project_root[512], src[512], source[512], manifest[512];
    SlophContextConfig config = sloph_context_config_default();
    SlophContext *context = NULL;
    SlophProject *project = NULL;
    SlophProjectOptions options = sloph_project_options_default();
    size_t index;
    join(libraries, sizeof(libraries), root, "libraries");
    assert(mkdir(libraries, 0700) == 0);
    join(project_root, sizeof(project_root), root, "project");
    assert(mkdir(project_root, 0700) == 0);
    create_minimal_project(project_root, src, source, manifest);
    create_library(libraries, "prelude", "[\"p0\"]");
    for (index = 0u; index < 20u; ++index) {
        char name[32], dependencies[64];
        assert(snprintf(name, sizeof(name), "p%zu", index) > 0);
        if (index + 1u < 20u)
            assert(snprintf(dependencies, sizeof(dependencies), "[\"p%zu\"]",
                            index + 1u) > 0);
        else strcpy(dependencies, "[]");
        create_library(libraries, name, dependencies);
    }
    config.limits.project_files = 1u;
    assert(sloph_context_create(&config, &context) == SLOPH_STATUS_OK);
    options.libraries_root = libraries; options.target_is_set = 1;
    options.target.operating_system = SLOPH_OS_LINUX;
    options.target.architecture = SLOPH_ARCH_AMD64;
    assert(sloph_project_load(context, project_root, &options, &project) ==
           SLOPH_STATUS_OK);
    sloph_project_free(project); sloph_context_destroy(context);
    remove_library(libraries, "prelude");
    create_library(libraries, "prelude", "[\"p0\",\"p0\"]");
    assert(sloph_context_create(NULL, &context) == SLOPH_STATUS_OK);
    assert(sloph_project_load(context, project_root, &options, &project) ==
           SLOPH_STATUS_INVALID_ARGUMENT);
    assert(strcmp(last_code(context), "project.dependency.manifest") == 0);
    sloph_context_destroy(context);
    remove_library(libraries, "prelude");
    for (index = 20u; index-- != 0u;) {
        char name[32]; assert(snprintf(name, sizeof(name), "p%zu", index) > 0);
        remove_library(libraries, name);
    }
    assert(unlink(source) == 0); assert(unlink(manifest) == 0);
    assert(rmdir(src) == 0); assert(rmdir(project_root) == 0);
    assert(rmdir(libraries) == 0); assert(rmdir(root) == 0);
}

static void test_integration_projects_load(void) {
    static const char *const projects[] = {
        "../../tests/v1/run/basic/project",
        "../../tests/v1/run/clause-guards/project",
        "../../tests/v1/run/function-main-output/project",
        "../../tests/v1/run/result-propagation/project",
        "../libraries/math/tests/int"
    };
    size_t index;
    for (index = 0u; index < sizeof(projects) / sizeof(projects[0]); ++index) {
        SlophContext *context = NULL;
        SlophProject *project = NULL;
        SlophProjectOptions options = sloph_project_options_default();
        SlophStatus status;
        assert(sloph_context_create(NULL, &context) == SLOPH_STATUS_OK);
        options.libraries_root = "../libraries";
        options.target_is_set = 1;
        options.target.operating_system = SLOPH_OS_DARWIN;
        options.target.architecture = SLOPH_ARCH_ARM64;
        status = sloph_project_load(context, projects[index], &options, &project);
        if (status != SLOPH_STATUS_OK) {
            SlophDiagnosticView diagnostic;
            size_t count = sloph_context_diagnostic_count(context);
            fprintf(stderr, "project load failed: %s: status %s\n",
                    projects[index], sloph_status_name(status));
            if (count != 0u && sloph_context_diagnostic(context, count - 1u,
                    &diagnostic) == SLOPH_STATUS_OK)
                fprintf(stderr, "diagnostic: %s\n", diagnostic.code);
        }
        assert(status == SLOPH_STATUS_OK);
        assert(project != NULL);
        sloph_project_free(project);
        sloph_context_destroy(context);
    }
}

int main(void) {
    test_manifest_and_order();
    test_duplicate_manifest_key();
    test_portable_manifest_diagnostics();
    test_unreachable_module_availability();
    test_library_dependency_validation_and_growth();
    test_integration_projects_load();
    return 0;
}
