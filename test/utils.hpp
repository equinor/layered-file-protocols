#ifndef LFP_TEST_UTILS_HPP
#define LFP_TEST_UTILS_HPP

#include <memory>

#include <catch2/catch.hpp>

#include <lfp/lfp.h>
#include <lfp/memfile.h>

namespace {

struct memfile_closer {
    void operator () (lfp_protocol* f) { lfp_close(f); }
};

using uniquemem = std::unique_ptr< lfp_protocol, memfile_closer >;

uniquemem memopen() {
    auto f = uniquemem{ lfp_memfile_open() };
    REQUIRE(f);
    return f;
}

uniquemem memopen(const unsigned char* p, std::size_t len) {
    auto f = uniquemem{ lfp_memfile_openwith(p, len) };
    REQUIRE(f);
    return f;
}

uniquemem memopen(const std::vector< unsigned char >& v) {
    return memopen(v.data(), v.size());
}

}

namespace {

std::vector< unsigned char > make_tempfile(std::size_t size) {
    return GENERATE_COPY(take(1, chunk(size, random< unsigned char >(0, 255))));
}

struct random_memfile {
    random_memfile() {
        size = GENERATE(take(1, random(1, 1000)));
        REQUIRE(size > 0);
        expected = make_tempfile(size);
        REQUIRE(expected.size() == size);
        out.assign(size, 0);
        f = lfp_memfile_openwith(expected.data(), size);
        REQUIRE(f);
    }

    ~random_memfile() {
        lfp_close(f);
    }

    lfp_protocol* copy() {
        /*
         * Get a copy of the underlying memfile, which is quite useful for
         * checking operations like seek against the *actual* behaviour of the
         * underlying handle
         */
        auto* c = lfp_memfile_openwith(expected.data(), size);
        REQUIRE(c);
        return c;
    }

    lfp_protocol* f = nullptr;
    int size;
    std::vector< unsigned char > expected;
    std::vector< unsigned char > out;
};

}

#endif //LFP_TEST_UTILS_HPP
