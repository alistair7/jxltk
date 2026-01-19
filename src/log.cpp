/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#include <cstdarg>
#include "log.h"

namespace jxltk {

LogLevel jxltkLogThreshold = LogLevel::Info;

void logPrintf(FILE* to, LogLevel severity, const char* file, const char* func,
               unsigned line, unsigned logflags, const char* format, ...)
{
  if (severity <= LogLevel::Error && file) {
    fprintf(to, "%s: ", file);
  }
  if ((severity <= LogLevel::Error || severity >= LogLevel::Trace) && func) {
    fprintf(to, "in function '%s':%u: ", func, line);
  }
  if (!(logflags & LogFlags::Continuation)) {
    if (severity <= LogLevel::Error) {
      fputs("ERROR: ", to);
    } else if (severity <= LogLevel::Warning) {
      fputs("Warning: ", to);
    }
  }
  va_list args;
  va_start(args, format);
  vfprintf(to, format, args);
  va_end(args);
  if (!(logflags & LogFlags::NoNewline)) {
    fputc('\n', to);
  }
}

}  // namespace jxltk
