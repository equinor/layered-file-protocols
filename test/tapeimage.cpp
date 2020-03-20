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
        const std::uint32_t record = 0;

        const std::int64_t record_size = std::ceil(double(size) / records);
        INFO("Partitioning " << size << " bytes into "
            << records << " records of max "
            << record_size << " bytes"
        );

        auto src = std::begin(expected);
        std::int64_t remaining = expected.size();
        REQUIRE(remaining > 0);
        std::uint32_t prev = 0;
        for (int i = 0; i < records; ++i) {
            const auto n = std::min(record_size, remaining);
            auto head = std::vector< unsigned char >(12, 0);
            const std::uint32_t next = n + tape.size() + head.size();
            std::memcpy(head.data() + 0, &record, sizeof(record));
            std::memcpy(head.data() + 4, &prev,   sizeof(prev));
            std::memcpy(head.data() + 8, &next,   sizeof(next));

            prev = tape.size();
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
        std::memcpy(tail.data() + 4, &prev, sizeof(prev));
        std::memcpy(tail.data() + 8, &eof,  sizeof(eof));
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
        CHECK(err == LFP_EOF);
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

        0x01, 0x00, 0x00, 0x00,
        0x14, 0x00, 0x00, 0x00,
        0x34, 0x00, 0x00, 0x00,
    };

    auto* mem = lfp_memfile_openwith(contents.data(), contents.size());
    auto* tif = lfp_tapeimage_open(mem);

    SECTION( "negative seek" ) {
        const auto err = lfp_seek(tif, -1);
        CHECK(err == LFP_INVALID_ARGS);
        auto msg = std::string(lfp_errormsg(tif));
        CHECK_THAT(msg, Contains("< 0"));
    }

    SECTION( "seek outside data borders" ) {
        const auto err = lfp_seek(tif, 16);
        CHECK(err == LFP_PROTOCOL_FATAL_ERROR);
        auto msg = std::string(lfp_errormsg(tif));
        CHECK_THAT(msg, Contains("beyond end"));
    }

    SECTION( "seek to header border" ) {
        auto err = lfp_seek(tif, 8);
        CHECK(err == LFP_OK);

        std::int64_t tell;
        err = lfp_tell(tif, &tell);
        CHECK(err == LFP_OK);
        CHECK(tell == 8);
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

TEST_CASE(
    "Error in tapeimage constructor doesn't destroy underlying file",
    "[tapeimage][open]") {

    class memfake : public lfp_protocol
    {
      public:
        memfake(int *livepointer) {
            this->livepointer = livepointer;
            *this->livepointer += 1;
        }
        ~memfake() override {
            *this->livepointer -= 1;
        }

        void close() noexcept(true) override {}
        lfp_status readinto(
            void *dst,
            std::int64_t len,
            std::int64_t *bytes_read) noexcept(true) override {
            return LFP_RUNTIME_ERROR;
        }

        int eof() const noexcept(true) override { return 0; }
        lfp_protocol* peel() noexcept (false) override { throw; }
        lfp_protocol* peek() const noexcept (false) override { throw; }

      private:
        int *livepointer;
    };

    int counter = 0;
    int* livepointer = &counter;
    auto* mem = new memfake(livepointer);
    CHECK(counter == 1);
    auto* tif = lfp_tapeimage_open(mem);

    CHECK(tif == nullptr);
    CHECK(counter == 1);

    lfp_close(mem);
}

TEST_CASE(
    "Reading truncated file return expected errors",
    "[tapeimage]") {

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

        auto* mem = lfp_memfile_openwith(contents.data(), contents.size());
        auto* tif = lfp_tapeimage_open(mem);

        auto out = std::vector< unsigned char >(10, 0xFF);
        std::int64_t bytes_read = -1;
        const auto err = lfp_readinto(tif, out.data(), 10, &bytes_read);

        CHECK(err == LFP_UNEXPECTED_EOF);
        auto msg = std::string(lfp_errormsg(tif));
        CHECK_THAT(msg, Contains("unexpected EOF"));
        CHECK_THAT(msg, Contains("got 0 bytes"));

        CHECK_THAT(out, Equals(expected));

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

        auto* mem = lfp_memfile_openwith(contents.data(), contents.size());
        auto* tif = lfp_tapeimage_open(mem);

        auto out = std::vector< unsigned char >(10, 0xFF);
        std::int64_t bytes_read = -1;
        const auto err = lfp_readinto(tif, out.data(), 10, &bytes_read);

        CHECK(err == LFP_UNEXPECTED_EOF);
        auto msg = std::string(lfp_errormsg(tif));
        CHECK_THAT(msg, Contains("unexpected EOF"));
        CHECK_THAT(msg, Contains("got 8 bytes"));

        CHECK_THAT(out, Equals(expected));

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


        auto* mem = lfp_memfile_openwith(contents.data(), contents.size());
        auto* tif = lfp_tapeimage_open(mem);

        auto out = std::vector< unsigned char >(8, 0xFF);
        std::int64_t bytes_read = -1;
        const auto err = lfp_readinto(tif, out.data(), 8, &bytes_read);

        CHECK(err == LFP_UNEXPECTED_EOF);
        auto msg = std::string(lfp_errormsg(tif));
        CHECK_THAT(msg, Contains("unexpected EOF"));
        CHECK_THAT(msg, Contains("got 4 bytes"));

        CHECK_THAT(out, Equals(expected));

        lfp_close(tif);
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
    auto err = lfp_seek(tif, 100);
    CHECK(err == LFP_OK);

    auto out = std::vector< unsigned char >(1, 0xFF);
    std::int64_t bytes_read = -1;
    err = lfp_readinto(tif, out.data(), 1, &bytes_read);
    CHECK(err == LFP_UNEXPECTED_EOF);

    lfp_close(tif);
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

            // TODO: possibly should return number of bytes actually read
            // before failure happened?
            /*
            CHECK(bytes_read == 12);
            auto read = std::vector< unsigned char >(
                out.begin(),
                std::next(out.begin(), 12)
                );
            CHECK_THAT(read, Equals(expected));
            */
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

        void seek(std::int64_t n) noexcept (false) {
            // for the purpose of this test do nothing and
            // let readinto to handle return of the correct header
            return;
        }
        lfp_protocol* peel() noexcept (false) override { throw; }
        lfp_protocol* peek() const noexcept (false) override { throw; }

      private:
        std::vector< header > headers;
        std::vector< header >::const_iterator it;
    };

    auto* mem = new memfake();
    auto* tif = lfp_tapeimage_open(mem);

    unsigned char fake_mem = 1;
    unsigned char* dst = &fake_mem;

    const size_t GB = 1024 * 1024 * 1024;

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
        /*
        // TODO:
        // would be the case of several logical files

        // set TIF start to be at 3GB + 24 bytes (header2)

        auto err = lfp_seek(tif, 2*GB);
        CHECK(err == LFP_PROTOCOL_FATAL_ERROR);

        auto msg = std::string(lfp_errormsg(tif));
        CHECK_THAT(msg, Contains("4GB")); */
    }

    lfp_close(tif);
}
