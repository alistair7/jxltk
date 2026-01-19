/**
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */

#include <gtest/gtest.h>

#include "enums.h"

/*
#include "enums.cpp"

TEST(StrnCaseCmp, Works) {
  struct {
    const char* a;
    const char* b;
    size_t count;
    int expectSign;
  } tests[] = {
    { "", "", 0, 0 },
    { "", "", 1, 0 },
    { "", "", 10, 0 },
    { "a", "A", 0, 0 },
    { "A", "a", 1, 0 },
    { "A", "A", 2, 0 },
    { "a", "a", 3, 0 },
    { "a", "b", 0, 0 },
    { "a", "b", 1, -1 },
    { "b", "a", 1, 1 },
    { "ab", "ac", 1, 0 },
    { "Ab", "ac", 1, 0 },
    { "ab", "ac", 2, -1 },
  };

  for (const auto& test : tests) {
    int result = jxltk::strncasecmp(test.a, test.b, test.count);
    EXPECT_EQ(result, test.expectSign) << "Failed on test " << (&test - tests);
  }
}
*/

TEST(BlendModeName, RoundTrips) {
  struct {
    const char* name;
    JxlBlendMode expect;
    bool retval;
  } tests[] = {
    { "JXL_BLEND_REPLACE", JXL_BLEND_REPLACE, true },
    { "JXL_BLEND_BLEND", JXL_BLEND_BLEND, true },
    { "JXL_BLEND_ADD", JXL_BLEND_ADD, true },
    { "JXL_BLEND_MUL", JXL_BLEND_MUL, true },
    { "JXL_BLEND_MULADD", JXL_BLEND_MULADD, true },
    { "", JXL_BLEND_REPLACE, false },
  };

  for (const auto& test : tests) {
    JxlBlendMode result;
    EXPECT_EQ(jxltk::blendModeFromName(test.name, &result), test.retval);
    EXPECT_EQ(result, test.expect);
    if (!test.retval) {
      continue;
    }

    char alternative[50];

    // Try mixed case
    strcpy(alternative, test.name);
    alternative[0] = tolower(static_cast<unsigned char>(alternative[0]));
    EXPECT_TRUE(jxltk::blendModeFromName(alternative, &result));
    EXPECT_EQ(result, test.expect);

    // Try without the JXL_BLEND_ prefix
    strcpy(alternative, test.name + 10);
    alternative[0] = tolower(static_cast<unsigned char>(alternative[0]));
    EXPECT_TRUE(jxltk::blendModeFromName(alternative, &result));
    EXPECT_EQ(result, test.expect);

    // Converting back should always give the full name
    EXPECT_STREQ(jxltk::blendModeName(result), test.name);
  }
}

TEST(ColorSpaceName, RoundTrips) {
  struct {
    const char* name;
    JxlColorSpace expect;
    bool retval;
  } tests[] = {
    { "JXL_COLOR_SPACE_RGB", JXL_COLOR_SPACE_RGB, true },
    { "JXL_COLOR_SPACE_GRAY", JXL_COLOR_SPACE_GRAY, true },
    { "JXL_COLOR_SPACE_XYB", JXL_COLOR_SPACE_XYB, true },
    { "JXL_COLOR_SPACE_UNKNOWN", JXL_COLOR_SPACE_UNKNOWN, true },
    { "JXL_COLOR_SPACE_RGBxxxx", JXL_COLOR_SPACE_UNKNOWN, false },
  };

  for (const auto& test : tests) {
    JxlColorSpace result;
    EXPECT_EQ(jxltk::colorSpaceFromName(test.name, &result), test.retval);
    EXPECT_EQ(result, test.expect);
    if (!test.retval) {
      continue;
    }

    char alternative[50];

    // Try mixed case
    strcpy(alternative, test.name);
    alternative[0] = tolower(static_cast<unsigned char>(alternative[0]));
    EXPECT_TRUE(jxltk::colorSpaceFromName(alternative, &result));
    EXPECT_EQ(result, test.expect);

    // Try without the JXL_COLOR_SPACE_ prefix
    strcpy(alternative, test.name + 16);
    alternative[0] = tolower(static_cast<unsigned char>(alternative[0]));
    EXPECT_TRUE(jxltk::colorSpaceFromName(alternative, &result));
    EXPECT_EQ(result, test.expect);

    // Converting back should always give the full name
    EXPECT_STREQ(jxltk::colorSpaceName(result), test.name);
  }
}

