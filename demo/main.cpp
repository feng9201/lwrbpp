/**
 * \file            main.cpp
 * \brief           LwRB C++ 封装使用示例
 */

#include "lwrb/lwrbpp.h"
#include <cstdio>
#include <cstring>
#include <cassert>

/* ========================== 辅助函数 ========================== */

static void
print_buffer_status(const lwrbpp::ring_buffer& rb, const char* tag) {
    printf("[%s] 可读=%zu  可写=%zu  总容量=%zu\n",
           tag, rb.available(), rb.free_space(), rb.capacity());
}

/* ========================== 示例 1: 外部内存 ========================== */

static void
demo_external_buffer() {
    printf("====== 示例 1: 外部传入的内存 ======\n");

    uint8_t mem[64];
    lwrbpp::ring_buffer rb(mem, sizeof(mem));

    assert(rb.is_ready());
    assert(rb.capacity() == 64);
    assert(rb.available() == 0);
    print_buffer_status(rb, "初始化后");

    /* 写入数据 */
    const char* hello = "Hello, LwRB!";
    size_t written = rb.write(hello, strlen(hello));
    printf("写入 %zu 字节: \"%s\"\n", written, hello);
    print_buffer_status(rb, "写入后");

    /* 读取数据 */
    char buf[32] = {0};
    size_t read = rb.read(buf, sizeof(buf) - 1);
    printf("读取 %zu 字节: \"%s\"\n", read, buf);
    print_buffer_status(rb, "读取后");

    assert(rb.available() == 0);
    printf("\n");
}

/* ========================== 示例 2: 内部内存 ========================== */

static void
demo_internal_buffer() {
    printf("====== 示例 2: 内部自动分配内存 ======\n");

    lwrbpp::ring_buffer rb(32);
    assert(rb.is_ready());
    assert(rb.capacity() == 32);
    print_buffer_status(rb, "初始化后");

    /* 写入数据 */
    const char* data = "0123456789ABCDEF";
    size_t written = rb.write(data, strlen(data));
    printf("写入 %zu 字节: \"%s\"\n", written, data);

    /* 缓存区满写测试 */
    size_t extra = rb.write("EXTRA", 5);
    printf("额外尝试写入 5 字节，实际写入 %zu 字节（缓冲区剩余 %zu 字节）\n",
           extra, rb.free_space());

    /* 全部读取 */
    char buf[64] = {0};
    size_t read = rb.read(buf, sizeof(buf) - 1);
    printf("读取 %zu 字节: \"%s\"\n", read, buf);
    printf("\n");
}

/* ========================== 示例 3: Peek 与 Skip ========================== */

static void
demo_peek_and_skip() {
    printf("====== 示例 3: Peek 与 Skip ======\n");

    lwrbpp::ring_buffer rb(32);
    const char* data = "HelloLwRB";
    rb.write(data, strlen(data));

    /* Peek 不消耗数据 */
    char peek_buf[16] = {0};
    size_t peeked = rb.peek(0, peek_buf, 5);
    printf("Peek 5 字节: \"%s\"，可读不变: %zu\n", peek_buf, rb.available());

    /* Skip */
    rb.skip(5);
    printf("Skip 5 字节后，可读: %zu\n", rb.available());

    char buf[16] = {0};
    rb.read(buf, sizeof(buf) - 1);
    printf("读取剩余数据: \"%s\"\n", buf);
    printf("\n");
}

/* ========================== 示例 4: Overwrite ========================== */

static void
demo_overwrite() {
    printf("====== 示例 4: Overwrite（覆盖写入）======\n");

    lwrbpp::ring_buffer rb(8);

    rb.write("ABCDEF", 6);
    printf("写入 ABCDEF 后，可读=%zu\n", rb.available());

    /* overwrite — 缓冲区满时自动丢弃旧数据 */
    rb.overwrite("XYZ", 3);
    printf("覆盖写入 XYZ 后，可读=%zu\n", rb.available());

    char buf[16] = {0};
    rb.read(buf, sizeof(buf) - 1);
    printf("读取结果: \"%s\"\n", buf);
    printf("\n");
}

/* ========================== 示例 5: 类型安全的 push_back / pop_front ========================== */

struct SensorData {
    float temperature;
    float humidity;
    uint32_t timestamp;
};

