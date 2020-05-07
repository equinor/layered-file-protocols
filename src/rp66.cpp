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

/*
 * The record headers already read by rp66, stored in an order
 * (lower-address first fashion).
 */
class record_index : public std::vector< header > {
    using base = std::vector< header >;

public:
    using iterator = base::const_iterator;

    void set(const address_map&) noexcept (true);
    /*
     * Check if the logical address offset n is already indexed. If it is, then
     * find() will be defined, and return the correct record.
     */
    bool contains(std::int64_t n) const noexcept (true);

    /*
     * Find the record header that contains the logical offset n. Behaviour is
     * undefined if contains(n) is false.
     */
    iterator find(std::int64_t n) const noexcept (false);

    iterator last() const noexcept (true);

    iterator::difference_type index_of(const iterator&) const noexcept (true);

private:
    address_map addr;
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
    record_index index;
    struct cursor : public record_index::const_iterator {
        using base_type = record_index::const_iterator;
        using base_type::base_type;
        cursor() = default;
        explicit cursor(const base_type& cur) : base_type(cur) {}

        std::int64_t remaining = 0;
    };

    cursor current;

    std::int64_t readinto(void*, std::int64_t) noexcept (false);
    void read_header() noexcept (false);
    void read_header_from_disk() noexcept (false);
    void append(const header&) noexcept (false);
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

void record_index::set(const address_map& m) noexcept (true) {
    this->addr = m;
}

bool record_index::contains(std::int64_t n) const noexcept (true) {
    const auto last = this->last();
    return n <= this->addr.logical(last->base + last->length, this->size());
}

record_index::iterator
record_index::find(std::int64_t n) const noexcept (false) {
    assert(this->contains(n));

    auto cur = this->begin();
    while (true) {
        const auto pos = this->index_of(cur);
        const auto off = cur->base + cur->length;

        if (n <= this->addr.logical(off, pos))
            return cur;

        cur++;
    }
}

record_index::iterator
record_index::last() const noexcept (true) {
    return std::prev(this->end());
}

record_index::iterator::difference_type
record_index::index_of(const iterator& itr) const noexcept (true) {
    return std::distance(this->begin(), itr);
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
    this->index.set(this->addr);

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
    assert(not this->index.empty());
    /*
     * There is no trailing header information. I.e. the end of the last
     * Visible Record *should* align with EOF from the underlying file handle.
     * If not, the VR is either truncated or there are some garbage bytes at
     * the end.
     */
    return this->fp->eof();
}

std::int64_t rp66::tell() const noexcept (true) {
    const auto pos = this->index.index_of(this->current);
    return this->addr.logical(this->fp->tell(), pos);
}

void rp66::seek(std::int64_t n) noexcept (false) {
    assert(not this->index.empty());
    /*
     * Have we already index'd the right section? If so, use it and seek there.
     */

    if (this->index.contains(n)) {
        const auto next = this->index.find(n);
        const auto pos  = this->index.index_of(next);
        const auto real_offset = this->addr.physical(n, pos);
        const auto remaining = next->base + next->length - real_offset;

        this->fp->seek(real_offset);
        this->current = cursor(next);
        this->current.remaining = remaining;
        return;
    }
    /*
     * target is past the already-index'd records, so follow the headers, and
     * index them as we go
     */
    while (true) {
        const auto last = this->index.last();
        const auto pos  = this->index.index_of(last);
        const auto real_offset = this->addr.physical(n, pos);
        const auto end = last->base + last->length;

        if (real_offset <= end) {
            this->fp->seek(real_offset);
            this->current = cursor(last);
            this->current.remaining = end - real_offset;
            return;
        }

        this->fp->seek(end);
        this->current = cursor(last);
        this->read_header_from_disk();
        if (this->eof()) return;
    }
}

std::int64_t rp66::readinto(void* dst, std::int64_t len) noexcept (false) {
    assert(this->current.remaining >= 0);
    assert(not this->index.empty());
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
    assert(this->index.empty()                    or
           this->current     == this->index.end() or
           this->current + 1 == this->index.end());

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
            if (n == 0 && not this->index.empty())
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
        throw protocol_fatal( fmt::format(msg, this->index.size() + 1) );
    }

    std::int64_t base = this->addr.base();
    if ( !this->index.empty() ) {
        base = this->index.back().base + this->index.back().length;
    }

    head.base = base;

    this->append(head);
    this->current = std::prev(this->index.end());
    this->current.remaining = head.length - header::size;
}

void rp66::read_header() noexcept (false) {
    // TODO: Make this a runtime check?
    assert(this->current.remaining == 0);

    if (std::next(this->current) == std::end(this->index)) {
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

void rp66::append(const header& head) noexcept (false) try {
    const auto size = std::int64_t(this->index.size());
    const auto n = std::max(size - 1, std::int64_t(0));
    this->index.push_back(head);
    this->current = this->index.begin() + n;
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
