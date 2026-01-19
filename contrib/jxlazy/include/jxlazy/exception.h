/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
/**
 * @file exception.h
 * @brief Exceptions thrown by the jxlazy API.
 */
#ifndef JXLAZY_EXCEPTION_H_
#define JXLAZY_EXCEPTION_H_
#ifndef __cplusplus
#error "This is a C++ header"
#endif

/// \cond
#include <cstring>
#include <stdexcept>
/// \endcond

#ifdef __GNUC__
#define JXLAZY_FORMAT_PRINTF(f,a) __attribute__((format(printf, (f), (a))))
#else
#define JXLAZY_FORMAT_PRINTF(f,a)
#endif

namespace jxlazy {

/**
 * All exceptions directly thrown by this library are instances of JxlazyException.
 */
class JxlazyException : public std::runtime_error {
public:
  JxlazyException(const std::string& msg = "");
  JxlazyException(const char* format, ...) JXLAZY_FORMAT_PRINTF(2,3);
  const char* what() const noexcept override;
protected:
  void format(const char* format, va_list args);
private:
  char msg_[256];
};

/**
 * Unexpected error returned from libjxl.
 */
class LibraryError : public JxlazyException {
public:
  LibraryError(const char* format, ...) JXLAZY_FORMAT_PRINTF(2,3);
};

/**
 * Generic error during processing.
 */
class ReadError : public JxlazyException {
public:
  ReadError();
  ReadError(const char* format, ...) JXLAZY_FORMAT_PRINTF(2,3);
};

/**
 * Operation requires a second pass over the input, but the input isn't seekable.
 */
class NotSeekableError : public ReadError {
public:
  NotSeekableError(const char* format, ...) JXLAZY_FORMAT_PRINTF(2,3);
};

/**
 * Jxlazy API used incorrectly.
 */
class UsageError : public JxlazyException {
public:
  UsageError();
  UsageError(const char* format, ...) JXLAZY_FORMAT_PRINTF(2,3);
};

class IndexOutOfRange : public UsageError {
public:
  IndexOutOfRange(const char* format, ...) JXLAZY_FORMAT_PRINTF(2,3);
};

class NotImplemented : public JxlazyException {
public:
  NotImplemented(const char* format, ...) JXLAZY_FORMAT_PRINTF(2,3);
};

class NoBrotliError : public JxlazyException {
public:
  NoBrotliError(const char* format, ...) JXLAZY_FORMAT_PRINTF(2,3);
};


}  // namespace jxlazy

#endif  // JXLAZY_EXCEPTION_H_
