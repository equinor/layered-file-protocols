#include <ciso646>
#include <memory>
#include <vector>

#include <catch2/catch.hpp>

#include <lfp/lfp.h>
#include <lfp/memfile.h>
#include <lfp/tapeimage.h>

#include "utils.hpp"

using namespace Catch::Matchers;

/*
 * The tests for memfile are largely intended as a test for the interface
 * itself, both for correctness, but also some "real"-world experience.
 */

TEST_CASE("Closing nullptr is a no-op", "[mem][close]") {
    const auto err = lfp_close(nullptr);
    CHECK(err == LFP_OK);
}

TEST_CASE("A mem-file can be closed", "[mem][close]") {
    // TODO: replace this with a custom "close-observer file"
    auto f = memopen();
    const auto err = lfp_close(f.get());
    CHECK(err == LFP_OK);
    f.release();
}

TEST_CASE(
    "Layered memfile closes correctly",
    "[mem][close]") {
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
    auto* mem = lfp_memfile_openwith(contents.data(), contents.size());
    auto* outer = lfp_tapeimage_open(mem);

    auto err = lfp_close(outer);
    CHECK(err == LFP_OK);
}

TEST_CASE_METHOD(
        random_memfile,
        "A mem-file can be opened with data and read from",
        "[mem]") {
    std::int64_t nread = 0;
    const auto err = lfp_readinto(f, out.data(), out.size(), &nread);
    CHECK(err == LFP_OK);
    CHECK(nread == expected.size());
    CHECK_THAT(out, Equals(expected));

    //EOF is reached
    CHECK(lfp_eof(f));
}

TEST_CASE_METHOD(
        random_memfile,
        "Reading 0 bytes is allowed",
        "[mem]") {
    std::int64_t nread = 0;
    const auto err = lfp_readinto(f, out.data(), 0, &nread);
    CHECK(err == LFP_OK);
    CHECK(nread == 0);
}

TEST_CASE_METHOD(
        random_memfile,
        "Asking more data than available stops at EOF",
        "[mem]") {
    std::int64_t nread = 0;
    const auto err = lfp_readinto(f, out.data(), 2*out.size(), &nread);

    CHECK(err == LFP_EOF);
    CHECK(lfp_eof(f));
    CHECK(nread == expected.size());
    CHECK_THAT(out, Equals(expected));
}

TEST_CASE_METHOD(
        random_memfile,
        "EOF not repored on seek to start after read past-end",
        "[mem]") {
    std::int64_t nread = 0;
    auto err = lfp_readinto(f, out.data(), 2*out.size(), &nread);

    REQUIRE(err == LFP_EOF);
    REQUIRE(lfp_eof(f));

    err = lfp_seek(f, 0);
    CHECK(err == LFP_OK);
    CHECK(!lfp_eof(f));

}

TEST_CASE_METHOD(
        random_memfile,
        "Doing multiple reads yields the full file",
        "[mem]") {
    test_split_read(this);
}

TEST_CASE("Negative seek return invalid args error", "[mem]") {
    auto* f = lfp_memfile_open();
    const auto err = lfp_seek(f, -1);
    CHECK(err == LFP_INVALID_ARGS);
    lfp_close(f);
}

TEST_CASE_METHOD(
    random_memfile,
    "Seeking past EOF returns invalid args error",
    "[mem]") {
    // check both (== size) and (> size)
    const auto off_by_one = GENERATE(0, 1);
    const auto err = lfp_seek(f, size + off_by_one);
    CHECK(err == LFP_INVALID_ARGS);
}

TEST_CASE_METHOD(
    random_memfile,
    "Forward positive seek",
    "[mem]") {
    test_random_seek(this);
}
