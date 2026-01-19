/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#ifndef JXLTK_LOG_H_
#define JXLTK_LOG_H_

#include <cstdio>

#define JXLTK_LOG(severity, logflags, ...) do { \
  if (jxltk::jxltkLogThreshold >= (severity)) { \
    jxltk::logPrintf(stderr, (severity), __FILE__, __func__, \
                     __LINE__, (logflags), __VA_ARGS__); \
  } \
} while(0)

#define JXLTK_ERROR(...) JXLTK_LOG(jxltk::LogLevel::Error, 0, __VA_ARGS__)

#define JXLTK_WARNING(...) JXLTK_LOG(jxltk::LogLevel::Warning, 0, __VA_ARGS__)

#define JXLTK_NOTICE(...) JXLTK_LOG(jxltk::LogLevel::Notice, 0, __VA_ARGS__)

#define JXLTK_INFO(...) JXLTK_LOG(jxltk::LogLevel::Info, 0, __VA_ARGS__)

#define JXLTK_DEBUG(...) JXLTK_LOG(jxltk::LogLevel::Debug, 0, __VA_ARGS__)

#define JXLTK_TRACE(...) JXLTK_LOG(jxltk::LogLevel::Trace, 0, __VA_ARGS__)

#ifdef __GNUC__
#define JXLTK_FORMAT_PRINTF(a,b) __attribute__(( format(printf,(a),(b)) ))
#else
#define JXLTK_FORMAT_PRINTF(a,b)
#endif

namespace jxltk {

enum LogFlags {
  NoNewline = 1,    // Don't append \n to the message.
  Continuation = 2, // This line is a continuation of a previous log message.
};

enum class LogLevel {
  Error = 10,
  Warning = 20,
  Notice = 30,
  Info = 40,
  Debug = 50,
  Trace = 60
};
extern LogLevel jxltkLogThreshold;

/**
 * Print a formatted message to a file.
 *
 * The message is written unconditionally.  @p severity affects the formatting.
 * @param[in] file,func,line Source file location, may be nullptr / 0.
 * @param[in] logflags Bitwise combination of @ref LogFlags, or 0.
 */
void logPrintf(FILE* to, LogLevel severity, const char* file, const char* func,
               unsigned line, unsigned logflags, const char* format, ...)
    JXLTK_FORMAT_PRINTF(7, 8);

}  // namespace jxltk

#endif  // JXLTK_LOG_H_
