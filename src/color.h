/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#ifndef JXLTK_COLOR_H_
#define JXLTK_COLOR_H_

#include <jxl/encode.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <optional>

namespace jxltk {

/**
 * Object containing a JxlColorProfile or an ICC blob, or both.
 * If both are present, the JxlColorEncoding should take priority.
 */
struct ColorProfile {
  ColorProfile() = default;
  ColorProfile(const ColorProfile& copy);
  ColorProfile(ColorProfile&& move) = default;
  ColorProfile& operator=(const ColorProfile& ass);
  ColorProfile& operator=(ColorProfile&& move) = default;
  operator bool() const { return icc.size > 0 || enc; }
  struct {
    std::unique_ptr<uint8_t[]> data;
    size_t size;
  } icc{};
  std::optional<JxlColorEncoding> enc{};
};

/**
 * Extract or derive the xy values of the primaries.
 *
 * Given an encoding, find the xy values of the red, green and blue
 * primaries.  On return, each of the @p r, @p g
 * and @p b parameters will be pointing to an array of 2 doubles
 * representing the xy values for that color.
 *
 * If @c enc->primaries is @c JXL_PRIMARIES_CUSTOM, the pointers will refer to
 * data within @p enc.  If @c enc->primaries is another known value, the
 * pointers will refer to constant static memory.
 *
 * @return @c true if the xy values could be determined, @c false otherwise.
 */
bool getPrimariesXY(const JxlColorEncoding& enc, const double** r,
                    const double** g, const double** b);

/**
 * Extract or derive the gamma exponent value of the transfer function, if applicable.
 *
 * @return @c true if the value could be determined, @c false otherwise.
 */
bool getGamma(const JxlColorEncoding& enc, double* gamma);

/**
 * Extract or derive the xy value of the white point.
 *
 * On return, @p *wp will point to an array of 2 doubles
 * representing the xy values for the white point.
 *
 * If @c enc->white_point is @c JXL_WHITE_POINT_CUSTOM, @p *wp will refer to
 * data within @p enc.  If @c enc->white_point is another known value, @p *wp
 * will refer to constant static memory.
 *
 * @return @c true if the xy value could be determined, @c false otherwise.
 */
bool getWhitePointXY(const JxlColorEncoding& enc, const double** wp);

/**
 * Return true iff the two color profiles are equivalent.
 *
 * If both profiles are using JxlColorEncoding, they are considered equal iff
 * the color model, primaries, transfer function and white point are equal. This
 * takes into account custom values that happen to exactly match pre-defined
 * values.
 *
 * If either profile is represented as an ICC blob, both must be ICC blobs
 * with matching content (considering only the bytes that affect the "ID")
 * to be considered equal.
 *
 * Renering intent is always ignored.
 */
bool colorProfilesMatch(const ColorProfile& left, const ColorProfile& right);

}  // namespace jxltk

#endif  // JXLTK_COLOR_H_
