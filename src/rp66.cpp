#include <cassert>
#include <limits>
#include <vector>

#include <fmt/format.h>

#include <lfp/protocol.hpp>
#include <lfp/rp66.h>

namespace lfp { namespace {

struct header {
    std::uint16_t  length;
    unsigned char  format;
    std::uint8_t   major;

    /*
     * Visible Records do not contain information about their own initial
     * offset into the file. That makes the mapping between physical- and
     * logical- offsets rather cumbersome. Calculating the offset of a record
     * can be quite expensive, as it's basically the sum of all previous record
     * lengths. Thus headers are augmented to include their physical offset.
     */
    std::int64_t base = 0;

    /*
     * Reflects the *actual* number of bytes in the Visible Record Header,
     * defined here as the VE part of the VR. That is, Visible Record
     * Length and Format Version.
     */
    static constexpr const int size = 4;
};

/**
 * Address translator between physical offsets (provided by the underlying
 * layer) and logical offsets (presented to the user).
 */
class address_map {
public:
    address_map() = default;
    explicit address_map(std::int64_t z) : zero(z) {}

    /**
     * Get the logical address from the physical address, i.e. the one reported
     * by rp66::tell(), in the bytestream with no interleaved headers.
     */
    std::int64_t logical(std::int64_t addr, int record) const noexcept (true);
    /**
     * Get the physical address from the logical address, i.e. the address with
     * headers accounted for.
     *
     * Warning
     * -------
     *  This function assumes the physical address within record.
     */
    std::int64_t physical(std::int64_t addr, int record) const noexcept (true);

    /**
     * Base address of the map, i.e. the first possible address. This is
     * usually, but not guaranteed to be, zero.
     */
    std::int64_t base() const noexcept (true);

private:
    std::int64_t zero = 0;
};

class rp66 : public lfp_protocol {
public:
    rp66(lfp_protocol*);

    // TODO: there must be a "reset" semantic for when there's a read error to
    // put it back into a valid state

    void close() noexcept (false) override;
    lfp_status readinto(void* dst, std::int64_t len, std::int64_t* bytes_read)
        noexcept (false) override;

    int eof() const noexcept (true) override;
    std::int64_t tell() const noexcept (true) override;
    void seek(std::int64_t) noexcept (false) override;
    lfp_protocol* peel() noexcept (false) override;
    lfp_protocol* peek() const noexcept (false) override;

private:
    unique_lfp fp;
    address_map addr;
    std::vector< header > markers;
    struct cursor : public std::vector< header >::const_iterator {
        using std::vector< header >::const_iterator::const_iterator;

        std::int64_t remaining = 0;
    };

    cursor current;

