/**
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */

#include <gtest/gtest.h>

#include "util.h"

TEST(FindCropRegion, ReturnsEmptyRegionForFullyTransparent) {
  uint8_t samples[16] = {0};
  jxltk::CropRegion cropRegion = { 1, 2, 3, 4 };
  int ret = jxltk::findCropRegion(samples, 1, 1, JXL_TYPE_UINT8, 4, &cropRegion);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(cropRegion.width, 0);
  EXPECT_EQ(cropRegion.height, 0);
  EXPECT_EQ(cropRegion.x0, 0);
  EXPECT_EQ(cropRegion.y0, 0);

  cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
  ret = jxltk::findCropRegion(samples, 2, 2, JXL_TYPE_UINT8, 4, &cropRegion);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(cropRegion.width, 0);
  EXPECT_EQ(cropRegion.height, 0);
  EXPECT_EQ(cropRegion.x0, 0);
  EXPECT_EQ(cropRegion.y0, 0);
}

TEST(FindCropRegion, ReturnsFullRegion) {

  constexpr uint8_t cross[] = { 0, 255,   0,
                                1,   0, 100,
                                0, 255,   0 };
  jxltk::CropRegion cropRegion;
  cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
  int result = jxltk::findCropRegion(cross, 3, 3, JXL_TYPE_UINT8, 1, &cropRegion);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(cropRegion.x0, 0);
  EXPECT_EQ(cropRegion.y0, 0);
  EXPECT_EQ(cropRegion.width, 3);
  EXPECT_EQ(cropRegion.height, 3);

  constexpr uint8_t span[] = { 0,0,   0,0,  255,0,
                               0,0,   0,0,  0,0,
                               0,255, 0,0,  0,0 };
  cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
  result = jxltk::findCropRegion(span, 3, 3, JXL_TYPE_UINT8, 2, &cropRegion);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(cropRegion.x0, 0);
  EXPECT_EQ(cropRegion.y0, 0);
  EXPECT_EQ(cropRegion.width, 3);
  EXPECT_EQ(cropRegion.height, 3);

  constexpr float crossF[] = { 0,0,0,0,0,    0,1,0,0,0,  0,   0,0,0,0,
                               0,0,0,0,0.1f, 0,0,0,0,0,  0.3f,0,0,0,0,
                               0,0,0,0,0,    0,0,1,0,0,  0,   0,0,0,0 };
  cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
  result = jxltk::findCropRegion(crossF, 3, 3, JXL_TYPE_FLOAT, 5, &cropRegion);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(cropRegion.x0, 0);
  EXPECT_EQ(cropRegion.y0, 0);
  EXPECT_EQ(cropRegion.width, 3);
  EXPECT_EQ(cropRegion.height, 3);
}

TEST(FindCropRegion, ReturnsSubRegion) {

  constexpr uint8_t dot[] = { 0,   0, 0,
                              0, 255, 0,
                              0,   0, 0 };
  jxltk::CropRegion cropRegion;
  cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
  int result = jxltk::findCropRegion(dot, 3, 3, JXL_TYPE_UINT8, 1, &cropRegion);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(cropRegion.x0, 1);
  EXPECT_EQ(cropRegion.y0, 1);
  EXPECT_EQ(cropRegion.width, 1);
  EXPECT_EQ(cropRegion.height, 1);

  constexpr uint8_t shape[] = { 0,0,  0,0,   0,0, 0,0,
                                0,0,  0,1,   0,0, 0,0,
                                1,0,  0,1,   0,0, 0,0,
                                0,0,  0,0,   0,1, 0,0, };
  cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
  result = jxltk::findCropRegion(shape, 4, 4, JXL_TYPE_UINT8, 2, &cropRegion);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(cropRegion.x0, 0);
  EXPECT_EQ(cropRegion.y0, 1);
  EXPECT_EQ(cropRegion.width, 3);
  EXPECT_EQ(cropRegion.height, 3);
}

TEST(FindCropRegion, ProtectsSpecifiedRegion) {
  uint8_t samples[16] = {0};
  jxltk::CropRegion protectRegion = { 2, 2, 1, 1 };
  jxltk::CropRegion cropRegion = { 1, 2, 3, 4 };
  int ret = jxltk::findCropRegion(samples, 4, 4, JXL_TYPE_UINT8, 1, &cropRegion,
                                  &protectRegion);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(cropRegion.width, 2);
  EXPECT_EQ(cropRegion.height, 2);
  EXPECT_EQ(cropRegion.x0, 1);
  EXPECT_EQ(cropRegion.y0, 1);

  samples[15] = 1;
  cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
  ret = jxltk::findCropRegion(samples, 4, 4, JXL_TYPE_UINT8, 1, &cropRegion,
                              &protectRegion);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(cropRegion.width, 3);
  EXPECT_EQ(cropRegion.height, 3);
  EXPECT_EQ(cropRegion.x0, 1);
  EXPECT_EQ(cropRegion.y0, 1);

  // Check invalid region
  protectRegion.width = 4;
  ret = jxltk::findCropRegion(samples, 4, 4, JXL_TYPE_UINT8, 1, &cropRegion,
                              &protectRegion);
  EXPECT_EQ(ret, -1);

  // Check full region
  protectRegion.width = 4;
  protectRegion.height = 4;
  protectRegion.x0 = 0;
  protectRegion.y0 = 0;
  ret = jxltk::findCropRegion(samples, 4, 4, JXL_TYPE_UINT8, 1, &cropRegion,
                              &protectRegion);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(cropRegion.width, 4);
  EXPECT_EQ(cropRegion.height, 4);
  EXPECT_EQ(cropRegion.x0, 0);
  EXPECT_EQ(cropRegion.y0, 0);
}
