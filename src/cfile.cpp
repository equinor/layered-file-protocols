#include <cassert>
#include <cerrno>
#include <ciso646>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>

#include <lfp/protocol.hpp>
#include <lfp/lfp.h>

namespace lfp { namespace {

/*
 * This is really just an interface adaptor for the C stdlib FILE
 */
class cfile : public lfp_protocol {
public:
    cfile(std::FILE* f) :
        fp(f),
        zero(std::ftell(f)),
        ftell_errmsg(zero != -1 ? "" : std::strerror(errno))
    {}

    void close() noexcept (false) override;
    lfp_status readinto(
            void* dst,
            std::int64_t len,
            std::int64_t* bytes_read)
        noexcept (false) override;

    int eof() const noexcept (false) override;

    void seek(std::int64_t) noexcept (false) override;
    std::int64_t tell() const noexcept (false) override;

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
    long zero = 0;
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
    else
        return LFP_OKINCOMPLETE;
}

int cfile::eof() const noexcept (false) {
    return std::feof(this->fp.get());
}

void cfile::seek(std::int64_t n) noexcept (false) {
    static_assert(
            std::numeric_limits< std::int64_t >::min() ==
            std::numeric_limits< long long >::min()
        and
            std::numeric_limits< std::int64_t >::max() ==
            std::numeric_limits< long long >:: max()
        ,
        "assuming long long is 64-bit. implement seek!"
    );

    if (this->zero == -1)
        throw not_supported(this->ftell_errmsg);

    const auto pos = n + this->zero;
    assert(pos >= 0);
    // TODO: handle fseek failure when pos > limits< long >::max()
    // e.g. by converting to relative seeks
    assert(pos < std::numeric_limits< long >::max());
    const auto err = std::fseek(this->fp.get(), pos, SEEK_SET);
    if (err)
        throw io_error(std::strerror(errno));
}

std::int64_t cfile::tell() const noexcept (false) {
    if (this->zero == -1)
        throw not_supported(this->ftell_errmsg);

    const auto off = std::ftell(this->fp.get());
    if (off == -1)
        throw io_error(std::strerror(errno));
    return off - this->zero;
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
    try {
        return new lfp::cfile(fp);
    } catch (...) {
        return nullptr;
    }
}
