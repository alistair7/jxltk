/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace jxlazy {

size_t getFileSize(const char* path) {
  std::error_code ec;
  uintmax_t result = std::filesystem::file_size(path, ec);
  if (result == static_cast<uintmax_t>(-1) ||
      result > SIZE_MAX) {
    return 0;
  }
  return result;
}

}  // namespace jxlazy