    std::int64_t readinto(void*, std::int64_t) noexcept (false);
    void read_header() noexcept (false);
    void read_header_from_disk() noexcept (false);
    void append(const header&) noexcept (false);
    void seek_with_index(std::int64_t) noexcept (false);
};

std::int64_t
address_map::logical(std::int64_t addr, int record)
const noexcept (true) {
    return addr - (header::size * (1 + record)) - this->zero;
}

std::int64_t
address_map::physical(std::int64_t addr, int record)
const noexcept (true) {
    return addr + (header::size * (1 + record)) + this->zero;
}

std::int64_t address_map::base() const noexcept (true) {
    return this->zero;
}

rp66::rp66(lfp_protocol* f) : fp(f) {
    /*
     * The real risk here is that the I/O device is *very* slow or blocked, and
     * won't yield the 4 first bytes, but instead something less. This is
     * currently not handled here, nor in the read_header_from_disk, but the
     * chance of it happening it the real world is quite slim.
     *
     * TODO: Should inspect error code, and determine if there's something to
     * do or more accurately report, rather than just 'failure'. At the end of
     * the day, though, the only way to properly determine what's going on is
     * to interrogate the underlying handle more thoroughly.
     */
    try {
        this->addr = address_map(this->fp->tell());
    } catch (...) {
        this->addr = address_map();
    }

    try {
        this->read_header_from_disk();
    } catch (...) {
        this->fp.release();
        throw;
    }
}

void rp66::close() noexcept (false) {
    if(!this->fp) return;
    this->fp.close();
}

lfp_protocol* rp66::peel() noexcept (false) {
    assert(this->fp);
    return this->fp.release();
}

lfp_protocol* rp66::peek() const noexcept (false) {
    assert(this->fp);
    return this->fp.get();
}

lfp_status rp66::readinto(
        void* dst,
        std::int64_t len,
        std::int64_t* bytes_read)
noexcept (false) {
    const auto n = this->readinto(dst, len);
    assert(n <= len);

    if (bytes_read) *bytes_read = n;

    if (n == len)
        return LFP_OK;

    if (this->eof())
        return LFP_EOF;
    else
        return LFP_OKINCOMPLETE;
}

int rp66::eof() const noexcept (true) {
    assert(not this->markers.empty());
    /*
     * There is no trailing header information. I.e. the end of the last
     * Visible Record *should* align with EOF from the underlying file handle.
     * If not, the VR is either truncated or there are some garbage bytes at
     * the end.
     */
    return this->fp->eof();
}

std::int64_t rp66::tell() const noexcept (true) {
    const auto pos = this->current - this->markers.begin();
    return this->addr.logical(this->fp->tell(), pos);
}

void rp66::seek(std::int64_t n) noexcept (false) {
    assert(not this->markers.empty());
    /*
     * Have we already index'd the right section? If so, use it and seek there.
     */
    const auto last = this->markers.back().base + this->markers.back().length;
    auto logical = this->addr.logical(last, this->markers.size() - 1);

    if (n <= logical) {
        return this->seek_with_index(n);
    }
    /*
     * target is past the already-index'd records, so follow the headers, and
     * index them as we go
     */

    this->current = std::prev(this->markers.end());

    std::int64_t physical = this->addr.physical(logical, this->markers.size() - 1);
    while (n > logical) {
        this->fp->seek( physical );
        this->read_header_from_disk();
        logical += (this->current->length - header::size);
        physical += this->current->length;
    }

    const auto remaining = logical - n;
    physical -= remaining;
    this->fp->seek( physical );
    this->current.remaining = remaining;
}

std::int64_t rp66::readinto(void* dst, std::int64_t len) noexcept (false) {
    assert(this->current.remaining >= 0);
    assert(not this->markers.empty());
    std::int64_t bytes_read = 0;

    while (true) {
        if (this->eof())
            return bytes_read;
        if (this->current.remaining == 0) {
            this->read_header();

            /* might be EOF, or even empty records, so re-start  */
            continue;
        }

        assert(this->current.remaining >= 0);
        std::int64_t n;
        const auto to_read = std::min(len, this->current.remaining);
        const auto err = this->fp->readinto(dst, to_read, &n);

        this->current.remaining -= n;
        bytes_read      += n;
        dst = advance(dst, n);

        if (err == LFP_OKINCOMPLETE)
            return bytes_read;

        if (err == LFP_EOF and this->current.remaining > 0) {
            const auto msg = "rp66: unexpected EOF when reading record "
                             "- got {} bytes, expected there to be {} more";
            throw unexpected_eof(fmt::format(msg, n, this->current.remaining));
        }

        if (err == LFP_EOF and this->current.remaining == 0)
            return bytes_read;

        assert(err == LFP_OK);

        if (n == len)
            return bytes_read;
        /*
         * The full read was performed, but there's still more requested - move
         * onto the next segment. This differs from when read returns OKINCOMPLETE,
         * in which case the underlying stream is temporarily exhausted or blocked,
         * and fewer bytes than requested could be provided.
         */

        len -= n;
    }
}

void rp66::read_header_from_disk() noexcept (false) {
    assert(this->markers.empty()                    or
           this->current     == this->markers.end() or
           this->current + 1 == this->markers.end());

    std::int64_t n;
    unsigned char b[header::size];
    auto err = this->fp->readinto(b, sizeof(b), &n);
    switch (err) {
        case LFP_OK: break;

        case LFP_OKINCOMPLETE:
            throw protocol_failed_recovery(
                "rp66: incomplete read of Visible Record Header, "
                "recovery not implemented"
            );
        case LFP_EOF:
            /*
             * The end of the *last* Visible Record aligns perfectly with
             * EOF as there are no trailing bytes. Because EOF are typically
             * not recorded before someone tries to read *past* the end, its
             * perfectly fine to exhaust the last VR without EOF being set.
             */
            if (n == 0 && not this->markers.empty())
                return;
            else {
                const auto msg = "rp66: unexpected EOF when reading header "
                                 "- got {} bytes";
                throw protocol_fatal(fmt::format(msg, n));
            }


        default:
            throw not_implemented(
                "rp66: unhandled error code in read_header_from_disk"
            );
    }

    // Check the makefile-provided IS_LITTLE_ENDIAN, or the one set by gcc
    #if (IS_LITTLE_ENDIAN || __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
        std::reverse(b + 0, b + 2);
    #endif

    header head;

    std::memcpy(&head.length, b + 0, sizeof(head.length));
    std::memcpy(&head.format, b + 2, sizeof(head.format));
    std::memcpy(&head.major,  b + 3, sizeof(head.major));

    /*
     * rp66v1 defines that the Format Version should _always_ be [0xFF 0x01].
     * Currently there are no other know applications of Visible Envelope (not
     * to be confused with rp66v2's Visible Record, which is a different
     * format). We therefore make this a strict requirement in the hopes that
     * it will help identify broken- and none VE files.
     */
    if (head.format != 0xFF or head.major != 1) {
        const auto msg = "rp66: Incorrect format version in Visible Record {}";
        throw protocol_fatal( fmt::format(msg, this->markers.size() + 1) );
    }

    std::int64_t base = this->addr.base();
    if ( !this->markers.empty() ) {
        base = this->markers.back().base + this->markers.back().length;
    }

    head.base = base;

    this->append(head);
    this->current = std::prev(this->markers.end());
    this->current.remaining = head.length - header::size;
}


void rp66::read_header() noexcept (false) {
    // TODO: Make this a runtime check?
    assert(this->current.remaining == 0);

    if (std::next(this->current) == std::end(this->markers)) {
        return this->read_header_from_disk();
    }

    /*
     * The record *has* been index'd, so just reposition the underlying stream
     * and update the internal state
     */
    const auto tell = this->current->base + this->current->length + header::size;
    this->fp->seek(tell);
    this->current = std::next(this->current);
    this->current.remaining = this->current->length - header::size;
}

void rp66::seek_with_index(std::int64_t n) noexcept (false) {
    auto current = this->markers.begin();
    std::int64_t logical = this->addr.logical(current->base + current->length, 0);
    while ( logical < n ) {
        current++;
        logical += (current->length - header::size);
    }

    auto remaining = logical - n;
    std::int64_t physical = this->addr.physical(n, current - this->markers.begin());

    this->fp->seek(physical);
    this->current = current;
    this->current.remaining = remaining;
}

void rp66::append(const header& head) noexcept (false) try {
    const auto size = std::int64_t(this->markers.size());
    const auto n = std::max(size - 1, std::int64_t(0));
    this->markers.push_back(head);
    this->current = this->markers.begin() + n;
} catch (...) {
    throw runtime_error("rp66: unable to store header");
}

}

}

lfp_protocol* lfp_rp66_open(lfp_protocol* f) {
    if (not f) return nullptr;

    try {
        return new lfp::rp66(f);
    } catch (...) {
        return nullptr;
    }
}
