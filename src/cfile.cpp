#include <cassert>
#include <cerrno>
#include <ciso646>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>

#include <fmt/format.h>
#include <stdio.h>

#include <lfp/protocol.hpp>
#include <lfp/lfp.h>

namespace lfp { namespace {

/*
 * As lfp is meant to work with files over 2GB, we have to deal with tell & seek
 * in a special way.
 *
 * Standard functions fseek/ftell accept or return long value. However, long
 * isn't always enough. On certain systems long is not defined as a 64-bit
 * number, but as a 32-bit one. Mainly it happens on Windows, both 64-bit and
 * 32-bit. That means that we must work around it to allow processing big files.
 *
 * Fortunatelly, there exist compiler-specific fseek/ftell methods which we can
 * use instead. By using templates we make sure that compiler-specific functions
 * are used only when standard ones are not enough.
 *
 */

template <typename T>
typename std::enable_if< sizeof(T) != sizeof(long long), std::int64_t >::type
dispatch_tell(std::FILE* fp) {
    #if HAVE_FTELLO
        return ftello(fp);
    #elif HAVE_FTELLI64
        return _ftelli64(fp);
    #else
        static_assert(
            sizeof(T) == sizeof(long long),
            "no 64-bit alternative to ftell() found, and long is too small"
        );
    #endif
    return -1;
}

template <typename T>
typename std::enable_if< sizeof(T) == sizeof(long long), std::int64_t >::type
dispatch_tell(std::FILE* fp) {
    return std::ftell(fp);
}

std::int64_t long_tell(std::FILE* fp) {
    return dispatch_tell< long >(fp);
}

template <typename T,
    typename std::enable_if< sizeof(T) != sizeof(long long), int>::type = 0>
int dispatch_seek(std::FILE* fp, std::int64_t pos) {
    #if HAVE_FSEEKO
        return fseeko(fp, pos, SEEK_SET);
    #elif HAVE_FSEEKI64
        return _fseeki64(fp, pos, SEEK_SET);
    #else
        static_assert(
            sizeof(T) == sizeof(long long),
            "no 64-bit alternative to fseek() found, and long is too small"
        );
    #endif
    return -1;
}

template <typename T,
    typename std::enable_if< sizeof(T) == sizeof(long long), int>::type = 0>
int dispatch_seek(std::FILE* fp, std::int64_t pos) {
    return std::fseek(fp, pos, SEEK_SET);
}

int long_seek(std::FILE* fp, std::int64_t pos) {
    return dispatch_seek< long >(fp, pos);
}

/*
 * This is really just an interface adaptor for the C stdlib FILE
 */
class cfile : public lfp_protocol {
public:
    cfile(std::FILE* f, std::int64_t z) :
        fp(f),
        zero(z),
        ftell_errmsg(zero != -1 ? "" : std::strerror(errno))
    {
        long_seek(f, zero);
    }

    void close() noexcept (false) override;
    lfp_status readinto(
            void* dst,
            std::int64_t len,
            std::int64_t* bytes_read)
        noexcept (false) override;

    int eof() const noexcept (false) override;

    void seek(std::int64_t) noexcept (false) override;
    std::int64_t tell() const noexcept (false) override;
    std::int64_t ptell() const noexcept (false) override;

    lfp_protocol* peel() noexcept (false) override;
    lfp_protocol* peek() const noexcept (false) override;

private:
    struct del {
        void operator () (FILE* f) noexcept (true) {
            if (f) std::fclose(f);
        };
    };

    using unique_file = std::unique_ptr< FILE, del >;
    unique_file fp;
    std::int64_t zero = 0;
    std::string ftell_errmsg;
};

void cfile::close() noexcept (false) {
    /*
     * The file handle will always be closed when the destructor is invoked,
     * but when close is invoked directly, errors will be propagated
     */
    if (!this->fp) return;
    const auto err = std::fclose(this->fp.get());

    if (err)
        throw runtime_error(std::strerror(errno));
    else
        this->fp.release();
}

lfp_status cfile::readinto(
        void* dst,
        std::int64_t len,
        std::int64_t* bytes_read)
noexcept (false) {
    const auto n = std::fread(dst, 1, len, this->fp.get());
    if (bytes_read)
        *bytes_read = n;

    if (n == std::size_t(len))
        return LFP_OK;

    if (this->eof())
        return LFP_EOF;

    if (std::ferror(this->fp.get())) {
        auto msg = "Unable to read from file: {}";
        throw io_error(fmt::format(msg, std::strerror(errno)));
    }

    return LFP_OKINCOMPLETE;
}

int cfile::eof() const noexcept (false) {
    return std::feof(this->fp.get());
}

void cfile::seek(std::int64_t n) noexcept (false) {
    if (this->zero == -1)
        throw not_supported(this->ftell_errmsg);

    const auto pos = n + this->zero;
    assert(pos >= 0);
    const auto err = long_seek(this->fp.get(), pos);
    if (err)
        throw io_error(std::strerror(errno));
}

std::int64_t cfile::ptell() const noexcept (false) {
    if (this->zero == -1)
        throw not_supported(this->ftell_errmsg);

    std::int64_t off = long_tell(this->fp.get());
    if (off == -1)
        throw io_error(std::strerror(errno));
    return off;
}

std::int64_t cfile::tell() const noexcept (false) {
    return this->ptell() - this->zero;
}

lfp_protocol* cfile::peel() noexcept (false) {
    throw lfp::leaf_protocol("peel: not supported for leaf protocol");
}

lfp_protocol* cfile::peek() const noexcept (false) {
    throw lfp::leaf_protocol("peek: not supported for leaf protocol");
}

}

}

lfp_protocol* lfp_cfile(std::FILE* fp) {
    if (!fp) return nullptr;
    return lfp_cfile_open_at_offset(fp, lfp::long_tell(fp));
}

lfp_protocol* lfp_cfile_open_at_offset(std::FILE* fp, std::int64_t zero) {
    if (!fp) return nullptr;
    try {
        return new lfp::cfile(fp, zero);
    } catch (...) {
        return nullptr;
    }
}
