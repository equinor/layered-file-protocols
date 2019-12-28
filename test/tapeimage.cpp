#include <ciso646>
#include <cstring>
#include <memory>
#include <vector>

#include <catch2/catch.hpp>

#include <lfp/memfile.h>
#include <lfp/tapeimage.h>
#include <lfp/lfp.h>

#include "utils.hpp"

using namespace Catch::Matchers;

namespace {

constexpr static const auto random_record_sizes = -1;

struct random_tapeimage : random_memfile {
    random_tapeimage() : mem(copy()) {
        REQUIRE(not expected.empty());
    }

    ~random_tapeimage() {
        lfp_close(mem);
    }

    /*
     * The tape image creation must still be parameterised on number-of-records
     * and size-of-records, which in catch cannot be passed to the constructor
     *
     * Only fixed-size records are made, which is slightly unfortunate.
     * However, generating consistent, variable-length records are a lot more
     * subtle and complicated, and it's reasonable to assume that a lot of tapeimage
     * files use fixed-size records anyway.
     *
     * TODO: variable-length records
     */
    void make(int records) {
        REQUIRE(records > 0);
        // constant defined by the format
        const std::uint32_t record = 0;

        const std::int64_t record_size = std::ceil(double(size) / records);
        INFO("Partitioning " << size << " bytes into "
            << records << " records of max "
            << record_size << " bytes"
        );

        auto src = std::begin(expected);
        std::int64_t remaining = expected.size();
        REQUIRE(remaining > 0);
        for (int i = 0; i < records; ++i) {
            const auto n = std::min(record_size, remaining);
            auto head = std::vector< unsigned char >(12, 0);
            const std::uint32_t prev = tape.size();
            const std::uint32_t next = n + prev + head.size();
            std::memcpy(head.data() + 0, &record, sizeof(record));
            std::memcpy(head.data() + 4, &prev,   sizeof(prev));
            std::memcpy(head.data() + 8, &next,   sizeof(next));

            tape.insert(tape.end(), head.begin(), head.end());
            tape.insert(tape.end(), src, src + n);
            src += n;
            remaining -= n;
        }
        REQUIRE(remaining == 0);
        REQUIRE(src == std::end(expected));

        auto tail = std::vector< unsigned char > {
            0x1, 0x0, 0x0, 0x0, // tape record
            0x0, 0x0, 0x0, 0x0, // start-of-tape, should be prev
            0x0, 0x0, 0x0, 0x0, // placeholder to get size right
        };

        const std::uint32_t eof = tape.size() + 12;
        std::memcpy(tail.data() + 8, &eof, sizeof(eof));
        tape.insert(tape.end(), tail.begin(), tail.end());

        REQUIRE(tape.size() == eof);
        REQUIRE(tape.size() == expected.size() + (records + 1) * 12);
        lfp_close(f);
        f = nullptr;
        auto* tmp = lfp_memfile_openwith(tape.data(), tape.size());
        REQUIRE(tmp);
        f = lfp_tapeimage_open(tmp);
        REQUIRE(f);
    }

    std::vector< unsigned char > tape;
    lfp_protocol* mem = nullptr;
};

}

TEST_CASE(
    "Empty file can be opened, but reads zero bytes",
    "[tapeimage][tif]") {
    const auto file = std::vector< unsigned char > {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x0C, 0x00, 0x00, 0x00,

        0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x18, 0x00, 0x00, 0x00,

        0x01, 0x00, 0x00, 0x00,
        0x0C, 0x00, 0x00, 0x00,
        0x24, 0x00, 0x00, 0x00,
    };

    auto* mem = lfp_memfile_openwith(file.data(), file.size());
    auto* tif = lfp_tapeimage_open(mem);

    auto out = std::vector< unsigned char >(10, 0xFF);
    std::int64_t bytes_read = -1;
    const auto err = lfp_readinto(tif, out.data(), 10, &bytes_read);

    CHECK(bytes_read == 0);
    CHECK(err == LFP_OKINCOMPLETE);
    lfp_close(tif);
}

TEST_CASE(
    "8-byte file reads 8 bytes",
    "[tapeimage][tif][empty]") {
    const auto file = std::vector< unsigned char > {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x14, 0x00, 0x00, 0x00,

        /* begin body */
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,
        /* end body */

        0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x20, 0x00, 0x00, 0x00,

        0x01, 0x00, 0x00, 0x00,
        0x14, 0x00, 0x00, 0x00,
        0x2C, 0x00, 0x00, 0x00,
    };

    const auto expected = std::vector< unsigned char > {
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,
    };

    auto* mem = lfp_memfile_openwith(file.data(), file.size());
    auto* tif = lfp_tapeimage_open(mem);

    auto out = std::vector< unsigned char >(10, 0xFF);
    std::int64_t bytes_read = -1;
    const auto err = lfp_readinto(tif, out.data(), 10, &bytes_read);
    REQUIRE(bytes_read < out.size());
    out.resize(bytes_read);

    CHECK(bytes_read == 8);
    CHECK(err == LFP_OKINCOMPLETE);
    CHECK_THAT(out, Equals(expected));
    lfp_close(tif);
}

