cmake_minimum_required(VERSION 3.28...3.28)

project(JXLazy VERSION 0.0.1
               LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

include(FindPkgConfig)
# If libjxl_dec exists, use that instead of libjxl
pkg_check_modules(LibJXL QUIET IMPORTED_TARGET libjxl_dec>=0.7.0)
if (LibJXL_FOUND)
  message(STATUS "Found libjxl_dec ${LibJXL_VERSION}")
else()
  pkg_check_modules(LibJXL REQUIRED IMPORTED_TARGET libjxl>=0.7.0)
  #message(STATUS "Found libjxl ${LibJXL_VERSION}")
endif()
pkg_check_modules(LibJXLThreads REQUIRED IMPORTED_TARGET libjxl_threads>=0.7.0)
#message(STATUS "Found libjxl_threads ${LibJXLThreads_VERSION}")


# Add library without "STATIC" or "SHARED".
# Run cmake with -DBUILD_SHARED_LIBS=ON to build shared instead of default static.
add_library(jxlazy
            decoder.cpp   include/jxlazy/decoder.h
            exception.cpp include/jxlazy/exception.h
            info.cpp      info.h
            util.cpp      util.h)
set_target_properties(jxlazy PROPERTIES VERSION ${PROJECT_VERSION})
set_target_properties(jxlazy PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})

if(BUILD_TESTING)
  pkg_check_modules(GoogleTest REQUIRED IMPORTED_TARGET gtest)
  pkg_check_modules(GoogleTestMain REQUIRED IMPORTED_TARGET gtest_main)
  enable_testing()
  message(STATUS "Tests enabled")

  add_executable(jxlazy_decoder_test
                 decoder_test.cpp)
  target_include_directories(jxlazy_decoder_test PUBLIC include)
  target_include_directories(jxlazy_decoder_test PRIVATE .)
  target_link_libraries(jxlazy_decoder_test PRIVATE jxlazy)
  target_link_libraries(jxlazy_decoder_test PRIVATE
                        PkgConfig::GoogleTest PkgConfig::GoogleTestMain)
  target_link_libraries(jxlazy_decoder_test PRIVATE PkgConfig::LibJXL PkgConfig::LibJXLThreads)
  target_compile_definitions(jxlazy_decoder_test PRIVATE $<$<CONFIG:Debug>: JXLAZY_DEBUG=1>)

  add_executable(jxlazy_util_test
                 util_test.cpp)
  target_include_directories(jxlazy_util_test PUBLIC include)
  target_include_directories(jxlazy_util_test PRIVATE .)
  target_link_libraries(jxlazy_util_test PRIVATE jxlazy)
  target_link_libraries(jxlazy_util_test PRIVATE
                        PkgConfig::GoogleTest PkgConfig::GoogleTestMain)
  target_link_libraries(jxlazy_util_test PRIVATE PkgConfig::LibJXL PkgConfig::LibJXLThreads)
  target_compile_definitions(jxlazy_util_test PRIVATE $<$<CONFIG:Debug>: JXLAZY_DEBUG=1>)
endif()

# Define installable heaaders
set_target_properties(jxlazy PROPERTIES PUBLIC_HEADER
                      "include/jxlazy/decoder.h;include/jxlazy/exception.h")

# Set install locations
include(GNUInstallDirs OPTIONAL RESULT_VARIABLE GNU_INSTALL_DIRS_SUPPORTED)
if(GNU_INSTALL_DIRS_SUPPORTED)
  install(TARGETS jxlazy
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/jxlazy)
else()
  # Controllable via CMAKE_INSTALL_PREFIX
  install(TARGETS jxlazy
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    PUBLIC_HEADER DESTINATION include/jxlazy)
endif()

get_property(SHARED_LIBS_SUPPORTED GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS)
if(NOT SHARED_LIBS_SUPPORTED)
  message(STATUS "Shared libs not supported on this target")
  set(BUILD_SHARED_LIBS "OFF")
endif()

# Enabled PIE binaries by default if supported.
include(CheckPIESupported OPTIONAL RESULT_VARIABLE CHECK_PIE_SUPPORTED)
if(CHECK_PIE_SUPPORTED)
  check_pie_supported(LANGUAGES CXX)
  if(CMAKE_CXX_LINK_PIE_SUPPORTED)
    set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
  endif()
endif()

# Enable link-time optimization
include(CheckIPOSupported CHECK_IPO_SUPPORTED)
if(CHECK_IPO_SUPPORTED)
  check_ipo_supported(RESULT result)
  if(result)
    set_target_properties(jxlazy PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

target_include_directories(jxlazy PUBLIC include)
target_include_directories(jxlazy PRIVATE .)

target_compile_definitions(jxlazy PRIVATE $<$<CONFIG:Debug>: JXLAZY_DEBUG=1>)

target_link_libraries(jxlazy PRIVATE PkgConfig::LibJXL PkgConfig::LibJXLThreads)

if (CMAKE_CXX_COMPILER_ID MATCHES GNU OR CMAKE_CXX_COMPILER_ID MATCHES Clang)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Weffc++ -Wno-missing-field-initializers")
endif()

set(CMAKE_VERBOSE_MAKEFILE ON)

message(STATUS "Will build with ${CMAKE_CXX_COMPILER_ID}; c++${CMAKE_CXX_STANDARD}; shared = ${BUILD_SHARED_LIBS}")
