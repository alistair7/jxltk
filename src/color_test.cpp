/**
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */

#include <gtest/gtest.h>

#include "color.h"

TEST(ColorProfilesMatch, Enumerated) {
  JxlColorEncoding left = { .color_space = JXL_COLOR_SPACE_RGB,
                            .white_point = JXL_WHITE_POINT_D65,
                            .primaries = JXL_PRIMARIES_SRGB,
                            .transfer_function = JXL_TRANSFER_FUNCTION_SRGB,
                            .rendering_intent = JXL_RENDERING_INTENT_RELATIVE };
  JxlColorEncoding right = left;

  // None of these should affect the comparison
  right.gamma = 1.0;
  right.primaries_red_xy[0] = 2.0;
  right.primaries_red_xy[1] = 3.0;
  right.primaries_green_xy[0] = 4.0;
  right.primaries_green_xy[1] = 5.0;
  right.primaries_blue_xy[0] = 6.0;
  right.primaries_blue_xy[1] = 7.0;
  right.white_point_xy[1] = 7.0;
  right.rendering_intent = JXL_RENDERING_INTENT_SATURATION;

  jxltk::ColorProfile lcp;
  lcp.enc = left;
  jxltk::ColorProfile rcp;
  rcp.enc = right;
  EXPECT_TRUE(colorProfilesMatch(lcp, rcp));

  rcp.enc->color_space = JXL_COLOR_SPACE_GRAY;
  EXPECT_FALSE(colorProfilesMatch(lcp, rcp));
  rcp.enc->color_space = right.color_space;

  rcp.enc->white_point = JXL_WHITE_POINT_DCI;
  EXPECT_FALSE(colorProfilesMatch(lcp, rcp));
  rcp.enc->white_point = right.white_point;

  rcp.enc->primaries = JXL_PRIMARIES_P3;
  EXPECT_FALSE(colorProfilesMatch(lcp, rcp));
  rcp.enc->primaries = right.primaries;

  rcp.enc->transfer_function = JXL_TRANSFER_FUNCTION_DCI;
  EXPECT_FALSE(colorProfilesMatch(lcp, rcp));
  rcp.enc->transfer_function = right.transfer_function;

  rcp.enc->rendering_intent = JXL_RENDERING_INTENT_ABSOLUTE;
  EXPECT_TRUE(colorProfilesMatch(lcp, rcp));
  rcp.enc->rendering_intent = right.rendering_intent;

  /* Linear and gamma 1.0 are equivalent */
  lcp.enc->transfer_function = JXL_TRANSFER_FUNCTION_LINEAR;
  rcp.enc->transfer_function = JXL_TRANSFER_FUNCTION_LINEAR;
  EXPECT_TRUE(colorProfilesMatch(lcp, rcp));
  rcp.enc->transfer_function = JXL_TRANSFER_FUNCTION_GAMMA;
  rcp.enc->gamma = 1.001;
  EXPECT_FALSE(colorProfilesMatch(lcp, rcp));
  rcp.enc->gamma = 1.0;
  EXPECT_TRUE(colorProfilesMatch(lcp, rcp));

  /* Custom white point identical to D65 */
  rcp.enc->white_point = JXL_WHITE_POINT_CUSTOM;
  EXPECT_FALSE(colorProfilesMatch(lcp, rcp));
  rcp.enc->white_point_xy[0] = 0.3127;
  rcp.enc->white_point_xy[1] = 0.3290;
  EXPECT_TRUE(colorProfilesMatch(lcp, rcp));

  /* Custom primaries identical to SRGB */
  rcp.enc->primaries = JXL_PRIMARIES_CUSTOM;
  EXPECT_FALSE(colorProfilesMatch(lcp, rcp));
  rcp.enc->primaries_red_xy[0] = 0.639998686;
  rcp.enc->primaries_red_xy[1] = 0.330010138;
  rcp.enc->primaries_green_xy[0] = 0.300003784;
  rcp.enc->primaries_green_xy[1] = 0.600003357;
  rcp.enc->primaries_blue_xy[0] = 0.150002046;
  rcp.enc->primaries_blue_xy[1] = 0.059997204;
  EXPECT_TRUE(colorProfilesMatch(lcp, rcp));
}

TEST(ColorProfilesMatch, Icc) {
  // Jxltk doesn't try to understand ICC profiles, so just use something
  // roughly ICC-shaped.
  const size_t iccSize = 130;
  auto icc1 = std::make_unique<uint8_t[]>(iccSize);
  uint8_t* data = icc1.get();
  data[36] = 'a';
  data[37] = 'c';
  data[38] = 's';
  data[39] = 'p';
  auto icc2 = std::make_unique<uint8_t[]>(iccSize);
  memcpy(icc2.get(), icc1.get(), iccSize);

  jxltk::ColorProfile lcp;
  lcp.icc.data = std::move(icc1);
  lcp.icc.size = iccSize;
  jxltk::ColorProfile rcp;
  rcp.icc.data = std::move(icc2);
  rcp.icc.size = iccSize;
  EXPECT_TRUE(colorProfilesMatch(lcp, rcp));

  rcp.icc.size --;
  EXPECT_FALSE(colorProfilesMatch(lcp, rcp));
  rcp.icc.size ++;

  // Change non-impactful fields
  memset(rcp.icc.data.get() + 44, 'a', 4);
  memset(rcp.icc.data.get() + 64, 'b', 4);
  memset(rcp.icc.data.get() + 84, 'c', 16);
  EXPECT_TRUE(colorProfilesMatch(lcp, rcp));
  rcp.icc.data[iccSize - 1] = 'x';
  EXPECT_FALSE(colorProfilesMatch(lcp, rcp));

  // Enumerated vs. ICC
  jxltk::ColorProfile enumProfile;
  JxlColorEncoding& cp = enumProfile.enc.emplace();
  JxlColorEncodingSetToSRGB(&cp, JXL_FALSE);
  EXPECT_FALSE(colorProfilesMatch(lcp, enumProfile));
}
