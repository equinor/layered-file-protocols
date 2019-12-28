#include <ciso646>
#include <memory>
#include <vector>

#include <catch2/catch.hpp>

#include <lfp/lfp.h>
#include <lfp/memfile.h>

#include "utils.hpp"

using namespace Catch::Matchers;

/*
 * The tests for memfile are largely intended as a test for the interface
 * itself, both for correctness, but also some "real"-world experience.
 */

TEST_CASE("Closing nullptr is a no-op") {
    const auto err = lfp_close(nullptr);
    CHECK(err == LFP_OK);
}

TEST_CASE("A mem-file can be closed") {
    // TODO: replace this with a custom "close-observer file"
    auto f = memopen();
    const auto err = lfp_close(f.get());
    CHECK(err == LFP_OK);
    f.release();
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
}

TEST_CASE_METHOD(
        random_memfile,
        "Asking more data than available stops at EOF",
        "[mem]") {
    std::int64_t nread = 0;
    const auto err = lfp_readinto(f, out.data(), 2*out.size(), &nread);

    CHECK(err == LFP_OKINCOMPLETE);
    CHECK(nread == expected.size());
    CHECK_THAT(out, Equals(expected));
}

TEST_CASE_METHOD(
        random_memfile,
        "Doing multiple reads yields the full file",
        "[mem]") {
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
    const auto n = GENERATE_COPY(take(1, random(0, size - 1)));
    auto err = lfp_seek(f, n);
    REQUIRE(err == LFP_OK);

    const auto remaining = size - n;
    expected.erase(expected.begin(), expected.begin() + n);

    std::int64_t nread = 0;
    out.resize(remaining);
    err = lfp_readinto(f, out.data(), remaining, &nread);

    CHECK(err == LFP_OK);
    CHECK(nread == remaining);
    CHECK_THAT(out, Equals(expected));
}
