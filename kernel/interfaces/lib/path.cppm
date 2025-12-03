// Copyright (C) 2024-2025  ilobilo

module;

#include <cwalk.h>

export module lib:path;

import fmt;
import std;

export namespace lib
{
    class path;
    class path_view
    {
        friend class path;

        private:
        std::string_view _str;

        public:
        static constexpr char preferred_separator = '/';

        path_view() = default;

        path_view(const path_view &other) = default;
        path_view(path_view &&other) = default;

        path_view(const auto &source) : _str { source } { }
        path_view(auto &&source) : _str { std::move(source) } { }

        ~path_view() = default;

        path_view &operator=(const path_view &) = default;
        path_view &operator=(path_view &&) = default;

        path_view &operator=(const auto &rhs) { _str = rhs; return *this; }
        path_view &operator=(auto &&rhs) { _str = std::move(rhs); return *this; }

        path_view basename() const
        {
            const char *buffer = nullptr;
            std::size_t length = 0;
            cwk_path_get_basename(_str.data(), &buffer, &length);

            return std::string_view { buffer, length };
        }

        path_view dirname() const
        {
            std::size_t length = 0;
            cwk_path_get_dirname(_str.data(), &length);

            return _str.substr(0, length);
        }

        path_view root() const
        {
            std::size_t length = 0;
            cwk_path_get_root(_str.data(), &length);

            return _str.substr(0, length);
        }

        bool is_absolute() const
        {
            return cwk_path_is_absolute(_str.data());
        }

        bool is_relative() const
        {
            return cwk_path_is_relative(_str.data());
        }

        path relative(const path_view &base) const;
        path absolute(const path_view &base) const;

        bool has_extension() const
        {
            return cwk_path_has_extension(_str.data());
        }

        path_view extension() const
        {
            if (_str.empty() || _str.back() == preferred_separator)
                return "";

            const char *extension = nullptr;
            std::size_t length = 0;
            cwk_path_get_extension(_str.data(), &extension, &length);

            return std::string_view { extension, length };
        }

        auto data() const { return _str.data(); }
        auto size() const { return _str.size(); }
        auto length() const { return _str.length(); }

        [[nodiscard]] bool empty() const { return _str.empty(); }

        auto begin() const { return _str.begin(); }
        auto end() const { return _str.end(); }

        void swap(path_view &rhs)
        {
            using std::swap;
            swap(_str, rhs._str);
        }

        friend void swap(path_view &lhs, path_view &rhs)
        {
            lhs.swap(rhs);
        }

        friend bool operator==(const path_view &lhs, const path_view &rhs)
        {
            return lhs._str.compare(rhs._str) == 0;
        }

        friend auto operator<=>(const path_view &lhs, const path_view &rhs)
        {
            return lhs._str.compare(rhs._str) <=> 0;
        }
    };

    class path
    {
        private:
        std::string _str;

        public:
        static constexpr char preferred_separator = '/';

        path() = default;

        path(const path &other) : _str { other._str } { normalise(); }
        path(path &&other) : _str { std::move(other._str) } { normalise(); }

        path(const path_view &other) : _str { other._str } { normalise(); }
        path(path_view &&other) : _str { std::move(other._str) } { normalise(); }

        path(const std::string &source) : _str { source } { normalise(); }
        path(std::string &&source) : _str { std::move(source) } { normalise(); }

        path(std::string_view source) : _str { source } { normalise(); }
        path(const char *source) : _str { source } { normalise(); }

        path(std::size_t n, char c) : _str(n, c) { }

        ~path() = default;

        path &operator=(const path &) = default;
        path &operator=(path &&) = default;

        path &operator=(const path_view &rhs) { _str = rhs._str; return *this; }
        path &operator=(path_view &&rhs) { _str = std::move(rhs._str); return *this; }

        path &operator=(const auto &rhs) { _str = rhs; return *this; }
        path &operator=(auto &&rhs) { _str = std::move(rhs); return *this; }

        path &operator/=(const path &rhs)
        {
            if (_str.back() != preferred_separator)
                _str += preferred_separator;

            _str += rhs._str;
            return *this;
        }

        path &operator/=(const path_view &rhs)
        {
            if (_str.back() != preferred_separator)
                _str += preferred_separator;

            _str += rhs._str;
            return *this;
        }

        path &operator/=(const auto &rhs)
        {
            if (_str.back() != preferred_separator)
                _str += preferred_separator;

            _str += rhs;
            return *this;
        }

        path operator/(const path &rhs) const
        {
            path result { *this };
            result /= rhs;
            return result;
        }

        path operator/(const path_view &rhs) const
        {
            path result { *this };
            result /= rhs;
            return result;
        }

