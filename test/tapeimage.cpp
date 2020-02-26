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
