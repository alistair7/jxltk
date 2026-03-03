/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
*/
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <jxl/types.h>

#include "../contrib/jxlazy/include/jxlazy/decoder.h"

#include "log.h"
#include "util.h"

using std::cerr;
using std::cin;
using std::ifstream;
using std::istream;
using std::ofstream;
using std::ostream;
using std::ostringstream;
using std::string;
using std::vector;

namespace jxltk {

vector<std::string_view> splitString(const string& str, char at, int maxsplit /*=0*/,
                                     bool keepEmpty /*=false*/) {
  vector<std::string_view> parts;
  size_t start = 0;
  size_t end;
  while ((end = str.find(at, start)) != string::npos &&
         (maxsplit < 0 || maxsplit-- > 0)) {
    if (keepEmpty || end - start > 0)
      parts.emplace_back(str.data() + start, end - start);
    start = end + 1;
  }
  if (keepEmpty || start != str.size() - 1)
    parts.emplace_back(str.data() + start, end - start);

  return parts;
}

int dataTypeRank(JxlDataType t) {
  return t == JXL_TYPE_UINT8 ? 10 :
         t == JXL_TYPE_UINT16 ? 20 :
         t == JXL_TYPE_FLOAT16 ? 30 :
         t == JXL_TYPE_FLOAT ? 40 : 0;
}

void loadFile(istream& in, vector<uint8_t>* data, size_t filesize /*=0*/) {
  data->clear();

  // Ensure exceptions on the input stream are restored before return
  struct Cleanup {
    istream& stream;
    istream::iostate state;
    ~Cleanup() { stream.exceptions(state); }
  } cleanup{.stream = in, .state = in.exceptions()};
  in.exceptions(istream::badbit);

  if (filesize > 0) {
    data->resize(filesize);
    in.read((char*)data->data(), filesize);
    if ((size_t)in.gcount() != filesize)
      throw std::runtime_error("Failed to read from provided input stream");
  } else {
    constexpr unsigned chunkSize = 1024 * 32;
    size_t got = 0;
    while (in.good()) {
      data->resize(got + chunkSize);
      in.read((char*)data->data() + got, data->size() - got);
      got += in.gcount();
    }
    data->resize(got);
  }
  data->shrink_to_fit();
}

void loadFile(const string& in, vector<uint8_t>* data, size_t filesize /*=0*/) {
  if (in == "-") return loadFile(cin, data, filesize);
  ifstream ins(in, std::ios::binary);
  return loadFile(ins, data, filesize);
}

string shellQuote(std::string_view str, bool mustQuote /*=false*/) {
  ostringstream oss;
  oss << '\'';
  for (char c : str) {
    if (c == '\'') {
      mustQuote = true;
      oss << "'\\''";
      continue;
    }
    int ord = static_cast<unsigned char>(c);
    if (!mustQuote &&
        (ord <= 0x2c || (ord >= 0x3b && ord <= 0x3f) ||
         (ord >= 0x5b && ord <= 0x5d) || ord == 0x60 ||
         (ord >= 0x7b && ord <= 0x7f))) {
      mustQuote = true;
    }
    oss << c;
  }
  /* If the string contains nothing interesting, ignore the
     ostringstream and just return a copy */
  if (!mustQuote) return std::string(str);
  oss << '\'';
  return oss.str();
}

uint32_t greatestCommonDivisor(const std::span<uint32_t>& numbers) {
  if (numbers.empty()) return 0;
  uint32_t ret = numbers[0];

  for (size_t i = 1; i < numbers.size(); ++i) {
    uint32_t tmp = numbers[i];
    // Ignoring zeros rather than letting them immediately return zero
    if (tmp == 0) continue;
    ret = std::gcd(ret, tmp);
  }
  return ret;
}

string simplifyString(std::string_view str, size_t max /*=0*/) {
  ostringstream oss;
  static const char allowed[] = " #$%&()+,-.=@[]_{}~";
  size_t count = 0;
  for (char c : str) {
    int ord = static_cast<unsigned char>(c);
    if ((ord >= 'A' && ord <= 'Z') || (ord >= 'a' && ord <= 'z') ||
        (ord >= '0' && ord <= '9') || strchr(allowed, ord)) {
      oss << static_cast<char>(c);
    } else {
      oss << '_';
    }
    if (max > 0 && ++count == max) break;
  }
  return oss.str();
}

std::optional<std::pair<uint32_t,uint32_t> > parseRational(const char* s) {
  char* endptr;
  errno = 0;
  unsigned long long tpsNumerator = strtoull(s, &endptr, 10);
  if (errno != 0) {
    return {};
  }
  unsigned long long tpsDenominator = 1;
  if (*endptr == '/') {
    errno = 0;
    tpsDenominator = strtoull(endptr + 1, &endptr, 10);
    if (errno != 0) {
      return {};
    }
  }
  if (*endptr != '\0' || tpsDenominator == 0) {
    return {};
  }
  return {{tpsNumerator, tpsDenominator}};
}

int removeInterleavedChannel(void* pixels, uint32_t xsize, uint32_t ysize,
                             const JxlPixelFormat& inFormat, uint32_t index) {
  if (index >= inFormat.num_channels) {
    return -1;
  }
  if (inFormat.num_channels == 1) {
    return 0;
  }

  size_t bytesPerSample = jxltk::bytesPerSample(inFormat.data_type);
  const size_t inStride = jxlazy::Decoder::getRowStride(xsize, inFormat, nullptr);
  if (inStride == 0) {
    return -1;
  }
  JxlPixelFormat outFormat = inFormat;
  --outFormat.num_channels;
  const size_t outStride = jxlazy::Decoder::getRowStride(xsize, outFormat, nullptr);

  for (uint32_t y = 0; y < ysize; ++y) {
    const char* in = static_cast<const char*>(pixels) + (y * inStride);
    char* out = static_cast<char*>(pixels) + (y * outStride);
    for (uint32_t x = 0; x < xsize; ++x) {
      for (uint32_t c = 0; c < inFormat.num_channels; ++c) {
        if (c == index) {
          in += bytesPerSample;
          continue;
        }
        if (out != in) {
          memcpy(out, in, bytesPerSample);
        }
        in += bytesPerSample;
        out += bytesPerSample;
      }
    }
  }
  return 0;
}

/**
 * Return the value of a "half step" when dividing the unit interval into (2**bits)-1 steps.
 *
 * The idea being a sample saved losslessly at this bit depth should be within this
 * distance of its original value after decoding, but obviously this crumbles as bits
 * approaches 32.
 *
 * TODO: learn to float.
 */
constexpr float getEpsilon(uint32_t bits) {
  return static_cast<float>(1.0 / (2 * (pow(2, bits) - 1)));
}

/**
 * Given two open Decoders, check that every pixel in every channel of every frame matches
 *
 * Channel layout must be identical. Frame durations, names and blending are ignored.
 * Whether frames are compared coalesced is determined by the options used when the inputs
 * were opened.  Channels depths can be different, but each channel is compared at the
 * higher depth.
 *
 * @return true if all pixels match, else false.
 */
bool haveSamePixels(jxlazy::Decoder& leftImage, jxlazy::Decoder& rightImage) {
  JxlBasicInfo leftInfo = leftImage.getBasicInfo();
  JxlBasicInfo rightInfo = rightImage.getBasicInfo();
  if (leftInfo.num_color_channels != rightInfo.num_color_channels) {
    JXLTK_DEBUG("Images have differing numbers of color channels (%" PRIu32
                " vs %" PRIu32 ").", leftInfo.num_color_channels,
                rightInfo.num_color_channels);
    return false;
  }
  if (leftInfo.num_extra_channels != rightInfo.num_extra_channels) {
    JXLTK_DEBUG("Images have differing numbers of extra channels (%" PRIu32
                " vs %" PRIu32 ").", leftInfo.num_extra_channels,
                rightInfo.num_extra_channels);
    return false;
  }
  std::vector<jxlazy::ExtraChannelInfo> leftEcInfo = leftImage.getExtraChannelInfo();
  std::vector<jxlazy::ExtraChannelInfo> rightEcInfo = rightImage.getExtraChannelInfo();
  for (size_t eci = 0; eci < leftEcInfo.size(); ++eci) {
    if (leftEcInfo[eci].info.type != rightEcInfo[eci].info.type) {
      JXLTK_DEBUG("Images have differing extra channel order/types.");
      return false;
    }
  }
  size_t frameCount = leftImage.frameCount();
  {
    size_t rightFrameCount = rightImage.frameCount();
    if (frameCount != rightFrameCount) {
      JXLTK_DEBUG("Images have differing numbers of frames (%zu vs %zu).",
                  frameCount, rightFrameCount);
      return false;
    }
  }
  auto frameLayerInfos = std::make_unique_for_overwrite<JxlLayerInfo[]>(frameCount);
  for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    jxlazy::FrameInfo frameInfo = leftImage.getFrameInfo(frameIndex);
    const JxlLayerInfo& layerInfo = frameInfo.header.layer_info;
    frameLayerInfos.get()[frameIndex] = layerInfo;
    {
      jxlazy::FrameInfo rightFrameInfo = rightImage.getFrameInfo(frameIndex);
      const JxlLayerInfo& rightLayerInfo = rightFrameInfo.header.layer_info;
      if (layerInfo.xsize != rightLayerInfo.xsize ||
          layerInfo.ysize != rightLayerInfo.ysize ||
          layerInfo.crop_x0 != rightLayerInfo.crop_x0 ||
          layerInfo.crop_y0 != rightLayerInfo.crop_y0) {
        JXLTK_DEBUG("Frame %zu has differing crop/offset ("
                    "%" PRIu32 "x%" PRIu32 "%c%" PRId32 "%c%" PRId32 " vs "
                    "%" PRIu32 "x%" PRIu32 "%c%" PRId32 "%c%" PRId32 ").", frameIndex,
                    layerInfo.xsize, layerInfo.ysize,
                    layerInfo.crop_x0 < 0 ? '-' : '+', abs(layerInfo.crop_x0),
                    layerInfo.crop_y0 < 0 ? '-' : '+', abs(layerInfo.crop_y0),
                    rightLayerInfo.xsize, rightLayerInfo.ysize,
                    rightLayerInfo.crop_x0 < 0 ? '-' : '+', abs(rightLayerInfo.crop_x0),
                    rightLayerInfo.crop_y0 < 0 ? '-' : '+', abs(rightLayerInfo.crop_y0));
        return false;
      }
    }
  }

