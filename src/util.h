/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#ifndef JXLTK_UTIL_H_
#define JXLTK_UTIL_H_

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

#include <jxl/types.h>

#include "../contrib/jxlazy/include/jxlazy/decoder.h"

#include "except.h"

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
 * Get a numeric rank for the data type.
 *
 * When dealing with data of different types, the lower-ranked type should be converted
 * to the higher-ranked type.
 */
int dataTypeRank(JxlDataType t);

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
 *
 * @param[out] product Result of the multiplication, allowed to point to one of the
 *   inputs.
 */
template<typename T>
typename std::enable_if<std::is_unsigned<T>::value, bool>::type
safeMul(T a, T b, T* product) {
  T result = a * b;
  if (a == 0 || a == 1 || b == 1 || result / a == b) {
    *product = result;
    return true;
  }
  return false;
}

/**
 * Add two unsigned values and return true if no overflow occurred.
 *
 * @param[out] sum Result of the addition, allowed to point to one of the inputs.
 */
template<typename T>
typename std::enable_if<std::is_unsigned<T>::value, bool>::type
safeAdd(T a, T b, T* sum) {
  T result = a + b;
  if (result >= a) {
    *sum = result;
    return true;
  }
  return false;
}

constexpr size_t bytesPerSample(JxlDataType t) {
  if (t == JXL_TYPE_UINT8) return 1;
  if (t == JXL_TYPE_UINT16) return 2;
  if (t == JXL_TYPE_FLOAT) return 4;
  if (t == JXL_TYPE_FLOAT16) return 2;
  throw JxltkError("%s: Unknown data type %d", __func__, static_cast<int>(t));
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

/**
 * Given two open Decoders, check that every pixel in every channel of every frame matches
 *
 * Channel layout must be identical. Frame durations, names and blending are ignored.
 * Whether frames are compared coalesced is determined by the options used when the inputs
 * were opened. Channel depths can be different, but each channel is compared at the
 * higher depth.
 */
bool haveSamePixels(jxlazy::Decoder& leftImage, jxlazy::Decoder& rightImage);



struct CropRegion {
  uint32_t width;
  uint32_t height;
  uint32_t x0;
  uint32_t y0;
};

/**
 * Find the smallest rectangular region of the frame that contains all the non-zero pixels.
 *
 * If there are no non-zero pixels, the region will be 0x0+0+0.
 *
 * @param[in] psamples Array of `width * height * numChannels` samples. Always
 *   native-endian with no row padding.
 * @param[in] xsize,ysize Width and height of the input in pixels.
 * @param[in] dataType Data type of samples in @p psamples.
 * @param[in] numChannels Number of interleaved channels in @p psamples.
 * @param[in] alphaCrop If true, consider only the last sample of each pixel. i.e.,
 *   assume the last channel is interleaved alpha, and we're cropping invisible pixels.
 * @param[out] cropRegion Result region.
 * @param[in] protectRegion If not nullptr, @p cropRegion will always contain this region.
 *   i.e. this function behaves as if all pixels within @p protectRegion are non-zero.
 *   This may be useful when checking multiple planar channels, as you might already know
 *   that a certain area must be preserved. This is allowed to be the same pointer as
 *   @p cropRegion.
 * @return 0 on success.
 */
int findCropRegion(const void* psamples, uint32_t xsize, uint32_t ysize,
                   JxlDataType dataType, size_t numChannels, bool alphaCrop,
                   CropRegion* cropRegion, const CropRegion* protectRegion = nullptr);


/**
Crop a frame buffer in place.

@param[in,out] psamples Array of `width * height * numChannels` interleaved samples in
  row-major order.  Updated in place to represent the requested crop.  After cropping,
  only the first `cropRegion.width * cropRegion.height * numChannels` samples in the
  array are valid.
@param[in] width,height Width and height of the uncropped image in pixels.
@param[in] numChannels Number of interleaved channels.
@param[in] dataType Sample data type.
@param[in] cropRegion Position and size of the region to keep, which must be a non-strict
  subset of the existing pixels.
@return 0 on success.
*/
int cropInPlace(void* psamples, uint32_t width, uint32_t height,
                JxlDataType dataType, size_t numChannels, const CropRegion& cropRegion);


/**
 * Wrapper for a file that gets deleted automatically on destruction.
 */
class TempFile {
public:
  std::fstream file{};
  std::string path{};

  TempFile() = default;
  TempFile(const TempFile&) = delete;
  TempFile(TempFile&&) = default;
  /**
   * Calls this->remove()
   */
  ~TempFile();

  /**
   * Call this->remove(), then open a new randomly-named file in the temporary directory.
   * (How securely this is done can vary.)  The file is always opened w+b.
   * Initialise @c file and @c path accordingly.
   */
  void open();

  /**
   * Close the current file if it's open (@c file).
   * This doesn't delete the file, and @c path remains set.
   */
  void close();

  /**
   * Close @c file if it's open.
   * Delete the file named @c path if it exists.
   * Clear @c path.
   */
  void remove();

  /**
   * Like close(), but also clear @c path, preventing automatic deletion.
   */
  void forget();
};

}  // namespace jxltk

#endif  // JXLTK_UTIL_H_
