/**
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */

#include <gtest/gtest.h>

#include "util.h"

TEST(SplitString, Works) {
  struct {
    const char* input;
    int maxsplit;
    bool keepEmpty;
    std::vector<const char*> expect;
  } tests[] = {
    { "", -1, false, {} },
    { "", 0, false, {} },
    { "", -1, true, {""} },
    { "", 0, true, {""} },
    { "a", -1, true, {"a"} },
    { ",", -1, false, {} },
    { ",", -1, true, {"", ""} },
    { ",,", -1, false, {} },
    { ",,", -1, true, {"", "", ""} },
    { "a,", -1, true, {"a", ""} },
    { "a,", -1, false, {"a"} },
    { ",a", -1, true, {"", "a"} },
    { ",a", -1, false, {"a"} },
    { "abc,def,ghi,", 0, false, {"abc,def,ghi,"} },
    { "abc,def,ghi,", 1, false, {"abc", "def,ghi,"} },
    { "abc,def,ghi,", 2, false, {"abc", "def", "ghi,"} },
    { "abc,def,ghi,", 3, false, {"abc", "def", "ghi"} },
    { "abc,def,ghi,", 3, true, {"abc", "def", "ghi", ""} },
    { "abc,def,ghi,", 4, false, {"abc", "def", "ghi"} },
    { "abc,def,ghi,", 5, true, {"abc", "def", "ghi", ""} },
  };

  for (const auto& test : tests) {
    std::vector<std::string_view> result =
        jxltk::splitString(test.input, ',', test.maxsplit, test.keepEmpty);
    ASSERT_EQ(result.size(), test.expect.size());
    for (size_t i = 0; i < result.size(); ++i) {
      EXPECT_EQ(result[i], std::string_view(test.expect[i])) << (&test - tests);
    }
  }
}

TEST(FindCropRegion, ReturnsEmptyRegionForFullyTransparent) {
  uint8_t samples[16] = {0};
  jxltk::CropRegion cropRegion = { 1, 2, 3, 4 };
  int ret = jxltk::findCropRegion(samples, 1, 1, JXL_TYPE_UINT8, 4, false, &cropRegion);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(cropRegion.width, 0);
  EXPECT_EQ(cropRegion.height, 0);
  EXPECT_EQ(cropRegion.x0, 0);
  EXPECT_EQ(cropRegion.y0, 0);

  cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
  ret = jxltk::findCropRegion(samples, 2, 2, JXL_TYPE_UINT8, 4, false, &cropRegion);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(cropRegion.width, 0);
  EXPECT_EQ(cropRegion.height, 0);
  EXPECT_EQ(cropRegion.x0, 0);
  EXPECT_EQ(cropRegion.y0, 0);

  // Add non-zero samples, but leave alpha as 0
  for (size_t i = 0; i < sizeof samples / sizeof *samples; ++i) {
    if (i % 4 != 3) {
      samples[i] = i;
    }
  }
  cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
  ret = jxltk::findCropRegion(samples, 2, 2, JXL_TYPE_UINT8, 4, true, &cropRegion);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(cropRegion.width, 0);
  EXPECT_EQ(cropRegion.height, 0);
  EXPECT_EQ(cropRegion.x0, 0);
  EXPECT_EQ(cropRegion.y0, 0);
}

TEST(FindCropRegion, ReturnsFullRegion) {
  int result;
  constexpr uint8_t cross[] = { 0, 255,   0,
                                1,   0, 100,
                                0, 255,   0 };
  jxltk::CropRegion cropRegion;
  for (bool alphaCrop : { false, true }) {
    cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
    result = jxltk::findCropRegion(cross, 3, 3, JXL_TYPE_UINT8, 1, alphaCrop,
                                   &cropRegion);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(cropRegion.x0, 0);
    EXPECT_EQ(cropRegion.y0, 0);
    EXPECT_EQ(cropRegion.width, 3);
    EXPECT_EQ(cropRegion.height, 3);
  }

  constexpr uint8_t span[] = { 0,0,   0,0,  255,0,
                               0,0,   0,0,  0,0,
                               0,255, 0,0,  0,0 };
  cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
  result = jxltk::findCropRegion(span, 3, 3, JXL_TYPE_UINT8, 2, false, &cropRegion);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(cropRegion.x0, 0);
  EXPECT_EQ(cropRegion.y0, 0);
  EXPECT_EQ(cropRegion.width, 3);
  EXPECT_EQ(cropRegion.height, 3);

  constexpr float crossF[] = { 0,0,0,0,0,    0,1,0,0,0,  0,   0,0,0,0,
                               0,0,0,0,0.1f, 0,0,0,0,0,  0.3f,0,0,0,0,
                               0,0,0,0,0,    0,0,1,0,0,  0,   0,0,0,0 };
  cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
  result = jxltk::findCropRegion(crossF, 3, 3, JXL_TYPE_FLOAT, 5, false, &cropRegion);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(cropRegion.x0, 0);
  EXPECT_EQ(cropRegion.y0, 0);
  EXPECT_EQ(cropRegion.width, 3);
  EXPECT_EQ(cropRegion.height, 3);
}

