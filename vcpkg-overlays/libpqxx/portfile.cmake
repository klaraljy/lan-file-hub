# overlay port：libpqxx 8.0.2
#
# 相对官方 port 的唯一改动：MSVC 下追加 /Zc:dllexportInlines-。
#
# 原因：libpqxx 8.0.2 的 CMakeLists 给导出类加了 __declspec(dllexport)，
# MSVC 会连带把用到的 STL 内联函数（实测 245 个 basic_string_view 成员）
# 一起从 pqxx.dll 导出。消费方只要自己用到 std::string_view，就会在链接期
# 撞上 LNK2005 / LNK1169「多重定义的符号」。
#
# /Zc:dllexportInlines- 让 dllexport 不再导出内联函数，是微软文档给出的
# 针对性修法。上游修好之后可以删掉这个 overlay（见 README.md）。

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO jtv/libpqxx
    REF "${VERSION}"
    SHA512 c4daebcf1b4b882c0b87274548c4081154e909aa379a54269c460ceac6c3000cf68add7ee2da9ff05530e8d8db8b93ca12ea954524467f662ea557ec6612727f
    HEAD_REF master
)

vcpkg_find_acquire_program(PYTHON3)

# 关键：让 libpqxx 以静态库形式构建。
#   libpqxx 8.0.2 把 zview（继承自 std::string_view）标了 dllexport，
#   生成的 pqxx.dll 因此导出了 245 个 basic_string_view 成员函数；
#   消费方一旦自己用到 std::string_view，链接期必然 LNK2005/LNK1169。
#   /Zc:dllexportInlines- 实测无法阻止基类内联成员被导出（已试过，无效），
#   而不产生 DLL 就根本没有导出表。只影响这一个包，其余依赖仍为动态库。
set(VCPKG_LIBRARY_LINKAGE static)

# 注意：不要通过 vcpkg_cmake_configure 的 OPTIONS 传 CMAKE_CXX_FLAGS ——
# vcpkg 会以 z_vcpkg_warn_ambiguous_system_variables 警告并覆盖它。

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        "-DPython3_EXECUTABLE=${PYTHON3}"
        -DSKIP_BUILD_TEST=ON
        -DBUILD_EXAMPLES=OFF
        -DBUILD_SHARED_LIBS=OFF
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/libpqxx)

if(VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_TARGET_IS_MINGW)
    # Not module from libpq
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/lib/pkgconfig" "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig")
else()
    vcpkg_fixup_pkgconfig()
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
