/**
 * \file            lwrbpp.h
 * \brief           LwRB - 轻量级环形缓冲区 C++ 封装
 *
 * 在 lwrb C 语言库的基础上提供现代 C++ RAII 封装。
 * 支持外部传入缓冲区内存和内部自动分配两种方式。
 */

#ifndef LWRBPP_H_HDR
#define LWRBPP_H_HDR

#include "lwrb/lwrb.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

namespace lwrbpp {

/**
 * \brief   lwrb（轻量级环形缓冲区）的 C++ RAII 封装
 *
 * 内存所有权语义：
 * - 使用 (buffer, size) 构造时：调用者保留缓冲区内存的所有权，析构时不会释放该内存。
 * - 仅使用 (size) 构造时：类内部自行分配缓冲区内存，析构时自动释放。
 *
 * 该类仅支持移动语义（禁止拷贝）。移动后，源对象处于有效但空的状态（is_ready() 返回 false）。
 */
class ring_buffer {
public:
    /**
     * \brief   使用外部传入的内存构造环形缓冲区。
     *
     * 调用者保留缓冲区内存的完全所有权。
     * 调用者需确保该内存在本对象的生命周期内始终有效。
     *
     * \param[in]   buffer: 指向用作环形缓冲区存储的内存块，不能为 NULL。
     * \param[in]   size:   内存块大小（字节数），最大可存储字节数为 (size - 1)。
     */
    ring_buffer(void* buffer, std::size_t size) noexcept
        : owns_buffer_(false) {
        lwrb_init(&handle_, buffer, size);
    }

    /**
     * \brief   使用内部自动分配的内存构造环形缓冲区。
     *
     * 类在堆上分配缓冲区内存并拥有其所有权，析构时自动释放。
     *
     * \param[in]   size: 期望的存储容量（字节数），实际可用容量为 (size - 1)。
     * \throws  std::bad_alloc 内存分配失败时抛出。
     */
    explicit ring_buffer(std::size_t size)
        : owns_buffer_(true) {
        auto* buf = new std::uint8_t[size];
        if (!lwrb_init(&handle_, buf, size)) {
            delete[] buf;
            throw std::bad_alloc();
        }
    }

    /** 禁止拷贝 */
    ring_buffer(const ring_buffer&) = delete;
    ring_buffer& operator=(const ring_buffer&) = delete;

    /**
     * \brief   移动构造函数。
     *
     * 将内部缓冲区所有权从 `other` 转移到新对象。
     * `other` 被置于空（未初始化）状态。
     */
    ring_buffer(ring_buffer&& other) noexcept
        : owns_buffer_(other.owns_buffer_) {
        /* 通过内存拷贝转移内部状态，然后使源对象失效 */
        std::memcpy(&handle_, &other.handle_, sizeof(handle_));
        other.owns_buffer_ = false;
        other.handle_.buff = nullptr;
        other.handle_.size = 0;
    }

    /**
     * \brief   移动赋值运算符。
     *
     * 释放当前拥有的缓冲区，然后从 `other` 转移所有权。
     * `other` 被置于空（未初始化）状态。
     */
    ring_buffer& operator=(ring_buffer&& other) noexcept {
        if (this != &other) {
            /* 如果拥有缓冲区所有权，先释放当前资源 */
            if (owns_buffer_ && handle_.buff != nullptr) {
                delete[] static_cast<std::uint8_t*>(handle_.buff);
            }
            /* 转移状态 */
            std::memcpy(&handle_, &other.handle_, sizeof(handle_));
            owns_buffer_ = other.owns_buffer_;
            /* 使源对象失效 */
            other.owns_buffer_ = false;
            other.handle_.buff = nullptr;
            other.handle_.size = 0;
        }
        return *this;
    }

    /**
     * \brief   析构函数。
     *
     * 如果类拥有缓冲区内存所有权（通过单参数构造函数分配），则在此释放。
     * 外部传入的缓冲区不会被释放。
     */
    ~ring_buffer() noexcept {
        if (owns_buffer_ && handle_.buff != nullptr) {
            delete[] static_cast<std::uint8_t*>(handle_.buff);
        }
        lwrb_free(&handle_);
    }