TEST(FindCropRegion, ReturnsSubRegion) {
  int result;
  constexpr uint8_t dot[] = { 0,   0, 0,
                              0, 255, 0,
                              0,   0, 0 };
  jxltk::CropRegion cropRegion;
  for (bool alphaCrop : { false, true }) {
    cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
    result = jxltk::findCropRegion(dot, 3, 3, JXL_TYPE_UINT8, 1, alphaCrop, &cropRegion);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(cropRegion.x0, 1);
    EXPECT_EQ(cropRegion.y0, 1);
    EXPECT_EQ(cropRegion.width, 1);
    EXPECT_EQ(cropRegion.height, 1);
  }

  constexpr uint8_t shape[] = { 0,0,  0,0,   0,0, 0,0,
                                0,0,  0,1,   0,0, 0,0,
                                1,0,  0,1,   0,0, 0,0,
                                0,0,  0,0,   0,1, 0,0, };
  cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
  result = jxltk::findCropRegion(shape, 4, 4, JXL_TYPE_UINT8, 2, false, &cropRegion);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(cropRegion.x0, 0);
  EXPECT_EQ(cropRegion.y0, 1);
  EXPECT_EQ(cropRegion.width, 3);
  EXPECT_EQ(cropRegion.height, 3);

  cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
  result = jxltk::findCropRegion(shape, 4, 4, JXL_TYPE_UINT8, 2, true, &cropRegion);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(cropRegion.x0, 1);
  EXPECT_EQ(cropRegion.y0, 1);
  EXPECT_EQ(cropRegion.width, 2);
  EXPECT_EQ(cropRegion.height, 3);
}

TEST(FindCropRegion, ProtectsSpecifiedRegion) {
  uint8_t samples[16] = {0};
  jxltk::CropRegion protectRegion = { 2, 2, 1, 1 };
  jxltk::CropRegion cropRegion = { 1, 2, 3, 4 };
  int ret = jxltk::findCropRegion(samples, 4, 4, JXL_TYPE_UINT8, 1, false, &cropRegion,
                                  &protectRegion);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(cropRegion.width, 2);
  EXPECT_EQ(cropRegion.height, 2);
  EXPECT_EQ(cropRegion.x0, 1);
  EXPECT_EQ(cropRegion.y0, 1);

  samples[15] = 1;
  cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
  ret = jxltk::findCropRegion(samples, 4, 4, JXL_TYPE_UINT8, 1, false, &cropRegion,
                              &protectRegion);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(cropRegion.width, 3);
  EXPECT_EQ(cropRegion.height, 3);
  EXPECT_EQ(cropRegion.x0, 1);
  EXPECT_EQ(cropRegion.y0, 1);

  // Check invalid region
  protectRegion.width = 4;
  ret = jxltk::findCropRegion(samples, 4, 4, JXL_TYPE_UINT8, 1, false, &cropRegion,
                              &protectRegion);
  EXPECT_EQ(ret, -1);

  // Check full region
  protectRegion.width = 4;
  protectRegion.height = 4;
  protectRegion.x0 = 0;
  protectRegion.y0 = 0;
  ret = jxltk::findCropRegion(samples, 4, 4, JXL_TYPE_UINT8, 1, false, &cropRegion,
                              &protectRegion);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(cropRegion.width, 4);
  EXPECT_EQ(cropRegion.height, 4);
  EXPECT_EQ(cropRegion.x0, 0);
  EXPECT_EQ(cropRegion.y0, 0);


  constexpr uint16_t rgba[64] = { 1, 2, 3,0,  4, 5, 6,0,     7, 8, 9,0, 10,11,12,0,
                                 13,14,15,0, 16,17,18,0,    19,20,21,0, 22,23,24,0,
                                 25,26,27,0, 28,29,30,0,    31,32,33,0, 34,35,36,0,
                                 37,38,39,0, 40,41,42,9999, 43,44,45,0, 46,47,48,0 };
  cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
  ret = jxltk::findCropRegion(rgba, 4, 4, JXL_TYPE_UINT16, 4, true, &cropRegion, nullptr);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(cropRegion.width, 1);
  EXPECT_EQ(cropRegion.height, 1);
  EXPECT_EQ(cropRegion.x0, 1);
  EXPECT_EQ(cropRegion.y0, 3);

  cropRegion.width = cropRegion.height = cropRegion.x0 = cropRegion.y0 = 1234;
  protectRegion.width = protectRegion.height = 2;
  protectRegion.x0 = protectRegion.y0 = 1;
  ret = jxltk::findCropRegion(rgba, 4, 4, JXL_TYPE_UINT16, 4, true, &cropRegion,
                              &protectRegion);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(cropRegion.width, 2);
  EXPECT_EQ(cropRegion.height, 3);
  EXPECT_EQ(cropRegion.x0, 1);
  EXPECT_EQ(cropRegion.y0, 1);
}

