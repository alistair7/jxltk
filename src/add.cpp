/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <jxl/encode_cxx.h>

#include "../contrib/jxlazy/include/jxlazy/decoder.h"

#include "common.h"
#include "enums.h"
#include "log.h"
#include "mergeconfig.h"
#include "util.h"

namespace jxltk {


int addOrSubtract(jxlazy::Decoder& leftImage, jxlazy::Decoder& rightImage, bool adding,
                  std::ostream* fout, const FrameConfig& frameConfig,
                  size_t numThreads, size_t* written) {
  const char* opName = adding ? "add" : "subtract";

  // Validate
  JxlBasicInfo leftInfo = leftImage.getBasicInfo();
  JxlBasicInfo rightInfo = rightImage.getBasicInfo();
  if (leftInfo.xsize != rightInfo.xsize ||
      leftInfo.ysize != rightInfo.ysize) {
    JXLTK_ERROR("Can't %s images of different dimensions.", opName);
    return EXIT_FAILURE;
  }
  if (leftInfo.num_color_channels != rightInfo.num_color_channels ||
      leftInfo.num_extra_channels != rightInfo.num_extra_channels) {
    JXLTK_ERROR("Can't %s images with differing numbers of channels.", opName);
    return EXIT_FAILURE;
  }
  std::vector<jxlazy::ExtraChannelInfo> leftExtra = leftImage.getExtraChannelInfo();
  {
    std::vector<jxlazy::ExtraChannelInfo> rightExtra = rightImage.getExtraChannelInfo();
    for (size_t i = 0; i < leftExtra.size(); ++i) {
      // TODO: could be less strict, and treat missing alpha as implicitly all 1s
      if (leftExtra[i].info.type != rightExtra[i].info.type) {
        JXLTK_ERROR("Can't %s images with differing types of channels.", opName);
        return EXIT_FAILURE;
      }
    }
  }
  size_t frameCount = leftImage.frameCount();
  if (rightImage.frameCount() != frameCount) {
    JXLTK_ERROR("Can't %s images with differing numbers of frames.", opName);
    return EXIT_FAILURE;
  }

  // Prepare encoder for result
  auto [encp, runner] = makeThreadedEncoder(nullptr, numThreads);
  JxlEncoder* enc = encp.get();
  JxlBasicInfo encInfo = leftInfo;
  encInfo.uses_original_profile =
      frameConfig.distance.value_or(0) < kLosslessDistanceThreshold ?
          JXL_TRUE : JXL_FALSE;
  encInfo.num_color_channels = leftInfo.num_color_channels;
  encInfo.num_extra_channels = std::max(leftInfo.num_extra_channels,
                                        rightInfo.num_extra_channels);
  encInfo.bits_per_sample = std::max(leftInfo.bits_per_sample, rightInfo.bits_per_sample);
  encInfo.exponent_bits_per_sample = std::max(leftInfo.exponent_bits_per_sample,
                                              rightInfo.exponent_bits_per_sample);
  encInfo.alpha_bits = std::max(leftInfo.alpha_bits, rightInfo.alpha_bits);
  encInfo.alpha_exponent_bits = std::max(leftInfo.alpha_exponent_bits,
                                         rightInfo.alpha_exponent_bits);
  JXLTK_INFO("Writing basic info: %s", toString(encInfo).c_str());
  if (JxlEncoderSetBasicInfo(enc, &encInfo) != JXL_ENC_SUCCESS) {
    JXLTK_ERROR("Failed to set basic info.");
    return EXIT_FAILURE;
  }
  for (size_t eci = 0; eci < leftInfo.num_extra_channels; ++eci) {
    const jxlazy::ExtraChannelInfo& ecInfo = leftExtra[eci];
    if (JxlEncoderSetExtraChannelInfo(enc, eci, &ecInfo.info) != JXL_ENC_SUCCESS) {
      JXLTK_ERROR("Failed to set extra channel info (%zu).", eci);
      return EXIT_FAILURE;
    }
    if (!ecInfo.name.empty() &&
        JxlEncoderSetExtraChannelName(enc, eci, ecInfo.name.data(), ecInfo.name.size())
            != JXL_ENC_SUCCESS) {
      JXLTK_ERROR("Failed to set extra channel name (%zu).", eci);
      return EXIT_FAILURE;
    }
  }
  JxlColorEncoding colorEnc;
  if (leftImage.getEncodedColorProfile(JXL_COLOR_PROFILE_TARGET_DATA, &colorEnc)) {
    if (JxlEncoderSetColorEncoding(enc, &colorEnc) != JXL_ENC_SUCCESS) {
      JXLTK_ERROR("Failed to set color encoding.");
      return EXIT_FAILURE;
    }
  } else {
    std::vector<uint8_t> icc = leftImage.getIccProfile(JXL_COLOR_PROFILE_TARGET_DATA);
    if (!icc.empty() && JxlEncoderSetICCProfile(enc, icc.data(), icc.size()) !=
                            JXL_ENC_SUCCESS) {
      JXLTK_ERROR("Failed to set ICC profile.");
      return EXIT_FAILURE;
    }
  }
  auto outbuf = std::make_unique_for_overwrite<uint8_t[]>(kDefaultIOBufferSize);

  // Always decode to float, as we're likely to encounter/create samples outside [0,1].
  jxlazy::FramePixels<float> leftFrame, rightFrame;
  JxlPixelFormat format = { .num_channels = leftInfo.num_color_channels,
                            .data_type = JXL_TYPE_FLOAT,
                            .endianness = JXL_NATIVE_ENDIAN,
                            .align = 0 };

  for (size_t frameIdx = 0; frameIdx < frameCount; ++frameIdx) {

    JXLTK_TRACE("%s frame %zu.", opName, frameIdx);
    // Get frames and update left in place
    jxlazy::FrameInfo frameInfo = leftImage.getFrameInfo(frameIdx);
    leftImage.getFramePixels(&leftFrame, frameIdx, format.num_channels,
                             std::span<const int>({-1}));
    rightImage.getFramePixels(&rightFrame, frameIdx, format.num_channels,
                              std::span<const int>({-1}));
    auto leftEcIter = leftFrame.ecs.begin();
    auto rightEcIter = rightFrame.ecs.cbegin();
    if (adding) {
      for (size_t s = 0; s < leftFrame.color.size(); ++s) {
        leftFrame.color[s] += rightFrame.color[s];
      }
      for (size_t ec = 0; ec < leftInfo.num_extra_channels; ++ec) {
        std::vector<float>& leftEc = (leftEcIter++)->second;
        const std::vector<float>& rightEc = (rightEcIter++)->second;
        for (size_t s = 0; s < leftEc.size(); ++s) {
          leftEc[s] += rightEc[s];
        }
      }
    } else {
      for (size_t s = 0; s < leftFrame.color.size(); ++s) {
        leftFrame.color[s] -= rightFrame.color[s];
      }
      for (size_t ec = 0; ec < leftInfo.num_extra_channels; ++ec) {
        std::vector<float>& leftEc = (leftEcIter++)->second;
        const std::vector<float>& rightEc = (rightEcIter++)->second;
        for (size_t s = 0; s < leftEc.size(); ++s) {
          leftEc[s] -= rightEc[s];
        }
      }
    }

    // Send result to encoder
    FrameConfig thisFrameConfig = FrameConfig::fromJxlFrameHeader(frameInfo.header);
    thisFrameConfig.update(frameConfig);
    JxlEncoderFrameSettings* settings =
      frameConfigToJxlEncoderFrameSettings(enc, encInfo, thisFrameConfig, 0, 0,
                                           frameInfo.header.layer_info.xsize,
                                           frameInfo.header.layer_info.ysize);
    if (!frameInfo.name.empty() &&
        JxlEncoderSetFrameName(settings, frameInfo.name.c_str()) != JXL_ENC_SUCCESS) {
      JXLTK_ERROR("Failed to set frame %zu name: %s.", frameIdx,
                  shellQuote(frameInfo.name).c_str());
      return EXIT_FAILURE;
    }
    for (size_t ec = 0; ec < frameInfo.ecBlendInfo.size(); ++ec) {
      if (JxlEncoderSetExtraChannelBlendInfo(settings, ec, &frameInfo.ecBlendInfo[ec])
          != JXL_ENC_SUCCESS) {
        JXLTK_ERROR("Failed to set extra channel %zu blend info for frame %zu.",
                    ec, frameIdx);
        return EXIT_FAILURE;
      }
    }
    if (JxlEncoderAddImageFrame(settings, &format, leftFrame.color.data(),
                                leftFrame.color.size() *
                                    bytesPerSample(format.data_type))
        != JXL_ENC_SUCCESS) {
      JXLTK_ERROR("Failed to add image frame %zu.", frameIdx);
      return EXIT_FAILURE;
    }
    for (const auto& ecNode : leftFrame.ecs) {
      size_t ec = ecNode.first;
      const std::vector<float>& ecData = ecNode.second;
      if (JxlEncoderSetExtraChannelBuffer(settings, &format, ecData.data(),
                                          ecData.size() *
                                              bytesPerSample(format.data_type), ec)
          != JXL_ENC_SUCCESS) {
        JXLTK_ERROR("Failed to set buffer for extra channel %zu in frame %zu.", ec,
                  frameIdx);
        return EXIT_FAILURE;
      }
    }
    if (frameIdx == frameCount - 1) {
      JxlEncoderCloseInput(enc);
    }
    if (encodeUntilSuccess(enc, outbuf.get(), kDefaultIOBufferSize, fout, written)
        != JXL_ENC_SUCCESS) {
      JXLTK_ERROR("Failed to write file.");
      return EXIT_FAILURE;
    }
  }

  return EXIT_SUCCESS;
}

}  // namespace jxltk