  uint32_t colorBitsPerSample =
      std::max(leftInfo.bits_per_sample, rightInfo.bits_per_sample);
  float colorEpsilon = getEpsilon(colorBitsPerSample);
  JXLTK_TRACE("Color epsilon for %" PRIu32 " bits is %f", colorBitsPerSample,
              colorEpsilon);
  auto ecEpsilons = std::make_unique_for_overwrite<float[]>(leftInfo.num_extra_channels);
  for (size_t eci = 0; eci < leftInfo.num_extra_channels; ++eci) {
    uint32_t ecBitsPerSample = std::max(leftEcInfo[eci].info.bits_per_sample,
                                        rightEcInfo[eci].info.bits_per_sample);
    ecEpsilons[eci] = getEpsilon(ecBitsPerSample);
    JXLTK_TRACE("EC epsilon for %" PRIu32 " bits is %f", ecBitsPerSample,
                ecEpsilons[eci]);
  }

  jxlazy::FramePixels<float> leftFrame, rightFrame;
  for (size_t frameIdx = 0; frameIdx < frameCount; ++frameIdx) {
    leftImage.getFramePixels(&leftFrame, frameIdx, leftInfo.num_color_channels,
                             std::span<const int>({-1}));
    rightImage.getFramePixels(&rightFrame, frameIdx, rightInfo.num_color_channels,
                              std::span<const int>({-1}));

    const float* thisLeftColorData = leftFrame.color.data();
    const float* thisRightColorData = rightFrame.color.data();
    for (size_t sampleIdx = 0; sampleIdx < leftFrame.color.size(); ++sampleIdx) {
      float diff = std::fabs(*thisLeftColorData++) - std::fabs(*thisRightColorData++);
      if (diff >= colorEpsilon) {
        size_t pixelIndex = sampleIdx / leftInfo.num_color_channels;
        JXLTK_DEBUG("Color samples in frame %zu at pixel %zux%zu channel %zu differ "
                    "when decoded with %" PRIu32 "-bit precision", frameIdx,
                    pixelIndex / frameLayerInfos[frameIdx].ysize,
                    pixelIndex % frameLayerInfos[frameIdx].ysize,
                    sampleIdx % leftInfo.num_color_channels, colorBitsPerSample);
        return false;
      }
    }
    auto leftEcIter = leftFrame.ecs.cbegin();
    auto rightEcIter = rightFrame.ecs.cbegin();
    for (size_t ec = 0; ec < leftFrame.ecs.size(); ++ec) {
      const std::vector<float>& leftEc = (leftEcIter++)->second;
      const std::vector<float>& rightEc = (rightEcIter++)->second;
      const float* leftEcData = rightEc.data();
      const float* rightEcData = rightEc.data();
      for (size_t sampleIdx = 0; sampleIdx < leftEc.size(); ++sampleIdx) {
        float diff = std::fabs(*leftEcData++) - std::fabs(*rightEcData++);
        if (diff >= ecEpsilons[ec]) {
          size_t pixelIndex = sampleIdx;
          JXLTK_DEBUG("Extra channel samples in frame %zu at pixel %zux%zu channel %zu"
                      " differ.", frameIdx,
                      pixelIndex / frameLayerInfos[frameIdx].ysize,
                      pixelIndex % frameLayerInfos[frameIdx].ysize,
                      ec);
          return false;
        }
      }
    }
  }
  return true;
}

