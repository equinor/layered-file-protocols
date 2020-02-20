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