TEST_CASE_METHOD(
    random_tapeimage,
    "Tape image: A file can be read in a single read",
    "[tapeimage][tif]") {
    const auto records = GENERATE(1, 2, 3, 5, 8, 13);
    make(records);

    std::int64_t nread = 0;
    const auto err = lfp_readinto(f, out.data(), out.size(), &nread);

    CHECK(err == LFP_OK);
    CHECK(nread == expected.size());
    CHECK_THAT(out, Equals(expected));
}

TEST_CASE_METHOD(
    random_tapeimage,
    "Tape image: A file can be read in multiple, smaller reads",
    "[tapeimage][tif]") {
    const auto records = GENERATE(1, 2, 3, 5, 8, 13);
    make(records);

    // +1 so that if size is 1, max is still >= min
    const auto readsize = GENERATE_COPY(take(1, random(1, (size + 1) / 2)));
    const auto complete_reads = size / readsize;

    auto* p = out.data();
    std::int64_t nread = 0;
    for (int i = 0; i < complete_reads; ++i) {
        const auto err = lfp_readinto(f, p, readsize, &nread);
        CHECK(err == LFP_OK);
        CHECK(nread == readsize);
        p += nread;
    }

    if (size % readsize != 0) {
        const auto err = lfp_readinto(f, p, readsize, &nread);
        CHECK(err == LFP_OKINCOMPLETE);
    }

    CHECK_THAT(out, Equals(expected));
}

TEST_CASE_METHOD(
    random_tapeimage,
    "Tape image: single seek matches underlying handle",
    "[tapeimage][tif]") {
    const auto real_size = size;
    auto records = GENERATE(1, 2, 3, 5, 8, 13);
    make(records);

    const auto n = GENERATE_COPY(take(1, random(0, real_size - 1)));
    auto err = lfp_seek(f, n);
    CHECK(err == LFP_OK);

    const auto remaining = real_size - n;
    out.resize(remaining);
    auto memout = out;

    std::int64_t nread = 0;
    err = lfp_readinto(f, out.data(), out.size(), &nread);
    CHECK(err == LFP_OK);

    auto memerr = lfp_seek(mem, n);
    CHECK(memerr == LFP_OK);
    std::int64_t memnread = 0;
    memerr = lfp_readinto(mem, memout.data(), memout.size(), &memnread);
    CHECK(memerr == LFP_OK);

    CHECK(nread == memnread);
    CHECK(nread == out.size());
    CHECK_THAT(out, Equals(memout));
}

TEST_CASE_METHOD(
    random_tapeimage,
    "Tape image: multiple seeks and tells match underlying handle",
    "[tapeimage][tif]") {
    const auto real_size = size;
    auto records = GENERATE(1, 2, 3, 5, 8, 13);
    make(records);

    for (int i = 0; i < 4; ++i) {
        const auto n = GENERATE_COPY(take(1, random(0, real_size - 1)));
        auto err = lfp_seek(f, n);
        CHECK(err == LFP_OK);

        auto memerr = lfp_seek(mem, n);
        CHECK(memerr == LFP_OK);

        std::int64_t tape_tell;
        std::int64_t mem_tell;
        err = lfp_tell(f, &tape_tell);
        CHECK(err == LFP_OK);
        memerr = lfp_tell(mem, &mem_tell);
        CHECK(memerr == LFP_OK);
        CHECK(tape_tell == mem_tell);
    }

    const auto n = GENERATE_COPY(take(1, random(0, real_size - 1)));
    const auto remaining = real_size - n;
    out.resize(remaining);
    auto memout = out;

    auto err = lfp_seek(f, n);
    CHECK(err == LFP_OK);
    std::int64_t nread = 0;
    err = lfp_readinto(f, out.data(), out.size(), &nread);
    CHECK(err == LFP_OK);

    auto memerr = lfp_seek(mem, n);
    CHECK(memerr == LFP_OK);
    std::int64_t memnread = 0;
    memerr = lfp_readinto(mem, memout.data(), memout.size(), &memnread);
    CHECK(memerr == LFP_OK);

    CHECK(nread == memnread);
    CHECK(nread == out.size());
    CHECK_THAT(out, Equals(memout));
}