TEST(CropInPlace, Trivial) {
  float sample = 1.f;
  EXPECT_EQ(jxltk::cropInPlace(&sample, 1, 1, JXL_TYPE_FLOAT, 1,
                               {.width = 1, .height = 1, .x0 = 0, .y0 = 0}), 0);
  EXPECT_EQ(sample, 1.f);

  constexpr uint16_t orig_rgb[] = { 1,2,3,  4,5,6, 7,8,9, 10,11,12};
  uint16_t rgb[12];

  memcpy(rgb, orig_rgb, sizeof rgb);
  EXPECT_EQ(jxltk::cropInPlace(rgb, 2, 2, JXL_TYPE_UINT16, 3,
                               {.width = 2, .height = 2, .x0 = 0, .y0 = 0}), 0);
  for (size_t i = 0; i < sizeof rgb / sizeof rgb[0]; ++i) {
    EXPECT_EQ(rgb[i], orig_rgb[i]);
  }

  memcpy(rgb, orig_rgb, sizeof rgb);
  EXPECT_EQ(jxltk::cropInPlace(rgb, 2, 2, JXL_TYPE_UINT16, 3,
                               {.width = 2, .height = 1, .x0 = 0, .y0 = 0}), 0);
  for (size_t i = 0; i < sizeof rgb / sizeof rgb[0]; ++i) {
    EXPECT_EQ(rgb[i], orig_rgb[i]);
  }

  memcpy(rgb, orig_rgb, sizeof rgb);
  EXPECT_EQ(jxltk::cropInPlace(rgb, 4, 1, JXL_TYPE_UINT16, 3,
                               {.width = 4, .height = 1, .x0 = 0, .y0 = 0}), 0);
  for (size_t i = 0; i < sizeof rgb / sizeof rgb[0]; ++i) {
    EXPECT_EQ(rgb[i], orig_rgb[i]);
  }
}

TEST(CropInPlace, Invalid) {
  float sample = 1.f;
  EXPECT_NE(jxltk::cropInPlace(&sample, 1, 1, JXL_TYPE_FLOAT, 1,
                               {.width = 2, .height = 1, .x0 = 0, .y0 = 0}), 0);
  EXPECT_NE(jxltk::cropInPlace(&sample, 1, 1, JXL_TYPE_FLOAT, 1,
                               {.width = 1, .height = 1, .x0 = 1, .y0 = 0}), 0);
  EXPECT_NE(jxltk::cropInPlace(&sample, 1, 1, JXL_TYPE_FLOAT, 1,
                               {.width = 0, .height = 0, .x0 = 0, .y0 = 0}), 0);
}

TEST(CropInPlace, Typical) {
  constexpr uint16_t orig_rgb[] = {  1, 2, 3,  4, 5, 6,  7, 8, 9,
                                    10,11,12, 13,14,15, 16,17,18,
                                    19,20,21, 22,23,24, 25,26,27 };
  uint16_t rgb[27];

  memcpy(rgb, orig_rgb, sizeof rgb);
  EXPECT_EQ(jxltk::cropInPlace(rgb, 3, 3, JXL_TYPE_UINT16, 3,
                               {.width = 2, .height = 2, .x0 = 0, .y0 = 0}), 0);
  {
    constexpr uint16_t expect_rgb[] = {  1, 2, 3,  4, 5, 6,
                                        10,11,12, 13,14,15 };
    for (size_t i = 0; i < sizeof expect_rgb / sizeof expect_rgb[0]; ++i) {
      EXPECT_EQ(rgb[i], expect_rgb[i]);
    }
  }

  memcpy(rgb, orig_rgb, sizeof rgb);
  EXPECT_EQ(jxltk::cropInPlace(rgb, 3, 3, JXL_TYPE_UINT16, 3,
                               {.width = 1, .height = 1, .x0 = 2, .y0 = 2}), 0);
  EXPECT_EQ(rgb[0], 25);
  EXPECT_EQ(rgb[1], 26);
  EXPECT_EQ(rgb[2], 27);

  memcpy(rgb, orig_rgb, sizeof rgb);
  EXPECT_EQ(jxltk::cropInPlace(rgb, 3, 3, JXL_TYPE_UINT16, 3,
                               {.width = 2, .height = 3, .x0 = 1, .y0 = 0}), 0);
  {
    constexpr uint16_t expect_rgb[] = {  4, 5, 6,  7, 8, 9,
                                        13,14,15, 16,17,18,
                                        22,23,24, 25,26,27 };
    for (size_t i = 0; i < sizeof expect_rgb / sizeof expect_rgb[0]; ++i) {
      EXPECT_EQ(rgb[i], expect_rgb[i]);
    }
  }
}
