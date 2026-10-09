cmake_minimum_required(VERSION 3.15)

project(lwrbpp
    VERSION     1.0.0
    LANGUAGES   C CXX
    DESCRIPTION "轻量级环形缓冲区 (lwrb) C++ RAII 封装"
)

# ============================================================================
# 构建选项
# ============================================================================

option(LWRP_BUILD_DEMO "构建 Demo 可执行文件" ON)

# ============================================================================
# 源文件
# ============================================================================

set(LWRP_CORE_SOURCES
    lwrb/lwrb.c
    lwrb/lwrb_ex.c
    rb/rb.c
)

# ============================================================================
# librb（rb/）配置 —— rb.h.in 模板占位符定值
# ============================================================================

# MSVC 无原生 pthread/semaphore，多线程模式（阻塞 API）仅在非 MSVC 平台开启
if(MSVC)
    set(ENABLE_THREADS 0)
else()
    set(ENABLE_THREADS 1)
    find_package(Threads REQUIRED)
endif()

# iovec 散读/散写 API（rb_readv/rb_writev 等）始终开启
set(ENABLE_IOV 1)

# 由 rb/rb.h.in 生成最终头文件到构建目录，避免污染源码树
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/rb/rb.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/rb/rb.h
    @ONLY
)

# ============================================================================
# 库目标
# ============================================================================

add_library(lwrbpp STATIC ${LWRP_CORE_SOURCES})

# C 标准: C11 (lwrb/rb 使用了 <stdatomic.h> 需要 C11)
# 注意：必须作用于目标属性，全局 CMAKE_C_STANDARD 在 add_library 之后设置不生效
set_target_properties(lwrbpp PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

target_include_directories(lwrbpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# 多线程 rb 需要 pthread（头文件 struct rb 内嵌 pthread_mutex_t/sem_t，
# 对 C 和 C++ 消费者均可见，故为 PUBLIC 链接）
if(NOT MSVC)
    target_link_libraries(lwrbpp PUBLIC Threads::Threads)
endif()

# MSVC 的 stdatomic.h 支持有限，使用 lwrb 自带的非原子回退实现
# /utf-8 用于 rb.c 中的 UTF-8 字符（注释里的框线字符），避免 C4819 警告
if(MSVC)
    target_compile_definitions(lwrbpp PUBLIC LWRB_DISABLE_ATOMIC)
    target_compile_options(lwrbpp PRIVATE /utf-8)
endif()

# rb.c 遵循上游约定以 #include "rb.h"（不含目录前缀）引用生成的头文件，
# 为其单独补充指向生成目录的私有 include 路径
target_include_directories(lwrbpp
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/include/rb
)

# C++ 标准: C++17 (lwrbpp.h 使用了 std::is_trivially_copyable_v 等特性)
# 虽然库本身是 C 写的，但 C++ 头文件需要 C++17 -> 设为 INTERFACE 传播给消费者
target_compile_features(lwrbpp INTERFACE cxx_std_17)

# lwrb_ex.c（overwrite / move 等）由 lwrbpp.h 直接调用，必须始终参与编译
# LWRB_DEV 用于通过 lwrb_ex.c 中的 #if defined(LWRB_DEV) 守卫
# HAVE_ASSERT_H=1 让 rb.c 启用 <assert.h>（否则其 assert 为空操作）
target_compile_definitions(lwrbpp PRIVATE LWRB_DEV HAVE_ASSERT_H=1)

# 别名，方便 add_subdirectory 方式的 CMake 项目使用
add_library(lwrbpp::lwrbpp ALIAS lwrbpp)

# ============================================================================
# Demo（可选）
# ============================================================================

if(LWRP_BUILD_DEMO)
    add_subdirectory(demo)
endif()

# ============================================================================
# 安装规则（兼容 GNUInstallDirs 与 vcpkg）
# ============================================================================

include(GNUInstallDirs)

# --- 安装库文件 ---
install(TARGETS lwrbpp
    EXPORT    lwrbppTargets
    ARCHIVE   DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY   DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME   DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# --- 安装头文件（C 头 .h + C++ 头 .hpp）---
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
        PATTERN "*.h"
)

# --- 安装由 rb.h.in 生成的 rb/rb.h ---
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/rb/rb.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rb
)

# --- 导出 CMake 目标文件 ---
install(EXPORT lwrbppTargets
    FILE      lwrbppTargets.cmake
    NAMESPACE lwrbpp::
    DESTINATION ${CMAKE_INSTALL_DATADIR}/lwrbpp
)

# --- 生成 Config.cmake 与 ConfigVersion.cmake（支持 find_package）---
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/lwrbppConfigVersion.cmake"
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

# 简单的 Config.cmake：直接引入 Targets 文件
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/lwrbppConfig.cmake"
    [=[
include("${CMAKE_CURRENT_LIST_DIR}/lwrbppTargets.cmake")
]=])

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/lwrbppConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/lwrbppConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/lwrbpp
)

# ============================================================================
# 打包支持 (CPack)
# ============================================================================

set(CPACK_PACKAGE_NAME          "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION       "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION   "${PROJECT_DESCRIPTION}")
#set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

if(NOT EXISTS "${CPACK_RESOURCE_FILE_LICENSE}")
    # LICENSE 文件不存在时不报错，跳过
    unset(CPACK_RESOURCE_FILE_LICENSE)
endif()
