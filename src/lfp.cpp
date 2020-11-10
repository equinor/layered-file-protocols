#include <cassert>
#include <ciso646>
#include <cstddef>
#include <cstdint>

#include <fmt/format.h>

#include <lfp/lfp.h>
#include <lfp/protocol.hpp>

int lfp_close(lfp_protocol* f) try {
    if (!f) return LFP_OK;
    f->close();
    delete f;
    return LFP_OK;
} catch (lfp::error& e) {
    f->errmsg(e.what());
    return e.status();
} catch (...) {
    return LFP_UNHANDLED_EXCEPTION;
}

int lfp_readinto(lfp_protocol* f,
        void* dst,
        std::int64_t len,
        std::int64_t* nread) try {
    assert(dst);
    assert(f);

    if (len < 0) {
        f->errmsg(fmt::format("expected len (which is {}) >= 0", len));
        return LFP_INVALID_ARGS;
    }

    return f->readinto(dst, len, nread);
} catch (const lfp::error& e) {
    f->errmsg(e.what());
    return e.status();
} catch (const std::exception& e) {
    f->errmsg(e.what());
    return LFP_UNHANDLED_EXCEPTION;
} catch (...) {
    assert(false);
    f->errmsg("Unhandled error that does not derive from std::exception");
    return LFP_UNHANDLED_EXCEPTION;
}

int lfp_seek(lfp_protocol* f, std::int64_t n) try {
    assert(f);

    if (n < 0) {
        f->errmsg(fmt::format("seek offset n < 0. Must be >= 0, was {}", n));
        return LFP_INVALID_ARGS;
    }

    f->seek(n);
    return LFP_OK;
} catch (const lfp::error& e) {
    f->errmsg(e.what());
    return e.status();
} catch (const std::exception& e) {
    f->errmsg(e.what());
    return LFP_UNHANDLED_EXCEPTION;
} catch (...) {
    assert(false);
    f->errmsg("Unhandled error that does not derive from std::exception");
    return LFP_UNHANDLED_EXCEPTION;
}

int lfp_tell(lfp_protocol* f, std::int64_t* n) try {
    assert(n);
    assert(f);
    *n = f->tell();
    return LFP_OK;
} catch (const lfp::error& e) {
    f->errmsg(e.what());
    return e.status();
} catch (const std::exception& e) {
    f->errmsg(e.what());
    return LFP_UNHANDLED_EXCEPTION;
} catch (...) {
    assert(false);
    f->errmsg("Unhandled error that does not derive from std::exception");
    return LFP_UNHANDLED_EXCEPTION;
}

int lfp_peel(lfp_protocol* outer, lfp_protocol** inner) try {
    assert(outer);
    assert(inner);

    auto* tmp = outer->peel();
    if (!tmp) {
        outer->errmsg("peel: no underlying protocol");
        return LFP_IOERROR;
    }

    *inner = tmp;
    return LFP_OK;
} catch(const lfp::error& e) {
    outer->errmsg(e.what());
    return e.status();
}

int lfp_peek(lfp_protocol* outer, lfp_protocol** inner) try {
    assert(outer);
    assert(inner);

    auto* tmp = outer->peek();
    if (!tmp) {
        outer->errmsg("peek: no underlying protocol");
        return LFP_IOERROR;
    }

    *inner = tmp;
    return LFP_OK;
} catch(const lfp::error& e) {
    outer->errmsg(e.what());
    return e.status();
}

int lfp_eof(lfp_protocol* f) {
    assert(f);
    return f->eof();
}

const char* lfp_errormsg(lfp_protocol* f) try {
    assert(f);
    return f->errmsg();
} catch (...) {
    return nullptr;
}

void lfp_protocol::seek(std::int64_t) noexcept (false) {
    throw lfp::not_implemented("seek: not implemented for layer");
}

std::int64_t lfp_protocol::tell() const noexcept (false) {
    throw lfp::not_implemented("tell: not implemented for layer");
}

const char* lfp_protocol::errmsg() noexcept (true) {
    if (this->error_message.empty())
        return nullptr;
    return this->error_message.c_str();
}

void lfp_protocol::errmsg(std::string msg) noexcept (false) {
    this->error_message = std::move(msg);
}

namespace lfp {

error::error(lfp_status c, const std::string& msg) :
    runtime_error(msg),
    errc(c)
{}

error::error(lfp_status c, const char* msg) :
    runtime_error(msg),
    errc(c)
{}

lfp_status error::status() const noexcept (true) {
    return this->errc;
}

error not_implemented(const std::string& msg) {
    return error(LFP_NOTIMPLEMENTED, msg);
}
error leaf_protocol(const std::string& msg) {
    return error(LFP_LEAF_PROTOCOL, msg);
}
error not_supported(const std::string& msg) {
    return error(LFP_NOTSUPPORTED, msg);
}
error io_error(const std::string& msg) {
    return error(LFP_IOERROR, msg);
}
error runtime_error(const std::string& msg) {
    return error(LFP_RUNTIME_ERROR, msg);
}
error invalid_args(const std::string& msg) {
    return error(LFP_INVALID_ARGS, msg);
}
error protocol_fatal(const std::string& msg) {
    return error(LFP_PROTOCOL_FATAL_ERROR, msg);
}
error protocol_failed_recovery(const std::string& msg) {
    return error(LFP_PROTOCOL_FAILEDRECOVERY, msg);
}
error unexpected_eof(const std::string& msg) {
    return error(LFP_UNEXPECTED_EOF, msg);
}

}
