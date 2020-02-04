#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <fmt/format.h>

#include <lfp/protocol.hpp>
#include <lfp/tapeimage.h>

namespace lfp { namespace {

class tapeimage : public lfp_protocol {
public:
    tapeimage(lfp_protocol*);

    // TODO: there must be a "reset" semantic for when there's a read error to
    // put it back into a valid state

    void close() noexcept (false) override;
    lfp_status readinto(void* dst, std::int64_t len, std::int64_t* bytes_read)
        noexcept (false) override;

    int eof() const noexcept (true);

    void seek(std::int64_t)   noexcept (false) override;
    std::int64_t tell() const noexcept (false) override;

private:
    static constexpr const std::uint32_t record = 0;
    static constexpr const std::uint32_t file   = 1;

    struct header {
        std::uint32_t type;
        std::uint32_t prev;
        std::uint32_t next;

        static constexpr const int size = 12;
    };

    unique_lfp fp;
    std::vector< header > markers;
    struct cursor : public std::vector< header >::const_iterator {
        using std::vector< header >::const_iterator::const_iterator;

        std::int64_t remaining = 0;
    };
    /*
     * The current record - it's an iterator to be able to move between
     * already-index'd records, and detect when the next needs to be read.
     *
     * When remaining == 0, the current record is exhausted.
     */
    cursor current;

    std::int64_t readinto(void* dst, std::int64_t) noexcept (false);
    void append(const header&) noexcept (false);
    void read_header() noexcept (false);
    void read_header(cursor) noexcept (false);
    bool is_indexed(std::int64_t) const noexcept (true);
    void seek_with_index(std::int64_t) noexcept (false);

