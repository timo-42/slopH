#include "sloph/context.h"
#include "sloph/project.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct Fixture {
    char root[512];
    char project[512];
    char libraries[512];
    char provider[512];
    char manifest[512];
    char bindings[512];
} Fixture;

static void join(char *output, size_t size, const char *left,
                 const char *right) {
    int count = snprintf(output, size, "%s/%s", left, right);
    assert(count > 0 && (size_t)count < size);
}

static void directory(const char *path) {
    assert(mkdir(path, 0700) == 0);
}

static void write_text(const char *path, const char *text) {
    FILE *stream = fopen(path, "wb");
    assert(stream != NULL);
    assert(fwrite(text, 1u, strlen(text), stream) == strlen(text));
    assert(fclose(stream) == 0);
}

static void add_directory(char *output, size_t size, const char *parent,
                          const char *name) {
    join(output, size, parent, name);
    directory(output);
}

static void fixture_create(Fixture *fixture) {
    char template[] = "/tmp/sloph-provider-XXXXXX";
    char *root;
    char path[512], nested[512], source[512];
    int descriptor = mkstemp(template);
    assert(descriptor >= 0);
    assert(close(descriptor) == 0);
    assert(unlink(template) == 0);
    directory(template);
    root = template;
    assert(strlen(root) < sizeof(fixture->root));
    strcpy(fixture->root, root);

    add_directory(fixture->project, sizeof(fixture->project), root, "project");
    add_directory(path, sizeof(path), fixture->project, "src");
    join(source, sizeof(source), path, "main.sloph");
    write_text(source,
        "module demo::main; import native::linux::amd64::{thing}; "
        "const main: Int { thing }");
    join(source, sizeof(source), fixture->project, "sloph.json");
    write_text(source,
        "{\"dependencies\":[\"native\"],\"entry\":\"demo::main::main\","
        "\"format\":1,\"package\":\"demo\",\"source-root\":\"src\"}");

    add_directory(fixture->libraries, sizeof(fixture->libraries), root,
                  "libraries");
    add_directory(path, sizeof(path), fixture->libraries, "prelude");
    join(source, sizeof(source), path, "library.json");
    write_text(source,
        "{\"dependencies\":[],\"format\":0,\"package\":\"prelude\"}");
    add_directory(nested, sizeof(nested), path, "src");
    join(source, sizeof(source), nested, "root.sloph");
    write_text(source, "module prelude; public const unit: Int { 0 }");

    add_directory(path, sizeof(path), fixture->libraries, "native");
    join(source, sizeof(source), path, "library.json");
    write_text(source,
        "{\"dependencies\":[],\"format\":0,\"package\":\"native\"}");
    add_directory(nested, sizeof(nested), path, "src");
    add_directory(path, sizeof(path), nested, "linux");
    join(source, sizeof(source), path, "amd64.sloph");
    write_text(source,
        "module native::linux::amd64; public const thing: Int { 1 }");
    add_directory(fixture->provider, sizeof(fixture->provider), path, "amd64");
    join(fixture->manifest, sizeof(fixture->manifest), fixture->provider,
         "provider.json");
    join(fixture->bindings, sizeof(fixture->bindings), fixture->provider,
         "bindings.json");
    write_text(fixture->bindings, "[]");
    join(source, sizeof(source), fixture->provider, "native.c");
    write_text(source, "int sloph_provider_test(void) { return 0; }\n");
}

static void fixture_remove(const Fixture *fixture) {
    char path[512], next[512];
    join(path, sizeof(path), fixture->project, "src/main.sloph"); unlink(path);
    join(path, sizeof(path), fixture->project, "sloph.json"); unlink(path);
    join(path, sizeof(path), fixture->project, "src"); rmdir(path);
    rmdir(fixture->project);
    join(path, sizeof(path), fixture->libraries, "prelude/src/root.sloph"); unlink(path);
    join(path, sizeof(path), fixture->libraries, "prelude/library.json"); unlink(path);
    join(path, sizeof(path), fixture->libraries, "prelude/src"); rmdir(path);
    join(path, sizeof(path), fixture->libraries, "prelude"); rmdir(path);
    join(path, sizeof(path), fixture->provider, "bindings.json"); unlink(path);
    join(path, sizeof(path), fixture->provider, "native.c"); unlink(path);
    unlink(fixture->manifest);
    rmdir(fixture->provider);
    join(path, sizeof(path), fixture->libraries, "native/src/linux/amd64.sloph");
    unlink(path);
    join(path, sizeof(path), fixture->libraries, "native/library.json"); unlink(path);
    join(path, sizeof(path), fixture->libraries, "native/src/linux"); rmdir(path);
    join(path, sizeof(path), fixture->libraries, "native/src"); rmdir(path);
    join(next, sizeof(next), fixture->libraries, "native"); rmdir(next);
    rmdir(fixture->libraries);
    rmdir(fixture->root);
}

