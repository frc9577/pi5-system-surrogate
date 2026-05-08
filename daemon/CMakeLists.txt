cmake_minimum_required(VERSION 3.28)
project(ds-surrogate CXX)

option(DS_SURROGATE_TESTS "Build tests" ON)
option(DS_SURROGATE_COVERAGE "Enable coverage instrumentation" OFF)

# CMake 3.28 doesn't yet have a feature map for GCC 16, so we set -std=c++26
# directly rather than via CMAKE_CXX_STANDARD/cxx_std_26.
add_compile_options(-std=c++26 -Wall -Wextra -Wpedantic)

if(DS_SURROGATE_COVERAGE)
  message(STATUS "ds-surrogate: coverage instrumentation enabled")
  add_compile_options(--coverage -O0 -g)
  add_link_options(--coverage)
endif()

if(NOT WPILIB_NATIVE_ROOT)
  set(WPILIB_NATIVE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../hal-port/upstream/allwpilib
      CACHE PATH "Root of the allwpilib checkout used by hal-port/")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
find_package(WPILibNative MODULE REQUIRED)

set(NANOPB_DIR ${WPILIB_NATIVE_ROOT}/wpiutil/src/main/native/thirdparty/nanopb)
set(MRC_GEN_DIR ${WPILIB_NATIVE_ROOT}/hal/src/generated/main/native/cpp)
set(MRC_NPB_CPP ${MRC_GEN_DIR}/mrc/protobuf/MrcComm.npb.cpp)
if(NOT EXISTS ${MRC_NPB_CPP})
  message(FATAL_ERROR
    "${MRC_NPB_CPP} not found.\n"
    "Did the hal-port build run? See hal-port/README.md.")
endif()

# Static core library: every testable unit lives here. Daemon executable is
# pure glue; tests link against this directly.
add_library(ds_surrogate_core STATIC
  src/nt_server.cpp
  src/daemon_state.cpp
  src/control_data_encoder.cpp
  src/smartio_state.cpp
  src/cadence_tracker.cpp
  src/control_data_publisher.cpp
  src/rsl_blinker.cpp
  src/imu_publisher.cpp
  src/smartio_bridge.cpp
  src/gamepad_ingest.cpp
  src/diagnostics_publisher.cpp
  src/web_ui.cpp
  src/libgpiod_backend.cpp
  src/linux_runtime.cpp
  src/match_controller.cpp
  ${MRC_NPB_CPP}
  ${NANOPB_DIR}/src/pb_common.cpp
  ${NANOPB_DIR}/src/pb_decode.cpp
  ${NANOPB_DIR}/src/pb_encode.cpp
)
target_include_directories(ds_surrogate_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${WPILibNative_INCLUDE_DIRS}
  ${MRC_GEN_DIR}
)
target_link_libraries(ds_surrogate_core PUBLIC
  WPILibNative::ntcore
  WPILibNative::wpinet
  WPILibNative::wpiutil
  WPILibNative::datalog
  gpiodcxx
  gpiod
)
# DT_RPATH (transitive) so libntcore's deps resolve through the executable's
# rpath instead of its own $ORIGIN.
target_link_options(ds_surrogate_core PUBLIC LINKER:--disable-new-dtags)

function(ds_surrogate_apply_rpath tgt)
  set_target_properties(${tgt} PROPERTIES
    BUILD_RPATH "${WPILibNative_LIBRARY_DIRS}"
    INSTALL_RPATH "$ORIGIN/../lib"
  )
endfunction()

add_executable(ds-surrogate src/main.cpp)
target_link_libraries(ds-surrogate PRIVATE ds_surrogate_core)
ds_surrogate_apply_rpath(ds-surrogate)

add_executable(check_server_ready src/check_server_ready.cpp)
target_link_libraries(check_server_ready PRIVATE ds_surrogate_core)
ds_surrogate_apply_rpath(check_server_ready)

add_executable(nt_smartio_test src/nt_smartio_test.cpp)
target_link_libraries(nt_smartio_test PRIVATE ds_surrogate_core)
ds_surrogate_apply_rpath(nt_smartio_test)

include(FetchContent)
FetchContent_Declare(cpp_httplib
  URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.5.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(cpp_httplib)
target_include_directories(ds_surrogate_core PUBLIC ${cpp_httplib_SOURCE_DIR})

if(DS_SURROGATE_TESTS)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_subdirectory(tests)
endif()
