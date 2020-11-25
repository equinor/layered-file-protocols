#include <ciso646>
#include <errno.h>

#include <catch2/catch.hpp>

#include <lfp/tapeimage.h>
#include <lfp/lfp.h>

#include "utils.hpp"


using namespace Catch::Matchers;

namespace {

struct random_cfile : random_memfile {
    random_cfile() {
        REQUIRE(not expected.empty());

        std::FILE* fp = std::tmpfile();
        std::fwrite(expected.data(), 1, expected.size(), fp);
        std::rewind(fp);

        lfp_close(f);
        f = nullptr;

        f = lfp_cfile(fp);
        REQUIRE(f);
    }
};

}

TEST_CASE(
    "File closes correctly",
    "[cfile][close]") {

    std::FILE* fp = std::tmpfile();
    std::fputs("Very simple file", fp);
    std::rewind(fp);

    auto* cfile = lfp_cfile(fp);

    auto err = lfp_close(cfile);
    CHECK(err == LFP_OK);
}

TEST_CASE(
    "Layered cfile closes correctly",
    "[cfile][close]") {
    const auto contents = std::vector< unsigned char > {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x18, 0x00, 0x00, 0x00,

        0x01, 0x02, 0x03, 0x04,
        0x54, 0x41, 0x50, 0x45,
        0x4D, 0x41, 0x52, 0x4B,

        0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x24, 0x00, 0x00, 0x00,
    };
    std::FILE* fp = std::tmpfile();
    std::fwrite(contents.data(), 1, contents.size(), fp);
    std::rewind(fp);

    auto* cfile = lfp_cfile(fp);
    auto* outer = lfp_tapeimage_open(cfile);

    auto err = lfp_close(outer);
    CHECK(err == LFP_OK);
}

TEST_CASE(
    "Layering non-existing file is a no-op",
    "[cfile][filehandle]") {

    FILE* fp    = nullptr;
    auto* cfile = lfp_cfile(fp);
    auto* tif   = lfp_tapeimage_open(cfile);

    CHECK(!cfile);
    CHECK(!tif);
}

TEST_CASE(
    "Operations on directory filehandle",
    "[cfile][filehandle]") {

    FILE* fp = std::fopen(".", "rb");
    /* On some systems error might happen already after open. Then test is not
     * relevant
     */
    if (fp) {
        auto* cfile = lfp_cfile(fp);

        SECTION( "Read on directory filehandle" ) {
            auto buffer = std::vector< unsigned char >(4, 0xFF);
            std::int64_t nread;
            auto err = lfp_readinto(cfile, buffer.data(), 4, &nread);
            CHECK(err == LFP_IOERROR);
            auto msg = std::string(lfp_errormsg(cfile));
            CHECK_THAT(msg, Contains("Is a directory"));
        }

        SECTION( "Seek on directory filehandle" ) {
            /*
            * It's not clear what happens on seek operation.
            * On certain systems fseek gives no indication of error (no error
            * code, no ferror), but errno gets set.
            * Hence the best option seems to be not to test this setup on
            * seek and leave delay dealing with errors to read
            */

            //auto err = lfp_seek(cfile, 1);
        }

        /*
         * Operations in this test happen to set errno, which persists up until
         * it is explicitly cleared. Hence manually unset it to prevent
         * possible collision with other tests.
         */
        errno = 0;

        lfp_close(cfile);
    }
}

TEST_CASE(
    "Unsupported peel leaves the protocol intact",
    "[cfile][peel]") {
    std::FILE* fp = std::tmpfile();
    std::fputs("Very simple file" , fp);
    std::rewind(fp);

    auto* cfile = lfp_cfile(fp);

    lfp_protocol* protocol;
    auto err = lfp_peel(cfile, &protocol);

    CHECK(err == LFP_LEAF_PROTOCOL);

    auto buffer = std::vector< unsigned char >(17, 0xFF);
    std::int64_t nread;
    err = lfp_readinto(cfile, buffer.data(), 17, &nread);

    CHECK(err == LFP_EOF);
    CHECK(nread == 16);

    err = lfp_close(cfile);
    CHECK(err == LFP_OK);
}

TEST_CASE(
    "Unsupported peek leaves the protocol intact",
    "[cfile][peek]") {
    std::FILE* fp = std::tmpfile();
    std::fputs("Very simple file" , fp);
    std::rewind(fp);

    auto* cfile = lfp_cfile(fp);

    lfp_protocol* protocol;
    auto err = lfp_peek(cfile, &protocol);

    CHECK(err == LFP_LEAF_PROTOCOL);

    auto buffer = std::vector< unsigned char >(17, 0xFF);
    std::int64_t nread;
    err = lfp_readinto(cfile, buffer.data(), 17, &nread);

    CHECK(err == LFP_EOF);
    CHECK(nread == 16);

    err = lfp_close(cfile);
    CHECK(err == LFP_OK);
}

