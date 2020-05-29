#include <ciso646>
#include <vector>
#include <cstring>

#include <catch2/catch.hpp>

#include <lfp/protocol.hpp>
#include <lfp/memfile.h>
#include <lfp/rp66.h>
#include <lfp/tapeimage.h>
#include <lfp/lfp.h>

#include "utils.hpp"

using namespace Catch::Matchers;

struct random_rp66 : random_memfile {
    random_rp66() : mem(copy()) {
        REQUIRE(not expected.empty());
    }

    ~random_rp66() {
        lfp_close(mem);
    }

    /*
     * The rp66 creation must still be parameterised on number-of-records
     * and size-of-records, which in catch cannot be passed to the constructor
     *
     * Only fixed-size records are made, which is slightly unfortunate.
     * However, generating consistent, variable-length records is a lot more
     * subtle and complicated, and it's reasonable to assume that a lot of rp66
     * files use fixed-size records anyway.
     *
     * TODO: variable-length records
     */
    void make(int records) {
        REQUIRE(records > 0);
        // constants defined by the format
        const char format = 0xFF;
        const std::uint8_t major = 1;

        const std::int64_t record_size = std::ceil(double(size) / records);
        assert(record_size + 4 <= std::numeric_limits< std::uint16_t >::max());

        INFO("Partitioning " << size << " bytes into "
            << records << " records of max "
            << record_size << " bytes"
        );

        auto src = std::begin(expected);
        std::int64_t remaining = expected.size();
        REQUIRE(remaining > 0);
        for (int i = 0; i < records; ++i) {
            const uint16_t n = std::min(record_size, remaining);
            auto head = std::vector< unsigned char >(4, 0);
            const uint16_t m = n + 4;
            std::memcpy(head.data() + 0, &m,      sizeof(m));
            std::memcpy(head.data() + 2, &format, sizeof(format));
            std::memcpy(head.data() + 3, &major,  sizeof(major));

            std::reverse(head.begin() + 0, head.begin() + 2);
            bytes.insert(bytes.end(), head.begin(), head.end());
            bytes.insert(bytes.end(), src, src + n);
            src += n;
            remaining -= n;
        }
        REQUIRE(remaining == 0);
        REQUIRE(src == std::end(expected));

        REQUIRE(bytes.size() == expected.size() + records * 4);
        lfp_close(f);
        f = nullptr;
        auto* tmp = lfp_memfile_openwith(bytes.data(), bytes.size());
        REQUIRE(tmp);
        f = lfp_rp66_open(tmp);
        REQUIRE(f);
    }

    std::vector< unsigned char > bytes;
    lfp_protocol* mem = nullptr;
};

TEST_CASE(
    "Empty file can be opened, reads zero bytes",
    "[visible envelope][rp66][empty]") {
    const auto file = std::vector< unsigned char > {
        /* First VE */
        0x00, 0x04,
        0xFF, 0x01,
        /* Second VE */
        0x00, 0x04,
        0xFF, 0x01,
        /* Third VE */
        0x00, 0x04,
        0xFF, 0x01,
    };

    auto* mem  = lfp_memfile_openwith(file.data(), file.size());
    auto* rp66 = lfp_rp66_open(mem);

    auto out = std::vector< unsigned char >(5, 0xFF);
    std::int64_t bytes_read = -1;
    const auto err = lfp_readinto(rp66, out.data(), 5, &bytes_read);

    CHECK(bytes_read == 0);
    CHECK(err == LFP_EOF);
    lfp_close(rp66);
}

TEST_CASE(
    "Reads 8 bytes from 8-bytes file",
    "[visible envelope][rp66]") {
    const auto file = std::vector< unsigned char > {
        /* First VE */
        0x00, 0x0C,
        0xFF, 0x01,

        /* Body */
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    };
    const auto expected = std::vector< unsigned char > {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    };

    auto* mem = lfp_memfile_openwith(file.data(), file.size());
    auto* rp66 = lfp_rp66_open(mem);

    auto out = std::vector< unsigned char >(8, 0xFF);
    std::int64_t bytes_read = -1;
    const auto err = lfp_readinto(rp66, out.data(), 8, &bytes_read);

    CHECK(bytes_read == 8);
    CHECK(err == LFP_OK);
    CHECK_THAT(out, Equals(expected));
    lfp_close(rp66);
}

