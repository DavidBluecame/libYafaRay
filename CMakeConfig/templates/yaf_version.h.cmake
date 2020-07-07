#pragma once
// Header file generated by CMake please don't change it

// This preprocessor macro is set by cmake during building in file yaf_version.h.cmake
// For example: cmake -DYAFARAY_CORE_VERSION="v1.2.3"
// the intention is to link the YafaRay Core version to the git information obtained, for example with:
// cmake /yafaray/src/Core -DYAFARAY_CORE_VERSION=`git --git-dir=/yafaray/src/Core/.git --work-tree=/yafaray/src/Core describe --dirty --always --tags --long`

// This file is now also used to store the runtime search path for the plugins, if needed

#ifndef Y_VERSION_H
#define Y_VERSION_H
#define YAFARAY_CORE_VERSION "@YAFARAY_CORE_VERSION@@DEBUG@"
#define YAF_RUNTIME_SEARCH_PLUGIN_DIR "@YAF_RUNTIME_SEARCH_PLUGIN_DIR@"
#endif
