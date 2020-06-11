#include <ciso646>
#include <cstring>
#include <memory>
#include <vector>

#include <catch2/catch.hpp>

#include <lfp/memfile.h>
#include <lfp/protocol.hpp>
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
        std::uint32_t record = 0;

        const std::int64_t record_size = std::ceil(double(size) / records);
        INFO("Partitioning " << size << " bytes into "
            << records << " records of max "
            << record_size << " bytes"
        );

        auto src = std::begin(expected);
        std::int64_t remaining = expected.size();
        REQUIRE(remaining > 0);
        std::uint32_t prev = 0;
        for (int i = 0; i < records + 1; ++i) {
            if (records == i) {
                REQUIRE(remaining == 0);
                REQUIRE(src == std::end(expected));
                // on reaching the end redefine record to be of file type
                record = 1;
            }
            const auto n = std::min(record_size, remaining);
            auto head = std::vector< unsigned char >(12, 0);
            const std::uint32_t next = n + tape.size() + head.size();
            std::memcpy(head.data() + 0, &record, sizeof(record));
            std::memcpy(head.data() + 4, &prev,   sizeof(prev));
            std::memcpy(head.data() + 8, &next,   sizeof(next));

            #if (defined(IS_BIG_ENDIAN) || \
                (defined(__BYTE_ORDER__) && \
                (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)))
                std::reverse(head.data() + 0, head.data() + 4);
                std::reverse(head.data() + 4, head.data() + 8);
                std::reverse(head.data() + 8, head.data() + 12);
            #endif

            prev = tape.size();
            tape.insert(tape.end(), head.begin(), head.end());
            tape.insert(tape.end(), src, src + n);
            src += n;
            remaining -= n;
        }

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
    CHECK(err == LFP_EOF);
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
    CHECK(err == LFP_EOF);
    CHECK(lfp_eof(tif));
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
    CHECK(!lfp_eof(f));
    CHECK(nread == expected.size());
    CHECK_THAT(out, Equals(expected));
}

