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

struct header {
    std::uint32_t type;
    std::uint32_t prev;
    std::uint32_t next;

    static constexpr const int size = 12;
};

/**
 * Address translator between physical offsets (provided by the underlying
 * file) and logical offsets (presented to the user).
 */
class address_map {
public:
    address_map() = default;
    explicit address_map(std::int64_t z) : zero(z) {}

    /**
     * Get the logical address from the physical address, i.e. the one reported
     * by tapeimage::tell(), in the bytestream with no interleaved headers.
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

class record_index : private std::vector< header > {
    using base = std::vector< header >;

public:
    using iterator = base::const_iterator;

    /*
     * Check if the logical address offset n is already indexed. If it is, then
     * find() will be defined, and return the correct record.
     */
    bool contains(std::int64_t n) const noexcept (true);

    /*
     * Find the record header that contains the logical offset n. Behaviour is
     * undefined if contains(n) is false.
     *
     * The hint will always be checked before the index is scanned.
     */
    iterator find(std::int64_t n, iterator hint) const noexcept (false);

    void set(const address_map&) noexcept (true);
    void append(const header&) noexcept (false);

    iterator last() const noexcept (true);
    using base::empty;
    using base::size;

    iterator::difference_type index_of(const iterator&) const noexcept (true);

private:
    address_map addr;
};

/**
 *
 * The read_head class implements parts of the abstraction of a physical file
 * (tape) reader, which moves back and forth.
 *
 * It is somewhat flawed, as it is also an iterator over the record index,
 * which will trigger undefined behaviour when trying to obtain unindexed
 * records.
 */
class read_head : public record_index::iterator {
public:
    /*
     * true if the current record is exhausted. If this is true, then
     * bytes_left() == 0
     */
    bool exhausted() const noexcept (true);
    std::int64_t bytes_left() const noexcept (true);

    using base_type = record_index::iterator;
    read_head() = default;
    read_head(const base_type&, std::int64_t base_addr);

    /*
     * Move the read head within this record. Throws invalid_argument if n >=
     * bytes_left
     */
    void move(std::int64_t n) noexcept (false);

    /*
     * Move the read head to the start of the record provided
     */
    void move(const base_type&) noexcept (true);

    /*
     * Get a read head moved to the start of the next record. Behaviour is
     * undefined if this is the last record in the file.
     */
    read_head next_record() const noexcept (true);

    /*
     * The position of the read head. This should correspond to the offset
     * reported by the underlying file.
     */
    std::int64_t tell() const noexcept (true);

private:
    explicit read_head(const base_type& cur) : base_type(cur) {}

    std::int64_t remaining = -1;
};

class tapeimage : public lfp_protocol {
public:
    tapeimage(lfp_protocol*);

    // TODO: there must be a "reset" semantic for when there's a read error to
    // put it back into a valid state

    void close() noexcept (false) override;
    lfp_status readinto(void* dst, std::int64_t len, std::int64_t* bytes_read)
        noexcept (false) override;

    int eof() const noexcept (true) override;

    void seek(std::int64_t)   noexcept (false) override;
    std::int64_t tell() const noexcept (false) override;
    lfp_protocol* peel() noexcept (false) override;
    lfp_protocol* peek() const noexcept (false) override;

private:
    static constexpr const std::uint32_t record = 0;
    static constexpr const std::uint32_t file   = 1;

    address_map addr;
    unique_lfp fp;
    record_index index;
    read_head current;

    std::int64_t readinto(void* dst, std::int64_t) noexcept (false);
    void read_header_from_disk() noexcept (false);