TEST_CASE(
    "Read past end-of-file"
    "[visible envelope][rp66]") {

    const auto file = std::vector< unsigned char > {
        /* First Visible Envelope */
        0x00, 0x0C,
        0xFF, 0x01,
        /* Body */
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        /* Second Visible Envelope */
        0x00, 0x06,
        0xFF, 0x01,
        /* Body */
        0x09, 0x0A,
    };
    const auto expected = std::vector< unsigned char > {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
    };

    auto* mem = lfp_memfile_openwith(file.data(), file.size());
    auto* rp66 = lfp_rp66_open(mem);

    auto out = std::vector< unsigned char >(10, 0xFF);
    std::int64_t bytes_read = -1;
    const auto err = lfp_readinto(rp66, out.data(), 12, &bytes_read);

    CHECK(bytes_read == 10);
    CHECK(err == LFP_EOF);
    CHECK_THAT(out, Equals(expected));

    lfp_close(rp66);
}

TEST_CASE(
    "Wrong format-version is fatal"
    "[visible envelope][rp66]") {

    const auto file = std::vector< unsigned char > {
        /* First Visible Envelope */
        0x00, 0x06,
        0xFF, 0x01,
        /* Body */
        0x01, 0x02,
        /* Second Visible Envelope */
        0x00, 0x06,
        0xFE, 0x01,
        /* Body */
        0x09, 0x0A,
    };

    auto* mem = lfp_memfile_openwith(file.data(), file.size());
    auto* rp66 = lfp_rp66_open(mem);

    auto out = std::vector< unsigned char >(4, 0xFF);
    std::int64_t bytes_read = -1;
    const auto err = lfp_readinto(rp66, out.data(), 4, &bytes_read);
    CHECK(err == LFP_PROTOCOL_FATAL_ERROR);
    CHECK(bytes_read == 2);

    lfp_close(rp66);
}

TEST_CASE_METHOD(
    random_rp66,
    "Visible Envelope: A file can be read in a single read",
    "[visible envelope][rp66]") {
    const auto records = GENERATE(1, 2, 3, 5, 8, 13);
    make(records);

    std::int64_t nread = 0;
    const auto err = lfp_readinto(f, out.data(), out.size(), &nread);

    CHECK(err == LFP_OK);
    CHECK(nread == expected.size());
    CHECK_THAT(out, Equals(expected));
}

TEST_CASE_METHOD(
    random_rp66,
    "Visible Envelope: A file can be read in multiple, smaller reads",
    "[visible envelope][rp66]") {
    const auto records = GENERATE(1, 2, 3, 5, 8, 13);
    make(records);

    test_split_read(this);
}

