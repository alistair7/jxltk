/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "common.h"
#include "except.h"
#include "mergeconfig.h"

namespace jxltk {

bool isReservedBoxType(const char* t) {
  return (toupper(static_cast<unsigned char>(t[0])) == 'J' &&
          toupper(static_cast<unsigned char>(t[1])) == 'X' &&
          toupper(static_cast<unsigned char>(t[2])) == 'L') ||
         memcmp(t, "ftyp", 4) == 0 ||
         memcmp(t, "jbrd", 4) == 0;
}

/**
 * Initialise frame settings for the next frame based on a FrameConfig.
 *
 * @param[in,out] enc Encoder the frame settings will be linked to
 * @param[in] basicInfo Info about the resulting image.
 * @param[in] frameConfig Jxltk frame settings.
 * @param[in] tpsNumerator,tpsDenominator Ticks per second fraction (used to
 * convert milliseconds to ticks when applicable).
 * @param[in] frameXsize,frameYsize Dimensions of this frame.
 * @param[in] brotliEffort Effort to use when compressing brob boxes.
 * (Not sure why this is a frame setting, or how the order of operations
 * affects which boxes are affected by it, but setting it the same for
 * every frame shouldn't hurt.)
 * @return A pointer to the new frame settings object.
 */
JxlEncoderFrameSettings* frameConfigToJxlEncoderFrameSettings(
    JxlEncoder* enc,
    const JxlBasicInfo& basicInfo,
    const FrameConfig& frameConfig,
    uint32_t tpsNumerator, uint32_t tpsDenominator,
    uint32_t frameXsize, uint32_t frameYsize,
    const std::optional<int32_t>& brotliEffort) {
  JxlEncoderFrameSettings* settings = JxlEncoderFrameSettingsCreate(enc, nullptr);
  // Set distance / lossiness - default to lossless
  float distance = frameConfig.distance.value_or(0);
  if (distance < kLosslessDistanceThreshold) {
    if (JxlEncoderSetFrameLossless(settings, JXL_TRUE) != JXL_ENC_SUCCESS) {
      throw JxltkError("%s: Failed in JxlEncoderSetFrameLossless",
                       __func__);
    }
  } else if (JxlEncoderSetFrameDistance(settings, distance) != JXL_ENC_SUCCESS) {
    throw JxltkError("%s: Failed in JxlEncoderSetFrameDistance(%f)",
                     __func__, *frameConfig.distance);
  }

  // Set various integer options
  const struct {
    const char* settingName;
    JxlEncoderFrameSettingId settingId;
    const std::optional<int32_t>& value;
  } int32Settings[] = {
    { "EFFORT", JXL_ENC_FRAME_SETTING_EFFORT, frameConfig.effort },
    { "MODULAR_NB_PREV_CHANNELS", JXL_ENC_FRAME_SETTING_MODULAR_NB_PREV_CHANNELS,
       frameConfig.maPrevChannels },
    { "MODULAR_MA_TREE_LEARNING_PERCENT",
      JXL_ENC_FRAME_SETTING_MODULAR_MA_TREE_LEARNING_PERCENT,
      frameConfig.maTreeLearnPct },
    { "PATCHES", JXL_ENC_FRAME_SETTING_PATCHES, frameConfig.patches },
    { "BROTLI_EFFORT", JXL_ENC_FRAME_SETTING_BROTLI_EFFORT, brotliEffort },
  };
  for (const auto& s : int32Settings) {
    if (s.value && *s.value != -1 &&
        JxlEncoderFrameSettingsSetOption(settings, s.settingId,
                                         *s.value) != JXL_ENC_SUCCESS) {
      throw JxltkError("%s: Failed to set JXL_ENC_FRAME_SETTING_%s = %"
                       PRId32, __func__, s.settingName, *s.value);
    }
  }

  JxlFrameHeader frameHeader;
  JxlEncoderInitFrameHeader(&frameHeader);
  bool setHeader = false;
  if (frameConfig.blendMode &&
      *frameConfig.blendMode != frameHeader.layer_info.blend_info.blendmode) {
    setHeader = true;
    frameHeader.layer_info.blend_info.blendmode = *frameConfig.blendMode;
  }
  if (frameConfig.durationTicks && *frameConfig.durationTicks != 0) {
    setHeader = true;
    frameHeader.duration = *frameConfig.durationTicks;
  }
  if (frameConfig.durationMs && *frameConfig.durationMs != 0) {
    setHeader = true;
    frameHeader.duration = std::roundf(
      (static_cast<float>(*frameConfig.durationMs) / 1000.f) *
        (static_cast<float>(tpsNumerator) / static_cast<float>(tpsDenominator)));
  }
  if (frameConfig.offset) {
    frameHeader.layer_info.crop_x0 = frameConfig.offset->first;
    frameHeader.layer_info.crop_y0 = frameConfig.offset->second;
    if (frameHeader.layer_info.crop_x0 != 0 ||
        frameHeader.layer_info.crop_y0 != 0) {
      setHeader = true;
      frameHeader.layer_info.have_crop = JXL_TRUE;
    }
  }
  if (frameHeader.layer_info.have_crop ||
      frameXsize != basicInfo.xsize || frameYsize != basicInfo.ysize) {
    setHeader = true;
    frameHeader.layer_info.have_crop = JXL_TRUE;
    frameHeader.layer_info.xsize = frameXsize;
    frameHeader.layer_info.ysize = frameYsize;
  }
  if (frameConfig.blendSource && *frameConfig.blendSource != 0) {
    setHeader = true;
    frameHeader.layer_info.blend_info.source = *frameConfig.blendSource;
  }
  if (frameConfig.saveAsReference && *frameConfig.saveAsReference != 0) {
    setHeader = true;
    frameHeader.layer_info.save_as_reference = *frameConfig.saveAsReference;
  }
  if (setHeader &&
      JxlEncoderSetFrameHeader(settings, &frameHeader) != JXL_ENC_SUCCESS) {
    throw JxltkError("%s: Failed in JxlEncoderSetFrameHeader", __func__);
  }
  if (frameConfig.name && !frameConfig.name->empty() &&
      JxlEncoderSetFrameName(settings, frameConfig.name->c_str())
          != JXL_ENC_SUCCESS) {
    throw JxltkError("%s: Failed in JxlEncoderSetFrameName(%s)", __func__,
                     frameConfig.name->c_str());
  }
  return settings;
}


/**
 * Process encoder input and write output to a file until JXL_ENC_SUCCESS.
 *
 * Returns when the encoder returns anything that isn't JXL_ENC_NEED_MORE_OUTPUT.
 *
 * @param[in,out] buffer Pre-allocated scratch buffer.
 * @param[in] bufferSize Size of @p buffer.
 * @param[in,out] fout Stream to write JXL bytes to.
 * @return JXL_ENC_SUCCESS if everything passed to the encoder so far was
 * successfully encoded and written out, else return the unexpected encoder
 * status (i.e. JXL_ENC_ERROR).
 */
JxlEncoderStatus encodeUntilSuccess(JxlEncoder* enc, uint8_t* buffer, size_t bufferSize,
                                    std::ostream& fout) {
  uint8_t* nextOut = buffer;
  size_t availOut = bufferSize;
  JxlEncoderStatus st;
  while ((st = JxlEncoderProcessOutput(enc, &nextOut, &availOut))
           == JXL_ENC_NEED_MORE_OUTPUT) {
    size_t buffered = bufferSize - availOut;
    fout.write(reinterpret_cast<const char*>(buffer),
               static_cast<std::streamsize>(buffered));
    nextOut = buffer;
    availOut = bufferSize;
  }
  if (st != JXL_ENC_SUCCESS) {
    return st;
  }
  size_t buffered = bufferSize - availOut;
  fout.write(reinterpret_cast<const char*>(buffer),
             static_cast<std::streamsize>(buffered));
  return JXL_ENC_SUCCESS;
}


/**
 * Return (index,info) pairs for all non-JXL-reserved ISO BMFF boxes.
 */
std::vector<std::pair<size_t, jxlazy::BoxInfo> > getNonReservedBoxes(
    jxlazy::Decoder& dec) {
  size_t boxCount = dec.boxCount();
  std::vector<std::pair<size_t, jxlazy::BoxInfo> > boxes;
  if (boxCount > 3) {
    boxes.reserve(boxCount - 3);
  }
  for (size_t boxIdx = 0; boxIdx < boxCount; ++boxIdx) {
    jxlazy::BoxInfo boxInfo = dec.getBoxInfo(boxIdx);
    if (!isReservedBoxType(boxInfo.type)) {
      boxes.emplace_back(boxIdx, boxInfo);
    }
  }
  return boxes;
}

size_t countNonReservedBoxes(jxlazy::Decoder& dec) {
  size_t boxCount = dec.boxCount();
  size_t nrBoxCount = 0;
  for (size_t boxi = 0; boxi < boxCount; ++boxi) {
    jxlazy::BoxInfo boxInfo = dec.getBoxInfo(boxi);
    if (!isReservedBoxType(boxInfo.type)) {
      ++nrBoxCount;
    }
  }
  return nrBoxCount;
}


}  // namespace jxltk