    /* --------------------------------------------------------------------------
     *  写入 / 读取
     * -------------------------------------------------------------------------- */

    /**
     * \brief           向环形缓冲区写入数据。
     *
     * 将 `data` 中最多 `size` 字节的数据拷贝到缓冲区。
     * 如果空闲空间不足，实际写入的字节数可能少于 `size`。
     *
     * \param[in]       data: 指向源数据的指针。
     * \param[in]       size: 期望写入的字节数。
     * \return          实际写入的字节数，空闲空间不足时可能小于 `size`。
     */
    std::size_t write(const void* data, std::size_t size) noexcept {
        return lwrb_write(&handle_, data, size);
    }

    /**
     * \brief           从环形缓冲区读取数据。
     *
     * 将缓冲区中最多 `size` 字节的数据拷贝到 `data`。
     * 如果可用数据不足，实际读取的字节数可能少于 `size`。
     *
     * \param[out]      data: 指向目标内存的指针。
     * \param[in]       size: 期望读取的字节数。
     * \return          实际读取的字节数，数据不足时可能小于 `size`。
     */
    std::size_t read(void* data, std::size_t size) noexcept {
        return lwrb_read(&handle_, data, size);
    }

    /* --------------------------------------------------------------------------
     *  状态查询
     * -------------------------------------------------------------------------- */

    /**
     * \brief   获取当前可供读取的字节数。
     * \return  缓冲区中可读取的字节数。
     */
    std::size_t available() const noexcept {
        return lwrb_get_full(&handle_);
    }

    /**
     * \brief   获取当前可供写入的空闲字节数。
     * \return  缓冲区中空闲字节数（还可写入的字节数）。
     */
    std::size_t free_space() const noexcept {
        return lwrb_get_free(&handle_);
    }

    /**
     * \brief   获取缓冲区的总容量。
     *
     * 注意：由于内部实现限制，任意时刻最多可存储的字节数为 (capacity() - 1)。
     *
     * \return  缓冲区总大小（字节数）。
     */
    std::size_t capacity() const noexcept {
        return handle_.size;
    }

    /**
     * \brief   检查缓冲区是否已初始化并可用。
     * \return  缓冲区就绪返回 `true`，否则返回 `false`。
     */
    bool is_ready() const noexcept {
        return lwrb_is_ready(const_cast<lwrb_t*>(&handle_)) != 0;
    }

    /**
     * \brief   检查缓冲区是否为空（无数据可读）。
     * \return  为空返回 `true`，否则返回 `false`。
     */
    bool is_empty() const noexcept {
        return available() == 0;
    }

    /**
     * \brief   检查缓冲区是否已满（无空间可写）。
     * \return  已满返回 `true`，否则返回 `false`。
     */
    bool is_full() const noexcept {
        return free_space() == 0;
    }

    /* --------------------------------------------------------------------------
     *  操作
     * -------------------------------------------------------------------------- */

    /**
     * \brief   重置缓冲区，所有已存储的数据将丢失。
     *
     * \note    此操作非线程安全，请确保没有并发的读/写操作在执行。
     */
    void reset() noexcept {
        lwrb_reset(&handle_);
    }

    /**
     * \brief   窥视缓冲区中的数据而不消耗数据。
     *
     * 从缓冲区读取数据但不移动读指针。
     *
     * \param[in]   skip_count: 窥视前要跳过的字节数。
     * \param[out]  data:       用于存放窥视数据的目标缓冲区。
     * \param[in]   size:       期望窥视的字节数。
     * \return  实际窥视到的字节数，可能小于 `size`。
     */
    std::size_t peek(std::size_t skip_count, void* data, std::size_t size) const noexcept {
        return lwrb_peek(&handle_, skip_count, data, size);
    }