static SlophStatus load(const Fixture *fixture, SlophContext **out_context,
                        SlophProject **out_project) {
    SlophProjectOptions options = sloph_project_options_default();
    assert(sloph_context_create(NULL, out_context) == SLOPH_STATUS_OK);
    options.libraries_root = fixture->libraries;
    options.source_version = 1u;
    options.target_is_set = 1;
    options.target.operating_system = SLOPH_OS_LINUX;
    options.target.architecture = SLOPH_ARCH_AMD64;
    return sloph_project_load(*out_context, fixture->project, &options,
                              out_project);
}

static const char *last_code(SlophContext *context) {
    SlophDiagnosticView diagnostic;
    size_t count = sloph_context_diagnostic_count(context);
    assert(count != 0u);
    assert(sloph_context_diagnostic(context, count - 1u, &diagnostic) ==
           SLOPH_STATUS_OK);
    return diagnostic.code;
}

static void expect_invalid(Fixture *fixture, const char *json) {
    SlophContext *context = NULL;
    SlophProject *project = NULL;
    write_text(fixture->manifest, json);
    assert(load(fixture, &context, &project) == SLOPH_STATUS_INVALID_ARGUMENT);
    assert(project == NULL);
    assert(strcmp(last_code(context), "project.provider.shape") == 0);
    sloph_context_destroy(context);
}

static void expect_invalid_bindings(Fixture *fixture, const char *json,
                                    const char *code) {
    SlophContext *context = NULL;
    SlophProject *project = NULL;
    write_text(fixture->manifest,
        "{\"bindings\":\"bindings.json\",\"format\":1,"
        "\"module\":\"native::linux::amd64\",\"sources\":[\"native.c\"]}");
    write_text(fixture->bindings, json);
    assert(load(fixture, &context, &project) == SLOPH_STATUS_INVALID_ARGUMENT);
    assert(project == NULL);
    assert(strcmp(last_code(context), code) == 0);
    sloph_context_destroy(context);
}

static void test_provider_metadata(void) {
    Fixture fixture;
    SlophContext *context = NULL;
    SlophProject *project = NULL;
    SlophProviderSourceView provider;
    fixture_create(&fixture);
    write_text(fixture.manifest,
        "{\"bindings\":\"bindings.json\",\"format\":1,"
        "\"module\":\"native::linux::amd64\",\"sources\":[\"native.c\"]}");
    assert(load(&fixture, &context, &project) == SLOPH_STATUS_OK);
    assert(sloph_project_provider_count(project) == 1u);
    assert(sloph_project_provider(project, 0u, &provider) == SLOPH_STATUS_OK);
    assert(strcmp(provider.module, "native::linux::amd64") == 0);
    assert(provider.source_count == 1u);
    assert(strstr(provider.sources[0], "/native.c") != NULL);
    sloph_project_free(project);
    sloph_context_destroy(context);

    expect_invalid(&fixture,
        "{\"bindings\":\"bindings.json\",\"format\":0,"
        "\"module\":\"native::linux::amd64\",\"libraries\":[]}");
    expect_invalid(&fixture,
        "{\"bindings\":\"bindings.json\",\"format\":\"1\","
        "\"module\":\"native::linux::amd64\",\"sources\":[\"native.c\"]}");
    expect_invalid(&fixture,
        "{\"bindings\":\"bindings.json\",\"format\":1,"
        "\"module\":\"native::linux::amd64\",\"sources\":[\"native.c\"],"
        "\"unknown\":true}");
    expect_invalid(&fixture,
        "{\"bindings\":\"../bindings.json\",\"format\":1,"
        "\"module\":\"native::linux::amd64\",\"sources\":[\"native.c\"]}");
    expect_invalid(&fixture,
        "{\"bindings\":\"bindings.json\",\"format\":1,"
        "\"module\":\"native::linux::amd64\","
        "\"sources\":[\"native.c\",\"native.c\"]}");
    expect_invalid(&fixture,
        "{\"bindings\":\"bindings.json\",\"format\":1,"
        "\"module\":\"native::linux::amd64\",\"sources\":[\"../native.c\"]}");

    expect_invalid(&fixture,
        "{\"bindings\":\"bad\\\\name.json\",\"format\":1,"
        "\"module\":\"native::linux::amd64\",\"sources\":[\"native.c\"]}");
    expect_invalid(&fixture,
        "{\"bindings\":\"bindings.json\",\"format\":1,"
        "\"module\":\"native::linux::amd64\",\"sources\":[\"bad\\\"name.c\"]}");
    fixture_remove(&fixture);
}

