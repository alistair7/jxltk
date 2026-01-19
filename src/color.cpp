/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#include "color.h"

#include <jxl/encode.h>

#include <cmath>
#include <cstring>

#include "enums.h"
#include "log.h"

namespace jxltk {

constexpr double PRIMARIES_SRGB[3][2] = {
    /*r*/ {.639998686, .330010138},
    /*g*/ {.300003784, .600003357},
    /*b*/ {.150002046, .059997204} };

ColorProfile::ColorProfile(const ColorProfile& copy)
    : icc({.size = copy.icc.size}),
      enc(copy.enc) {
  if (icc.size > 0) {
    icc.data = std::make_unique<uint8_t[]>(icc.size);
    memcpy(icc.data.get(), copy.icc.data.get(), icc.size);
  }
}

ColorProfile& ColorProfile::operator=(const ColorProfile& ass) {
  if (&ass == this) return *this;

  this->enc = ass.enc;
  if ((icc.size = ass.icc.size) > 0) {
    icc.data = std::make_unique<uint8_t[]>(icc.size);
    memcpy(icc.data.get(), ass.icc.data.get(), icc.size);
  }
  return *this;
}

bool getPrimariesXY(const JxlColorEncoding& enc, const double** r,
                      const double** g, const double** b) {
  if (enc.primaries == JXL_PRIMARIES_SRGB) {
    *r = PRIMARIES_SRGB[0];
    *g = PRIMARIES_SRGB[1];
    *b = PRIMARIES_SRGB[2];
    return true;
  }

  *r = enc.primaries_red_xy;
  *g = enc.primaries_green_xy;
  *b = enc.primaries_blue_xy;
  return enc.primaries == JXL_PRIMARIES_CUSTOM;
}

bool getGamma(const JxlColorEncoding& enc, double* gamma) {
  // gamma is only available for LINEAR or GAMMA transfer functions.
  if (enc.transfer_function == JXL_TRANSFER_FUNCTION_LINEAR) {
    *gamma = 1.0;
    return true;
  }
  if (enc.transfer_function == JXL_TRANSFER_FUNCTION_SRGB) {
    // Best approximation, but not really correct for SRGB
    *gamma = .45455;
    return false;
  }
  *gamma = enc.gamma;
  return enc.transfer_function == JXL_TRANSFER_FUNCTION_GAMMA;
}

static const double WP_D65[] = {.3127, .3290};
static const double WP_E[] = {1 / 3.0, 1 / 3.0};
static const double WP_DCI[] = {.314, .351};

bool getWhitePointXY(const JxlColorEncoding& enc, const double** wp) {
  switch (enc.white_point) {
    case JXL_WHITE_POINT_D65:
      *wp = WP_D65;
      break;
    case JXL_WHITE_POINT_E:
      *wp = WP_E;
      break;
    case JXL_WHITE_POINT_DCI:
      *wp = WP_DCI;
      break;
    default:
      *wp = enc.white_point_xy;
      return enc.white_point == JXL_WHITE_POINT_CUSTOM;
  }
  return true;
}

static const double maxerr = .000000001;

static bool encodedProfilesMatch(const JxlColorEncoding& left_color,
                                 const JxlColorEncoding& right_color) {
  // Distinct color space -> profiles differ
  if (left_color.color_space != right_color.color_space ||
      left_color.color_space == JXL_COLOR_SPACE_UNKNOWN ||
      right_color.color_space == JXL_COLOR_SPACE_UNKNOWN) {
    JXLTK_TRACE("Color spaces don't match.");
    return false;
  }

  // Distinct, non-custom primaries -> profiles differ
  if (left_color.primaries != right_color.primaries &&
      left_color.primaries != JXL_PRIMARIES_CUSTOM &&
      right_color.primaries != JXL_PRIMARIES_CUSTOM) {
    JXLTK_TRACE("Primaries don't match (%s vs %s).",
                primariesName(left_color.primaries),
                primariesName(right_color.primaries));
    return false;
  }

  // If one or both primaries are custom, derive xy and compare coordinates
  if (left_color.primaries != right_color.primaries ||
      left_color.primaries == JXL_PRIMARIES_CUSTOM) {
    JXLTK_TRACE("One or both encodings use custom primaries.");
    const double *left_rgb[3], *right_rgb[3];
    if (!getPrimariesXY(left_color, &left_rgb[0], &left_rgb[1],
                          &left_rgb[2]) ||
        !getPrimariesXY(right_color, &right_rgb[0], &right_rgb[1],
                          &right_rgb[2])) {
      JXLTK_TRACE("Could not get xy coordinates for both profiles' primaries, "
                  "so assuming not equal.");
      return false;
    }

    // Derived primary xy values differ -> profiles differ
    for (unsigned i = 0; i < (sizeof left_rgb / sizeof left_rgb[0]); ++i) {
      for (int j = 0; j < 2; ++j) {
        if (fabs(left_rgb[i][j] - right_rgb[i][j]) > maxerr) {
          JXLTK_TRACE("Primaries xy mismatch on channel %u", i);
          return false;
        }
      }
    }
  }
  JXLTK_TRACE("Primaries match.");

  // Unknown transfer function -> profiles differ
  if (left_color.transfer_function == JXL_TRANSFER_FUNCTION_UNKNOWN ||
      right_color.transfer_function == JXL_TRANSFER_FUNCTION_UNKNOWN) {
    JXLTK_TRACE("One or both profiles have an unknown transfer function.");
    return false;
  }

  // If both transfer functions are gamma (or linear), compare gamma
  if ((left_color.transfer_function == JXL_TRANSFER_FUNCTION_GAMMA ||
       left_color.transfer_function == JXL_TRANSFER_FUNCTION_LINEAR) &&
      (right_color.transfer_function == JXL_TRANSFER_FUNCTION_GAMMA ||
       right_color.transfer_function == JXL_TRANSFER_FUNCTION_LINEAR)) {
    JXLTK_TRACE("Both profiles have a power-law transfer function.");
    double left_gamma, right_gamma;
    if (!getGamma(left_color, &left_gamma) ||
        !getGamma(right_color, &right_gamma)) {
      JXLTK_TRACE("Could not get specific gamma values.");
      return false;
    }
    // Gamma differs -> profiles differ
    if (fabs(left_gamma - right_gamma) > .000001) {
      JXLTK_TRACE("Gamma doesn't match (%f vs %f)",
                  left_gamma, right_gamma);
      return false;
    }
  } else if (left_color.transfer_function != right_color.transfer_function) {
    JXLTK_TRACE("Transfer functions don't match (%s vs %s).",
                transferFunctionName(left_color.transfer_function),
                transferFunctionName(right_color.transfer_function));
    return false;
  }

  // Distinct, non-custom white point -> profiles differ
  if (left_color.white_point != right_color.white_point &&
      left_color.white_point != JXL_WHITE_POINT_CUSTOM &&
      right_color.white_point != JXL_WHITE_POINT_CUSTOM) {
    JXLTK_TRACE("White points don't match (%s vs %s).",
                whitePointName(left_color.white_point),
                whitePointName(right_color.white_point));
    return false;
  }

  // If one or both white points are custom, derive xy and compare values
  if (left_color.white_point != right_color.white_point ||
      left_color.white_point == JXL_WHITE_POINT_CUSTOM) {
    const double *left_wp, *right_wp;
    if (!getWhitePointXY(left_color, &left_wp) ||
        !getWhitePointXY(right_color, &right_wp)) {
      JXLTK_TRACE("Could not get xy coordinates for both profiles' white "
                  "points, so assuming not equal.");
      return false;
    }

    // Derived gamma xy values differ -> profiles differ
    for (int j = 0; j < 2; ++j) {
      if (fabs(left_wp[j] - right_wp[j]) > maxerr) {
        JXLTK_TRACE("White point xy mismatch.");
        return false;
      }
    }
  }

  // Note, rendering intent is ignored
  JXLTK_TRACE("Encoded profiles are equivalent%s.",
              (left_color.rendering_intent != right_color.rendering_intent) ?
              " (even though they have different rendering intents)" : "");
  return true;
}

bool colorProfilesMatch(const ColorProfile& left, const ColorProfile& right) {
  if (left.enc && right.enc) {
    return encodedProfilesMatch(*left.enc, *right.enc);
  }
  if (left.icc.size >= 128 && left.icc.size == right.icc.size) {
    // If we have two ICCs, compare the parts over which the embedded MD5 is
    // calculated. The MD5 itself is allowed to be blank.
    const uint8_t* leftIcc = left.icc.data.get();
    const uint8_t* rightIcc = right.icc.data.get();
    bool icc_equal =
        memcmp(leftIcc, rightIcc, 44) == 0 &&
        // skip profile flags [44,48)
        memcmp(leftIcc + 48, rightIcc + 48, 16) == 0 &&
        // skip rendering intent [64,68)
        memcmp(leftIcc + 68, rightIcc + 68, 16) == 0 &&
        // skip MD5 [84,100)
        memcmp(leftIcc + 100, rightIcc + 100, left.icc.size - 100) == 0;
    JXLTK_TRACE("ICCs %smatch.", (icc_equal ? "" : "do not "));
    return icc_equal;
  }
  return false;
}

}  // namespace jxltk