    /**
     * \brief   以覆盖方式写入数据。
     *
     * 如果空闲空间不足，将自动丢弃缓冲区中最旧的数据以腾出空间。
     *
     * \param[in]   data: 指向源数据的指针。
     * \param[in]   size: 期望写入的字节数。
     * \return  始终返回实际写入的字节数。除非 `size` 超过缓冲区总容量，否则等于 `size`。
     */
    std::size_t overwrite(const void* data, std::size_t size) noexcept {
        return lwrb_overwrite(&handle_, data, size);
    }

    /**
     * \brief   丢弃（跳过）最多 `len` 字节的可读数据。
     *
     * 直接前进读指针而不拷贝数据。
     *
     * \param[in]   len: 要跳过的字节数。
     * \return  实际跳过的字节数。
     */
    std::size_t skip(std::size_t len) noexcept {
        return lwrb_skip(&handle_, len);
    }

    /* --------------------------------------------------------------------------
     *  线性（零拷贝）访问 — 读取
     * -------------------------------------------------------------------------- */

    /**
     * \brief   获取用于零拷贝读取的线性读地址。
     * \return  指向第一个可读字节的指针，或 NULL。
     */
    const void* linear_read_address() const noexcept {
        return lwrb_get_linear_block_read_address(&handle_);
    }

    void* linear_read_address() noexcept {
        return lwrb_get_linear_block_read_address(&handle_);
    }

    /**
     * \brief   获取连续可读的块长度。
     *
     * 表示可从 linear_read_address() 返回的地址连续读取（无需绕回）的字节数。
     *
     * \return  线性读取块长度（字节数）。
     */
    std::size_t linear_read_length() const noexcept {
        return lwrb_get_linear_block_read_length(&handle_);
    }

    /* --------------------------------------------------------------------------
     *  线性（零拷贝）访问 — 写入
     * -------------------------------------------------------------------------- */

    /**
     * \brief   获取用于零拷贝写入的线性写地址。
     * \return  指向第一个可写字节的指针，或 NULL。
     */
    void* linear_write_address() noexcept {
        return lwrb_get_linear_block_write_address(&handle_);
    }

    /**
     * \brief   获取连续可写的块长度。
     *
     * 表示可从 linear_write_address() 返回的地址连续写入（无需绕回）的字节数。
     *
     * \return  线性写入块长度（字节数）。
     */
    std::size_t linear_write_length() const noexcept {
        return lwrb_get_linear_block_write_length(&handle_);
    }

    /**
     * \brief   在零拷贝写入后前进写指针。
     *
     * 在向 linear_write_address() 获取的地址直接写入数据后调用此函数，
     * 以通知缓冲区有新的数据写入。
     *
     * \param[in]   len: 已写入的字节数。
     * \return  写指针实际前进的字节数。
     */
    std::size_t advance(std::size_t len) noexcept {
        return lwrb_advance(&handle_, len);
    }

    /* --------------------------------------------------------------------------
     *  查找
     * -------------------------------------------------------------------------- */

    /**
     * \brief   在缓冲区中查找字节序列。
     *
     * \param[in]   data:         要搜索的字节序列。
     * \param[in]   len:          字节序列的长度。
     * \param[in]   start_offset: 在已缓冲数据中的起始偏移量。
     * \param[out]  found_idx:    接收找到的位置索引（相对于可用数据）。
     * \return  找到返回 `true`，否则返回 `false`。
     */
    bool find(const void* data, std::size_t len,
              std::size_t start_offset, std::size_t& found_idx) const noexcept {
        lwrb_sz_t idx = 0;
        const uint8_t ret = lwrb_find(&handle_, data, len, start_offset, &idx);
        if (ret) {
            found_idx = static_cast<std::size_t>(idx);
        }
        return ret != 0;
    }

    /* --------------------------------------------------------------------------
     *  扩展写入 / 读取（带标志位）
     * -------------------------------------------------------------------------- */

