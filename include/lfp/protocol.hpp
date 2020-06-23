#ifndef LFP_INTERNAL_HPP
#define LFP_INTERNAL_HPP

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <memory>
#include <string>

#include <lfp/lfp.h>

/** \file protocol.hpp */

/**
 * The functions of this class roughly correspond to the public interface in
 * lfp.h, but with C++-isms. Since it is not exposed in the ABI except through
 * pointers, it can be a lot more volatile and can change quite dramatically
 * between versions.
 *
 * When methods have default implementations, they typically just return
 * `LFP_NOTIMPLEMENTED`.
 */
class lfp_protocol {
public:
    /** \copybrief lfp_close
     *
     * Multiple calls to 'close' must be allowed in order to correctly handle
     * nested protocols.
     */
    virtual void close() noexcept (false) = 0;

    /** \copybrief lfp_readinto
     *
     * This must be implemented by all protocols.
     *
     * \param dst buffer of size `len`
     * \param len maximum length of data to be read
     * \param bytes_read number of bytes actually read into the buffer
     */
    virtual lfp_status readinto(
            void* dst,
            std::int64_t len,
            std::int64_t* bytes_read)
        noexcept (false) = 0;

    /*
     * Whenever read operations return OKINCOMPLETE, it could be because the
     * read succeeded, but the file is at EOF (probably the most common cause).
     *
     * Use this function to determine if it actually was EOF, like C's stdlib
     * feof(3).
     *
     * TODO: embed EOF in return code instead?
     */
    virtual int eof() const noexcept (false) = 0;

    /** \copybrief lfp_seek
     *
     * If this is not implemented, `lfp_seek()` will always return
     * `LFP_NOTIMPLEMENTED`.
     */
    virtual void seek(std::int64_t) noexcept (false);
    /** \copybrief lfp_tell
     *
     * If this is not implemented, `lfp_seek()` will always return
     * `LFP_NOTIMPLEMENTED`.
     */
    virtual std::int64_t tell() const noexcept (false);

    /** \copybrief lfp_peel
     *
     * If this is not implemented, `lfp_peel()` will throw
     * `LFP_NOTIMPLEMENTED`.
     */
    virtual lfp_protocol* peel() noexcept (false) = 0;

    /** \copybrief lfp_peek
     *
     * If this is not implemented, `lfp_peek()` will throw
     * `LFP_NOTIMPLEMENTED`.
     */
    virtual lfp_protocol* peek() const noexcept (false) = 0;

    /** \copybrief lfp_errormsg */
    const char* errmsg() noexcept (true);

    /** Set the error message */
    void errmsg(std::string) noexcept (false);

    virtual ~lfp_protocol() = default;

private:
    std::string error_message;
};

namespace lfp {

/**
 * Deleter for use with unique_ptr and similar.
 */
struct protocol_deleter {
    void operator () (lfp_protocol* f) noexcept (true) {
        lfp_close(f);
    }
};

/** RAII for `lfp_protocol`
 *
 * Some automation of the boilerplate when creating new protocols. Whenever a
 * protocol only provides features on top of I/O, they must contain another
 * `lfp_protocol`. The inner protocol should be tied to the lifetime of the
 * outer layer, and std::unique_ptr is a perfect fit. However, since protocols
 * are not cleaned up with `delete`, but rather with `lfp_close()`, a custom
 * deleter is required.
 *
 * Instances are implicitly convertible to `lfp_protocol*`, and support
 * `operator ->`.
 */
class unique_lfp {
public:
    explicit unique_lfp(lfp_protocol* f) : fp(f) {}

    /** Implicit conversion to `lfp*`
     *
     * Implicitly convert to `lfp*`, so that unique_lfp objects can be
     * passed directly to the `lfp_*` functions.
     *
     *     unique_lfp protocol(make_protocol());
     *     std::int64_t offset;
     *     auto err = lfp_tell(protocol, &offset);
     */
    operator lfp_protocol*() noexcept (true) {
        assert(this->fp);
        return this->fp.get();
    }
    /** Implicit conversion to `const lfp*`
     *
     * Implicitly convert to `const lfp*`, so that unique_lfp objects can be
     * passed directly to the `lfp_*` functions.
     *
     *     unique_lfp protocol(make_protocol());
     *     std::int64_t offset;
     *     auto err = lfp_tell(protocol, &offset);
     */
    operator const lfp_protocol*() const noexcept (true) {
        assert(this->fp);
        return this->fp.get();
    }

