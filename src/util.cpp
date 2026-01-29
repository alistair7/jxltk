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

}  // namespace jxltk
