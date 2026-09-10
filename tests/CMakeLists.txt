# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0

# gtest comes prebuilt from the sysroot rather than from source, so
# webos_use_gtest() (which expects ${WEBOS_INSTALL_SRCDIR}/gtest) is not used.
if(NOT WEBOS_CONFIG_BUILD_TESTS)
    return()
endif()

pkg_check_modules(GTEST REQUIRED gtest_main)

include_directories(${GTEST_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)

webos_add_test(pdm_test
    SOURCES ${TEST_SOURCES}
    LIBRARIES ${BIN_NAME}-core ${LIB_LIST} ${GTEST_LDFLAGS}
)
