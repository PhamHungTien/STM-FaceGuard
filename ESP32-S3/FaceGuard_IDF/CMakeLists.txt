cmake_minimum_required(VERSION 3.16)

# ── IDF v6.0 + picolibc compatibility ─────────────────────────────────────────
# picolibc (default libc in IDF v6.0) is stricter than newlib:
#   • printf / fprintf are NOT in the global namespace unless <stdio.h> is included
#   • std::cos / std::sin / std::atan require <cmath> (not pulled in by <math.h>)
#   • CONFIG_TIE728_BOOST was removed from Kconfig; ESP-DL 3.x still references it
#
# Calling add_compile_options() BEFORE include(project.cmake) injects these flags
# into every compilation unit in the project, including all managed components.
# This survives component re-downloads and requires no patching of managed files.

add_compile_options(
    # C++ files: inject <stdio.h> for ::printf and <cmath> for std::sin/cos/atan
    $<$<COMPILE_LANGUAGE:CXX>:-include stdio.h>
    $<$<COMPILE_LANGUAGE:CXX>:-include cmath>
    # C files: inject <stdio.h> for ::printf
    $<$<COMPILE_LANGUAGE:C>:-include stdio.h>
)
# Force CONFIG_TIE728_BOOST=1 for all units — fixes ESP-DL dl_tool_cache.cpp
add_compile_definitions(CONFIG_TIE728_BOOST=1)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(FaceGuard_IDF)