    lfp_status recovery = LFP_OK;
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

bool record_index::contains(std::int64_t n) const noexcept (true) {
    const auto last = this->last();
    return n < this->addr.logical(last->next, this->size() - 1);
}

record_index::iterator
record_index::find(std::int64_t n, iterator hint) const noexcept (false) {
    /*
     * A real world usage pattern is a lot of small (forward) seeks still
     * within the same record. A lot of time can be saved by not looking
     * through the index when the seek is inside the current record.
     *
     * There are three cases:
     * - Backwards seek, into a different record
     * - Forward or backwards seek within this record
     * - Forward seek, into a different record
     */
    assert(n >= 0);
    const auto in_hint = [this, hint] (std::int64_t n) noexcept (true) {
        const auto pos = this->index_of(hint);
        const auto end = this->addr.logical(hint->next, pos);

        if (pos == 0)
            return n < end;

        const auto begin = this->addr.logical(std::prev(hint)->next, pos - 1);

        return n >= begin and n < end;
    };

    if (in_hint(n)) {
        return hint;
    }

    /**
     * Look up the record containing the logical offset n in the index.
     *
     * seek() is a pretty common operation, and experience from dlisio [1]
     * shows that a poor algorithm here significantly slows down programs.
     *
     * The algorithm actually makes two searches:
     *
     * Phase 1 is an approximating binary search that pretends the logical and
     * phyiscal offset are the same. Since phyiscal offset >= logical offset,
     * we know that the result is always correct or before the correct one in
     * the ordered index.
     *
     * Phase 2 is a linear search from [cur, end) that is aware of the
     * logical/physical offset distinction. Because of the approximation, it
     * should do fairly few hops.
     *
     * The main reason for the two-phase search is that an elements' index is
     * required to compare logical addresses to physical ones, and upper_bound
     * is oblivious to the current item's position.
     *
     * [1] https://github.com/equinor/dlisio
     */

    // phase 1
    const auto addr = this->addr;
    auto less = [addr] (std::int64_t n, const header& h) noexcept (true) {
        return n < addr.logical(h.next, 0);
    };

    const auto lower = std::upper_bound(this->begin(), this->end(), n, less);

    // phase 2
    /*
     * We found the right record when record->next > n. All hits after will
     * also be a match, but this is ok since the search is in an ordered list.
     *
     * Using a mutable lambda to carry the header contribution is a pretty
     * convoluted approach, but both the element *and* the header sizes need to
     * be accounted for, and the latter is only available through the
     * *position* in the index, which doesn't play so well with the std
     * algorithms. The use of lambda + find-if is still valuable though, as it
     * gives a clean error check if the offset n is somehow *not* in the index.
     */
    auto pos = this->index_of(lower);
    auto next_larger = [addr, n, pos] (const header& rec) mutable {
        return n < addr.logical(rec.next, pos++);
    };

    const auto cur = std::find_if(lower, this->end(), next_larger);
    if (cur >= this->end()) {
        const auto msg = "seek: n = {} not found in index, end->next = {}";
        throw std::logic_error(fmt::format(msg, n, this->back().next));
    }

    return cur;
}

void record_index::set(const address_map& m) noexcept (true) {
    this->addr = m;
}

void record_index::append(const header& h) noexcept (false) {
    try {
        this->push_back(h);
    } catch (...) {
        throw runtime_error("tapeimage: unable to store header");
    }
}

record_index::iterator record_index::last() const noexcept (true) {
    assert(not this->empty());
    return std::prev(this->end());
}

record_index::iterator::difference_type
record_index::index_of(const iterator& itr) const noexcept (true) {
    return itr - this->begin();
}

read_head::read_head(const base_type& b, std::int64_t base_addr) :
    base_type(b),
    remaining(b->next - (base_addr + header::size))
{}

bool read_head::exhausted() const noexcept (true) {
    assert(this->remaining >= 0);
    return this->remaining == 0;
}

std::int64_t read_head::bytes_left() const noexcept (true) {
    assert(this->remaining >= 0);
    return this->remaining;
}

void read_head::move(std::int64_t n) noexcept (false) {
    assert(n >= 0);
    assert(this->remaining >= 0);
    if (this->remaining - n < 0)
        throw std::invalid_argument("advancing read_head past end-of-record");

    this->remaining -= n;
}

void read_head::move(const base_type& itr) noexcept (true) {
    assert(this->remaining >= 0);
    /*
     * This is carefully implemented not to reference any this-> members, as
     * the underlying iterator may have been invalidated by an index append.
     * move() is the correct way to position the read_head in a new record.
     */
    const auto base_offset = std::prev(itr)->next + header::size;
    read_head copy(itr);
    copy.remaining = copy->next - base_offset;
    *this = copy;
}

read_head read_head::next_record() const noexcept (true) {
    assert(this->remaining >= 0);
    const auto base = (*this)->next + header::size;
    auto next = std::next(*this);
    next.remaining = next->next - base;
    return next;
}

std::int64_t read_head::tell() const noexcept (true) {
    assert(this->remaining >= 0);
    return (*this)->next - this->remaining;
}

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
    try {
        this->addr = address_map(this->fp->tell());
        this->index.set(this->addr);
    } catch (const lfp::error&) {
        this->addr = address_map();
        this->index.set(this->addr);
    }

