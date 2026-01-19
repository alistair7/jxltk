/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#ifndef JXLAZY_UTIL_H_
#define JXLAZY_UTIL_H_

#include <cstddef>

namespace jxlazy {

/**
Return the size of the named file in bytes.

Quietly returns 0 if the file size can't be determined
or is too large for a size_t.
 */
size_t getFileSize(const char* path);

}  // namespace jxlazy

#endif  // JXLAZY_UTIL_H_