TEST_CASE_METHOD(
    random_cfile,
    "Cfile can be read",
    "[cfile][read]") {

    SECTION( "full read" ) {
        std::int64_t nread = -1;
        const auto err = lfp_readinto(f, out.data(), out.size(), &nread);

        CHECK(err == LFP_OK);
        CHECK(nread == expected.size());
        CHECK_THAT(out, Equals(expected));
    }

    SECTION( "incomplete read" ) {
        std::int64_t nread = -1;
        const auto err = lfp_readinto(f, out.data(), 2*out.size(), &nread);

        CHECK(err == LFP_EOF);
        CHECK(nread == expected.size());
        CHECK_THAT(out, Equals(expected));
    }

    SECTION( "A file can be read in multiple, smaller reads" ) {
        test_split_read(this);
    }

    SECTION( "negative read" ) {
        std::int64_t nread = -1;
        const auto err = lfp_readinto(f, out.data(), -1, &nread);

        CHECK(err == LFP_INVALID_ARGS);
        auto msg = std::string(lfp_errormsg(f));
        CHECK_THAT(msg, Contains(">= 0"));
    }

    SECTION( "zero read" ) {
        std::int64_t nread = -1;
        const auto err = lfp_readinto(f, out.data(), 0, &nread);

        CHECK(err == LFP_OK);
        CHECK(nread == 0);
    }
}

TEST_CASE_METHOD(
    random_cfile,
    "Cfile can be seeked",
    "[cfile][seek]") {

    SECTION( "correct seek" ) {
        test_random_seek(this);
    }

    SECTION( "seek beyond file end" ) {
        // untested. behavior is determined by the handle
    }

    SECTION( "negative seek" ) {
        const auto err = lfp_seek(f, -1);

        CHECK(err == LFP_INVALID_ARGS);
        auto msg = std::string(lfp_errormsg(f));
        CHECK_THAT(msg, Contains(">= 0"));
    }
}


TEST_CASE_METHOD(
    random_cfile,
    "Cfile eof",
    "[cfile][eof]") {

    SECTION( "eof reports after read past-end" ) {
        std::int64_t nread = -1;
        const auto err = lfp_readinto(f, out.data(), out.size() +1, &nread);

        CHECK(err == LFP_EOF);
        CHECK(lfp_eof(f));
    }

    SECTION( "eof not reported on read to end" ) {
        std::int64_t nread = -1;
        const auto err = lfp_readinto(f, out.data(), out.size(), &nread);

        CHECK(err == LFP_OK);
        CHECK(!lfp_eof(f));
    }

    SECTION( "eof not reported on seek to end" ) {
        const auto err = lfp_seek(f, out.size());

        CHECK(err == LFP_OK);
        CHECK(!lfp_eof(f));
    }
}

TEST_CASE(
    "> 2GB file",
    "[cfile] [2GB] [long]") {

    const std::int64_t GB = 1024 * 1024 * 1024;
    const std::string s = "Big, 2GB file";
    const auto slen = s.length();
    const std::int64_t begin = 2*GB - 1;

    std::FILE* fp = std::tmpfile();
    std::fseek(fp, begin, SEEK_SET);
    std::fputs(s.c_str(), fp);
    std::rewind(fp);

    const auto expected = std::vector< unsigned char > {
         0x66, 0x69, 0x6C, 0x65,
    };

    SECTION( "seek and read beyond 2GB" ) {
        auto* cfile = lfp_cfile(fp);
        auto err = lfp_seek(cfile, begin + slen - 4);
        CHECK(err == LFP_OK);

        std::int64_t bytes_read = -1;
        auto out = std::vector< unsigned char >(4, 0xFF);
        err = lfp_readinto(cfile, out.data(), 4, &bytes_read);

        CHECK(bytes_read == 4);
        CHECK(err == LFP_OK);
        CHECK_THAT(out, Equals(expected));

        std::int64_t tell;
        err = lfp_tell(cfile, &tell);
        CHECK(err == LFP_OK);
        CHECK(tell == begin + slen);

        //should delete the file
        err = lfp_close(cfile);
        CHECK(err == LFP_OK);
    }

    SECTION( "seek for more and less than 2GB" ) {
        auto* cfile = lfp_cfile(fp);

        std::int64_t tell;

        auto err = lfp_seek(cfile, begin + slen);
        CHECK(err == LFP_OK);
        err = lfp_tell(cfile, &tell);
        CHECK(err == LFP_OK);
        CHECK(tell == begin + slen);

        err = lfp_seek(cfile, 1);
        CHECK(err == LFP_OK);
        err = lfp_tell(cfile, &tell);
        CHECK(err == LFP_OK);
        CHECK(tell == 1);

        //should delete the file
        err = lfp_close(cfile);
        CHECK(err == LFP_OK);
    }

    SECTION( "open file near 2GB mark" ) {
        std::fseek(fp, begin, SEEK_SET);

        auto* cfile = lfp_cfile(fp);
        std::int64_t tell;
        auto err = lfp_tell(cfile, &tell);
        CHECK(err == LFP_OK);
        CHECK(tell == 0);

        err = lfp_seek(cfile, slen - 4);
        CHECK(err == LFP_OK);

        std::int64_t bytes_read = -1;
        auto out = std::vector< unsigned char >(4, 0xFF);
        err = lfp_readinto(cfile, out.data(), 4, &bytes_read);

        CHECK(bytes_read == 4);
        CHECK(err == LFP_OK);
        CHECK_THAT(out, Equals(expected));

        err = lfp_tell(cfile, &tell);
        CHECK(err == LFP_OK);
        CHECK(tell == slen);

        //should delete the file
        err = lfp_close(cfile);
        CHECK(err == LFP_OK);
    }
}
