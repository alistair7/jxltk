/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <jxl/encode_cxx.h>

#include "common.h"
#include "enums.h"
#include "except.h"
#include "log.h"
#include "mergeconfig.h"
#include "pixmap.h"
#include "util.h"

using std::optional;
using std::vector;

namespace jxltk {

constexpr FrameConfig kJxltkDefaultFrameConfig = { .distance = 0 };

void split(std::string_view input, std::string_view poutputDir,
           bool coalesce = false, size_t numThreads = 0,
           const FrameConfig& frameConfig = {},
           const std::optional<JxlDataType>& forceDataType = {},
           bool wantPixels = true, bool wantBoxes = true,
           std::string_view configFile = "merge.json",
           bool useTicks = true, bool full = false) {
  JXLTK_TRACE("Entered %s", __func__);
  uint32_t decoderFlags = coalesce ? 0 :
                              static_cast<uint32_t>(jxlazy::DecoderFlag::NoCoalesce);
  uint32_t decoderHints = 0;
  if (!wantPixels) {
    decoderHints |= jxlazy::DecoderHint::NoPixels;
  }
  // Always look for jxll if we're generating a config file
  if (wantBoxes || !configFile.empty()) {
    decoderHints |= jxlazy::DecoderHint::WantBoxes;
  }

  jxlazy::Decoder dec;
  dec.openFile(std::string(input).c_str(), decoderFlags, decoderHints);

  JxlPixelFormat decFormat;
  dec.suggestPixelFormat(&decFormat);
  if (forceDataType) {
    decFormat.data_type = *forceDataType;
  }

  const JxlBasicInfo decInfo = dec.getBasicInfo();

  JxlColorEncoding colorEncoding;
  vector<uint8_t> icc;
  bool isGray = decInfo.num_color_channels == 1;
  if (!dec.getEncodedColorProfile(JXL_COLOR_PROFILE_TARGET_DATA, &colorEncoding)) {
    icc = dec.getIccProfile(JXL_COLOR_PROFILE_TARGET_DATA);
    if (icc.empty()) {
      JXLTK_LOG(LogLevel::Warning, LogFlags::NoNewline,
                "Failed to get color profile of input.");
      if (decFormat.data_type == JXL_TYPE_UINT8 || decFormat.data_type == JXL_TYPE_UINT16) {
        JXLTK_LOG(LogLevel::Warning, LogFlags::Continuation, " Defaulting to SRGB.");
        JxlColorEncodingSetToSRGB(&colorEncoding, isGray);
      } else {
        JXLTK_LOG(LogLevel::Warning, LogFlags::Continuation, " Defaulting to linear SRGB.");
        JxlColorEncodingSetToLinearSRGB(&colorEncoding, isGray);
      }
    }
  }

  // Build a MergeConfig that will be serialized as json later
  MergeConfig mergeCfg;
  float tps = 1.0f;
  if (!configFile.empty()) {
    mergeCfg.xsize = decInfo.xsize;
    mergeCfg.ysize = decInfo.ysize;
    if (decInfo.intrinsic_xsize != 0 &&
        (decInfo.intrinsic_xsize != decInfo.xsize ||
          decInfo.intrinsic_ysize != decInfo.ysize)) {
      mergeCfg.intrinsicXsize =
          decInfo.intrinsic_xsize ? decInfo.intrinsic_xsize : decInfo.xsize;
      mergeCfg.intrinsicYsize =
          decInfo.intrinsic_ysize ? decInfo.intrinsic_ysize : decInfo.ysize;
    }

    if (decInfo.have_animation) {
      const JxlAnimationHeader& ah = decInfo.animation;
      if (ah.num_loops > 0) {
        mergeCfg.loops = ah.num_loops;
      }
      if (useTicks) {
        mergeCfg.tps.emplace(ah.tps_numerator, ah.tps_denominator);
      }
      tps = static_cast<float>(ah.tps_numerator) /
            static_cast<float>(ah.tps_denominator);
    }

    if (full && !configFile.empty()) {
      JxlColorEncoding colEnc;
      if (dec.getEncodedColorProfile(JXL_COLOR_PROFILE_TARGET_DATA, &colEnc)) {
        ColorConfig& colConfig = mergeCfg.color.emplace();
        colConfig.type = ColorSpecType::Enum;
        colConfig.cicp = colEnc;
      }
    }
  }

  std::filesystem::path outputDir;
  if (wantPixels || wantBoxes || (!configFile.empty() && configFile != "-")) {
    outputDir = poutputDir;
    std::filesystem::create_directories(outputDir);
  }

  // Init encoder if needed
  JxlEncoderPtr encp;
  JxlMemoryManager* memoryManager = nullptr;
  JxlThreadParallelRunnerPtr runner;
  if (wantPixels) {
    encp = JxlEncoderMake(memoryManager);
    numThreads = numThreads > 0 ? numThreads :
                 JxlThreadParallelRunnerDefaultNumWorkerThreads();
    runner = JxlThreadParallelRunnerMake(memoryManager, numThreads);
  }

  // Check for non-main-alpha extra channels.
  // Create the right number of buffers, but don't set sizes - it varies for each frame.
  const vector<jxlazy::ExtraChannelInfo> decEcInfo = dec.getExtraChannelInfo();
  if (decEcInfo.size() != decInfo.num_extra_channels) {
    throw JxltkError("%s: Have %" PRIu32 " extra channels, but only %zu extra channel infos",
                     __func__, decInfo.num_extra_channels, decEcInfo.size());
  }
  std::optional<size_t> alphaEcIndex;
  const size_t numNonAlphaExtraChannels =
    decInfo.num_extra_channels - (decInfo.alpha_bits > 0 ? 1 : 0);
  vector<jxlazy::ExtraChannelRequest> ecRequests;
  vector<vector<uint8_t> > ecBuffers;
  if (wantPixels) {
    ecRequests.reserve(numNonAlphaExtraChannels);
    ecBuffers.resize(numNonAlphaExtraChannels);
    for (size_t ec = 0; ec < decEcInfo.size(); ++ec) {
      const jxlazy::ExtraChannelInfo& thisEcInfo = decEcInfo[ec];
      if (!alphaEcIndex && thisEcInfo.info.type == JXL_CHANNEL_ALPHA) {
        alphaEcIndex = ec;
        JXLTK_TRACE("Input has alpha channel at index %zu.", ec);
        continue;
      }
      jxlazy::ExtraChannelRequest& thisEcReq = ecRequests.emplace_back();
      thisEcReq.channelIndex = ec;
      jxlazy::Decoder::suggestPixelFormat(thisEcInfo.info.bits_per_sample,
                                          thisEcInfo.info.exponent_bits_per_sample,
                                          1, &thisEcReq.format);
    }
  }

  size_t frameCount = dec.frameCount();
  int filenameDigits = static_cast<int>(
                           floorf(log10f(static_cast<float>(frameCount) - 1))) + 1;
  size_t frameIndex = 0;
  vector<uint8_t> frameBuffer;
  vector<uint8_t> jxlBuffer;
  for (jxlazy::FrameInfo frameInfo : dec) {
    const JxlLayerInfo& layerInfo = frameInfo.header.layer_info;

    // Decide the filename for this frame
    std::string frameBaseName;
    {
      std::ostringstream nameBuild;
      nameBuild << std::setfill('0') << std::setw(filenameDigits) << frameIndex;
      if (layerInfo.have_crop &&
          (layerInfo.crop_x0 != 0 ||
           layerInfo.crop_y0 != 0)) {
        nameBuild << "_" << std::showpos << layerInfo.crop_x0 << std::showpos
                  << layerInfo.crop_y0;
      }
      if (!frameInfo.name.empty())
        nameBuild << "_" << simplifyString(frameInfo.name, 50);
      nameBuild << ".jxl";
      frameBaseName = nameBuild.str();
    }

    // Append an element to the JSON frames[] array
    if (!configFile.empty()) {
      FrameConfig jsonFrameConfig;
      jsonFrameConfig.file = frameBaseName;
      if (!frameInfo.name.empty()) {
        jsonFrameConfig.name = std::move(frameInfo.name);
      }
      jsonFrameConfig.blendMode = layerInfo.blend_info.blendmode;
      if (frameIndex > 0 || layerInfo.blend_info.source != 0) {
        jsonFrameConfig.blendSource = layerInfo.blend_info.source;
      }
      if (!frameInfo.header.is_last) {
        jsonFrameConfig.saveAsReference = layerInfo.save_as_reference;
      }
      if (decInfo.have_animation) {
        if (useTicks) {
          jsonFrameConfig.durationTicks = frameInfo.header.duration;
        } else {
          jsonFrameConfig.durationMs = std::roundf(
              1000.f * (static_cast<float>(frameInfo.header.duration) / tps));
        }
      }
      if (layerInfo.have_crop && (layerInfo.crop_x0 != 0 || layerInfo.crop_y0 != 0)) {
        jsonFrameConfig.offset.emplace(layerInfo.crop_x0, layerInfo.crop_y0);
      }
      mergeCfg.frames.push_back(std::move(jsonFrameConfig));
    }

    // Decode this frame's pixels and encode to a new file
    if (wantPixels) {
      JxlEncoder* enc = encp.get();
      JxlEncoderReset(enc);
      if (runner &&
          JxlEncoderSetParallelRunner(enc, JxlThreadParallelRunner, runner.get())
          != JXL_ENC_SUCCESS) {
        throw JxltkError("%s: Failed to set parallel runner for encoder", __func__);
      }

      // Allocate main frame buffer
      size_t bufferSize = dec.getFrameBufferSize(frameIndex, decFormat);
      frameBuffer.clear();
      frameBuffer.resize(bufferSize);

      // Allocate non-main-alpha extra channel buffers.
      // Update pointers in ecRequests as the buffers might move.
      // Reset extra channel indexes, as we might have shifted them on the previous loop
      for (size_t req = 0; req < ecRequests.size(); ++req) {
        jxlazy::ExtraChannelRequest& thisEcReq = ecRequests[req];
        vector<uint8_t>& thisEcBuffer = ecBuffers[req];
        thisEcReq.capacity = dec.getFrameBufferSize(frameIndex, thisEcReq.format);
        thisEcBuffer.clear();
        thisEcBuffer.resize(thisEcReq.capacity);
        thisEcReq.target = thisEcBuffer.data();
        thisEcReq.channelIndex = req + (alphaEcIndex && req >= *alphaEcIndex ? 1 : 0);
      }

      dec.getFramePixels(frameIndex, decFormat, frameBuffer.data(), bufferSize,
                         ecRequests);

      JxlPixelFormat encFormat = decFormat;
      JxlBasicInfo encInfo = decInfo;
      encInfo.xsize = layerInfo.xsize;
      encInfo.ysize = layerInfo.ysize;
      encInfo.have_animation = JXL_FALSE;
      encInfo.have_preview = JXL_FALSE;
      encInfo.intrinsic_xsize = encInfo.intrinsic_ysize = 0;
      encInfo.uses_original_profile =
        frameConfig.distance.value_or(*kJxltkDefaultFrameConfig.distance)
          < kLosslessDistanceThreshold ? JXL_TRUE : JXL_FALSE;

      // Check for and remove redundant alpha channel (TODO: maybe don't do this if it's named)
      if (alphaEcIndex &&
          Pixmap::isFullyOpaque(frameBuffer.data(), layerInfo.xsize,
                                layerInfo.ysize, decFormat)) {
        if (removeInterleavedChannel(frameBuffer.data(), layerInfo.xsize,
                                     layerInfo.ysize, decFormat,
                                     decFormat.num_channels - 1)) {
          throw JxltkError("%s: Failed to remove interleaved alpha for frame %zu",
                           __func__, frameIndex);
        }
        JXLTK_DEBUG("Removed redundant alpha channel from frame %zu", frameIndex);
        encInfo.alpha_bits = 0;
        encInfo.alpha_exponent_bits = 0;
        encInfo.num_extra_channels--;
        encFormat.num_channels--;
        // Convert the channel indexes in ecRequests from input to output indexes.
        // i.e., shift them down to account for the missing alpha index.
        for (auto ecr = ecRequests.begin() + *alphaEcIndex;
             ecr != ecRequests.end();
             ++ecr) {
          --ecr->channelIndex;
        }
      }

      if (jxltkLogThreshold >= LogLevel::Trace) {
        std::ostringstream biStr;
        biStr << encInfo;
        JXLTK_TRACE("Writing basic info: %s", biStr.str().c_str());
      }
      if (JxlEncoderSetBasicInfo(enc, &encInfo) != JXL_ENC_SUCCESS) {
        throw JxltkError("%s: Failed to set basic info for frame %zu",
                         __func__, frameIndex);
      }

      JXLTK_TRACE("Setting extra channel info.");
      for (const auto& thisEcReq : ecRequests) {
        const jxlazy::ExtraChannelInfo& thisEcInfo = decEcInfo[thisEcReq.channelIndex];
        JXLTK_TRACE("Frame %zu: Setting extra channel %zu info (%s)(%s)", frameIndex,
                    thisEcReq.channelIndex, channelTypeName(thisEcInfo.info.type), thisEcInfo.name.c_str());
        if (JxlEncoderSetExtraChannelInfo(enc, thisEcReq.channelIndex, &thisEcInfo.info) != JXL_ENC_SUCCESS) {
          throw JxltkError("%s: Failed to set extra channel info for frame %zu, "
                           "channel %zu", __func__, frameIndex, thisEcReq.channelIndex);
        }
        if (!thisEcInfo.name.empty() &&
            JxlEncoderSetExtraChannelName(enc, thisEcReq.channelIndex, thisEcInfo.name.c_str(),
                                          thisEcInfo.name.size()) != JXL_ENC_SUCCESS) {
          throw JxltkError("%s: Failed to set extra channel info for frame %zu, "
                           "channel %zu", __func__, frameIndex, thisEcReq.channelIndex);
        }
        // TODO: should also get ExtraChannelBlendInfo and write it to the merge config
      }

      if (icc.empty()) {
        if (JxlEncoderSetColorEncoding(enc, &colorEncoding) != JXL_ENC_SUCCESS) {
          throw JxltkError("%s: Failed to set color encoding for frame %zu",
                           __func__, frameIndex);
        }
      } else {
        if (JxlEncoderSetICCProfile(enc, icc.data(), icc.size()) != JXL_ENC_SUCCESS) {
          throw JxltkError("%s: Failed to set ICC for frame %zu",
                           __func__, frameIndex);
        }
      }

      JxlEncoderFrameSettings* settings =
          frameConfigToJxlEncoderFrameSettings(enc, encInfo, frameConfig,
                                               1, 1, frameInfo.header.layer_info.xsize,
                                               frameInfo.header.layer_info.ysize);
      if (JxlEncoderAddImageFrame(settings, &encFormat, frameBuffer.data(),
                                  bufferSize) != JXL_ENC_SUCCESS) {
        throw JxltkError("%s: Failed to add frame %zu", __func__, frameIndex);
      }
      for (const auto& thisEcReq : ecRequests) {
        JXLTK_TRACE("Frame %zu: Adding extra channel %zu", frameIndex, thisEcReq.channelIndex);
        if (JxlEncoderSetExtraChannelBuffer(settings, &thisEcReq.format, thisEcReq.target,
                                            thisEcReq.capacity, thisEcReq.channelIndex)
            != JXL_ENC_SUCCESS) {
          throw JxltkError("%s: Failed to add extra channel %zu for frame %zu", __func__,
                           thisEcReq.channelIndex, frameIndex);
        }
      }
      JxlEncoderCloseInput(enc);

      std::string filePath = (outputDir / frameBaseName).string();
      std::ofstream outFile(filePath, std::ios::binary);
      if (jxlBuffer.empty()) {
        jxlBuffer.resize(kBufferSize);
      }
      JxlEncoderStatus st = encodeUntilSuccess(enc, jxlBuffer.data(),
                                               jxlBuffer.size(), outFile);
      if (st != JXL_ENC_SUCCESS) {
        throw JxltkError("%s: Unexpected encoder status while writing frame %zu: %s",
                         __func__, frameIndex, encoderStatusName(st));
      }
      JXLTK_INFO("Wrote %s.", shellQuote(filePath, true).c_str());
    }

    ++frameIndex;
  }

  // Read jxll box if applicable
  if (decInfo.have_container && !configFile.empty()) {
    int codestreamLevel = dec.getCodestreamLevel();
    if (full || codestreamLevel != -1) {
      mergeCfg.codestreamLevel = codestreamLevel;
    }
  }

  // Output boxes
  if (decInfo.have_container && wantBoxes) {

    // Only care about non-JXL-reserved boxes
    std::vector<std::pair<size_t, jxlazy::BoxInfo> > boxes = getNonReservedBoxes(dec);
    size_t boxCount = boxes.size();

    filenameDigits =
        static_cast<int>(floorf(log10f(static_cast<float>(boxCount - 1)))) + 1;
    for (size_t i = 0; i < boxCount; ++i) {
      size_t boxIndex = boxes[i].first;
      const jxlazy::BoxInfo& boxInfo = boxes[i].second;
      std::string boxType = std::string(boxInfo.type, sizeof boxInfo.type);

      // Decide the filename for this box
      std::string boxBaseName;
      {
        std::ostringstream oss;
        oss << "box" << std::setfill('0') << std::setw(filenameDigits)
            << boxIndex << "_[" << simplifyString(boxType) << "].box";
        boxBaseName = oss.str();
      }

      // Append an element to the JSON boxes[] array
      if (!configFile.empty()) {
        BoxConfig jsonBoxConfig;
        jsonBoxConfig.type = std::move(boxType);
        jsonBoxConfig.file = boxBaseName;
        jsonBoxConfig.compress = boxInfo.compressed;
        mergeCfg.boxes.push_back(std::move(jsonBoxConfig));
      }

      // Output this box's content to a file
      std::string filePath = (outputDir / boxBaseName).string();
      std::ofstream outFile(filePath, std::ios::binary);
      vector<uint8_t> boxContent;
      dec.getBoxContent(boxIndex, &boxContent);
      outFile.write(reinterpret_cast<const char*>(boxContent.data()),
                    boxContent.size());
      JXLTK_INFO("Wrote %s.", shellQuote(filePath, true).c_str());
    }
  }

  if (!configFile.empty()) {
    if (configFile == "-") {
      mergeCfg.toJson(std::cout, full);
    } else {
      std::string filePath = (outputDir / configFile).string();
      std::ofstream jsonFile(filePath, std::ios::binary);
      if (!jsonFile) {
        throw JxltkError(std::string("Failed to open ") + shellQuote(filePath) + " for writing");
      }
      mergeCfg.toJson(jsonFile, full);
    }
  }
}

}  // namespace jxltk