static void
demo_typed_io() {
    printf("====== 示例 5: 类型安全的 push_back / pop_front ======\n");

    lwrbpp::ring_buffer rb(64);

    SensorData data1 = { 25.3f, 60.1f, 1001 };
    SensorData data2 = { 26.8f, 55.4f, 1002 };

    /* 写入两个结构体 */
    bool ok1 = rb.push_back(data1);
    bool ok2 = rb.push_back(data2);
    printf("写入 SensorData: %s %s\n", ok1 ? "OK" : "FAIL", ok2 ? "OK" : "FAIL");
    printf("可读字节: %zu (期望 %zu)\n", rb.available(), sizeof(SensorData) * 2);

    /* 读取一个结构体 */
    SensorData out;
    bool ok3 = rb.pop_front(out);
    printf("读取 SensorData: %s  (温度=%.1f, 湿度=%.1f, 时间戳=%u)\n",
           ok3 ? "OK" : "FAIL",
           out.temperature, out.humidity, out.timestamp);
    printf("剩余可读: %zu\n", rb.available());
    printf("\n");
}

/* ========================== 示例 6: 零拷贝线性访问 ========================== */

static void
demo_linear_access() {
    printf("====== 示例 6: 零拷贝线性访问 ======\n");

    lwrbpp::ring_buffer rb(16);
    const char* msg = "ZeroCopyTest";
    rb.write(msg, strlen(msg));

    /* 获取线性读地址，直接访问内部数据 */
    const void* addr = rb.linear_read_address();
    size_t len = rb.linear_read_length();
    printf("线性读地址: %p, 长度: %zu\n", addr, len);

    /* 用 memcmp 直接比较内部缓存 */
    assert(len >= strlen(msg));
    assert(memcmp(addr, msg, strlen(msg)) == 0);
    printf("memcmp 验证通过: 内部数据与源数据一致\n");

    /* 然后正常读取 */
    char buf[32] = {0};
    rb.read(buf, sizeof(buf) - 1);
    printf("读取结果: \"%s\"\n", buf);
    printf("\n");
}

/* ========================== 示例 7: 移动语义 ========================== */

static void
demo_move_semantics() {
    printf("====== 示例 7: 移动语义 ======\n");

    lwrbpp::ring_buffer rb1(32);
    rb1.write("MoveTest", 8);
    printf("rb1: 可读=%zu\n", rb1.available());

    /* 移动构造 */
    lwrbpp::ring_buffer rb2(std::move(rb1));
    printf("移动后 rb1: is_ready=%d, rb2: 可读=%zu\n",
           rb1.is_ready(), rb2.available());

    /* 移动赋值 */
    lwrbpp::ring_buffer rb3(16);
    rb3 = std::move(rb2);
    printf("移动赋值后 rb2: is_ready=%d, rb3: 可读=%zu\n",
           rb2.is_ready(), rb3.available());

    char buf[16] = {0};
    rb3.read(buf, sizeof(buf) - 1);
    printf("rb3 读取: \"%s\"\n", buf);
    printf("\n");
}

/* ========================== 示例 8: 事件回调 ========================== */

static void
my_event_callback(lwrb_t* /*buff*/, lwrb_evt_type_t evt, lwrb_sz_t bp) {
    const char* evt_str = "";
    switch (evt) {
        case LWRB_EVT_READ:  evt_str = "READ";  break;
        case LWRB_EVT_WRITE: evt_str = "WRITE"; break;
        case LWRB_EVT_RESET: evt_str = "RESET"; break;
    }
    printf("  [事件] %s  %zu 字节\n", evt_str, (size_t)bp);
}

static void
demo_event_callback() {
    printf("====== 示例 8: 事件回调 ======\n");

    lwrbpp::ring_buffer rb(16);
    rb.set_event_callback(my_event_callback);

    rb.write("ABCD", 4);

    char buf[8] = {0};
    rb.read(buf, 4);
    printf("  读取到: \"%s\"\n", buf);

    rb.reset();
    printf("\n");
}

/* ========================== Main ========================== */

int
main() {
    printf("LwRB C++ 封装 — 使用示例\n");
    printf("===========================\n\n");

    demo_external_buffer();
    demo_internal_buffer();
    demo_peek_and_skip();
    demo_overwrite();
    demo_typed_io();
    demo_linear_access();
    demo_move_semantics();
    demo_event_callback();

    printf("所有示例执行完毕 ✓\n");
    return 0;
}
