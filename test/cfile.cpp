#include <catch2/catch.hpp>

#include <lfp/tapeimage.h>
#include <lfp/lfp.h>

#include "utils.hpp"


using namespace Catch::Matchers;

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