template<class T>
int findCropRegion(const T* psamples, uint32_t xsize, uint32_t ysize,
                   size_t numChannels, bool alphaCrop, CropRegion* cropRegion,
                   const CropRegion* protectRegion) {

  JXLTK_TRACE("%" PRIu32 "x%" PRIu32 "; %zu channels", xsize, ysize, numChannels);
  if (xsize == 0 || ysize == 0 || numChannels == 0) {
    JXLTK_ERROR("Invalid arguments to %s", __func__);
    cropRegion->x0 = cropRegion->y0 = cropRegion->width = cropRegion->height = 0;
    return -1;
  }
  const size_t startChannel = alphaCrop ? numChannels - 1 : 0;

  // x0 is the coordinate of the first pixel and x1 is the coordinate PAST the last pixel.
  // Set x0 and y0 to their respective upper bounds (inclusive).
  // Set x1 and y1 to their respective lower bounds (inclusive).
  uint32_t x0 = xsize - 1, y0 = ysize - 1, x1 = 0, y1 = 0;
  if (protectRegion) {
    if (protectRegion->width > xsize ||
        protectRegion->x0 > xsize - protectRegion->width ||
        protectRegion->height > ysize ||
        protectRegion->y0 > ysize - protectRegion->height) {
      JXLTK_ERROR("Invalid region: %" PRIu32 "x%" PRIu32 "+%" PRIu32 "+%" PRIu32
                  " doesn't fit within %" PRIu32 "x%" PRIu32 ".",
                  protectRegion->width, protectRegion->height, protectRegion->x0,
                  protectRegion->y0, xsize, ysize);
      return -1;
    }
    x0 = protectRegion->x0;
    x1 = protectRegion->x0 + protectRegion->width;
    y0 = protectRegion->y0;
    y1 = protectRegion->y0 + protectRegion->height;
    JXLTK_TRACE("Preserve region %" PRIu32 "x%" PRIu32 "+%" PRIu32 "+%" PRIu32 ", "
                "so x0 <= %" PRIu32 ", y0 <= %" PRIu32 ", x1 >= %" PRIu32 ", "
                "y1 >= %" PRIu32 "",
                protectRegion->width,
                protectRegion->height,
                protectRegion->x0,
                protectRegion->y0,
                x0, y0, x1, y1);
  }

  // Find top non-zero pixel (y0)
  const T* samples = psamples;
  bool foundPixel = false;
  for (uint32_t y = 0; y < ysize && !foundPixel; ++y) {
    if (y == y0) {
      // Reached caller's upper bound on y0, so have it exactly.
      JXLTK_TRACE("Searching down reached y0 upper bound, so break; "
                  "x0 <= %" PRIu32 " y0 = %" PRIu32 ", x1 >= %" PRIu32 ", y1 >= %" PRIu32,
                  x0, y0, x1, y1);
      foundPixel = true;
      break;
    }
    for (uint32_t x = 0; x < xsize && !foundPixel; ++x, samples += numChannels) {
      for (uint32_t c = startChannel; c < numChannels; ++c) {
        if (samples[c] != 0) {
          x0 = (x < x0) ? x : x0; // decrease upper bound
          y0 = y; // exact
          x1 = (x >= x1) ? x + 1 : x1; // increase lower bound
          y1 = (y >= y1) ? y + 1 : y1; // increase lower bound
          JXLTK_TRACE("Searching down: found pixel at (%" PRIu32 ",%" PRIu32 ");"
                      " so... x0 <= %" PRIu32 " y0 = %" PRIu32 ", x1 >= %" PRIu32 ", "
                      "y1 >= %" PRIu32 "",
                      x, y, x0, y0, x1, y1);
          foundPixel = true;
          break;
        }
      }
    }
  }

  if (!foundPixel) {
    JXLTK_INFO("No non-zero pixels");
    cropRegion->x0 = cropRegion->y0 = cropRegion->width = cropRegion->height = 0;
    return 0;
  }

  // Scan rows from bottom to top to find y1
  foundPixel = false;
  for (uint32_t y = ysize; y-- > 0 && !foundPixel; ) {
    if (y == y1 - 1) {
      // Reached known lower bound on y1, so have it exactly.
      JXLTK_TRACE("Searching up: %" PRIu32 " <= y1 < %" PRIu32 ", so stop", y1, y + 1);
      break;
    }
    for (uint32_t x = 0; x < xsize && !foundPixel; ++x) {
      samples = &psamples[numChannels * (y*xsize + x)];
      for (uint32_t c = startChannel; c < numChannels; ++c) {
        if (samples[c] != 0) {
          y1 = y + 1; // exact
          x0 = (x < x0) ? x : x0; // upper bound
          x1 = (x >= x1) ? x + 1 : x1; // lower bound
          JXLTK_TRACE("Searching up: found pixel at (%" PRIu32 ",%" PRIu32 ");"
                      " so... x0 <= %" PRIu32 ", x1 >= %" PRIu32 ", y1 = %" PRIu32 "",
                      x, y, x0, x1, y1);
          foundPixel = true;
          break;
        }
      }
    }
  }

  // Scan columns from left to find x0
  // x0 is currently an upper bound - if we find no pixels, it's now exact
  foundPixel = false;
  for (uint32_t x = 0; x < x0 && !foundPixel; x++) {
    for (uint32_t y = y0 + 1; y < y1 && !foundPixel; y++) {
      samples = &psamples[numChannels * (y*xsize + x)];
      for (uint32_t c = startChannel; c < numChannels; ++c) {
        if (samples[c] != 0) {
          x0 = x; // exact
          JXLTK_TRACE("Searching left->right: found pixel at (%" PRIu32 ",%" PRIu32 ");"
                      " so... x0 = %" PRIu32 ", x1 >= %" PRIu32 "", x, y, x0, x1);
          foundPixel = true;
          break;
        }
      }
    }
  }

  // Scan columns from right to find x1
  foundPixel = false;
  for (uint32_t x = xsize; x-- > 0 && !foundPixel; ) {
    if (x == x1 - 1) {
      // Reached known lower bound on x1, so have it exactly.
      JXLTK_TRACE("Searching right->left: %" PRIu32 " <= x1 < %" PRIu32 ", so stop",
                  x1, x + 1);
      break;
    }
    for (uint32_t y = y0; y < y1 && !foundPixel; y++) {
      samples = &psamples[numChannels * (y*xsize + x)];
      for (uint32_t c = startChannel; c < numChannels; ++c) {
        if (samples[c] != 0) {
          x1 = x + 1; // exact
          JXLTK_TRACE("Searching right->left: found pixel at (%" PRIu32 ",%" PRIu32 ");"
                      " so... x1 = %" PRIu32 "", x, y, x1);
          foundPixel = true;
          break;
        }
      }
    }
  }

  if (x0 >= xsize || y0 >= ysize || x1 <= x0 || y1 <= y0) {
    cropRegion->x0 = cropRegion->y0 = cropRegion->width = cropRegion->height = 0;
    return 0;
  }
  cropRegion->x0 = x0;
  cropRegion->y0 = y0;
  cropRegion->width = (x1 > x0 ? x1 - x0 : 0);
  cropRegion->height = (y1 > y0 ? y1 - y0 : 0);
  return 0;
}

