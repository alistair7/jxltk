/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#include "except.h"

#include <cstdarg>
#include <string>

using std::string;

namespace jxltk {

JxltkError::JxltkError(const string& msg /*=""*/) : std::runtime_error(msg) {}

JxltkError::JxltkError(const char* format, ...) : JxltkError() {
  va_list args;
  va_start(args, format);
  this->format(format, args);
  va_end(args);
}

void JxltkError::format(const char* format, va_list args) {
  if (vsnprintf(msg_, sizeof msg_, format, args) < 1) msg_[0] = '\0';
}

const char* JxltkError::what() const noexcept { return msg_; }

ReadError::ReadError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  this->format(format, args);
  va_end(args);
}

WriteError::WriteError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  this->format(format, args);
  va_end(args);
}

NotImplemented::NotImplemented(const char* format, ...) {
  va_list args;
  va_start(args, format);
  this->format(format, args);
  va_end(args);
}

InvalidConfigError::InvalidConfigError(const char* format, ...) : ReadError("%s", "") {
  va_list args;
  va_start(args, format);
  this->format(format, args);
  va_end(args);
}

}  // namespace jxltk
