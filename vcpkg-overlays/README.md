# vcpkg overlay ports

这个目录存放**对 vcpkg 官方 port 的项目级修补**。CMake 通过 `VCPKG_OVERLAY_PORTS`
（见 `CMakePresets.json`）加载它。

## libpqxx

**问题**：libpqxx 8.0.2 在 Windows/MSVC 下构建出的 `pqxx.dll` 把 STL 内联函数也导出了
（实测 245 个 `std::basic_string_view` 成员，用 `dumpbin /exports pqxx.dll` 可复现）。
消费方只要自己用到 `std::string_view`，链接期就会报：

```text
pqxx.lib(pqxx.dll) : error LNK2005: std::basic_string_view<...>::basic_string_view(char const * const)
                      已经在 main.cpp.obj 中定义
fatal error LNK1169: 找到一个或多个多重定义的符号
```

**修法**：给 libpqxx 的编译加上 `/Zc:dllexportInlines-`，让 `dllexport` 不再导出内联函数。
这是微软文档针对该错误给出的修法，只影响这个包的编译，不改变我们自己的代码。

**为什么用 overlay 而不是全局 triplet**：overlay 只改变 libpqxx 这一个包的 ABI 哈希，
其它 19 个依赖继续复用已装好的二进制；换 triplet 会导致全部依赖重编。

**什么时候可以删掉**：上游修复该问题（或 vcpkg 官方 port 加上这个开关）之后，
删掉本目录与 `CMakePresets.json` 里的 `VCPKG_OVERLAY_PORTS` 即可。
