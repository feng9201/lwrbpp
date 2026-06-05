# lwrbpp — 轻量级环形缓冲区 C++ RAII 封装

基于 [lwrb](https://github.com/MaJerle/lwrb) C 语言库的现代 C++ 封装，提供 RAII 风格的内存安全接口。

## 特性

- **RAII 封装** — `lwrb_t` 完全封装在类内部，自动管理资源
- **两种内存模式** — 支持外部传入缓冲区或类内自动分配
- **Move-only** — 禁止拷贝，支持移动语义，所有权清晰
- **零拷贝访问** — 提供线性地址直接读写内部缓冲区
- **类型安全** — `push_back<T>` / `pop_front<T>`，编译期检查可平凡拷贝类型
- **线程安全基元** — 底层使用 C11 atomics（MSVC 下自动回退为非原子实现）

## 要求

- C++17 编译器
- C11 编译器（仅编译 C 源文件）
- CMake 3.15+

## 构建

```bash
# 仅构建库
cmake -B build
cmake --build build

# 构建库 + Demo
cmake -B build -DLWRP_BUILD_DEMO=ON
cmake --build build
./build/demo/lwrbpp_demo
```

## 快速使用

```cpp
#include "lwrb/lwrbpp.h"
#include <cstdio>

int main() {
    // 方式一：外部传入缓冲区内存
    uint8_t mem[64];
    lwrbpp::ring_buffer rb(mem, sizeof(mem));

    // 方式二：类内自动分配（RAII）
    // lwrbpp::ring_buffer rb(64);

    rb.write("Hello", 5);

    char buf[16] = {0};
    rb.read(buf, sizeof(buf));
    printf("%s\n", buf);   // 输出: Hello

    return 0;
}
```

## CMake 集成

### add_subdirectory

```cmake
add_subdirectory(path/to/lwrbpp/src)
target_link_libraries(my_target PRIVATE lwrbpp::lwrbpp)
```

### find_package（安装后）

```cmake
find_package(lwrbpp CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE lwrbpp::lwrbpp)
```

### vcpkg 私有 port

参考 `vcpkg.json` 和 `CMakeLists.txt` 中的安装规则，将 `src/` 目录配置为 vcpkg port 即可。

## API 一览

| 方法 | 说明 |
|------|------|
| `write(data, size)` | 写入数据，返回实际写入字节数 |
| `read(data, size)` | 读取数据，返回实际读取字节数 |
| `available()` | 可读取字节数 |
| `free_space()` | 可写入字节数 |
| `capacity()` | 缓冲区总容量 |
| `peek(skip, data, size)` | 窥视数据（不移动读指针）|
| `overwrite(data, size)` | 覆盖写入（自动丢弃旧数据）|
| `skip(len)` | 跳过已读数据 |
| `push_back(obj)` | 写入单个对象（模板）|
| `pop_front(obj)` | 读取单个对象（模板）|
| `linear_read_address()` | 零拷贝读取线性地址 |
| `linear_write_address()` | 零拷贝写入线性地址 |

## 许可

MIT License。底层 lwrb 库同样使用 MIT 许可。
