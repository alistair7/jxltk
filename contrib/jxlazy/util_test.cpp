/**
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
*/
#include <string>

#include <gtest/gtest.h>

#include "util.h"

#ifndef JXLAZY_TEST_DATA_DIR
#define JXLAZY_TEST_DATA_DIR "testfiles"
#endif

static std::string getPath(const std::string& relative) {
  return std::string(JXLAZY_TEST_DATA_DIR "/") + relative;
}

TEST(Util, GetFileSize) {
  size_t size = jxlazy::getFileSize(getPath("jpeg.jpg").c_str());
  EXPECT_TRUE(size == 517 || size == 0);
}