    /**
     * \brief   带扩展选项的写入。
     *
     * 支持 LWRB_FLAG_WRITE_ALL 等标志，确保只在所有 `size` 字节
     * 都能写入时才执行写入操作。
     *
     * \param[in]   data:  源数据。
     * \param[in]   size:  期望写入的字节数。
     * \param[in]   flags: LWRB_FLAG_* 标志的组合。
     * \return  实际写入的字节数，失败时返回 0。
     */
    std::size_t write_ex(const void* data, std::size_t size, std::uint16_t flags) noexcept {
        lwrb_sz_t written = 0;
        if (lwrb_write_ex(&handle_, data, size, &written, flags)) {
            return static_cast<std::size_t>(written);
        }
        return 0;
    }

    /**
     * \brief   带扩展选项的读取。
     *
     * 支持 LWRB_FLAG_READ_ALL 等标志，确保只在所有 `size` 字节
     * 都可用时才执行读取操作。
     *
     * \param[out]  data:  目标缓冲区。
     * \param[in]   size:  期望读取的字节数。
     * \param[in]   flags: LWRB_FLAG_* 标志的组合。
     * \return  实际读取的字节数，失败时返回 0。
     */
    std::size_t read_ex(void* data, std::size_t size, std::uint16_t flags) noexcept {
        lwrb_sz_t read = 0;
        if (lwrb_read_ex(&handle_, data, size, &read, flags)) {
            return static_cast<std::size_t>(read);
        }
        return 0;
    }

    /* --------------------------------------------------------------------------
     *  事件回调
     * -------------------------------------------------------------------------- */

    /**
     * \brief   设置事件回调函数。
     *
     * 每次读取、写入和重置操作时都会调用该回调。
     *
     * \param[in]   fn: 指向事件回调函数的指针，传入 NULL 可清除回调。
     */
    void set_event_callback(lwrb_evt_fn fn) noexcept {
        lwrb_set_evt_fn(&handle_, fn);
    }

    /**
     * \brief   设置可在事件回调中访问的用户参数。
     * \param[in]   arg: 指向任意用户数据的指针。
     */
    void set_arg(void* arg) noexcept {
        lwrb_set_arg(&handle_, arg);
    }

    /**
     * \brief   获取之前通过 set_arg() 设置的用户参数。
     * \return  指向用户参数的指针，未设置时返回 NULL。
     */
    void* get_arg() noexcept {
        return lwrb_get_arg(&handle_);
    }

    /* --------------------------------------------------------------------------
     *  原生句柄访问（用于直接调用 C API 函数）
     * -------------------------------------------------------------------------- */

    /**
     * \brief   获取底层 lwrb_t 结构体的指针。
     * \return  指向原生 lwrb_t 句柄的指针。
     */
    lwrb_t* native_handle() noexcept {
        return &handle_;
    }

    const lwrb_t* native_handle() const noexcept {
        return &handle_;
    }

    /* --------------------------------------------------------------------------
     *  类型安全辅助方法
     * -------------------------------------------------------------------------- */

    /**
     * \brief   将一个对象写入缓冲区。
     *
     * \tparam          T: 要写入的对象类型，必须是可平凡拷贝的。
     * \param[in]       obj: 要写入的对象。
     * \return  完整写入返回 `true`，否则返回 `false`。
     */
    template <typename T,
              typename = std::enable_if_t<std::is_trivially_copyable_v<T>>>
    bool push_back(const T& obj) noexcept {
        return lwrb_write(&handle_, &obj, sizeof(T)) == sizeof(T);
    }

    /**
     * \brief   从缓冲区读取一个对象。
     *
     * \tparam          T: 要读取的对象类型，必须是可平凡拷贝的。
     * \param[out]      obj: 接收从缓冲区读取的对象。
     * \return  完整读取返回 `true`，否则返回 `false`。
     */
    template <typename T,
              typename = std::enable_if_t<std::is_trivially_copyable_v<T>>>
    bool pop_front(T& obj) noexcept {
        return lwrb_read(&handle_, &obj, sizeof(T)) == sizeof(T);
    }

private:
    lwrb_t handle_;         /*!< 底层 C 环形缓冲区结构体 */
    bool    owns_buffer_;   /*!< 是否自行分配了缓冲区内存 */
};

} /* namespace lwrbpp */

#endif /* LWRBPP_H_HDR */
