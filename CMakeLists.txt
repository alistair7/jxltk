cmake_minimum_required(VERSION 3.16...3.27)

project(jxltk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FindPkgConfig)
# Look in /usr/local/lib/pkgconfig too
#set(ENV{PKG_CONFIG_PATH} "/usr/local/lib/pkgconfig")
pkg_check_modules(LibJXL REQUIRED IMPORTED_TARGET libjxl>=0.7.0)
pkg_check_modules(LibJXLThreads REQUIRED IMPORTED_TARGET libjxl_threads>=0.7.0)

# Decoder logic is built as a separate static library
add_subdirectory(contrib/jxlazy EXCLUDE_FROM_ALL)
# EXCLUDE_FROM_ALL prevents jxlazy's .a and .h files from being installed with jxltk
# - may cause issues on Windows (https://gitlab.kitware.com/cmake/cmake/-/issues/18048)?

add_executable(jxltk src/main.cpp src/cmdline.cpp src/color.cpp src/common.cpp src/pixmap.cpp src/merge.cpp src/mergeconfig.cpp src/enums.cpp src/except.cpp src/split.cpp src/util.cpp src/log.cpp
                                  src/cmdline.h   src/color.h   src/common.h   src/pixmap.h   src/merge.h   src/mergeconfig.h   src/enums.h   src/except.h   src/split.h   src/util.h   src/log.h
                     contrib/nlohmann/json.hpp contrib/optparse/optparse.h)

target_link_directories(jxltk PRIVATE BEFORE contrib/jxlazy)
#target_link_directories(jxltk PRIVATE BEFORE /usr/local/lib)
target_link_libraries(jxltk PRIVATE PkgConfig::LibJXL PkgConfig::LibJXLThreads jxlazy)
if (CMAKE_CXX_COMPILER_ID MATCHES GNU OR CMAKE_CXX_COMPILER_ID MATCHES Clang)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Weffc++ -Wno-missing-field-initializers")
endif()

# Enable link-time optimization
include(CheckIPOSupported CHECK_IPO_SUPPORTED)
if(CHECK_IPO_SUPPORTED)
  check_ipo_supported(RESULT result)
  if(result)
    set_target_properties(jxltk PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

# Set install locations
include(GNUInstallDirs OPTIONAL RESULT_VARIABLE GNU_INSTALL_DIRS_SUPPORTED)
if(GNU_INSTALL_DIRS_SUPPORTED)
  install(TARGETS jxltk RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
  # Controllable via CMAKE_INSTALL_PREFIX
  install(TARGETS jxltk RUNTIME DESTINATION bin)
endif()


if(BUILD_TESTING)
  pkg_check_modules(GoogleTest REQUIRED IMPORTED_TARGET gtest)
  pkg_check_modules(GoogleTestMain REQUIRED IMPORTED_TARGET gtest_main)
  enable_testing()
  message(STATUS "Tests enabled")

  add_executable(jxltk_test src/enums_test.cpp src/color_test.cpp
                            src/enums.cpp      src/color.cpp      src/util.cpp src/log.cpp)
  target_include_directories(jxltk_test PUBLIC include)
  target_include_directories(jxltk_test PRIVATE .)
  target_link_libraries(jxltk_test PRIVATE
                        PkgConfig::GoogleTest PkgConfig::GoogleTestMain)
  target_link_libraries(jxltk_test PRIVATE PkgConfig::LibJXL PkgConfig::LibJXLThreads)
  target_link_libraries(jxltk_test PRIVATE jxlazy)
endif()


set(CMAKE_VERBOSE_MAKEFILE ON)
