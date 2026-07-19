#include "project_internal.h"

#include <string.h>

const char *sloph_operating_system_name(SlophOperatingSystem value) {
    switch (value) {
        case SLOPH_OS_LINUX: return "linux";
        case SLOPH_OS_DARWIN: return "darwin";
    }
    return NULL;
}

const char *sloph_architecture_name(SlophArchitecture value) {
    switch (value) {
        case SLOPH_ARCH_AMD64: return "amd64";
        case SLOPH_ARCH_ARM64: return "arm64";
    }
    return NULL;
}

SlophStatus sloph_project_validate_target(SlophContext *context,
                                          SlophCompilerTarget target) {
    if (sloph_operating_system_name(target.operating_system) == NULL ||
        sloph_architecture_name(target.architecture) == NULL) {
        return sloph_project_diag(context, SLOPH_STATUS_INVALID_ARGUMENT,
                                  "compiler.target.invalid", "environment",
                                  "unknown compiler target value", "{}");
    }
    return SLOPH_STATUS_OK;
}

SlophStatus sloph_compiler_target_host(const SlophHost *requested,
                                       SlophCompilerTarget *out_target) {
    SlophHost fallback;
    const SlophHost *host = requested;
    SlophTargetInfo info;
    SlophStatus status;
    if (out_target == NULL) return SLOPH_STATUS_INVALID_ARGUMENT;
    if (host == NULL) {
        fallback = sloph_posix_host();
        host = &fallback;
    }
    if (host->target_info == NULL) return SLOPH_STATUS_INVALID_ARGUMENT;
    status = host->target_info(host->user_data, &info);
    if (status != SLOPH_STATUS_OK) return status;
    if (strcmp(info.operating_system, "linux") == 0) {
        out_target->operating_system = SLOPH_OS_LINUX;
    } else if (strcmp(info.operating_system, "darwin") == 0) {
        out_target->operating_system = SLOPH_OS_DARWIN;
    } else {
        return SLOPH_STATUS_INVALID_ARGUMENT;
    }
    if (strcmp(info.architecture, "amd64") == 0) {
        out_target->architecture = SLOPH_ARCH_AMD64;
    } else if (strcmp(info.architecture, "arm64") == 0) {
        out_target->architecture = SLOPH_ARCH_ARM64;
    } else {
        return SLOPH_STATUS_INVALID_ARGUMENT;
    }
    return SLOPH_STATUS_OK;
}