TEST_CASE_METHOD(
    random_rp66,
    "Visible Envelope: single seek matches underlying handle",
    "[visible envelope][rp66]") {
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
    random_rp66,
    "Visible Envelope: multiple seeks and tells match underlying handle",
    "[visible envelope][rp66]") {
    const auto real_size = size;
    const auto records = GENERATE(1, 2, 3, 5, 8, 13);
    make(records);

    for (int i = 0; i < 4; ++i) {
        const auto n = GENERATE_COPY(take(1, random(0, real_size - 1)));
        auto err = lfp_seek(f, n);
        CHECK(err == LFP_OK);

        auto memerr = lfp_seek(mem, n);
        CHECK(memerr == LFP_OK);

        std::int64_t rp66_tell;
        std::int64_t mem_tell;
        err = lfp_tell(f, &rp66_tell);
        CHECK(err == LFP_OK);
        memerr = lfp_tell(mem, &mem_tell);
        CHECK(memerr == LFP_OK);
        CHECK(rp66_tell == mem_tell);
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
    "Seek and read to record boarders",
    "[rp66]") {
    const auto file = std::vector< unsigned char > {
        /* Some dummy bytes to emulate zero > 0 */
        0x10, 0x11, 0x12,

        /* Visible Record 0 */
        0x00, 0x0C,
        0xFF, 0x01,

        /* Body */
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,

        /* Visible Record 1*/
        0x00, 0x06,
        0xFF, 0x01,

        /* Body */
        0x09, 0x0A,

        /* broken record */
        0x00, 0x00, 0x00, 0x00
    };

    auto* mem = lfp_memfile_openwith(file.data(), file.size());
    CHECK(mem != nullptr);
    /*
     * Emulate opening rp66 at an arbitrary record by reading the first 3 dummy
     * bytes before opening the rp66 protocol
     */
    auto dummy = std::vector< unsigned char >(3, 0xFF);
    std::int64_t bytes_read = -1;
    auto err = lfp_readinto(mem, dummy.data(), 3, &bytes_read);
    CHECK(err == LFP_OK);
    CHECK(bytes_read == 3);

    auto* rp66 = lfp_rp66_open(mem);
    CHECK(rp66 != nullptr);

    SECTION( "seek to header border: not indexed" ) {
        test_seek_and_read(rp66, 10, LFP_PROTOCOL_FATAL_ERROR);
    }

    SECTION( "seek to header border: indexed" ) {
        std::int64_t bytes_read = -1;
        auto out = std::vector< unsigned char >(10, 0xFF);
        err = lfp_readinto(rp66, out.data(), 10, &bytes_read);
        CHECK(err == LFP_OK);

        err = lfp_seek(rp66, 1);
        CHECK(err == LFP_OK);

        bytes_read = -1;
        char buf;
        err = lfp_readinto(rp66, &buf, 1, &bytes_read);
        CHECK(err == LFP_OK);

        test_seek_and_read(rp66, 10, LFP_PROTOCOL_FATAL_ERROR);
    }

    SECTION( "read to header border: not indexed" ) {
        auto out = std::vector< unsigned char >(10, 0xFF);
        std::int64_t bytes_read = -1;
        auto err = lfp_readinto(rp66, out.data(), 10, &bytes_read);
        CHECK(err == LFP_OK);
        CHECK(bytes_read == 10);

        std::int64_t tell;
        err = lfp_tell(rp66, &tell);
        CHECK(tell == 10);
        CHECK(err == LFP_OK);
    }

    SECTION( "read to header border: indexed" ) {
        auto err = lfp_seek(rp66, 10);
        CHECK(err == LFP_OK);

        err = lfp_seek(rp66, 0);
        CHECK(err == LFP_OK);

        auto out = std::vector< unsigned char >(10, 0xFF);
        std::int64_t bytes_read = -1;
        err = lfp_readinto(rp66, out.data(), 10, &bytes_read);
        CHECK(err == LFP_OK);
        CHECK(bytes_read == 10);

        std::int64_t tell;
        err = lfp_tell(rp66, &tell);
        CHECK(tell == 10);
        CHECK(err == LFP_OK);
    }

    lfp_close(rp66);
}

TEST_CASE(
    "Read sul before opening rp66",
    "[rp66][tif][open]") {
    const auto file = std::vector< unsigned char > {
        /* tapemark */
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x22, 0x00, 0x00, 0x00,

        /* Some dummy bytes to emulate the precense of a SUL */
        0x10, 0x11, 0x12, 0x13,

        /* First Visible Envelope */
        0x00, 0x0C,
        0xFF, 0x01,
        /* Body */
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        /* Second Visible Envelope */
        0x00, 0x06,
        0xFF, 0x01,
        /* Body */
        0x09, 0x0A,
        /* tapemark */
        0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x2F, 0x00, 0x00, 0x00,

        0x01, 0x00, 0x00, 0x00,
        0x22, 0x00, 0x00, 0x00,
        0x3A, 0x00, 0x00, 0x00,
    };
    const auto expected_sul = std::vector< unsigned char > {
        0x10, 0x11, 0x12, 0x13,
    };
    const auto expected = std::vector< unsigned char > {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
    };

    auto* mem = lfp_memfile_openwith(file.data(), file.size());
    CHECK(mem != nullptr);

    auto* tif = lfp_tapeimage_open(mem);
    CHECK(tif != nullptr);

    /*
     * Before opening the rp66 protocol, read the "SUL" with the current
     * protocol. This should leave the handle at the right offset
     * when opening the rp66 later.
     */
    auto sul = std::vector< unsigned char >(4, 0xFF);
    std::int64_t bytes_read = -1;
    auto err = lfp_readinto(tif, sul.data(), 4, &bytes_read);

    CHECK(bytes_read == 4);
    CHECK(err == LFP_OK);
    CHECK_THAT(sul, Equals(expected_sul));

    auto* rp66 = lfp_rp66_open(tif);
    CHECK(rp66 != nullptr);

    SECTION( "Tell starts at 0" ) {
        std::int64_t tell;
        lfp_tell(rp66, &tell);
        CHECK(tell == 0);

        char buf;
        err = lfp_readinto(rp66, &buf, 1, nullptr);
        CHECK(err == LFP_OK);
        CHECK(buf == 0x01);
    }

    SECTION( "Seek past index" ) {
        err = lfp_seek(rp66, 9);
        CHECK(err == 0);

        std::int64_t tell;
        lfp_tell(rp66, &tell);
        CHECK(tell == 9);

        char buf;
        err = lfp_readinto(rp66, &buf, 1, nullptr);
        CHECK(err == LFP_OK);
        CHECK(buf == 0x0A);
    }

    SECTION( "Seek with index" ) {
        err = lfp_seek(rp66, 2);
        CHECK(err == 0);

        std::int64_t tell;
        lfp_tell(rp66, &tell);
        CHECK(tell == 2);

        char buf;
        err = lfp_readinto(rp66, &buf, 1, nullptr);
        CHECK(err == LFP_OK);
        CHECK(buf == 0x03);
    }

    SECTION( "Read past index" ) {
        auto out = std::vector< unsigned char >(10, 0xFF);
        err = lfp_readinto(rp66, out.data(), 10, &bytes_read);

        CHECK(bytes_read == 10);
        CHECK(err == LFP_OK);
        CHECK_THAT(out, Equals(expected));
    }

    SECTION( "Read indexed records" ) {
        err = lfp_seek(rp66, 10);
        CHECK(err == 0);

        err = lfp_seek(rp66, 0);
        CHECK(err == 0);

        auto out = std::vector< unsigned char >(10, 0xFF);
        err = lfp_readinto(rp66, out.data(), 10, &bytes_read);

        CHECK(bytes_read == 10);
        CHECK(err == LFP_OK);
        CHECK_THAT(out, Equals(expected));
    }

    lfp_close(rp66);
}

TEST_CASE(
    "Operation past eof"
    "[rp66][eof]") {

    const auto contents = std::vector< unsigned char > {
        0x00, 0x0C,
        0xFF, 0x01,

        /* begin body */
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,
        /* end body */
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
    auto* rp66 = lfp_rp66_open(cfile);

    SECTION( "Read past eof" ) {
        auto out = std::vector< unsigned char >(10, 0xFF);
        std::int64_t bytes_read = -1;
        const auto err = lfp_readinto(rp66, out.data(), 10, &bytes_read);

        CHECK(bytes_read == 8);
        CHECK(err == LFP_EOF);
        CHECK_THAT(out, Equals(expected1));

        std::int64_t tell;
        lfp_tell(rp66, &tell);
        CHECK(tell == 8);
    }

    SECTION( "Read past eof - after a seek past eof" ) {
        auto err = lfp_seek(rp66, 10);
        CHECK(err == LFP_OK);

        char x = 0xFF;
        std::int64_t bytes_read = -1;
        err = lfp_readinto(rp66, &x, 1, &bytes_read);

        CHECK(err == LFP_EOF);
        CHECK(bytes_read == 0);
    }
    lfp_close(rp66);
}

TEST_CASE(
    "Trying to open protocol at end-of-file",
    "[visible envelope][rp66]") {

    std::FILE* fp = std::tmpfile();
    std::fputs("Very simple file", fp);
    std::rewind(fp);

    auto* cfile = lfp_cfile(fp);

    auto err = lfp_seek(cfile, 20);
    CHECK(err == LFP_OK);

    auto* rp66 = lfp_rp66_open(cfile);

    char x = 0xFF;
    std::int64_t bytes_read = -1;
    err = lfp_readinto(rp66, &x, 1, &bytes_read);

    CHECK(err == LFP_EOF);
    CHECK(bytes_read == 0);

    lfp_close(rp66);
}

TEST_CASE(
    "Layered rp66 closes correctly",
    "[visible envelope][rp66]") {
    const auto file = std::vector< unsigned char > {
        /* First Visible Envelope - outer layer*/
        0x00, 0x0A,
        0xFF, 0x01,
        /* First Visible Envelope - inner layer*/
        0x00, 0x06,
        0xFF, 0x01,
        /* Body */
        0x01, 0x02,
    };

    auto* mem = lfp_memfile_openwith(file.data(), file.size());
    auto* rp66  = lfp_rp66_open(mem);
    auto* outer = lfp_rp66_open(rp66);

    auto err = lfp_close(outer);
    CHECK(err == LFP_OK);
}

TEST_CASE(
    "Blocked inner layer is processed correctly in rp66",
    "[rp66][blockedpipe]") {

    std::vector< unsigned char > data = {
        0x00, 0x40, 0xFF, 0x01,

        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C,
        0x0D, 0x0E, 0x0F, 0x10,
    };

    SECTION( "incomplete in header" ) {

        auto* blocked = new blockedpipe(data, 3);
        auto* rp66 = lfp_rp66_open(blocked);

        auto out = std::vector< unsigned char >(16, 0xFF);
        std::int64_t bytes_read = -1;
        const auto err = lfp_readinto(rp66, out.data(), 16, &bytes_read);

        // TODO: questionable. There was never recovery in the first place
        // incomplete would make more sense
        CHECK(err == LFP_PROTOCOL_FAILEDRECOVERY);
        CHECK(bytes_read == 0);

        lfp_close(rp66);
    }

    SECTION( "incomplete in data" ) {
        auto* blocked = new blockedpipe(data, 10);
        auto* rp66 = lfp_rp66_open(blocked);

        SECTION ("read") {
            auto out = std::vector< unsigned char >(12, 0xFF);
            std::int64_t bytes_read = -1;
            const auto err = lfp_readinto(rp66, out.data(), 12, &bytes_read);

            CHECK(err == LFP_OKINCOMPLETE);
            CHECK(bytes_read == 6);

            const auto expected = std::vector< unsigned char > {
                0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF //never written
            };
            CHECK_THAT(out, Equals(expected));
        }

        SECTION ("seek") {
            test_seek_and_read(rp66, 12, LFP_OKINCOMPLETE);
        }

        lfp_close(rp66);
    }
}

TEST_CASE_METHOD(
    device,
    "rp66: Reading truncated file return expected errors",
    "[rp66]") {

    SECTION( "testing on "+ device_type) {

    SECTION( "truncated in header" ) {
        const auto contents = std::vector< unsigned char > {
            0x00, 0x0C,
            0xFF, 0x01,

            0x01, 0x02, 0x03, 0x04,
            0x05, 0x06, 0x07, 0x08,

            0x00,
            //oops
        };

        const auto expected = std::vector< unsigned char > {
            0x01, 0x02, 0x03, 0x04,
            0x05, 0x06, 0x07, 0x08,
            0xFF, 0xFF //Last two bytes are never written by lfp
        };

        auto* inner = create(contents);
        auto* rp66 = lfp_rp66_open(inner);

        SECTION( "read" ){
            auto out = std::vector< unsigned char >(10, 0xFF);
            std::int64_t bytes_read = -1;
            const auto err = lfp_readinto(rp66, out.data(), 10, &bytes_read);

            CHECK(err == LFP_UNEXPECTED_EOF);
            CHECK(bytes_read == 8);
            auto msg = std::string(lfp_errormsg(rp66));
            CHECK_THAT(msg, Contains("unexpected EOF"));
            CHECK_THAT(msg, Contains("got 1 bytes"));

            CHECK_THAT(out, Equals(expected));

            CHECK(lfp_eof(rp66));
        }

        SECTION( "seek past data" ) {
            test_seek_and_read(rp66, 10, LFP_UNEXPECTED_EOF, LFP_EOF);
        }

        lfp_close(rp66);
    }

    SECTION( "truncated after header" ) {
        const auto contents = std::vector< unsigned char > {
            0x00, 0x08,
            0xFF, 0x01,

            0x01, 0x02, 0x03, 0x04,

            0x00, 0x0C,
            0xFF, 0x01,
        };

        auto* inner = create(contents);
        auto* rp66 = lfp_rp66_open(inner);

        SECTION( "read" ) {
            auto out = std::vector< unsigned char >(10, 0xFF);
            std::int64_t bytes_read = -1;
            const auto err = lfp_readinto(rp66, out.data(), 10, &bytes_read);

            CHECK(err == LFP_UNEXPECTED_EOF);
            CHECK(bytes_read == 4);

            std::int64_t tell;
            lfp_tell(rp66, &tell);
            CHECK(tell == 4);
        }

        SECTION( "seek to border" ) {
            test_seek_and_read(rp66, 4, LFP_UNEXPECTED_EOF);
        }

        SECTION( "seek in declared data" ) {
            test_seek_and_read(rp66, 10, LFP_UNEXPECTED_EOF);
        }

        SECTION( "seek past declared data" ) {
            test_seek_and_read(rp66, 100, LFP_EOF);
        }

        lfp_close(rp66);
    }

    SECTION( "truncated in data" ) {
        const auto contents = std::vector< unsigned char > {
            0x00, 0x0C,
            0xFF, 0x01,

            0x01, 0x02, 0x03, 0x04,
            //oops
        };

        const auto expected = std::vector< unsigned char > {
            0x01, 0x02, 0x03, 0x04,
            0xFF, 0xFF, 0xFF, 0xFF,
        };


        auto* inner = create(contents);
        auto* rp66 = lfp_rp66_open(inner);

        SECTION( "read" ) {
            auto out = std::vector< unsigned char >(8, 0xFF);
            std::int64_t bytes_read = -1;
            const auto err = lfp_readinto(rp66, out.data(), 8, &bytes_read);

            CHECK(err == LFP_UNEXPECTED_EOF);
            CHECK(bytes_read == 4);
            auto msg = std::string(lfp_errormsg(rp66));
            CHECK_THAT(msg, Contains("unexpected EOF"));
            CHECK_THAT(msg, Contains("got 4 bytes"));

            CHECK_THAT(out, Equals(expected));

            CHECK(lfp_eof(rp66));
        }

        SECTION( "seek in data" ) {
            test_seek_and_read(rp66, 3, LFP_OK);
        }

        // TODO: memfile for all the tests
        SECTION( "seek to border" ) {
            test_seek_and_read(rp66, 4, LFP_OK, LFP_UNEXPECTED_EOF, this);
        }

        SECTION( "seek into declared data" ) {
            test_seek_and_read(rp66, 6, LFP_OK, LFP_UNEXPECTED_EOF, this);
        }

        SECTION( "seek past declared data" ) {
            test_seek_and_read(rp66, 100, LFP_OK, LFP_EOF, this);
        }

        lfp_close(rp66);
    }

    }
}

TEST_CASE_METHOD(
    device,
    "rp66: Empty record",
    "[rp66]") {

    SECTION( "tested on "+device_type) {

    SECTION( "empty record in the middle" ) {
        const auto contents = std::vector< unsigned char > {
            0x00, 0x08,
            0xFF, 0x01,

            0x01, 0x02, 0x03, 0x04,

            0x00, 0x04,  //empty record
            0xFF, 0x01,

            0x00, 0x08,
            0xFF, 0x01,

            0x05, 0x06, 0x07, 0x08,
        };

        auto* inner = create(contents);
        auto* rp66 = lfp_rp66_open(inner);

        SECTION( "read through empty record" ) {
            auto out = std::vector< unsigned char >(10, 0xFF);
            std::int64_t bytes_read = -1;
            const auto err = lfp_readinto(rp66, out.data(), 10, &bytes_read);

            CHECK(err == LFP_EOF);
            CHECK(bytes_read == 8);
        }

        SECTION( "seek through empty record" ) {
            test_seek_and_read(rp66, 6, LFP_OK);
        }

        lfp_close(rp66);
    }

    SECTION( "ending on empty record" ) {
        const auto contents = std::vector< unsigned char > {
            0x00, 0x08,
            0xFF, 0x01,

            0x01, 0x02, 0x03, 0x04,

            0x00, 0x04,  //empty record
            0xFF, 0x01,
        };

        auto* mem = lfp_memfile_openwith(contents.data(), contents.size());
        auto* rp66 = lfp_rp66_open(mem);

        auto out = std::vector< unsigned char >(10, 0xFF);
        std::int64_t bytes_read = -1;
        const auto err = lfp_readinto(rp66, out.data(), 10, &bytes_read);

        CHECK(err == LFP_EOF);
        CHECK(bytes_read == 4);

        lfp_close(rp66);
    }

    }

}
