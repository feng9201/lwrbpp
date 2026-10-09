/**
 * \file            rbpp.h
 * \brief           librb - 环形缓冲消息队列 C++ 封装
 *
 * 在 librb（rb/rb.h）C 语言库的基础上提供现代 C++ RAII 封装。
 * 与 lwrbpp（字节流）不同，librb 是对象/消息队列语义：
 * 缓冲区容量为 count 个定长对象（object_size 字节），
 * 或使用 rb_dynamic 标志存储变长消息。
 *
 * 支持两种内存模式：rb_new 全堆分配（支持 rb_round_count / rb_growable）
 * 与外部传入缓冲区（rb_init，调用者保留缓冲区所有权）。
 *
 * \note            多线程阻塞模式（flag::multithread）仅在非 MSVC 构建可用；
 *                  MSVC 构建以 ENABLE_THREADS=0 编译，传入该标志构造将抛出异常。
 */

#ifndef RBPP_H_HDR
#define RBPP_H_HDR

#include "rb/rb.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace rbpp {

/**
 * \brief   环形缓冲区标志（与 ::rb_flags 一一对应，可位或组合）
 */
enum class flag : int {
    none        = 0,
    peek        = rb_peek,        ///< 读取但不前进读指针，数据保留在缓冲区中
    nonblock    = rb_nonblock,    ///< 创建非阻塞缓冲区（仅多线程模式有意义）
    dontwait    = rb_dontwait,    ///< 单次调用不阻塞（仅多线程模式有意义）
    multithread = rb_multithread, ///< 多线程模式，读/写可阻塞等待（仅非 MSVC 构建）
    round_count = rb_round_count, ///< 容量向上取整到 2 的幂（仅全堆分配构造）
    growable    = rb_growable,    ///< 空间不足时容量自动翻倍（仅全堆分配构造）
    dynamic     = rb_dynamic,     ///< 动态消息模式，单次读取可取出整条变长消息
    continue_   = rb_continue,    ///< claim/commit 后不释放锁（仅多线程模式）
};

/** \brief  标志位或组合 */
constexpr flag operator|(flag a, flag b) noexcept {
    return static_cast<flag>(static_cast<int>(a) | static_cast<int>(b));
}

/** \brief  标志位与 */
constexpr flag operator&(flag a, flag b) noexcept {
    return static_cast<flag>(static_cast<int>(a) & static_cast<int>(b));
}

/** \brief  标志位取反 */
constexpr flag operator~(flag a) noexcept {
    return static_cast<flag>(~static_cast<int>(a));
}

/** \brief  转换为 C API 使用的 ::rb_flags 枚举 */
constexpr enum rb_flags to_c(flag f) noexcept {
    return static_cast<enum rb_flags>(static_cast<int>(f));
}

/**
 * \brief   librb（环形缓冲消息队列）的 C++ RAII 封装
 *
 * 内存所有权语义：
 * - 使用 (count, object_size, flags) 构造时：通过 rb_new() 全堆分配，
 *   析构时由 rb_destroy() 自动释放全部资源。
 * - 使用 (buffer, count, object_size, flags) 构造时：调用者保留缓冲区内存
 *   的所有权，析构时仅清理 rb 对象本身（rb_cleanup()），不释放该内存。
 *
 * 该类仅支持移动语义（禁止拷贝）。移动后，源对象处于有效但空的状态
 * （is_valid() 返回 false）。
 *
 * 错误处理约定与 C API 一致：读写类函数返回负值（-1）表示失败，
 * 具体原因见 errno（如 EAGAIN 缓冲区空/满、ENOBUFS 消息过大等）。
 */
class ring_buffer {
public:
    /**
     * \brief   全堆分配构造（对应 rb_new()）。
     *
     * 所有资源（rb 对象与数据缓冲区）由 librb 分配，析构时自动释放。
     * 此构造允许使用 flag::round_count 与 flag::growable。
     *
     * \param[in]   count: 缓冲区可容纳的对象个数，必须为 2 的幂
     *              （除非指定 flag::round_count）。
     * \param[in]   object_size: 单个对象大小（字节）。
     *              flag::dynamic 模式下表示长度字段宽度，仅允许 1/2/4/8。
     * \param[in]   flags: 构造标志。
     * \throws  std::invalid_argument 参数非法（如 count 非 2 的幂）。
     * \throws  std::bad_alloc 内存分配失败。
     */
    explicit ring_buffer(std::size_t count, std::size_t object_size,
                         flag flags = flag::none)
        : heap_created_(true) {
        rb_ = rb_new(count, object_size, to_c(flags));
        if (rb_ == nullptr) {
            if (errno == EINVAL) {
                throw std::invalid_argument("rbpp::ring_buffer: 非法的 count/object_size/flags");
            }
            throw std::bad_alloc();
        }
    }