static void test_binding_metadata(void) {
    static const char valid[] =
        "{\"identity\":\"foreign.demo.write\",\"symbol\":\"demo_write\","
        "\"c_parameters\":[\"int\"],\"c_result\":\"int\","
        "\"provider\":\"native::linux::amd64\",\"requires\":[],"
        "\"effects\":[\"io\"],\"facts\":{\"result\":\"must_use\"},"
        "\"provenance\":\"test\",\"header\":\"bindings.json\","
        "\"adapter\":{\"kind\":\"demo_adapter\",\"arguments\":[\"c_size\"],"
        "\"result\":\"pointer_errno\",\"sloph_parameters\":[\"Int\"],"
        "\"sloph_result\":\"Result[Int, demo::Error]\"}}";
    Fixture fixture;
    SlophContext *context = NULL;
    SlophProject *project = NULL;
    char json[4096];
    fixture_create(&fixture);
    write_text(fixture.manifest,
        "{\"bindings\":\"bindings.json\",\"format\":1,"
        "\"module\":\"native::linux::amd64\",\"sources\":[\"native.c\"]}");
    (void)snprintf(json, sizeof(json), "[%s]", valid);
    write_text(fixture.bindings, json);
    assert(load(&fixture, &context, &project) == SLOPH_STATUS_OK);
    sloph_project_free(project); sloph_context_destroy(context);

    expect_invalid_bindings(&fixture,
        "[{\"identity\":\"x\"}]", "project.foreign_binding.shape");
    (void)snprintf(json, sizeof(json), "[%.*s,\"unknown\":true}]",
                   (int)strlen(valid) - 1, valid);
    expect_invalid_bindings(&fixture, json, "project.foreign_binding.shape");
    (void)snprintf(json, sizeof(json), "[%s,%s]", valid, valid);
    expect_invalid_bindings(&fixture, json, "project.foreign_binding.shape");
    expect_invalid_bindings(&fixture,
        "[{\"identity\":\"x\",\"symbol\":\"s\",\"c_parameters\":{},"
        "\"c_result\":\"int\",\"provider\":\"native::linux::amd64\","
        "\"requires\":[],\"effects\":[],\"facts\":{},"
        "\"provenance\":\"p\",\"header\":\"bindings.json\"}]",
        "project.foreign_binding.shape");
    expect_invalid_bindings(&fixture,
        "[{\"identity\":\"x\",\"symbol\":\"s\",\"c_parameters\":[],"
        "\"c_result\":\"int\",\"provider\":\"native::linux::amd64\","
        "\"requires\":[],\"effects\":[],\"facts\":{\"x\":1},"
        "\"provenance\":\"p\",\"header\":\"bindings.json\"}]",
        "project.foreign_binding.shape");
    expect_invalid_bindings(&fixture,
        "[{\"identity\":\"x\",\"symbol\":\"s\",\"c_parameters\":[],"
        "\"c_result\":\"int\",\"provider\":\"native::linux::amd64\","
        "\"requires\":[],\"effects\":[],\"facts\":{},"
        "\"provenance\":\"p\",\"header\":\"bindings.json\","
        "\"adapter\":{\"kind\":\"x\"}}]",
        "project.foreign_binding.adapter");
    expect_invalid_bindings(&fixture,
        "[{\"identity\":\"x\",\"symbol\":\"s\",\"c_parameters\":[],"
        "\"c_result\":\"int\",\"provider\":\"native::linux::amd64\","
        "\"requires\":[],\"effects\":[],\"facts\":{},"
        "\"provenance\":\"p\",\"header\":\"bindings.json\","
        "\"adapter\":{\"kind\":\"x\",\"arguments\":[],\"result\":\"x\","
        "\"sloph_parameters\":[\"Int[]\"],\"sloph_result\":\"Int\"}}]",
        "project.foreign_binding.adapter");
    expect_invalid_bindings(&fixture,
        "[{\"identity\":\"x\",\"symbol\":\"s\",\"c_parameters\":[],"
        "\"c_result\":\"int\",\"provider\":\"native::linux::amd64\","
        "\"requires\":[],\"effects\":[],\"facts\":{},"
        "\"provenance\":\"p\",\"header\":\"bad\\\\name.h\"}]",
        "project.provider.header");
    fixture_remove(&fixture);
}

int main(void) {
    test_provider_metadata();
    test_binding_metadata();
    return 0;
}
