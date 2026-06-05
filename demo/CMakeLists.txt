#
# Demo 可执行文件
#

add_executable(lwrbpp_demo main.cpp)

target_link_libraries(lwrbpp_demo PRIVATE lwrbpp::lwrbpp)

# MSVC 中文字符串支持
if(MSVC)
    target_compile_options(lwrbpp_demo PRIVATE /utf-8)
endif()