TEST(OrientationName, RoundTrips) {
  struct {
    const char* name;
    JxlOrientation expect;
    bool retval;
  } tests[] = {
    { "JXL_ORIENT_IDENTITY", JXL_ORIENT_IDENTITY, true },
    { "JXL_ORIENT_ROTATE_90_CCW", JXL_ORIENT_ROTATE_90_CCW, true },
    { "JXL_ORIENT_ROTATE_180", JXL_ORIENT_ROTATE_180, true },
    { "JXL_ORIENT_ROTATE_90_CW", JXL_ORIENT_ROTATE_90_CW, true },
    { "JXL_ORIENT_FLIP_HORIZONTAL", JXL_ORIENT_FLIP_HORIZONTAL, true },
    { "JXL_ORIENT_FLIP_VERTICAL", JXL_ORIENT_FLIP_VERTICAL, true },
    { "JXL_ORIENT_TRANSPOSE", JXL_ORIENT_TRANSPOSE, true },
    { "JXL_ORIENT_ANTI_TRANSPOSE", JXL_ORIENT_ANTI_TRANSPOSE, true },
    { " JXL_ORIENT_TRANSPOSE", JXL_ORIENT_IDENTITY, false },
  };

  for (const auto& test : tests) {
    JxlOrientation result;
    EXPECT_EQ(jxltk::orientationFromName(test.name, &result), test.retval);
    EXPECT_EQ(result, test.expect);
    if (!test.retval) {
      continue;
    }

    char alternative[50];

    // Try mixed case
    strcpy(alternative, test.name);
    alternative[0] = tolower(static_cast<unsigned char>(alternative[0]));
    EXPECT_TRUE(jxltk::orientationFromName(alternative, &result));
    EXPECT_EQ(result, test.expect);

    // Try without the JXL_ORIENT_ prefix
    strcpy(alternative, test.name + 11);
    alternative[0] = tolower(static_cast<unsigned char>(alternative[0]));
    EXPECT_TRUE(jxltk::orientationFromName(alternative, &result));
    EXPECT_EQ(result, test.expect);

    // Converting back should always give the full name
    EXPECT_STREQ(jxltk::orientationName(result), test.name);
  }
}

TEST(PrimariesName, RoundTrips) {
  struct {
    const char* name;
    JxlPrimaries expect;
    bool retval;
  } tests[] = {
    { "JXL_PRIMARIES_SRGB", JXL_PRIMARIES_SRGB, true },
    { "JXL_PRIMARIES_2100", JXL_PRIMARIES_2100, true },
    { "JXL_PRIMARIES_P3", JXL_PRIMARIES_P3, true },
    { "JXL_PRIMARIES_CUSTOM", JXL_PRIMARIES_CUSTOM, true },
    { "", JXL_PRIMARIES_SRGB, false },
  };

  for (const auto& test : tests) {
    JxlPrimaries result;
    EXPECT_EQ(jxltk::primariesFromName(test.name, &result), test.retval);
    EXPECT_EQ(result, test.expect);
    if (!test.retval) {
      continue;
    }

    char alternative[50];

    // Try mixed case
    strcpy(alternative, test.name);
    alternative[0] = tolower(static_cast<unsigned char>(alternative[0]));
    EXPECT_TRUE(jxltk::primariesFromName(alternative, &result));
    EXPECT_EQ(result, test.expect);

    // Try without the JXL_PRIMARIES_ prefix
    strcpy(alternative, test.name + 14);
    alternative[0] = tolower(static_cast<unsigned char>(alternative[0]));
    EXPECT_TRUE(jxltk::primariesFromName(alternative, &result));
    EXPECT_EQ(result, test.expect);

    // Converting back should always give the full name
    EXPECT_STREQ(jxltk::primariesName(result), test.name);
  }
}

TEST(RenderingIntentName, RoundTrips) {
  struct {
    const char* name;
    JxlRenderingIntent expect;
    bool retval;
  } tests[] = {
    { "JXL_RENDERING_INTENT_RELATIVE", JXL_RENDERING_INTENT_RELATIVE, true },
    { "", JXL_RENDERING_INTENT_RELATIVE, false },
  };

  for (const auto& test : tests) {
    JxlRenderingIntent result;
    EXPECT_EQ(jxltk::renderingIntentFromName(test.name, &result), test.retval);
    EXPECT_EQ(result, test.expect);
    if (!test.retval) {
      continue;
    }

    char alternative[50];

    // Try mixed case
    strcpy(alternative, test.name);
    alternative[0] = tolower(static_cast<unsigned char>(alternative[0]));
    EXPECT_TRUE(jxltk::renderingIntentFromName(alternative, &result));
    EXPECT_EQ(result, test.expect);

    // Try without the JXL_RENDERING_INTENT_ prefix
    strcpy(alternative, test.name + 21);
    alternative[0] = tolower(static_cast<unsigned char>(alternative[0]));
    EXPECT_TRUE(jxltk::renderingIntentFromName(alternative, &result));
    EXPECT_EQ(result, test.expect);

    // Converting back should always give the full name
    EXPECT_STREQ(jxltk::renderingIntentName(result), test.name);
  }
}

