# Third-party licenses

`pi5-system-surrogate` is MIT-licensed (see [LICENSE](LICENSE)). The build pulls in
third-party code under different but compatible licenses; this file
inventories them.

| Component | License | How it's pulled in |
| --- | --- | --- |
| WPILib (allwpilib) | BSD-3-Clause | git submodule at [hal-port/upstream/allwpilib](hal-port/upstream/allwpilib). Build artifacts (`libntcore.so`, `libwpiutil.so`, `libwpiHal.so`, `libdatalog.so`, `ntcore.jar`, `wpiutil.jar`) we link against retain BSD-3 license. |
| nanopb 0.4.9 | zlib | Source files compiled into `daemon_core` from the `wpiutil/src/main/native/thirdparty/nanopb/` subtree of the allwpilib submodule. |
| cpp-httplib 0.18.5 | MIT | Fetched at build time via CMake `FetchContent` from yhirose/cpp-httplib. |
| GoogleTest 1.15.2 | BSD-3-Clause | Test-only dependency, fetched at build time via CMake `FetchContent` when `DAEMON_TESTS=ON`. |

Generated `MrcComm.npb.{h,cpp}` files included in the daemon are derived
from `MrcComm.proto` in the WPILib HAL and inherit BSD-3-Clause.