    /** Forward `->` to `lfp`
     *
     * Call `lfp` methods directly on the `unique_lfp` handle.
     *
     *     unique_lfp protocol(make_protocol());
     *     auto tell = protocol->tell();
     */
    lfp_protocol* operator -> () noexcept (true) {
        assert(this->fp);
        return this->fp.get();
    }

    /** Forward `->` to `lfp`
     *
     * Call `lfp` methods directly on the `unique_lfp` handle.
     *
     *     unique_lfp protocol(make_protocol());
     *     auto tell = protocol->tell();
     */
    const lfp_protocol* operator -> () const noexcept (true) {
        assert(this->fp);
        return this->fp.get();
    }

    /** Recursively close and release resources
     *
     * Release the underlying `lfp_protocol*` object, and set nullptr. This
     * calls `lfp_close()`, and recursively releases lfp resources.
     *
     * This method is not necessary to call, unless there is a desire to get
     * the status code of the close operation. The destructor will clean it up
     * otherwise.
     */
    void close() noexcept (false) {
        this->fp->close();
        this->fp.reset(nullptr);
    }

    /** Releases the pointer from unique_lfp ownership
     *
     * Underlying pointer is not destroyed, but its fate is no longer
     * unique_lfp concern
     */
    lfp_protocol* release() noexcept (true) {
        assert(this->fp);
        return this->fp.release();
    }

    /** Return a pointer to the owned object
     *
     * The ownership is *not* released.
     *
     */
    lfp_protocol* get() const noexcept (true) {
        assert(this->fp);
        return this->fp.get();
    }

    /** Conversion to `bool`
     *
     *  Checks whether an object is owned.
     */
    explicit operator bool() noexcept (true) {
        return bool(this->fp);
    }

private:
    std::unique_ptr< lfp_protocol, protocol_deleter > fp;
};

/** \addtogroup advance
 * @{
 */

/** Arithmetic for void pointers
 *
 * Automation for doing pointer arithetic on void pointers. C++, of course,
 * does not allow arithmetic on void pointers, but lfp works on untyped bytes
 * anyway. It is assumed that one increment means one byte/addressable unit,
 * i.e. the behaviour of char*.
 *
 * This does *not* behave like std::advance and modifies the pointer
 * in-place - it returns a copy.
 *
 *     protocol->readinto(dst, 10, &nread);
 *     dst = advance(dst, nread);
 */
inline void* advance(void* p, std::ptrdiff_t n) noexcept (true) {
    return static_cast< char* >(p) + n;
}

/** Arithmetic for void pointers
 *
 * Automation for doing pointer arithetic on void pointers. C++, of course,
 * does not allow arithmetic on void pointers, but lfp works on untyped bytes
 * anyway. It is assumed that one increment means one byte/addressable unit,
 * i.e. the behaviour of char*.
 *
 * This does *not* behave like std::advance and modifies the pointer
 * in-place - it returns a copy.
 *
 *     protocol->readinto(dst, 10, &nread);
 *     dst = advance(dst, nread);
 */
inline const void* advance(const void* p, std::ptrdiff_t n) noexcept (true) {
    return static_cast< const char* >(p) + n;
}

/** @} */

/** Base class for lfp exceptions */
class error : public std::runtime_error {
public:
    error(lfp_status, const std::string& msg);
    error(lfp_status, const char* msg);

    lfp_status status() const noexcept (true);

private:
    lfp_status errc;
};

/** \addtogroup exceptions Exceptions
 * @{
 */

error not_implemented(const std::string& msg);
error leaf_protocol(const std::string& msg);
error not_supported(const std::string& msg);
error io_error(const std::string& msg);
error runtime_error(const std::string& msg);
error invalid_args(const std::string& msg);
error protocol_fatal(const std::string& msg);
error protocol_failed_recovery(const std::string& msg);
error unexpected_eof(const std::string& msg);

/** @} */

}

#endif // LFP_INTERNAL_HPP