    /**
     * \brief   使用外部传入的内存构造（对应 rb_init()）。
     *
     * 调用者保留缓冲区内存的完全所有权，析构时不会释放该内存。
     * 此构造不允许 flag::round_count 与 flag::growable（rb_init 限制）。
     *
     * \param[in]   buffer: 指向用作存储的内存块，
     *              大小至少为 count * object_size 字节，不能为 NULL。
     * \param[in]   count: 缓冲区可容纳的对象个数，必须为 2 的幂。
     * \param[in]   object_size: 单个对象大小（字节）。
     * \param[in]   flags: 构造标志。
     * \throws  std::invalid_argument 参数非法。
     * \throws  std::bad_alloc 内存分配失败。
     */
    ring_buffer(void* buffer, std::size_t count, std::size_t object_size,
                flag flags = flag::none)
        : heap_created_(false) {
        rb_ = new (std::nothrow) struct rb;
        if (rb_ == nullptr) {
            throw std::bad_alloc();
        }
        if (rb_init(rb_, buffer, count, object_size, to_c(flags)) != 0) {
            delete rb_;
            rb_ = nullptr;
            throw std::invalid_argument("rbpp::ring_buffer: 非法的 buffer/count/object_size/flags");
        }
    }

    /** 禁止拷贝 */
    ring_buffer(const ring_buffer&) = delete;
    ring_buffer& operator=(const ring_buffer&) = delete;

    /**
     * \brief   移动构造函数。
     *
     * 转移 rb 对象所有权；`other` 被置于空状态（is_valid() 返回 false）。
     * \note    多线程模式下，请确保移动时没有线程正阻塞在该对象上。
     */
    ring_buffer(ring_buffer&& other) noexcept
        : rb_(other.rb_), heap_created_(other.heap_created_) {
        other.rb_ = nullptr;
        other.heap_created_ = false;
    }

    /**
     * \brief   移动赋值运算符。
     *
     * 释放当前持有的资源，然后接管 `other` 的所有权。
     */
    ring_buffer& operator=(ring_buffer&& other) noexcept {
        if (this != &other) {
            destroy();
            rb_ = other.rb_;
            heap_created_ = other.heap_created_;
            other.rb_ = nullptr;
            other.heap_created_ = false;
        }
        return *this;
    }

    /**
     * \brief   析构函数。
     *
     * 全堆分配模式调用 rb_destroy() 释放全部资源；
     * 外部内存模式调用 rb_cleanup() 并释放 rb 对象本身，
     * 调用者的缓冲区内存不受影响。
     */
    ~ring_buffer() noexcept {
        destroy();
    }

    /* --------------------------------------------------------------------------
     *  写入 / 读取
     * -------------------------------------------------------------------------- */

    /**
     * \brief   向缓冲区写入最多 count 个对象（对应 rb_write()）。
     * \param[in]   buffer: 源数据指针。
     * \param[in]   count:  期望写入的对象个数。
     * \return  实际写入的对象个数；失败返回 -1（见 errno，如 EAGAIN 缓冲区满）。
     */
    long write(const void* buffer, std::size_t count) noexcept {
        return rb_write(rb_, buffer, count);
    }

    /**
     * \brief   与 write() 相同，但可附加单次调用标志（对应 rb_send()）。
     * \param[in]   flags: 单次调用标志，如 flag::dontwait。
     */
    long send(const void* buffer, std::size_t count, flag flags = flag::none) noexcept {
        return rb_send(rb_, buffer, count, to_c(flags));
    }

    /**
     * \brief   从缓冲区读取最多 count 个对象（对应 rb_read()）。
     * \param[out]  buffer: 目标内存指针。
     * \param[in]   count:  期望读取的对象个数。
     * \return  实际读取的对象个数（dynamic 模式为字节数）；失败返回 -1。
     */
    long read(void* buffer, std::size_t count) noexcept {
        return rb_read(rb_, buffer, count);
    }

    /**
     * \brief   与 read() 相同，但可附加单次调用标志（对应 rb_recv()）。
     * \param[in]   flags: 单次调用标志，如 flag::peek / flag::dontwait。
     */
    long recv(void* buffer, std::size_t count, flag flags = flag::none) noexcept {
        return rb_recv(rb_, buffer, count, to_c(flags));
    }