        path operator/(const auto &rhs) const
        {
            path result { *this };
            result /= rhs;
            return result;
        }

        path &operator+=(const path &rhs) { _str += rhs._str; return *this; }
        path &operator+=(const path_view &rhs) { _str += rhs._str; return *this; }
        path &operator+=(const auto &rhs) { _str += rhs; return *this; }

        path_view view() const
        {
            return path_view { _str };
        }

        path basename() const { return view().basename(); }
        path dirname() const { return view().dirname(); }
        path root() const { return view().root(); }

        path &change_basename(const path &newbasename)
        {
            std::size_t length = cwk_path_change_basename(_str.c_str(), newbasename._str.c_str(), nullptr, 0) + 1;
            auto buffer = std::make_unique<char[]>(length);
            cwk_path_change_basename(_str.c_str(), newbasename._str.c_str(), buffer.get(), length);

            _str = buffer.get();
            return *this;
        }

        path &change_root(const path &newroot)
        {
            std::size_t length = cwk_path_change_root(_str.c_str(), newroot._str.c_str(), nullptr, 0) + 1;
            auto buffer = std::make_unique<char[]>(length);
            cwk_path_change_root(_str.c_str(), newroot._str.c_str(), buffer.get(), length);

            _str = buffer.get();
            return *this;
        }

        bool is_absolute() const { return view().is_absolute(); }
        bool is_relative() const { return view().is_relative(); }

        path relative(const path_view &base) const { return view().relative(base); }
        path absolute(const path_view &base) const { return view().absolute(base); }

        bool has_extension() const { return view().has_extension(); }
        path extension() const { return view().extension(); }

        path &change_extension(const path &newext)
        {
            std::size_t length = cwk_path_change_extension(_str.c_str(), newext._str.c_str(), nullptr, 0) + 1;
            auto buffer = std::make_unique<char[]>(length);
            cwk_path_change_extension(_str.c_str(), newext._str.c_str(), buffer.get(), length + 1);

            _str = buffer.get();
            return *this;
        }

        path &normalise()
        {
            std::size_t length = cwk_path_normalize(_str.c_str(), nullptr, 0) + 1;
            auto buffer = std::make_unique<char[]>(length);
            cwk_path_normalize(_str.c_str(), buffer.get(), length);

            _str = buffer.get();
            return *this;
        }

        auto c_str() const { return _str.c_str(); }
        auto &str() { return _str; }

        auto data() const { return _str.data(); }
        auto size() const { return _str.size(); }
        auto length() const { return _str.length(); }

        [[nodiscard]] bool empty() const { return _str.empty(); }

        auto begin() const { return _str.begin(); }
        auto end() const { return _str.end(); }

        void swap(path &rhs)
        {
            using std::swap;
            swap(_str, rhs._str);
        }

        friend void swap(path &lhs, path &rhs)
        {
            lhs.swap(rhs);
        }

        friend bool operator==(const path &lhs, const path &rhs)
        {
            return lhs._str.compare(rhs._str) == 0;
        }

        friend auto operator<=>(const path &lhs, const path &rhs)
        {
            return lhs._str.compare(rhs._str) <=> 0;
        }

        operator std::string() const
        {
            return _str;
        }

        operator std::string_view() const
        {
            return std::string_view { _str };
        }

        operator path_view() const
        {
            return path_view { _str };
        }
    };

    path path_view::relative(const path_view &base) const
    {
        std::size_t length = cwk_path_get_relative(base._str.data(), _str.data(), nullptr, 0) + 1;
        auto buffer = std::make_unique<char[]>(length);
        cwk_path_get_relative(base._str.data(), _str.data(), buffer.get(), length);

        return buffer.get();
    }

    path path_view::absolute(const path_view &base) const
    {
        std::size_t length = cwk_path_get_absolute(base._str.data(), _str.data(), nullptr, 0) + 1;
        auto buffer = std::make_unique<char[]>(length);
        cwk_path_get_absolute(base._str.data(), _str.data(), buffer.get(), length);

        return buffer.get();
    }
} // export namespace lib

template<>
struct fmt::formatter<lib::path_view> : fmt::formatter<std::string_view>
{
    template<typename FormatContext>
    auto format(lib::path_view path, FormatContext &ctx) const
    {
        return formatter<std::string_view>::format(lib::path(path).c_str(), ctx);
    }
};

template<>
struct fmt::formatter<lib::path> : fmt::formatter<std::string>
{
    template<typename FormatContext>
    auto format(lib::path path, FormatContext &ctx) const
    {
        return formatter<std::string>::format(path.c_str(), ctx);
    }
};