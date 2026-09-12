# AddressSanitizer 专用 triplet（阶段十四）。
#
# 为什么需要它：MSVC 的 ASan 要求**所有参与链接的目标都带插桩**，否则会报
# `LNK2038: annotate_string 不匹配`。vcpkg 默认 triplet 装出来的静态库没有插桩，
# 所以必须用这个 triplet 单独装一套依赖（装到 vcpkg_installed_asan，不影响默认开发环境）。
#
# 用法：cmake --preset asan（见 CMakePresets.json；该预设用独立二进制目录与独立依赖目录）
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_C_FLAGS "/fsanitize=address")
set(VCPKG_CXX_FLAGS "/fsanitize=address")
set(VCPKG_LINKER_FLAGS "/fsanitize=address")
