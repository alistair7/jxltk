/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#ifndef JXLTK_UTIL_H_
#define JXLTK_UTIL_H_

#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <jxl/types.h>

// Evaluates to a quoted string of the numeric value of macro s
#define JXLTK_ITOA(s) JXLTK_ITOA_(s)
#define JXLTK_ITOA_(s) #s

// >= C++20
#if __cplusplus >= 202002L
#define JXLTK_MAKE_UNIQUE_FOR_OVERWRITE std::make_unique_for_overwrite
#else
#define JXLTK_MAKE_UNIQUE_FOR_OVERWRITE std::make_unique
#endif

// Whether we can treat JXL_TYPE_FLOAT as float
#if defined(__STDC_IEC_559__)
#define JXLTK_FLOATS_ARE_IEEE754 1
#else
#include <cfloat>
#if FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MIN_EXP == -125
#define JXLTK_FLOATS_ARE_IEEE754 1
#endif
#endif

namespace jxltk {

/**
 * Split a string at each occurance of the specified char.
 */
std::vector<std::string_view> splitString(const std::string& str, char at,
                                    int maxsplit = 0, bool keepEmpty = false);

/**
 * Load bytes from a stream into the specified vector.
 * If filesize > 0, read exactly this number of bytes or fail,
 * else read until EOF.
 */
void loadFile(std::istream& in, std::vector<uint8_t>* data,
              size_t filesize = 0);

/**
 * Wrapper for loadFile that reads from the named file (which may be "-" for
 * stdin)
 */
void loadFile(const std::string& in, std::vector<uint8_t>* data,
              size_t filesize = 0);

/**
 * Return a copy of @p str in a safe-ish quoted format
 * suitable for pasting into a Unix-like shell as a single argument.
 * Assumes single-byte encoding or UTF-8.
 *
 * @param[in] alwaysQuote If true, always wrap the result in singe quotes.
 * If false, the string will be returned unmodified if it doesn't contain
 * any interesting characters.
 */
std::string shellQuote(std::string_view str, bool alwaysQuote = false);

/**
 * Return a simplified copy of @p str that should be safe to use as a filename
 * on most platforms. i.e. Reduce to a subset of printable ASCII and cap the
 * length.
 * @param[in] max If > 0, Truncate the string after this number of characters.
 */
std::string simplifyString(std::string_view str, size_t max = 0);

/**
 * Return the greatest common divisor of all numbers in @p numbers.
 *
 * An empty list causes 0 to be returned.
 * Zeros in the input are ignored.
 *
 */
uint32_t greatestCommonDivisor(const std::span<uint32_t>& numbers);

/**
 * Parse a numerator and denominator from a string of the form `[0-9]+(/[0-9]+)?`
 * e.g. "123/4", "42".  Returns an empty std::optional if the string couldn't
 * be parsed or the denominator was 0.
 */
std::optional<std::pair<uint32_t,uint32_t> > parseRational(const char* s);

/**
 * Multiply two unsigned values and return true if no overflow occurred.
 */
template<typename T>
typename std::enable_if<std::is_unsigned<T>::value, bool>::type
safeMul(T a, T b, T* product) {
  *product = a * b;
  return (a == 0 || a == 1 || b == 1 || *product / a == b);
}

/**
 * Add two unsigned values and return true if no overflow occurred.
 */
template<typename T>
typename std::enable_if<std::is_unsigned<T>::value, bool>::type
safeAdd(T a, T b, T* sum) {
  *sum = a + b;
  return *sum >= a;
}

constexpr size_t bytesPerSample(JxlDataType t) {
  if (t == JXL_TYPE_UINT8) return 1;
  if (t == JXL_TYPE_UINT16) return 2;
  if (t == JXL_TYPE_FLOAT) return 4;
  if (t == JXL_TYPE_FLOAT16) return 2;
  return 0;
}

constexpr size_t bytesPerPixel(JxlDataType t, uint32_t numChannels) {
  return numChannels * bytesPerSample(t);
}

/**
 * Remove one channel from a frame buffer, in place.
 *
 * @param[in,out] pixels Pointer to the pixel buffer, which will be "shrunk".
 * @param[in] index Index of the channel to remove (< format.num_channels).
 * @return 0 on success.
 */
int removeInterleavedChannel(void* pixels, uint32_t xsize, uint32_t ysize,
                             const JxlPixelFormat& format, uint32_t index);

}  // namespace jxltk

#endif  // JXLTK_UTIL_H_
