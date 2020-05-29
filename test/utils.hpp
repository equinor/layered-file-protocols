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
    auto content =  GENERATE_COPY(take(1, chunk(size, random< unsigned short >(0, 255))));
    return std::vector< unsigned char >(content.begin(), content.end());
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

namespace {

void test_split_read(random_memfile* file) {
    // +1 so that if size is 1, max is still >= min
    const auto readsize = GENERATE_COPY(
                          take(1, random(1, (file->size + 1) / 2)));
    const auto complete_reads = file->size / readsize;

    auto* p = file->out.data();
    std::int64_t nread = 0;
    for (int i = 0; i < complete_reads; ++i) {
        const auto err = lfp_readinto(file->f, p, readsize, &nread);
        CHECK(err == LFP_OK);
        CHECK(nread == readsize);
        p += nread;
    }

    if (file->size % readsize != 0) {
        const auto err = lfp_readinto(file->f, p, readsize, &nread);
        CHECK(err == LFP_EOF);
    }

    CHECK_THAT(file->out, Catch::Matchers::Equals(file->expected));
}

void test_random_seek(random_memfile* file) {
    const auto n = GENERATE_COPY(take(1, random(0, file->size - 1)));
    auto err = lfp_seek(file->f, n);
    REQUIRE(err == LFP_OK);

    std::int64_t tell;
    err = lfp_tell(file->f, &tell);
    REQUIRE(err == LFP_OK);
    CHECK(tell == n);

    const auto remaining = file->size - n;
    file->expected.erase(file->expected.begin(),
                         file->expected.begin() + n);

    std::int64_t nread = 0;
    file->out.resize(remaining);
    err = lfp_readinto(file->f, file->out.data(), remaining, &nread);

    CHECK(err == LFP_OK);
    CHECK(nread == remaining);
    CHECK_THAT(file->out, Catch::Matchers::Equals(file->expected));
}

}


#endif //LFP_TEST_UTILS_HPP