int findCropRegion(const void* psamples, uint32_t xsize, uint32_t ysize,
                   JxlDataType dataType, size_t numChannels, bool alphaCrop,
                   CropRegion* cropRegion, const CropRegion* protectRegion) {
  if (dataType == JXL_TYPE_UINT8) {
    return findCropRegion<uint8_t>(static_cast<const uint8_t*>(psamples),
                                   xsize, ysize, numChannels, alphaCrop, cropRegion,
                                   protectRegion);
  }
  if (dataType == JXL_TYPE_UINT16) {
    return findCropRegion<uint16_t>(static_cast<const uint16_t*>(psamples),
                                    xsize, ysize, numChannels, alphaCrop, cropRegion,
                                    protectRegion);
  }
  if (dataType == JXL_TYPE_FLOAT) {
    return findCropRegion<float>(static_cast<const float*>(psamples),
                                 xsize, ysize, numChannels, alphaCrop, cropRegion,
                                 protectRegion);
  }
  JXLTK_ERROR("Unsupported data type: %d", static_cast<int>(dataType));
  return -1;
}

int cropInPlace(void* psamples, uint32_t width, uint32_t height,
                JxlDataType dataType, size_t numChannels, const CropRegion& cropRegion) {
  uint32_t x1, y1;
  if (width == 0 || height == 0 || cropRegion.width == 0 || cropRegion.height == 0 ||
      numChannels == 0 || cropRegion.x0 >= width || cropRegion.y0 >= height ||
      !safeAdd(cropRegion.x0, cropRegion.width, &x1) ||
      !safeAdd(cropRegion.y0, cropRegion.height, &y1)) {
    JXLTK_ERROR("Invalid arguments");
    return -1;
  }
  if (x1 > width || y1 > height) {
    JXLTK_ERROR("Crop region cannot extend outside the frame");
    return -1;
  }

  // Special case where no pixels need to move:
  if (cropRegion.y0 == 0 && cropRegion.x0 == 0 &&
     (cropRegion.width == width || cropRegion.height == 1)) {
    JXLTK_DEBUG("No-op crop!");
    return 0;
  }

  const size_t bytesPerPixel = bytesPerSample(dataType) * numChannels;
  const size_t fullStride = width * bytesPerPixel;
  const size_t cropStride = cropRegion.width * bytesPerPixel;
  // Whatever format the samples are in, access as a char[].
  char* samples = (char*)psamples;

  const size_t cropOffsetPixels = cropRegion.y0 * width + cropRegion.x0;
  const size_t unwidth = width - cropRegion.width;

  // Use memcpy when the number of pixels being moved is <= the distance they're moving.
  // i.e. `cropWidth <= cropOffsetPixels + y * unwidth`
  // ...where `y` is the output scanline in [0, cropHeight - 1].
  // With each row, we gain an extra `unwidth` of headroom.

  // Special cases to avoid division by 0 unwidth, and negative numbers:
  uint32_t firstMemcpyScan;
  if (cropOffsetPixels >= cropRegion.width || unwidth == 0) {
    firstMemcpyScan = 0;
  } else {
    firstMemcpyScan = (cropRegion.width - cropOffsetPixels) / unwidth + 1;
    firstMemcpyScan = std::min(firstMemcpyScan, cropRegion.height);
  }

  for (size_t y = 0; y < firstMemcpyScan; y++) {
    size_t moveTo = y * cropStride;
    size_t moveFrom = (y + cropRegion.y0) * fullStride + cropRegion.x0 * bytesPerPixel;
    memmove(&samples[moveTo], &samples[moveFrom], cropStride);
  }
  for (size_t y = firstMemcpyScan; y < cropRegion.height; y++) {
    size_t moveTo = y * cropStride;
    size_t moveFrom = (y + cropRegion.y0) * fullStride + cropRegion.x0 * bytesPerPixel;
    memcpy(&samples[moveTo], &samples[moveFrom], cropStride);
  }

  return 0;
}

}  // namespace jxltk