    /**
     * \brief   类型安全地写入单个对象。
     *
     * \tparam  T 可平凡拷贝类型，其大小应等于构造时的 object_size。
     * \return  写入成功返回 1，失败返回 -1。
     */
    template <typename T>
    long push_back(const T& obj) noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "T 必须是可平凡拷贝类型");
        return write(&obj, 1);
    }

    /**
     * \brief   类型安全地读取单个对象。
     *
     * \tparam  T 可平凡拷贝类型，其大小应等于构造时的 object_size。
     * \return  读取成功返回 1，失败返回 -1。
     */
    template <typename T>
    long pop_front(T& obj) noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "T 必须是可平凡拷贝类型");
        return read(&obj, 1);
    }

    /* --------------------------------------------------------------------------
     *  iovec 散读 / 散写
     * -------------------------------------------------------------------------- */

    /**
     * \brief   从多个缓冲区聚合写入（对应 rb_writev()）。
     *
     * 非 dynamic 模式等价于循环 write()；dynamic 模式将所有缓冲区
     * 拼接为单条消息（全有或全无）。
     */
    long writev(const rb_iovec* vec, int iovcnt) noexcept {
        return rb_writev(rb_, vec, iovcnt);
    }

    /** \brief  writev() 的单次调用标志版本（对应 rb_sendv()）。 */
    long sendv(const rb_iovec* vec, int iovcnt, flag flags = flag::none) noexcept {
        return rb_sendv(rb_, vec, iovcnt, to_c(flags));
    }

    /**
     * \brief   读取并散布到多个缓冲区（对应 rb_readv()）。
     *
     * dynamic 模式下仅取出一条消息并依次填充各缓冲区。
     */
    long readv(const rb_iovec* vec, int iovcnt) noexcept {
        return rb_readv(rb_, vec, iovcnt);
    }

    /** \brief  readv() 的单次调用标志版本（对应 rb_recvv()）。 */
    long recvv(const rb_iovec* vec, int iovcnt, flag flags = flag::none) noexcept {
        return rb_recvv(rb_, vec, iovcnt, to_c(flags));
    }

    /* --------------------------------------------------------------------------
     *  零拷贝 claim / commit
     * -------------------------------------------------------------------------- */

    /**
     * \brief   申请写区域（对应 rb_write_claim()）。
     *
     * 返回可直接写入的内部地址。注意 buffer 有效长度以“对象个数”计，
     * 字节数为 count * object_size。写完后必须调用 write_commit()。
     *
     * \param[out]  buffer: 可写区域起始地址。
     * \param[out]  count:  可写入的最大对象个数。
     * \param[out]  object_size: 单个对象大小（字节）。
     * \return  成功返回 0，失败返回 -1。
     */
    int write_claim(void*& buffer, std::size_t& count, std::size_t& object_size) noexcept {
        return rb_write_claim(rb_, &buffer, &count, &object_size);
    }

    /** \brief  write_claim() 的标志版本（对应 rb_send_claim()）。 */
    int send_claim(void*& buffer, std::size_t& count, std::size_t& object_size,
                   flag flags = flag::none) noexcept {
        return rb_send_claim(rb_, &buffer, &count, &object_size, to_c(flags));
    }

    /**
     * \brief   提交已写入的对象个数（对应 rb_write_commit()）。
     *
     * 在 write_claim()/send_claim() 获取的区域内写入数据后调用，
     * 使数据对消费者可见。可以传 0。
     */
    int write_commit(std::size_t count) noexcept {
        return rb_write_commit(rb_, count);
    }

    /** \brief  write_commit() 的标志版本（对应 rb_send_commit()）。 */
    int send_commit(std::size_t count, flag flags = flag::none) noexcept {
        return rb_send_commit(rb_, count, to_c(flags));
    }

    /**
     * \brief   申请读区域（对应 rb_read_claim()）。
     *
     * 返回可直接读取的内部地址。读完后必须调用 read_commit()。
     */
    int read_claim(const void*& buffer, std::size_t& count, std::size_t& object_size) noexcept {
        void* buf = nullptr;
        int ret = rb_read_claim(rb_, &buf, &count, &object_size);
        buffer = buf;
        return ret;
    }

    /** \brief  read_claim() 的标志版本（对应 rb_recv_claim()）。 */
    int recv_claim(const void*& buffer, std::size_t& count, std::size_t& object_size,
                   flag flags = flag::none) noexcept {
        void* buf = nullptr;
        int ret = rb_recv_claim(rb_, &buf, &count, &object_size, to_c(flags));
        buffer = buf;
        return ret;
    }

    /** \brief  提交已读取的对象个数（对应 rb_read_commit()）。 */
    int read_commit(std::size_t count) noexcept {
        return rb_read_commit(rb_, count);
    }

    /** \brief  read_commit() 的标志版本（对应 rb_recv_commit()）。 */
    int recv_commit(std::size_t count, flag flags = flag::none) noexcept {
        return rb_recv_commit(rb_, count, to_c(flags));
    }

    /**
     * \brief   提交写区域并立即重新申请（对应 rb_send_commit_claim()）。
     *
     * count 为入出参：传入本次提交的对象个数，返回新区域可写的对象个数。
     */
    int send_commit_claim(void*& buffer, std::size_t& count, std::size_t& object_size,
                          flag flags = flag::none) noexcept {
        return rb_send_commit_claim(rb_, &buffer, &count, &object_size, to_c(flags));
    }

    /** \brief  send_commit_claim() 的无标志版本（对应 rb_write_commit_claim()）。 */
    int write_commit_claim(void*& buffer, std::size_t& count, std::size_t& object_size) noexcept {
        return rb_write_commit_claim(rb_, &buffer, &count, &object_size);
    }

    /** \brief  提交读区域并立即重新申请（对应 rb_recv_commit_claim()）。 */
    int recv_commit_claim(const void*& buffer, std::size_t& count, std::size_t& object_size,
                          flag flags = flag::none) noexcept {
        void* buf = nullptr;
        int ret = rb_recv_commit_claim(rb_, &buf, &count, &object_size, to_c(flags));
        buffer = buf;
        return ret;
    }

    /** \brief  recv_commit_claim() 的无标志版本（对应 rb_read_commit_claim()）。 */
    int read_commit_claim(const void*& buffer, std::size_t& count, std::size_t& object_size) noexcept {
        void* buf = nullptr;
        int ret = rb_read_commit_claim(rb_, &buf, &count, &object_size);
        buffer = buf;
        return ret;
    }

    /* --------------------------------------------------------------------------
     *  状态查询与操作
     * -------------------------------------------------------------------------- */

    /**
     * \brief   窥视下一条消息的大小（对应 rb_peek_size()）。
     *
     * 仅在 flag::dynamic 模式下有意义。
     * \return  下一条消息的字节数；失败返回 -1。
     */
    long peek_size() const noexcept {
        return rb_peek_size(rb_);
    }

    /**
     * \brief   获取缓冲区中当前的对象个数（对应 rb_count()）。
     * \return  对象个数；失败返回 -1。
     */
    long count() const noexcept {
        return rb_count(rb_);
    }

    /**
     * \brief   获取缓冲区中空闲的对象个数（对应 rb_space()）。
     * \return  空闲对象个数；失败返回 -1。
     */
    long space() const noexcept {
        return rb_space(rb_);
    }

    /**
     * \brief   丢弃 count 个对象而不拷贝数据（对应 rb_discard()）。
     * \return  实际丢弃的对象个数；失败返回 -1。
     */
    long discard(std::size_t count) noexcept {
        return rb_discard(rb_, count);
    }

    /**
     * \brief   清空缓冲区（对应 rb_clear()）。
     * \param[in]   hard: 为 true 时同时将内存清零（用于清除敏感数据）。
     * \return  成功返回 0，失败返回 -1。
     */
    int clear(bool hard = false) noexcept {
        return rb_clear(rb_, hard ? 1 : 0);
    }

    /**
     * \brief   设置 growable 缓冲区的容量上限（对应 rb_set_hard_max_count()）。
     * \return  成功返回 0，失败返回 -1。
     */
    int set_hard_max_count(std::size_t count) noexcept {
        return rb_set_hard_max_count(rb_, count);
    }

    /**
     * \brief   唤醒所有阻塞中的读/写调用并使其返回错误（对应 rb_stop()）。
     *
     * 仅在多线程模式（非 MSVC 构建 + flag::multithread）下有意义，
     * 用于析构前安全解除其他线程的阻塞。
     * \return  成功返回 0，失败返回 -1。
     */
    int stop() noexcept {
        return rb_stop(rb_);
    }

    /* --------------------------------------------------------------------------
     *  对象状态
     * -------------------------------------------------------------------------- */

    /**
     * \brief   检查对象是否持有有效的 rb 实例。
     *
     * 被移动后的源对象返回 false，此时除析构/移动赋值外不应调用其他方法。
     */
    bool is_valid() const noexcept {
        return rb_ != nullptr;
    }

    /** \brief  同 is_valid()。 */
    explicit operator bool() const noexcept {
        return is_valid();
    }

    /**
     * \brief   获取底层 C API 句柄，用于调用本封装未覆盖的 librb 函数。
     * \return  struct rb 指针，对象无效时为 nullptr。
     */
    struct rb* native_handle() noexcept {
        return rb_;
    }

    const struct rb* native_handle() const noexcept {
        return rb_;
    }

private:
    /** \brief  按持有方式释放资源（可安全调用于空对象） */
    void destroy() noexcept {
        if (rb_ != nullptr) {
            if (heap_created_) {
                rb_destroy(rb_);
            } else {
                rb_cleanup(rb_);
                delete rb_;
            }
            rb_ = nullptr;
        }
    }

    struct rb* rb_ = nullptr;          ///< librb 对象指针
    bool        heap_created_ = false; ///< true: rb_new() 创建；false: rb_init() 创建
};

} /* namespace rbpp */

#endif /* RBPP_H_HDR */