    lfp_status recovery = LFP_OK;
};

tapeimage::tapeimage(lfp_protocol* f) : fp(f) {
    /*
     * The real risks here is that the I/O device is *very* slow or blocked,
     * and won't yield the 12 first bytes, but instead something less. This is
     * currently not handled here, nor in the read_header, but the chance of it
     * happening it he real world is quite slim.
     *
     * TODO: Should inspect error code, and determine if there's something to
     * do or more accurately report, rather than just 'failure'. At the end of
     * the day, though, the only way to properly determine what's going on is
     * to interrogate the underlying handle more thoroughly.
     */
    this->read_header();
}

void tapeimage::close() noexcept (false) {
    assert(this->fp);
    this->fp.close();
}

lfp_status tapeimage::readinto(
        void* dst,
        std::int64_t len,
        std::int64_t* bytes_read)
noexcept (false) {
    if (std::numeric_limits<std::uint32_t>::max() < len)
        throw invalid_args("len > uint32_max");

    const auto n = this->readinto(dst, len);
    assert(n <= len);

    if (bytes_read)
        *bytes_read = n;

    if (this->recovery)
        return this->recovery;

    if (n < len)
        return LFP_OKINCOMPLETE;

    return LFP_OK;
}

std::int64_t tapeimage::readinto(void* dst, std::int64_t len) noexcept (false) {
    assert(this->current.remaining >= 0);
    assert(not this->markers.empty());
    std::int64_t bytes_read = 0;

    while (true) {
        if (this->eof())
            return bytes_read;

        if (this->current.remaining == 0) {
            /*
             * There has been a backwards seek, and the current record is now
             * exhausted. Conceptually, just read the header, but pull it from
             * the index instead of disk.
             */
            this->read_header(this->current);

            /* might be EOF, or even empty records, so re-start  */
            continue;
        }

        assert(this->current.remaining >= 0);
        std::int64_t n;
        const auto to_read = std::min(len, this->current.remaining);
        const auto err = this->fp->readinto(dst, to_read, &n);
        assert(err == LFP_OKINCOMPLETE ? (n < to_read) : true);

        this->current.remaining -= n;
        bytes_read += n;
        dst = advance(dst, n);

        if (err == LFP_OKINCOMPLETE)
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

void tapeimage::read_header(cursor cur) noexcept (false) {
    // TODO: Make this a runtime check?
    assert(this->current.remaining == 0);
    /*
     * The next record has not been index'd yet, so read it from disk
     */
    if (std::next(cur) == std::end(this->markers))
        return this->read_header();

    /*
     * The record *has* been index'd, so just reposition the underlying stream
     * and update the internal state
     */
    const auto tell = cur->next + header::size;
    this->fp->seek(tell);
    this->current = std::next(cur);
    this->current.remaining = this->current->next - tell;
}

// TODO: status instead of boolean?
int tapeimage::eof() const noexcept (true) {
    assert(not this->markers.empty());
    // TODO: consider when this says record, but phyiscal file is EOF
    // TODO: end-of-file is an _empty_ record, i.e. two consecutive tape marks
    return this->current->type == tapeimage::file;
}

void tapeimage::read_header() noexcept (false) {
    assert(this->current     == this->markers.end() or
           this->current + 1 == this->markers.end());

    std::int64_t n;
    unsigned char b[sizeof(std::uint32_t) * 3];
    const auto err = this->fp->readinto(b, sizeof(b), &n);

    /* TODO: should also check INCOMPLETE */
    switch (err) {
        case LFP_OK: break;

        case LFP_OKINCOMPLETE:
            if (this->fp->eof()) {
                const auto msg = "tapeimage: unexpected EOF when reading header "
                                 "- got {} bytes";
                throw protocol_fatal(fmt::format(msg, n));
            }
            /* For now, don't try to recover from this - if it is because the
             * read was paused (stream blocked, for example) then it can be
             * recovered from later
             */
            throw protocol_failed_recovery(
                "tapeimage: incomplete read of tapeimage header, "
                "recovery not implemented"
            );

        default:
            throw not_implemented(
                "tapeimage: unhandled error code in read_header"
            );
    }

    // Check the makefile-provided IS_BIG_ENDIAN, or the one set by gcc
    #if (IS_BIG_ENDIAN || __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
        std::reverse(b + 0, b + 4);
        std::reverse(b + 4, b + 8);
        std::reverse(b + 8, b + 12);
    #endif
    header head;
    std::memcpy(&head.type, b + 0 * 4, 4);
    std::memcpy(&head.prev, b + 1 * 4, 4);
    std::memcpy(&head.next, b + 2 * 4, 4);

    if (head.next <= head.prev) {
        /*
         * There's no reasonable recovery if next is smaller than prev, as it's
         * likely either the previous pointer which is broken, or this entire
         * header.
         *
         * At least for now, consider it a non-recoverable error.
         */
        const auto msg = "tapeimage: head.next (= {}) <= head.prev (= {})";
        throw protocol_fatal(fmt::format(msg, head.next, head.prev));
    }

    if (not this->markers.empty()) {
        /*
         * backpointer is not consistent with this header's previous - this is
         * recoverable, under the assumption it's the *back pointer* that is
         * wrong.
         *
         * The back pointer is patched by just assuming the previous was ok,
         * but only in memory - to be sure, the file needs to be walked
         * back-to-front, but that's out-of-scope for now
         *
         * TODO: should taint the handle, unless explicitly cleared
         */
        if (head.type == 0 and head.prev != this->markers.back().next) {
            this->recovery = LFP_PROTOCOL_TRYRECOVERY;
            head.prev = this->markers.back().next;
        }
    }

    if (head.type != tapeimage::record and head.type != tapeimage::file) {
        /*
         * probably recoverable *if* this is the only error - maybe someone
         * wrote the wrong record type by accident, or simply use some
         * extension with more record types for semantics.
         *
         * If it's the only error in this record, recover by ignoring it and
         * pretend it's a record (= 0) type.
         */
        if (this->recovery) {
            const auto msg = "tapeimage: unknown head.type in recovery, "
                             "file probably corrupt";
            throw protocol_failed_recovery(msg);
        }
        this->recovery = LFP_PROTOCOL_TRYRECOVERY;
    }

    this->append(head);
    const auto tell = this->markers.size() == 1
                    ? header::size
                    : this->current->next + header::size;

    this->current = std::prev(this->markers.end());
    this->current.remaining = head.next - tell;
}

void tapeimage::seek_with_index(std::int64_t n) noexcept (false) {
    decltype(this->current.remaining) base = 0;
    auto end = std::find_if(this->markers.begin(), this->markers.end(),
        [&base, n](const header& head) noexcept (true) {
            base += 12;
            return head.next > base + n;
        }
    );

    // TODO: check runtime too?
    assert(end < this->markers.end());

    const auto preceeding = 1 + std::distance(this->markers.begin(), end);
    const auto real_offset = n + (preceeding * header::size);
    this->fp->seek(real_offset);
    this->current = end;
    this->current.remaining = end->next - real_offset;
}

void tapeimage::seek(std::int64_t n) noexcept (false) {
    assert(not this->markers.empty());
    assert(n >= 0);
    if (n < 0)
        throw invalid_args("seek offset n < 0");

    /*
     * Have we already index'd the right section? If so, use it and seek there.
     */
    if (this->is_indexed(n)) {
        return this->seek_with_index(n);
    }

    /*
     * The target is past the already-index'd records, so follow the headers,
     * and index them as we go.
     */
    auto preceeding = this->markers.size();
    this->current = std::prev(this->markers.end());
    while (true) {
        const auto& head = this->markers.back();
        if (head.next > n + preceeding * header::size) {
            // TODO: maybe reposition directly *or* refactor out proper
            return this->seek(n);
        }

        if (head.type == tapeimage::file) {
            throw protocol_fatal(
                "tapeimage: segment type is file, expected record"
            );
        }

        this->fp->seek(head.next);
        this->read_header();
        ++preceeding;
    }
}

std::int64_t tapeimage::tell() const noexcept (false) {
    const auto real_offset = this->fp->tell();

    assert(not this->markers.empty());
    const auto begin          = std::begin(this->markers);
    const decltype(begin) end = std::next(this->current);
    const auto preceeding = std::distance(begin, end);
    assert(preceeding >= 0);

    return real_offset - preceeding*header::size;
}

bool tapeimage::is_indexed(std::int64_t n) const noexcept (true) {
    const auto last = this->markers.back().next;
    const auto header_contrib = this->markers.size() * header::size;
    return last > n + header_contrib;
}

void tapeimage::append(const header& head) noexcept (false) try {
    const auto size = std::int64_t(this->markers.size());
    const auto n = std::max(size - 1, std::int64_t(0));
    this->markers.push_back(head);
    this->current = this->markers.begin() + n;
} catch (...) {
    throw runtime_error("tapeimage: unable to store header");
}

}

}

lfp_protocol* lfp_tapeimage_open(lfp_protocol* f) {
    if (not f) return nullptr;

    try {
        return new lfp::tapeimage(f);
    } catch (...) {
        return nullptr;
    }
}