TEST_CASE_METHOD(
    random_tapeimage,
    "Tape image: A file can be read in multiple, smaller reads",
    "[tapeimage][tif]") {
    const auto records = GENERATE(1, 2, 3, 5, 8, 13);
    make(records);

    test_split_read(this);
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

TEST_CASE(
    "Tell values are relative to the layer data",
    "[tapeimage][tell]") {

    const auto contents = std::vector< unsigned char > {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x14, 0x00, 0x00, 0x00,

        0x54, 0x41, 0x50, 0x45,
        0x4D, 0x41, 0x52, 0x4B,

        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x28, 0x00, 0x00, 0x00,

        0x54, 0x41, 0x50, 0x45,
        0x4D, 0x41, 0x52, 0x4B,

        0x01, 0x00, 0x00, 0x00,
        0x14, 0x00, 0x00, 0x00,
        0x34, 0x00, 0x00, 0x00,
    };

    auto* mem = lfp_memfile_openwith(contents.data(), contents.size());
    auto* tif = lfp_tapeimage_open(mem);

    SECTION( "tell on 0" ) {
        std::int64_t tell;
        const auto err = lfp_tell(tif, &tell);
        CHECK(err == LFP_OK);
        CHECK(tell == 0);
    }

    SECTION( "tell after all data has been read" ) {
        auto out = std::vector< unsigned char >(100, 0xFF);
        std::int64_t bytes_read = -1;
        auto err = lfp_readinto(tif, out.data(), 100, &bytes_read);
        CHECK(err == LFP_EOF);
        CHECK(bytes_read == 16);

        std::int64_t tell;
        err = lfp_tell(tif, &tell);
        CHECK(err == LFP_OK);
        CHECK(tell == 16);
    }

    SECTION( "tell on header border" ) {
        auto out = std::vector< unsigned char >(8, 0xFF);
        std::int64_t bytes_read = -1;
        auto err = lfp_readinto(tif, out.data(), 8, &bytes_read);
        CHECK(err == LFP_OK);

        std::int64_t tell;
        err = lfp_tell(tif, &tell);
        CHECK(err == LFP_OK);
        CHECK(tell == 8);
    }

    SECTION( "tell inside data" ) {
        auto out = std::vector< unsigned char >(4, 0xFF);
        std::int64_t bytes_read = -1;
        auto err = lfp_readinto(tif, out.data(), 4, &bytes_read);
        CHECK(err == LFP_OK);

        std::int64_t tell;
        err = lfp_tell(tif, &tell);
        CHECK(err == LFP_OK);
        CHECK(tell == 4);
    }

    SECTION( "tell after backwards seek" ) {
        auto out = std::vector< unsigned char >(12, 0xFF);
        std::int64_t bytes_read = -1;
        auto err = lfp_readinto(tif, out.data(), 12, &bytes_read);
        CHECK(err == LFP_OK);

        err = lfp_seek(tif, 8);
        CHECK(err == LFP_OK);

        std::int64_t tell;
        err = lfp_tell(tif, &tell);
        CHECK(err == LFP_OK);
        CHECK(tell == 8);
    }

    lfp_close(tif);
}

TEST_CASE(
    "Seeks are performed relative to layer",
    "[tapeimage][seek]") {

    const auto contents = std::vector< unsigned char > {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x14, 0x00, 0x00, 0x00,

        0x54, 0x41, 0x50, 0x45,
        0x4D, 0x41, 0x52, 0x4B,

        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x28, 0x00, 0x00, 0x00,

        0x54, 0x41, 0x50, 0x45,
        0x4D, 0x41, 0x52, 0x4B,

        /*spoiled border*/
        0xAA, 0xAA, 0xAA, 0xAA,
        0x99, 0x99, 0x99, 0x99,
        0x88, 0x88, 0x88, 0x88,
    };

    auto* mem = lfp_memfile_openwith(contents.data(), contents.size());
    auto* tif = lfp_tapeimage_open(mem);

    SECTION( "negative seek" ) {
        const auto err = lfp_seek(tif, -1);
        CHECK(err == LFP_INVALID_ARGS);
        auto msg = std::string(lfp_errormsg(tif));
        CHECK_THAT(msg, Contains("< 0"));
    }

    SECTION( "seek to header border: not indexed" ) {
        test_seek_and_read(tif, 16, LFP_PROTOCOL_FATAL_ERROR);
    }

    SECTION( "seek to header border: index border" ) {
        auto out = std::vector< unsigned char >(16, 0xFF);
        std::int64_t bytes_read = -1;
        auto err = lfp_readinto(tif, out.data(), 16, &bytes_read);
        CHECK(err == LFP_OK);

        err = lfp_seek(tif, 0);
        CHECK(err == LFP_OK);

        bytes_read = -1;
        char buf;
        err = lfp_readinto(tif, &buf, 1, &bytes_read);
        CHECK(err == LFP_OK);

        test_seek_and_read(tif, 16, LFP_PROTOCOL_FATAL_ERROR);
    }

    SECTION( "seek to header border: indexed" ) {
        auto out = std::vector< unsigned char >(16, 0xFF);
        std::int64_t bytes_read = -1;
        auto err = lfp_readinto(tif, out.data(), 16, &bytes_read);
        CHECK(err == LFP_OK);

        err = lfp_seek(tif, 0);
        CHECK(err == LFP_OK);

        bytes_read = -1;
        char buf;
        err = lfp_readinto(tif, &buf, 1, &bytes_read);
        CHECK(err == LFP_OK);

        test_seek_and_read(tif, 8, LFP_OK);
    }

    SECTION("seek to header border: start of current record") {
        auto out = std::vector< unsigned char >(16, 0xFF);
        std::int64_t bytes_read = -1;
        auto err = lfp_readinto(tif, out.data(), 16, &bytes_read);
        CHECK(err == LFP_OK);

        err = lfp_seek(tif, 12);
        CHECK(err == LFP_OK);

        bytes_read = -1;
        char buf;
        err = lfp_readinto(tif, &buf, 1, &bytes_read);
        CHECK(err == LFP_OK);

        test_seek_and_read(tif, 8, LFP_OK);
    }

    lfp_close(tif);
}

TEST_CASE(
    "Operation past end-of-file"
    "[tapeimage][eof]") {

    const auto contents = std::vector< unsigned char > {
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

    const auto expected1 = std::vector< unsigned char > {
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,
        0xFF, 0xFF // never read by lfp
    };

    std::FILE* fp = std::tmpfile();
    std::fwrite(contents.data(), 1, contents.size(), fp);
    std::rewind(fp);

    auto* cfile = lfp_cfile(fp);
    auto* tif = lfp_tapeimage_open(cfile);

    SECTION( "Read past eof" ) {
        auto out = std::vector< unsigned char >(10, 0xFF);
        std::int64_t bytes_read = -1;
        const auto err = lfp_readinto(tif, out.data(), 10, &bytes_read);

        CHECK(bytes_read == 8);
        CHECK(err == LFP_EOF);
        CHECK_THAT(out, Equals(expected1));

        std::int64_t tell;
        lfp_tell(tif, &tell);
        CHECK(tell == 8);
    }

    SECTION( "Read past eof - after a seek past eof" ) {
        auto err = lfp_seek(tif, 10);
        CHECK(err == LFP_OK);

        char x = 0xFF;
        std::int64_t bytes_read = -1;
        err = lfp_readinto(tif, &x, 1, &bytes_read);

        CHECK(err == LFP_EOF);
        CHECK(bytes_read == 0);
    }
    lfp_close(tif);
}


TEST_CASE(
    "Layered tapeimage closes correctly",
    "[tapeimage][close]") {
    const auto contents = std::vector< unsigned char > {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x30, 0x00, 0x00, 0x00,

        /* inner tapeimage */
        0x00, 0x00, 0x00, 0x00,
        0x0C, 0x00, 0x00, 0x00,
        0x24, 0x00, 0x00, 0x00,

        0x01, 0x02, 0x03, 0x04,
        0x54, 0x41, 0x50, 0x45,
        0x4D, 0x41, 0x52, 0x4B,

        0x01, 0x00, 0x00, 0x00,
        0x0C, 0x00, 0x00, 0x00,
        0x30, 0x00, 0x00, 0x00,
        /* end inner tapeimage */

        0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x3C, 0x00, 0x00, 0x00,
    };
    auto* mem = lfp_memfile_openwith(contents.data(), contents.size());
    auto* tif = lfp_tapeimage_open(mem);
    auto* outer = lfp_tapeimage_open(tif);

    auto err = lfp_close(outer);
    CHECK(err == LFP_OK);
}

TEST_CASE(
    "Peel off the current protocol and expose the underlying one"
    "[tapeimage][peel]") {

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

    const auto expected1 = std::vector< unsigned char > {
        0x01, 0x02, 0x03, 0x04,
    };

    const auto expected2 = std::vector< unsigned char > {
        0x05, 0x06, 0x07, 0x08,
    };

    auto* mem = lfp_memfile_openwith(file.data(), file.size());
    auto* tif = lfp_tapeimage_open(mem);

    auto out = std::vector< unsigned char >(4, 0xFF);
    std::int64_t bytes_read = -1;
    auto err = lfp_readinto(tif, out.data(), 4, &bytes_read);

    CHECK(bytes_read == 4);
    CHECK(err == LFP_OK);
    CHECK_THAT(out, Equals(expected1));

    std::int64_t tif_tell;
    lfp_tell(tif, &tif_tell);
    CHECK(tif_tell == 4);

    lfp_protocol* inner;
    err = lfp_peel(tif, &inner);
    CHECK(err == LFP_OK);

    std::int64_t mem_tell;
    lfp_tell(inner, &mem_tell);
    CHECK(mem_tell == 16);

    err = lfp_readinto(inner, out.data(), 4, &bytes_read);
    CHECK(bytes_read == 4);
    CHECK(err == LFP_OK);
    CHECK_THAT(out, Equals(expected2));

    lfp_close(inner);
    lfp_close(tif);
}

TEST_CASE(
    "Peek into the underlying protocol"
    "[tapeimage][peek]") {

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

    const auto expected1 = std::vector< unsigned char > {
        0x01, 0x02, 0x03, 0x04,
    };

    const auto expected2 = std::vector< unsigned char > {
        0x05, 0x06, 0x07, 0x08,
    };

    auto* mem = lfp_memfile_openwith(file.data(), file.size());
    auto* tif = lfp_tapeimage_open(mem);

    auto out = std::vector< unsigned char >(4, 0xFF);
    std::int64_t bytes_read = -1;
    auto err = lfp_readinto(tif, out.data(), 4, &bytes_read);

    CHECK(bytes_read == 4);
    CHECK(err == LFP_OK);
    CHECK_THAT(out, Equals(expected1));

    std::int64_t tif_tell;
    lfp_tell(tif, &tif_tell);
    CHECK(tif_tell == 4);

    lfp_protocol* inner;
    lfp_peek(tif, &inner);

    std::int64_t mem_tell;
    lfp_tell(inner, &mem_tell);
    CHECK(mem_tell == 16);

    err = lfp_readinto(tif, out.data(), 4, &bytes_read);
    CHECK(bytes_read == 4);
    CHECK(err == LFP_OK);
    CHECK_THAT(out, Equals(expected2));

    lfp_close(tif);
}

TEST_CASE_METHOD(
    device,
    "Reading truncated file return expected errors",
    "[tapeimage]") {

    SECTION( "testing on "+ device_type) {

    SECTION( "eof mark is missing, but data could still be correctly read" ) {
        const auto contents = std::vector< unsigned char > {
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x14, 0x00, 0x00, 0x00,

            0x54, 0x41, 0x50, 0x45,
            0x4D, 0x41, 0x52, 0x4B,
        };

        const auto expected = std::vector< unsigned char > {
            0x54, 0x41, 0x50, 0x45,
            0x4D, 0x41, 0x52, 0x4B,
            0xFF, 0xFF //Last two bytes are never written by lfp
        };

        auto* inner = create(contents);
        auto* tif = lfp_tapeimage_open(inner);

        SECTION( "read" ) {
            auto out = std::vector< unsigned char >(10, 0xFF);
            std::int64_t bytes_read = -1;
            auto err = lfp_readinto(tif, out.data(), 10, &bytes_read);
            /* it's not absolutely clear whether this situation should be
            * considered valid, but some complete and valid files are only missing
            * end tapemarks, so expect LFP_EOF, not LFP_UNEXPECTED_EOF
            */
            CHECK(err == LFP_EOF);
            CHECK(bytes_read == 8);
            CHECK_THAT(out, Equals(expected));

            std::int64_t tell;
            lfp_tell(tif, &tell);
            CHECK(tell == 8);

            CHECK(lfp_eof(tif));

            err == lfp_seek(tif, 0);
            CHECK(!lfp_eof(tif));

            bytes_read = -1;
            err = lfp_readinto(tif, out.data(), 8, &bytes_read);
            CHECK(err == LFP_OK);
            CHECK(bytes_read == 8);
            CHECK(lfp_eof(tif) == lfp_eof(inner));
        }

        SECTION( "seek to the data border" ) {
            // TODO: memfile
            test_seek_and_read(tif, 8, LFP_OK, LFP_EOF, this);
        }

        SECTION( "seek past data" ) {
            // TODO: memfile
            test_seek_and_read(tif, 10, LFP_OK, LFP_EOF, this);
        }

        lfp_close(tif);
    }

    SECTION( "truncated in header" ) {
        const auto contents = std::vector< unsigned char > {
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x14, 0x00, 0x00, 0x00,

            0x54, 0x41, 0x50, 0x45,
            0x4D, 0x41, 0x52, 0x4B,

            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            //oops
        };

        const auto expected = std::vector< unsigned char > {
            0x54, 0x41, 0x50, 0x45,
            0x4D, 0x41, 0x52, 0x4B,
            0xFF, 0xFF //Last two bytes are never written by lfp
        };

        auto* inner = create(contents);
        auto* tif = lfp_tapeimage_open(inner);

        SECTION( "read" ) {
            auto out = std::vector< unsigned char >(10, 0xFF);
            std::int64_t bytes_read = -1;
            const auto err = lfp_readinto(tif, out.data(), 10, &bytes_read);

            CHECK(err == LFP_UNEXPECTED_EOF);
            CHECK(bytes_read == 8);
            auto msg = std::string(lfp_errormsg(tif));
            CHECK_THAT(msg, Contains("unexpected EOF"));
            CHECK_THAT(msg, Contains("got 8 bytes"));

            CHECK_THAT(out, Equals(expected));

            CHECK(lfp_eof(tif));
        }

        SECTION( "seek past data" ) {
            test_seek_and_read(tif, 10, LFP_UNEXPECTED_EOF, LFP_EOF);
        }

        lfp_close(tif);
    }

    SECTION( "truncated after header" ) {
        const auto contents = std::vector< unsigned char > {
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x14, 0x00, 0x00, 0x00,

            0x54, 0x41, 0x50, 0x45,
            0x4D, 0x41, 0x52, 0x4B,

            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x40, 0x00, 0x00, 0x00,
        };

        auto* inner = create(contents);
        auto* tif = lfp_tapeimage_open(inner);

        SECTION( "read" ) {
            auto out = std::vector< unsigned char >(10, 0xFF);
            std::int64_t bytes_read = -1;
            const auto err = lfp_readinto(tif, out.data(), 10, &bytes_read);

            CHECK(err == LFP_UNEXPECTED_EOF);
            CHECK(bytes_read == 8);

            std::int64_t tell;
            lfp_tell(tif, &tell);
            CHECK(tell == 8);

            CHECK(lfp_eof(tif));
        }

        SECTION( "seek to border" ) {
            test_seek_and_read(tif, 8, LFP_UNEXPECTED_EOF);
        }

        SECTION( "seek in declared data" ) {
            test_seek_and_read(tif, 10, LFP_UNEXPECTED_EOF);
        }

        SECTION( "seek past declared data" ) {
            test_seek_and_read(tif, 100, LFP_EOF);
        }

        lfp_close(tif);
    }

    SECTION( "truncated in data" ) {
        const auto contents = std::vector< unsigned char > {
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x14, 0x00, 0x00, 0x00,

            0x54, 0x41, 0x50, 0x45,
            //oops
        };

        const auto expected = std::vector< unsigned char > {
            0x54, 0x41, 0x50, 0x45,
            0xFF, 0xFF, 0xFF, 0xFF,
        };


        auto* inner = create(contents);
        auto* tif = lfp_tapeimage_open(inner);

        SECTION( "read" ) {
            auto out = std::vector< unsigned char >(8, 0xFF);
            std::int64_t bytes_read = -1;
            const auto err = lfp_readinto(tif, out.data(), 8, &bytes_read);
            CHECK(bytes_read == 4);

            CHECK(err == LFP_UNEXPECTED_EOF);
            auto msg = std::string(lfp_errormsg(tif));
            CHECK_THAT(msg, Contains("unexpected EOF"));
            CHECK_THAT(msg, Contains("got 4 bytes"));

            CHECK_THAT(out, Equals(expected));

            CHECK(lfp_eof(tif));
        }

        SECTION( "seek inside data " ) {
            test_seek_and_read(tif, 3, LFP_OK);
        }

        // TODO: memfile for all the tests
        SECTION( "seek to border" ) {
            test_seek_and_read(tif, 4, LFP_OK, LFP_UNEXPECTED_EOF, this);
        }

        SECTION( "seek into declared data" ) {
            test_seek_and_read(tif, 6, LFP_OK, LFP_UNEXPECTED_EOF, this);
        }

        SECTION( "seek past declared data" ) {
            test_seek_and_read(tif, 100, LFP_OK, LFP_EOF, this);
        }

        lfp_close(tif);
    }

    }
}

TEST_CASE(
    "head.next points outside border",
    "[tapeimage][seek]") {

    const auto contents = std::vector< unsigned char > {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x14, 0x00, 0x10, 0x00, //next points way outside borders

        0x54, 0x41, 0x50, 0x45,
        0x4D, 0x41, 0x52, 0x4B,

        0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x20, 0x00, 0x00, 0x00,
    };

    std::FILE* fp = std::tmpfile();
    std::fwrite(contents.data(), 1, contents.size(), fp);
    std::rewind(fp);

    auto* cfile = lfp_cfile(fp);
    auto* tif = lfp_tapeimage_open(cfile);

    /*
     * If seek still falls *inside* the inner file, TIF protocol has no
     * chance of figuring out something is wrong. We can do some verifications
     * for headers only, so until we read fake header, we are blind.
     */
    test_seek_and_read(tif, 100, LFP_UNEXPECTED_EOF);

    lfp_close(tif);
}

TEST_CASE_METHOD(
    device,
    "tapeimage: Empty record",
    "[tapeimage]") {

    SECTION( "run on device "+device_type ) {

    SECTION( "FILE tapemark at the beginning" ) {
        const auto contents = std::vector< unsigned char > {
            0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x0C, 0x00, 0x00, 0x00,

            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x1C, 0x00, 0x00, 0x00,

            0x01, 0x02, 0x03, 0x04,

            0x01, 0x00, 0x00, 0x00,
            0x0C, 0x00, 0x00, 0x00,
            0x28, 0x00, 0x00, 0x00,
        };

        auto* mem = lfp_memfile_openwith(contents.data(), contents.size());
        auto* tif = lfp_tapeimage_open(mem);

        SECTION( "read" ) {
            auto out = std::vector< unsigned char >(4, 0xFF);
            std::int64_t bytes_read = -1;
            const auto err = lfp_readinto(tif, out.data(), 4, &bytes_read);

            CHECK(err == LFP_EOF);
            CHECK(bytes_read == 0);
        }

        SECTION( "seek ") {
            test_seek_and_read(tif, 2, LFP_EOF);
        }

        lfp_close(tif);
    }

    SECTION( "empty record in the middle" ) {
        const auto contents = std::vector< unsigned char > {
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x10, 0x00, 0x00, 0x00,

            0x54, 0x41, 0x50, 0x45,

            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x1C, 0x00, 0x00, 0x00,

            0x00, 0x00, 0x00, 0x00,
            0x10, 0x00, 0x00, 0x00,
            0x2C, 0x00, 0x00, 0x00,

            0x4D, 0x41, 0x52, 0x4B,

            0x01, 0x00, 0x00, 0x00,
            0x1C, 0x00, 0x00, 0x00,
            0x38, 0x00, 0x00, 0x00,
        };

        auto* mem = lfp_memfile_openwith(contents.data(), contents.size());
        auto* tif = lfp_tapeimage_open(mem);

        SECTION( "read through empty record" ) {
            auto out = std::vector< unsigned char >(8, 0xFF);
            std::int64_t bytes_read = -1;
            const auto err = lfp_readinto(tif, out.data(), 8, &bytes_read);

            CHECK(err == LFP_OK);
            CHECK(bytes_read == 8);
        }

        SECTION( "seek through empty record" ) {
            test_seek_and_read(tif, 6, LFP_OK);
        }

        lfp_close(tif);
    }

    SECTION( "ending on empty record of record type" ) {
        const auto contents = std::vector< unsigned char > {
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x10, 0x00, 0x00, 0x00,

            0x54, 0x41, 0x50, 0x45,

            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x1C, 0x00, 0x00, 0x00,
        };

        auto* mem = lfp_memfile_openwith(contents.data(), contents.size());
        auto* tif = lfp_tapeimage_open(mem);

        auto out = std::vector< unsigned char >(10, 0xFF);
        std::int64_t bytes_read = -1;
        const auto err = lfp_readinto(tif, out.data(), 10, &bytes_read);

        CHECK(err == LFP_EOF);

        lfp_close(tif);
    }

    }

}

TEST_CASE(
    "Tapeimage can be opened at any TM"
    "[tapeimage][offset]") {

    const auto file = std::vector< unsigned char > {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x0C, 0x00, 0x00, 0x00,

        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x1C, 0x00, 0x00, 0x00,

        /* begin body */
        0x01, 0x02, 0x03, 0x04,
        /* end body */

        0x00, 0x00, 0x00, 0x00,
        0x0C, 0x00, 0x00, 0x00,
        0x2C, 0x00, 0x00, 0x00,

        /* begin body */
        0x05, 0x06, 0x07, 0x08,
        /* end body */

        0x01, 0x00, 0x00, 0x00,
        0x1C, 0x00, 0x00, 0x00,
        0x38, 0x00, 0x00, 0x00,
    };

    const auto expected = std::vector< unsigned char > {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    };

    auto* mem = lfp_memfile_openwith(file.data(), file.size());

    auto err = lfp_seek(mem, 12);
    CHECK(err == LFP_OK);

    auto* tif = lfp_tapeimage_open(mem);

    SECTION( "Tell starts at 0" ) {
        std::int64_t tell;
        lfp_tell(tif, &tell);
        CHECK(tell == 0);
    }
    SECTION( "Seek past index" ) {
        test_seek_and_read(tif, 6, LFP_OK);
    }
    SECTION( "Seek with index" ) {
        auto out = std::vector< unsigned char >(8, 0xFF);
        std::int64_t bytes_read = -1;
        err = lfp_readinto(tif, out.data(), 8, &bytes_read);
        CHECK(err == LFP_OK);

        test_seek_and_read(tif, 2, LFP_OK);
    }
    SECTION( "Read from already indexed records" ) {
        /* won't matter when read is the only indexing operation*/
        err = lfp_seek(tif, 6);
        CHECK(err == LFP_OK);
        err = lfp_seek(tif, 0);
        CHECK(err == LFP_OK);

        auto out = std::vector< unsigned char >(8, 0xFF);
        std::int64_t bytes_read = -1;
        err = lfp_readinto(tif, out.data(), 8, &bytes_read);

        CHECK(bytes_read == 8);
        CHECK(err == LFP_OK);
        CHECK_THAT(out, Equals(expected));
    }
    SECTION( "Read past index" ) {
        auto out = std::vector< unsigned char >(8, 0xFF);
        std::int64_t bytes_read = -1;
        err = lfp_readinto(tif, out.data(), 8, &bytes_read);

        CHECK(bytes_read == 8);
        CHECK(err == LFP_OK);
        CHECK_THAT(out, Equals(expected));
    }
    lfp_close(tif);
}

TEST_CASE(
    "Blocked inner layer is processed correctly in tapeimage",
    "[tapeimage][blockedpipe]") {

    std::vector< unsigned char > data = {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x40, 0x00, 0x00, 0x00,

        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C,
        0x0D, 0x0E, 0x0F, 0x10,
    };

    SECTION( "incomplete in header" ) {

        auto* blocked = new blockedpipe(data, 10);
        auto* tif = lfp_tapeimage_open(blocked);

        auto out = std::vector< unsigned char >(16, 0xFF);
        std::int64_t bytes_read = -1;
        const auto err = lfp_readinto(tif, out.data(), 16, &bytes_read);

        CHECK(err == LFP_IOERROR);
        CHECK(bytes_read == 0);

        lfp_close(tif);
    }

    SECTION( "incomplete in data" ) {
        auto* blocked = new blockedpipe(data, 20);
        auto* tif = lfp_tapeimage_open(blocked);

        SECTION ("read") {
            auto out = std::vector< unsigned char >(12, 0xFF);
            std::int64_t bytes_read = -1;
            const auto err = lfp_readinto(tif, out.data(), 12, &bytes_read);

            CHECK(err == LFP_OKINCOMPLETE);
            CHECK(bytes_read == 8);

            const auto expected = std::vector< unsigned char > {
                0x01, 0x02, 0x03, 0x04,
                0x05, 0x06, 0x07, 0x08,
                0xFF, 0xFF, 0xFF, 0xFF //never written
            };
            CHECK_THAT(out, Equals(expected));
        }

        SECTION ("seek") {
            test_seek_and_read(tif, 12, LFP_OKINCOMPLETE);
        }

        lfp_close(tif);
    }
}

TEST_CASE_METHOD(
    device,
    "Broken TIF: FILE tapemark has data inside of it",
    "[tapeimage][seek]") {

    SECTION ("running on "+device_type) {

    const auto contents = std::vector< unsigned char > {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x0C, 0x00, 0x00, 0x00,

        0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x20, 0x00, 0x00, 0x00,

        0x54, 0x41, 0x50, 0x45,
        0x4D, 0x41, 0x52, 0x4B,

        0x01, 0x00, 0x00, 0x00,
        0x0C, 0x00, 0x00, 0x00,
        0x2C, 0x00, 0x00, 0x00,
    };

    auto* inner = create(contents);
    auto* tif = lfp_tapeimage_open(inner);

    SECTION( "read data" ) {
        auto out = std::vector< unsigned char >(8, 0xFF);
        std::int64_t bytes_read = -1;
        const auto err = lfp_readinto(tif, out.data(), 8, &bytes_read);

        CHECK(err == LFP_UNEXPECTED_EOF);
        CHECK(bytes_read == 0);
    }

    SECTION( "seek inside data" ) {
        test_seek_and_read(tif, 4, LFP_UNEXPECTED_EOF);
    }

    SECTION( "seek outside data" ) {
        // it's questionable whether we expect LFP_EOF or LFP_UNEXPECTED_EOF
        test_seek_and_read(tif, 100, LFP_EOF);
    }

    lfp_close(tif);
    }
}

TEST_CASE(
    "Broken TIF - recovery mode",
    "[tapeimage][errorcase][incomplete]") {
    const auto contents = std::vector< unsigned char > {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x18, 0x00, 0x00, 0x00,

        0x01, 0x02, 0x03, 0x04,
        0x54, 0x41, 0x50, 0x45,
        0x4D, 0x41, 0x52, 0x4B,

        0xFF, 0xFF, 0xFF, 0xFF,  //broken type
        0x00, 0x00, 0x00, 0x00,
        0x30, 0x00, 0x00, 0x00,

        0x01, 0x02, 0x03, 0x04,
        0x54, 0x41, 0x50, 0x45,
        0x4D, 0x41, 0x52, 0x4B,

        0xFF, 0xFF, 0xFF, 0xFF,  //broken again
        0x18, 0x00, 0x00, 0x00,
        0x48, 0x00, 0x00, 0x00,

        0x01, 0x02, 0x03, 0x04,
        0x54, 0x41, 0x50, 0x45,
        0x4D, 0x41, 0x52, 0x4B,

        0x01, 0x00, 0x00, 0x00,
        0x30, 0x00, 0x00, 0x00,
        0x54, 0x00, 0x00, 0x00,
    };

    auto* mem = lfp_memfile_openwith(contents.data(), contents.size());
    auto* tif = lfp_tapeimage_open(mem);
    {
        auto out = std::vector< unsigned char >(16, 0xFF);
        std::int64_t bytes_read = -1;
        const auto err = lfp_readinto(tif, out.data(), 16, &bytes_read);

        CHECK(err == LFP_PROTOCOL_TRYRECOVERY);
        CHECK(bytes_read == 16);

        const auto expected = std::vector< unsigned char > {
            0x01, 0x02, 0x03, 0x04,
            0x54, 0x41, 0x50, 0x45,
            0x4D, 0x41, 0x52, 0x4B,
            0x01, 0x02, 0x03, 0x04,
        };
        CHECK_THAT(out, Equals(expected));
    }

    SECTION( "try recovery: no new error" ) {
        auto out = std::vector< unsigned char >(8, 0xFF);
        std::int64_t bytes_read = -1;
        const auto err = lfp_readinto(tif, out.data(), 8, &bytes_read);

        CHECK(err == LFP_PROTOCOL_TRYRECOVERY);
        CHECK(bytes_read == 8);

        const auto expected = std::vector< unsigned char > {
            0x54, 0x41, 0x50, 0x45,
            0x4D, 0x41, 0x52, 0x4B,
        };
        CHECK_THAT(out, Equals(expected));
    }

    SECTION( "try recovery: new error" ) {
        auto out = std::vector< unsigned char >(12, 0xFF);
        std::int64_t bytes_read = -1;
        const auto err = lfp_readinto(tif, out.data(), 12, &bytes_read);

        CHECK(err == LFP_PROTOCOL_FAILEDRECOVERY);
        auto msg = std::string(lfp_errormsg(tif));
        CHECK_THAT(msg, Contains("in recovery"));
    }

    /*
    SECTION( "try recovery: cleared" ) {

        // if explicit clearing implemented
        // clear error message
        auto out = std::vector< unsigned char >(12, 0xFF);
        std::int64_t bytes_read = -1;
        const auto err = lfp_readinto(tif, out.data(), 12, &bytes_read);

        CHECK(err == LFP_PROTOCOL_TRYRECOVERY);
        CHECK(bytes_read == 12);

        const auto expected = std::vector< unsigned char > {
            0x54, 0x41, 0x50, 0x45,
            0x4D, 0x41, 0x52, 0x4B,
            0x01, 0x02, 0x03, 0x04,
        };
        CHECK_THAT(out, Equals(expected));
    }
    */

    lfp_close(tif);
}

TEST_CASE(
    "Recovery mode and EOF",
    "[tapeimage][recovery]") {

    SECTION( "recovery and valid EOF" ) {
        std::vector< unsigned char > contents = {
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x14, 0x00, 0x00, 0x00,

            0x01, 0x02, 0x03, 0x04,
            0x05, 0x06, 0x07, 0x08,

            0xFF, 0xFF, 0xFF, 0xFF,  //broken type
            0x00, 0x00, 0x00, 0x00,
            0x20, 0x00, 0x00, 0x00,
        };

        auto* mem = lfp_memfile_openwith(contents.data(), contents.size());
        auto* tif = lfp_tapeimage_open(mem);

        auto out = std::vector< unsigned char >(16, 0xFF);
        std::int64_t bytes_read = -1;
        const auto err = lfp_readinto(tif, out.data(), 16, &bytes_read);

        // two options are possible: EOF or TRYRECOVERY
        CHECK(err == LFP_PROTOCOL_TRYRECOVERY);
        CHECK(bytes_read == 8);

        lfp_close(tif);
    }

    SECTION( "recovery and unexpected EOF" ) {
        std::vector< unsigned char > contents = {
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x14, 0x00, 0x00, 0x00,

            0x01, 0x02, 0x03, 0x04,
            0x05, 0x06, 0x07, 0x08,

            0xFF, 0xFF, 0xFF, 0xFF,  //broken type
            0x00, 0x00, 0x00, 0x00,
            0x40, 0x00, 0x00, 0x00,
        };

        auto* mem = lfp_memfile_openwith(contents.data(), contents.size());
        auto* tif = lfp_tapeimage_open(mem);

        auto out = std::vector< unsigned char >(16, 0xFF);
        std::int64_t bytes_read = -1;
        const auto err = lfp_readinto(tif, out.data(), 16, &bytes_read);

        // unexpected EOF is a more severe error than try recovery
        CHECK(err == LFP_UNEXPECTED_EOF);
        CHECK(bytes_read == 8);

        lfp_close(tif);
    }
}


TEST_CASE(
    "Broken TIF - data missing",
    "[tapeimage][errorcase][incomplete]") {

    const auto expected = std::vector< unsigned char > {
        0x54, 0x41, 0x50, 0x45,
        0x4D, 0x41, 0x52, 0x4B,
        0x44, 0x41, 0x54, 0x41,
    };

    auto run = [&](std::vector< unsigned char > contents, int expected_error)
    {
        auto* mem = lfp_memfile_openwith(contents.data(), contents.size());
        auto* tif = lfp_tapeimage_open(mem);

        {
            auto out = std::vector< unsigned char >(16, 0xFF);
            std::int64_t bytes_read = -1;
            const auto err = lfp_readinto(tif, out.data(), 16, &bytes_read);

            CHECK(err == expected_error);
            auto msg = std::string(lfp_errormsg(tif));
            CHECK_THAT(msg, Contains("missing data"));

            CHECK(bytes_read == 12);
            auto read = std::vector< unsigned char >(out.begin(),
                std::next(out.begin(), 12));
            CHECK_THAT(read, Equals(expected));
        }

        // in case state revert for before the operation implemented
        /*
        {
            // revert-to-before-the-operation
            auto out = std::vector< unsigned char >(12, 0xFF);
            std::int64_t bytes_read = -1;
            const auto err = lfp_readinto(tif, out.data(), 12, &bytes_read);

            CHECK(err == LFP_OK);
            CHECK(bytes_read == 12);
            CHECK_THAT(out, Equals(expected));
        }
        */
        lfp_close(tif);
    };

    SECTION( "failed checks: header type and previous (2nd header)" ) {
        auto contents = std::vector< unsigned char > {
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x18, 0x00, 0x00, 0x00,

            0x54, 0x41, 0x50, 0x45,
            0x4D, 0x41, 0x52, 0x4B,

            /* data went missing */
            0x44, 0x41, 0x54, 0x41,
            0x04, 0x05, 0x06, 0x07, //perceived header
            0x08, 0x09, 0x0A, 0x0B,
            0x0C, 0x0D, 0x0E, 0x0F,

            0x01, 0x00, 0x00, 0x00,
            0x04, 0x67, 0x70, 0x00,
            0x00, 0x68, 0x70, 0x00,
        };

        auto expected_error = LFP_PROTOCOL_FAILEDRECOVERY;
        run(contents, expected_error);
    }

    SECTION( "failed checks: header type and previous (> 2nd header)" ) {
        auto contents = std::vector< unsigned char > {
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x0C, 0x00, 0x00, 0x00,

            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x24, 0x00, 0x00, 0x00,

            0x54, 0x41, 0x50, 0x45,
            0x4D, 0x41, 0x52, 0x4B,

            /* data went missing */
            0x44, 0x41, 0x54, 0x41,
            0x04, 0x05, 0x06, 0x07, //perceived header
            0x08, 0x09, 0x0A, 0x0B,
            0x0C, 0x0D, 0x0E, 0x0F,

            0x01, 0x00, 0x00, 0x00,
            0x04, 0x67, 0x70, 0x00,
            0x00, 0x68, 0x70, 0x00,
        };

        auto expected_error = LFP_PROTOCOL_FAILEDRECOVERY;
        run(contents, expected_error);
    }

    SECTION( "failed checks: header type and next > prev" ) {
        auto contents = std::vector< unsigned char > {
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x18, 0x00, 0x00, 0x00,

            0x54, 0x41, 0x50, 0x45,
            0x4D, 0x41, 0x52, 0x4B,

            /* data went missing */
            0x44, 0x41, 0x54, 0x41,
            0x0B, 0x0A, 0x09, 0x08, //perceived header
            0x07, 0x06, 0x05, 0x04,
            0x03, 0x02, 0x01, 0x00,

            0x01, 0x00, 0x00, 0x00,
            0x04, 0x67, 0x70, 0x00,
            0x00, 0x68, 0x70, 0x00,
        };

        auto expected_error = LFP_PROTOCOL_FATAL_ERROR;
        run(contents, expected_error);
    }
}

/*
 * Only run this test on 64bit architectures - skip 32bit windows
 *
 * This test emulates files bigger than 4GB to test tapeimages overflow
 * protection of the header fields 'next' and 'prev'. Skip due to the 4GB memory
 * limit of 32bit windows.
 */
#if (not (defined(_WIN32) and not defined(_WIN64)))
TEST_CASE(
    "Operations on 4GB file",
    "[tapeimage][4GB][unsafe]") {
    /*
     * Setup created to avoid dealing with actual 4GB files. Space to read the
     * data is not created, lonely pointer is used instead. Pointer arithmetics
     * is performed on it, but the memory under it is never read. Still, some
     * memory-leaking tools might detect this behavior. Depending on the
     * assigned address, pointer overflow might happen, but it doesn't matter
     * for the test.
     *
     * Currently test asumes that we ignore files over 4GB and do not attempt
     * to read overflown parts.
     */

    using header = std::vector< unsigned char >;
    const size_t GB = 1024 * 1024 * 1024;

    class memfake : public lfp_protocol
    {
      public:
        memfake()
        {
            //assumption that it zeroes out on overflow
            //2GB + 12 header bytes
            header h1 = {
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
                0x0C, 0x00, 0x00, 0x80,
            };

            //1GB + 12 header bytes
            header h2 = {
                0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
                0x18, 0x00, 0x00, 0xC0,
            };

            //2GB + 12 header bytes - overflow header
            header h3 = {
                0x00, 0x00, 0x00, 0x00,
                0x0C, 0x00, 0x00, 0x80,
                0x24, 0x00, 0x00, 0x40,
            };

            //5GB + 3 headers by 12 bytes
            this->headers.push_back(h1);
            this->headers.push_back(h2);
            this->headers.push_back(h3);
            this->it = this->headers.begin();
        }

        void close() noexcept(true) override {}
        lfp_status readinto(
            void *dst,
            std::int64_t len,
            std::int64_t *bytes_read) noexcept(true) override
        {
            if(len == 12){
                //pretend we are reading header
                header hd = *this->it;
                this->it = std::next(this->it);
                std::memcpy(dst, hd.data(), len);
            }

            *bytes_read = len;
            return LFP_OK;
        }

        int eof() const noexcept(true) override { return 0; }

        /* seek and tell handle only expected calls at the headers borders */

        void seek(std::int64_t n) noexcept (false) {
            int jump = 0;
            switch (n) {
                case 0:
                    break;
                case 2*GB + 12:
                    jump = 1;
                    break;
                case 3*GB + 24:
                    jump = 2;
                    break;
                default:
                    throw std::runtime_error("unexpected seek in test");
            }

            this->it = std::next(std::begin(headers), jump);
            return;
        }

        std::int64_t tell() const noexcept (false) {
            if(*this->it == headers[1]) {
                return 2*GB + 12;
            } else if (*this->it == headers[2]) {
                return 3*GB + 24;
            } else {
                return 0;
            }
        }

        lfp_protocol* peel() noexcept (false) override { throw; }
        lfp_protocol* peek() const noexcept (false) override { throw; }

      private:
        std::vector< header > headers;
        std::vector< header >::const_iterator it;
    };

    lfp_protocol* mem = new memfake();
    lfp_protocol* tif = lfp_tapeimage_open(mem);

    unsigned char fake_mem = 1;
    unsigned char* dst = &fake_mem;

    SECTION( "read over 4GB data in one chunk" ) {
        std::int64_t nread = 0;
        const auto err = lfp_readinto(tif, dst, 4*GB + 1, &nread);
        CHECK(err == LFP_PROTOCOL_FATAL_ERROR);

        auto msg = std::string(lfp_errormsg(tif));
        CHECK_THAT(msg, Contains("4GB"));
    }

    SECTION( "read over 4GB data in 2 chunks" ) {
        std::int64_t nread = 0;
        auto err = lfp_readinto(tif, dst, 3*GB, &nread);
        CHECK(err == LFP_OK);

        err = lfp_readinto(tif, dst, 2*GB, &nread);
        CHECK(err == LFP_PROTOCOL_FATAL_ERROR);

        auto msg = std::string(lfp_errormsg(tif));
        CHECK_THAT(msg, Contains("4GB"));
    }

    SECTION( "read little less than 4GB of data in over 4GB file" ) {
        /* We read less than 4GB, but added header size causes overflow */
        std::int64_t nread = 0;
        auto err = lfp_readinto(tif, dst, 4*GB - 2, &nread);
        CHECK(err == LFP_PROTOCOL_FATAL_ERROR);

        auto msg = std::string(lfp_errormsg(tif));
        CHECK_THAT(msg, Contains("4GB"));
    }

    SECTION( "seek beyond 4GB" ) {
        auto err = lfp_seek(tif, 4*GB + 1);
        CHECK(err == LFP_INVALID_ARGS);

        auto msg = std::string(lfp_errormsg(tif));
        CHECK_THAT(msg, Contains("4GB"));
    }

    SECTION( "seek beyond 4GB when zero is close to 4GB" ) {
        lfp_peel(tif, &mem);
        lfp_close(tif);

        auto err = lfp_seek(mem, 2*GB + 12);
        REQUIRE(err == LFP_OK);
        tif = lfp_tapeimage_open(mem);

        err = lfp_seek(tif, 2*GB);

        CHECK(err == LFP_PROTOCOL_FATAL_ERROR);
        const auto msg = std::string(lfp_errormsg(tif));
        CHECK_THAT(msg, Contains("4GB"));
    }

    lfp_close(tif);
}
#endif
