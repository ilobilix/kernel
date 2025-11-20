// Copyright (C) 2024-2025  ilobilo

export module lib:types;

import :log;
import fmt;
import cppstd;

export namespace lib
{
    template<std::size_t N>
    struct bits2uint;

    template<>
    struct bits2uint<8> { using type = std::uint8_t; };

    template<>
    struct bits2uint<16> { using type = std::uint16_t; };

    template<>
    struct bits2uint<32> { using type = std::uint32_t; };

    template<>
    struct bits2uint<64> { using type = std::uint64_t; };

    template<std::size_t N>
    using bits2uint_t = bits2uint<N>::type;

    template<typename ...Funcs>
    struct overloaded : Funcs... { using Funcs::operator()...; };

    template<typename ...Funcs>
    overloaded(Funcs ...) -> overloaded<Funcs...>;

    template<typename>
    class signature;

    template<typename Ret, typename ...Args>
    class signature<Ret(Args...)>
    {
        public:
        using type = Ret(Args...);
        using return_type = Ret;
        using args_type = std::tuple<Args...>;
    };

    template<typename Type>
    struct remove_address_space { using type = Type; };

    template<typename Type, std::size_t N>
    struct remove_address_space<Type __attribute__((address_space(N)))> { using type = Type; };

    template<typename Type>
    struct remove_address_space_helper
    {
        using base_type = typename std::remove_pointer<Type>::type;
        using clean_type = typename remove_address_space<base_type>::type;
        using type = clean_type *;
    };

    template<typename Type>
    struct remove_address_space<Type *> : remove_address_space_helper<Type *> { };

    template<typename Type>
    struct remove_address_space<const Type> { using type = const typename remove_address_space<Type>::type; };

    template<typename Type>
    struct remove_address_space<const Type *> { using type = const typename remove_address_space<Type>::type *; };

    template<typename Type>
    using remove_address_space_t = typename remove_address_space<Type>::type;

    struct user_string
    {
        std::string str;
        explicit user_string(const char __user *ustr);
    };

    template<std::size_t N>
    struct comptime_string
    {
        consteval comptime_string(const char (&str)[N])
        {
            std::copy_n(str, N, value);
        }

        consteval bool is_empty() const
        {
            return N <= 1;
        }

        char value[N];
    };
} // export namespace lib

template<>
struct fmt::formatter<lib::user_string> : fmt::formatter<std::string>
{
    template<typename FormatContext>
    auto format(lib::user_string str, FormatContext &ctx) const
    {
        return formatter<std::string>::format(str.str.empty() ? std::string { "(null)" } : str.str, ctx);
    }
};

export
{
    using time_t = std::int64_t;
    using clockid_t = std::int32_t;
    using pid_t = std::int32_t;
    using rlim_t = std::uint64_t;

    using dev_t = std::uint64_t;
    using ino_t = std::uint64_t;
    using mode_t = std::uint32_t;
    using nlink_t = std::uint64_t;
    using uid_t = std::uint32_t;
    using gid_t = std::uint32_t;
    using dev_t = std::uint64_t;
    using off_t = std::int64_t;
    using blksize_t = std::int64_t;
    using blkcnt_t = std::int64_t;

    enum fmode : mode_t
    {
        s_irwxu = 00700, // user rwx
        s_irusr = 00400, // user r
        s_iwusr = 00200, // user w
        s_ixusr = 00100, // user x
        s_irwxg = 00070, // group rwx
        s_irgrp = 00040, // group r
        s_iwgrp = 00020, // group w
        s_ixgrp = 00010, // group x
        s_irwxo = 00007, // others rwx
        s_iroth = 00004, // others r
        s_iwoth = 00002, // others w
        s_ixoth = 00001, // others x
        s_isuid = 04000, // set-user-id
        s_isgid = 02000, // set-group-id
        s_isvtx = 01000, // set-sticky
        s_iread = s_irusr,
        s_iwrite = s_iwusr,
        s_iexec = s_ixusr
    };

    struct timespec
    {
        time_t tv_sec;
        long tv_nsec;

        constexpr timespec() : tv_sec { 0 }, tv_nsec { 0 } { }

        constexpr timespec(time_t sec, long nsec)
            : tv_sec { sec }, tv_nsec { nsec } { }

        constexpr timespec(std::uint64_t ns)
            : tv_sec { static_cast<time_t>(ns / 1'000'000'000) },
              tv_nsec { static_cast<long>(ns % 1'000'000'000) } { }

        constexpr timespec(const timespec &other) = default;
        constexpr timespec(timespec &&other) = default;

        constexpr timespec &operator=(const timespec &other)= default;
        constexpr timespec &operator=(timespec &&other)= default;

        constexpr timespec &operator+=(const timespec &other)
        {
            tv_sec += other.tv_sec;
            tv_nsec += other.tv_nsec;
            if (tv_nsec >= 1'000'000'000)
            {
                tv_sec += tv_nsec / 1'000'000'000;
                tv_nsec = tv_nsec % 1'000'000'000;
            }
            return *this;
        }

        constexpr timespec &operator-=(const timespec &other)
        {
            tv_sec -= other.tv_sec;
            tv_nsec -= other.tv_nsec;
            if (tv_nsec < 0)
            {
                tv_sec -= 1 + (-tv_nsec) / 1'000'000'000;
                tv_nsec = 1'000'000'000 - ((-tv_nsec) % 1'000'000'000);
            }
            return *this;
        }

        constexpr timespec operator+(const timespec &other) const
        {
            timespec result = *this;
            result += other;
            return result;
        }

        constexpr timespec operator-(const timespec &other) const
        {
            timespec result = *this;
            result -= other;
            return result;
        }

        constexpr auto operator<=>(const timespec &other) const
        {
            if (tv_sec != other.tv_sec)
                return tv_sec <=> other.tv_sec;
            return tv_nsec <=> other.tv_nsec;
        }

        constexpr long to_ns() const
        {
            return tv_sec * 1'000'000'000 + tv_nsec;
        }
    };

    struct stat
    {
        enum type : mode_t
        {
            s_ifmt = 0170000,
            s_ifsock = 0140000,
            s_iflnk = 0120000,
            s_ifreg = 0100000,
            s_ifblk = 0060000,
            s_ifdir = 0040000,
            s_ifchr = 0020000,
            s_ififo = 0010000
        };

        dev_t st_dev;
        ino_t st_ino;
        mode_t st_mode;
        nlink_t st_nlink;
        uid_t st_uid;
        gid_t st_gid;
        dev_t st_rdev;
        off_t st_size;
        blksize_t st_blksize;
        blkcnt_t st_blocks;

        timespec st_atim;
        timespec st_mtim;
        timespec st_ctim;

        static constexpr type type(mode_t mode)
        {
            return static_cast<enum type>(mode & static_cast<mode_t>(type::s_ifmt));
        }

        constexpr enum type type() const
        {
            return type(st_mode);
        }

        static constexpr mode_t mode(mode_t mode)
        {
            return mode & ~static_cast<mode_t>(type::s_ifmt);
        }

        constexpr mode_t mode() const
        {
            return mode(st_mode);
        }

        enum time : std::uint8_t { access = (1 << 0), modify = (1 << 1), status = (1 << 2) };
        void update_time(std::uint8_t flags);
    };
} // export