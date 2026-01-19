/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#ifndef JXLTK_EXCEPT_H_
#define JXLTK_EXCEPT_H_

#include <stdexcept>
#include <string>

#include "log.h"

#ifdef __GNUC__
#define JXLTK_PRINTF(f, a) __attribute__((format(printf, (f), (a))))
#else
#define JXLTK_PRINTF(f, a)
#endif

#define JXLTK_ERROR_AND_THROW(ExceptionType, ...) do { \
  JXLTK_LOG(jxltk::LogLevel::Error, 0, __VA_ARGS__); \
  throw ExceptionType(__VA_ARGS__); \
} while(0)

namespace jxltk {

class JxltkError : public std::runtime_error {
 public:
  JxltkError(const std::string& msg = "");
  JxltkError(const char* format, ...) JXLTK_PRINTF(2, 3);
  const char* what() const noexcept override;

 protected:
  void format(const char* format, va_list args);

 private:
  char msg_[128];
};

class ReadError : public JxltkError {
 public:
  ReadError(const char* format, ...) JXLTK_PRINTF(2, 3);
};

class WriteError : public JxltkError {
 public:
  WriteError(const char* format, ...) JXLTK_PRINTF(2, 3);
};

class NotImplemented : public JxltkError {
 public:
  NotImplemented(const char* format, ...) JXLTK_PRINTF(2, 3);
};

class InvalidConfigError : public ReadError {
 public:
  InvalidConfigError(const char* format, ...) JXLTK_PRINTF(2, 3);
};

}  // namespace jxltk
#endif  // JXLTK_EXCEPT_H_