TEST(TransferFunctionName, RoundTrips) {
  struct {
    const char* name;
    JxlTransferFunction expect;
    bool retval;
  } tests[] = {
    { "JXL_TRANSFER_FUNCTION_SRGB", JXL_TRANSFER_FUNCTION_SRGB, true },
    { "JXL_TRANSFER_FUNCTION_GAMMA", JXL_TRANSFER_FUNCTION_GAMMA, true },
    { "JXL_TRANSFER_FUNCTION_LINEAR", JXL_TRANSFER_FUNCTION_LINEAR, true },
    { "JXL_TRANSFER_FUNCTION_709", JXL_TRANSFER_FUNCTION_709, true },
    { "JXL_TRANSFER_FUNCTION_DCI", JXL_TRANSFER_FUNCTION_DCI, true },
    { "JXL_TRANSFER_FUNCTION_HLG", JXL_TRANSFER_FUNCTION_HLG, true },
    { "JXL_TRANSFER_FUNCTION_PQ", JXL_TRANSFER_FUNCTION_PQ, true },
    { "JXL_TRANSFER_FUNCTION_UNKNOWN", JXL_TRANSFER_FUNCTION_UNKNOWN, true },
    { "", JXL_TRANSFER_FUNCTION_UNKNOWN, false },
  };

  for (const auto& test : tests) {
    JxlTransferFunction result;
    EXPECT_EQ(jxltk::transferFunctionFromName(test.name, &result), test.retval);
    EXPECT_EQ(result, test.expect);
    if (!test.retval) {
      continue;
    }

    char alternative[50];

    // Try mixed case
    strcpy(alternative, test.name);
    alternative[0] = tolower(static_cast<unsigned char>(alternative[0]));
    EXPECT_TRUE(jxltk::transferFunctionFromName(alternative, &result));
    EXPECT_EQ(result, test.expect);

    // Try without the JXL_TRANSFER_FUNCTION_ prefix
    strcpy(alternative, test.name + 22);
    alternative[0] = tolower(static_cast<unsigned char>(alternative[0]));
    EXPECT_TRUE(jxltk::transferFunctionFromName(alternative, &result));
    EXPECT_EQ(result, test.expect);

    // Converting back should always give the full name
    EXPECT_STREQ(jxltk::transferFunctionName(result), test.name);
  }
}

TEST(WhitePointName, RoundTrips) {
  struct {
    const char* name;
    JxlWhitePoint expect;
    bool retval;
  } tests[] = {
    { "JXL_WHITE_POINT_D65", JXL_WHITE_POINT_D65, true },
    { "JXL_WHITE_POINT_DCI", JXL_WHITE_POINT_DCI, true },
    { "JXL_WHITE_POINT_E", JXL_WHITE_POINT_E, true },
    { "JXL_WHITE_POINT_CUSTOM", JXL_WHITE_POINT_CUSTOM, true },
    { "JXL_WHITE_POINT_", JXL_WHITE_POINT_D65, false },
  };

  for (const auto& test : tests) {
    JxlWhitePoint result;
    EXPECT_EQ(jxltk::whitePointFromName(test.name, &result), test.retval);
    EXPECT_EQ(result, test.expect);
    if (!test.retval) {
      continue;
    }

    char alternative[50];

    // Try mixed case
    strcpy(alternative, test.name);
    alternative[0] = tolower(static_cast<unsigned char>(alternative[0]));
    EXPECT_TRUE(jxltk::whitePointFromName(alternative, &result));
    EXPECT_EQ(result, test.expect);

    // Try without the JXL_WHITE_POINT_ prefix
    strcpy(alternative, test.name + 16);
    alternative[0] = tolower(static_cast<unsigned char>(alternative[0]));
    EXPECT_TRUE(jxltk::whitePointFromName(alternative, &result));
    EXPECT_EQ(result, test.expect);

    // Converting back should always give the full name
    EXPECT_STREQ(jxltk::whitePointName(result), test.name);
  }
}
