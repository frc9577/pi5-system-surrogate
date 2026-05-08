# Resolves headers and shared libraries for libntcore/libwpiutil/libwpinet
# from the hal-port/ build tree. Expects WPILIB_NATIVE_ROOT to point at the
# allwpilib checkout (e.g., hal-port/upstream/allwpilib).

if(NOT WPILIB_NATIVE_ROOT)
  message(FATAL_ERROR "WPILIB_NATIVE_ROOT must be set")
endif()

set(WPILibNative_INCLUDE_DIRS
  ${WPILIB_NATIVE_ROOT}/ntcore/src/main/native/include
  ${WPILIB_NATIVE_ROOT}/ntcore/src/generated/main/native/include
  ${WPILIB_NATIVE_ROOT}/wpiutil/src/main/native/include
  ${WPILIB_NATIVE_ROOT}/wpinet/src/main/native/include
)

# wpiutil + wpinet shard headers across thirdparty packages (llvm, expected,
# fmtlib, nanopb, libuv, ...). Each one carries an `include/` subdir that's
# expected to be on the include path. NetworkTableInstance.hpp transitively
# pulls many of them, so we add them all rather than chase one-by-one.
file(GLOB WPILibNative_THIRDPARTY_INCLUDES
  ${WPILIB_NATIVE_ROOT}/wpiutil/src/main/native/thirdparty/*/include
  ${WPILIB_NATIVE_ROOT}/wpinet/src/main/native/thirdparty/*/include
)
list(APPEND WPILibNative_INCLUDE_DIRS ${WPILibNative_THIRDPARTY_INCLUDES})

set(WPILibNative_LIBRARY_DIRS "")
foreach(lib ntcore wpiutil wpinet datalog)
  set(_so_dir ${WPILIB_NATIVE_ROOT}/${lib}/build/libs/${lib}/shared/release)
  find_library(WPILibNative_${lib}_LIBRARY ${lib}
    PATHS ${_so_dir}
    NO_DEFAULT_PATH
    REQUIRED
  )
  add_library(WPILibNative::${lib} SHARED IMPORTED)
  set_target_properties(WPILibNative::${lib} PROPERTIES
    IMPORTED_LOCATION "${WPILibNative_${lib}_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${WPILibNative_INCLUDE_DIRS}"
  )
  list(APPEND WPILibNative_LIBRARY_DIRS ${_so_dir})
endforeach()

set(WPILibNative_FOUND TRUE)
