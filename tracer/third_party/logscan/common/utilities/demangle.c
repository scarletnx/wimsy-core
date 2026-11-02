/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* snx_demangle (demangle.h): the Itanium C++ ABI's __cxa_demangle, which the ABI gives C linkage and every C++ runtime
   the build uses has (libstdc++, libc++abi), declared here as the ABI defines it. It returns a malloc'd string */
#include "common/utilities/demangle.h"
#include <stddef.h>

char* __cxa_demangle(const char* mangled, char* buffer, size_t* length, int* status);

char* snx_demangle(const char* name) {
    int status = -1;
    char* plain = __cxa_demangle(name, NULL, NULL, &status);
    return status == 0 ? plain : NULL;
}
