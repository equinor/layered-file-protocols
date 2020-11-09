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


TEST_CASE(
    "Simple test which caused failure",
    "[tapeimage][tif]") {
    const auto file = std::vector< unsigned char > {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x0C, 0x00, 0x00, 0x00,
    };

    auto* mem = lfp_memfile_openwith(file.data(), file.size());
    auto* tif = lfp_tapeimage_open(mem);

    // before seek print statement
    lfp_seek(tif, 3);
    // before close print statement
    lfp_close(tif);
}
