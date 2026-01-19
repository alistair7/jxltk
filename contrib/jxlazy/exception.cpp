/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#include <cstdarg>
#include <string>

#include <jxlazy/exception.h>

#define JXLAZY_COMMON_IMPLEMENTATION() do { \
  va_list args; \
  va_start(args, format); \
  this->format(format, args); \
  va_end(args); \
} \
while(0)

using namespace std;

namespace jxlazy {

JxlazyException::JxlazyException(const string& msg/*=""*/) : std::runtime_error(msg) {
}

JxlazyException::JxlazyException(const char* format, ...) : JxlazyException() {
  JXLAZY_COMMON_IMPLEMENTATION();
}

void JxlazyException::format(const char* format, va_list args) {
  if(vsnprintf(msg_, sizeof msg_, format, args) < 1)
    msg_[0] = '\0';
}

const char* JxlazyException::what() const noexcept {
  return msg_;
}

LibraryError::LibraryError(const char* format, ...) : JxlazyException() {
  JXLAZY_COMMON_IMPLEMENTATION();
}

ReadError::ReadError() : JxlazyException() { }

ReadError::ReadError(const char* format, ...) : JxlazyException() {
  JXLAZY_COMMON_IMPLEMENTATION();
}

NotSeekableError::NotSeekableError(const char* format, ...) : ReadError() {
  JXLAZY_COMMON_IMPLEMENTATION();
}

UsageError::UsageError() : JxlazyException() { }

UsageError::UsageError(const char* format, ...) : JxlazyException() {
  JXLAZY_COMMON_IMPLEMENTATION();
}

IndexOutOfRange::IndexOutOfRange(const char* format, ...) : UsageError() {
  JXLAZY_COMMON_IMPLEMENTATION();
}

NotImplemented::NotImplemented(const char* format, ...) {
  JXLAZY_COMMON_IMPLEMENTATION();
}

NoBrotliError::NoBrotliError(const char* format, ...) {
  JXLAZY_COMMON_IMPLEMENTATION();
}

}  // namespace jxlazy