    try {
        this->read_header_from_disk();
        this->current = read_head(this->index.last(), this->addr.base());
    } catch (...) {
        this->fp.release();
        throw;
    }
}

void tapeimage::close() noexcept (false) {
    if(!this->fp) return;
    this->fp.close();
}

lfp_protocol* tapeimage::peel() noexcept (false) {
    assert(this->fp);
    return this->fp.release();
}

lfp_protocol* tapeimage::peek() const noexcept (false) {
    assert(this->fp);
    return this->fp.get();
}

lfp_status tapeimage::readinto(
        void* dst,
        std::int64_t len,
        std::int64_t* bytes_read)
noexcept (false) {

    const auto n = this->readinto(dst, len);
    assert(n <= len);

    if (bytes_read)
        *bytes_read = n;

    if (this->recovery)
        return this->recovery;

    if (n == len)
        return LFP_OK;

    if (this->eof())
        return LFP_EOF;

    else
        return LFP_OKINCOMPLETE;
}

std::int64_t tapeimage::readinto(void* dst, std::int64_t len) noexcept (false) {
    assert(this->current.bytes_left() >= 0);
    assert(not this->index.empty());
    std::int64_t bytes_read = 0;

    while (true) {
        if (this->eof())
            return bytes_read;

        if (this->current.exhausted()) {
            if (this->current == this->index.last()) {
                this->read_header_from_disk();
                this->current.move(this->index.last());
            } else {
                const auto next = this->current.next_record();
                this->fp->seek(next.tell());
                this->current.move(next);
            }

            /* might be EOF, or even empty records, so re-start  */
            continue;
        }

        assert(not this->current.exhausted());
        std::int64_t n;
        const auto to_read = std::min(len, this->current.bytes_left());
        const auto err = this->fp->readinto(dst, to_read, &n);
        assert(err == LFP_OKINCOMPLETE ? (n < to_read) : true);
        assert(err == LFP_EOF ? (n < to_read) : true);

        this->current.move(n);
        bytes_read += n;
        dst = advance(dst, n);

        if (err == LFP_OKINCOMPLETE)
            return bytes_read;

        if (err == LFP_EOF and not this->current.exhausted()) {
            const auto msg = "tapeimage: unexpected EOF when reading header "
                             "- got {} bytes";
            throw unexpected_eof(fmt::format(msg, bytes_read));
        }

        if (err == LFP_EOF and this->current.exhausted())
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

// TODO: status instead of boolean?
int tapeimage::eof() const noexcept (true) {
    assert(not this->index.empty());
    // TODO: consider when this says record, but physical file is EOF
    // TODO: end-of-file is an _empty_ record, i.e. two consecutive tape marks
    return this->current->type == tapeimage::file;
}

void tapeimage::read_header_from_disk() noexcept (false) {
    assert(this->index.empty() or this->current == this->index.last());

    std::int64_t n;
    unsigned char b[sizeof(std::uint32_t) * 3];
    const auto err = this->fp->readinto(b, sizeof(b), &n);

    /* TODO: should also check INCOMPLETE */
    switch (err) {
        case LFP_OK: break;

        case LFP_OKINCOMPLETE:
            /* For now, don't try to recover from this - if it is because the
             * read was paused (stream blocked, for example) then it can be
             * recovered from later
             */
            throw protocol_failed_recovery(
                "tapeimage: incomplete read of tapeimage header, "
                "recovery not implemented"
            );

        case LFP_EOF:
        {
            const auto msg = "tapeimage: unexpected EOF when reading header "
                                "- got {} bytes";
            throw unexpected_eof(fmt::format(msg, n));
        }
        default:
            throw not_implemented(
                "tapeimage: unhandled error code in read_header"
            );
    }

    // Check the makefile-provided IS_BIG_ENDIAN, or the one set by gcc
    #if (defined(IS_BIG_ENDIAN) || __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
        std::reverse(b + 0, b + 4);
        std::reverse(b + 4, b + 8);
        std::reverse(b + 8, b + 12);
    #endif
    header head;
    std::memcpy(&head.type, b + 0 * 4, 4);
    std::memcpy(&head.prev, b + 1 * 4, 4);
    std::memcpy(&head.next, b + 2 * 4, 4);

    const auto header_type_consistent = head.type == tapeimage::record or
                                        head.type == tapeimage::file;

    if (!header_type_consistent) {
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
        head.type = tapeimage::record;
    }

    if (head.next <= head.prev) {
        /*
         * There's no reasonable recovery if next is smaller than prev, as it's
         * likely either the previous pointer which is broken, or this entire
         * header.
         *
         * This will happen for over 4GB files. As we do not support them at
         * the moment, this check should detect them and prevent further
         * invalid state.
         *
         * At least for now, consider it a non-recoverable error.
         */
        if (!header_type_consistent) {
            const auto msg = "file corrupt: header type is not 0 or 1, "
                             "head.next (= {}) <= head.prev (= {}). "
                             "File might be missing data";
            throw protocol_fatal(fmt::format(msg, head.next, head.prev));
        } else {
            const auto msg = "file corrupt: head.next (= {}) <= head.prev "
                             "(= {}). File size might be > 4GB";
            throw protocol_fatal(fmt::format(msg, head.next, head.prev));
        }
    }

    if (this->index.size() >= 2) {
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
        const auto& back2 = *std::prev(this->index.last());
        if (head.prev != back2.next) {
            if (this->recovery) {
                const auto msg = "file corrupt: head.prev (= {}) != "
                                 "prev(prev(head)).next (= {}). "
                                 "Error happened in recovery mode. "
                                 "File might be missing data";
                throw protocol_failed_recovery(
                      fmt::format(msg, head.prev, back2.next));
            }
            this->recovery = LFP_PROTOCOL_TRYRECOVERY;
            head.prev = back2.next;
        }
    } else if (this->recovery && !this->index.empty()) {
        /*
         * In this case we have just two headers (A and B)
         * ------------------------
         * prev|A|next  prev|B|next
         * ------------------------
         * B.prev must be pointing to A.position. As we can open file on on
         * tape header, we know that position of A is actually our zero.
         */
        if (head.prev != this->addr.base()) {
            const auto msg = "file corrupt: second header prev (= {}) must be "
                             "pointing to zero (= {}). Error happened in "
                             "recovery mode. File might be missing data";
            throw protocol_failed_recovery(fmt::format(
                  msg, head.prev, this->addr.base()));
        }
    }

    this->index.append(head);
}

void tapeimage::seek(std::int64_t n) noexcept (false) {
    assert(not this->index.empty());
    assert(n >= 0);

    if (std::numeric_limits<std::uint32_t>::max() < n)
        throw invalid_args("Too big seek offset. TIF protocol does not "
                           "support files larger than 4GB");

    if (this->index.contains(n)) {
        const auto next = this->index.find(n, this->current);
        const auto pos  = this->index.index_of(next);
        const auto real_offset = this->addr.physical(n, pos);

        /*
         * If n = 0 there's no previous, and to remain consistent with open(),
         * the read head is *not* put at the first byte of the preceeding
         * header. This is effectively the same as any other position.
         */
        if (pos > 0 and real_offset == std::prev(next)->next + header::size) {
            /*
             * read(n) would stop right before the next header. To make seek(n)
             * and read(n) move the underlying tell to the same position, move
             * the read head to the byte after the last byte in the previous
             * record.
             */
            const auto preceeding = std::prev(next);
            this->fp->seek(preceeding->next);
            this->current.move(preceeding);
            this->current.move(this->current.bytes_left());
            return;
        }

        this->fp->seek(real_offset);
        this->current.move(next);
        assert(real_offset >= this->current.tell());
        this->current.move(real_offset - this->current.tell());
        return;
    }

    /*
     * The target is beyond what we have indexed, so chase the headers and add
     * them to the index as we go
     */
    this->current.move(this->index.last());
    while (true) {
        const auto last = this->index.last();
        const auto pos  = this->index.index_of(last);
        const auto real_offset = this->addr.physical(n, pos);

        if (real_offset == last->next) {
            this->fp->seek(last->next);
            this->current.move(this->current.bytes_left());
            break;
        }

        if (real_offset < last->next) {
            this->fp->seek(real_offset);
            this->current.move(real_offset - this->current.tell());
            break;
        }

        if (last->type == tapeimage::file) {
            /*
             * Seeking past eof will is allowed (as in C FILE), but tell is
             * left undefined. Trying to read after a seek-past-eof will
             * immediately report eof.
             */
            break;
        }

        this->fp->seek(last->next);
        this->read_header_from_disk();
        this->current.move(this->index.last());
    }
}

std::int64_t tapeimage::tell() const noexcept (false) {
    assert(not this->index.empty());

    const auto pos = this->index.index_of(this->current);
    return this->addr.logical(this->current.tell(), pos);
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
