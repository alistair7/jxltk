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
#include <string>
#include <vector>

#include <jxl/encode_cxx.h>

#include "color.h"
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

namespace {

int dataTypeRank(JxlDataType t) {
  return t == JXL_TYPE_UINT8 ? 10 :
         t == JXL_TYPE_UINT16 ? 20 :
         t == JXL_TYPE_FLOAT16 ? 30 :
         t == JXL_TYPE_FLOAT ? 40 : 0;
}

ColorProfile getColorProfile(jxlazy::Decoder& dec) {
  ColorProfile color;
  JxlColorEncoding encColorTemp;
  if (dec.getEncodedColorProfile(JXL_COLOR_PROFILE_TARGET_DATA,
                                 &encColorTemp)) {
    color.enc = encColorTemp;
  }
  if ((color.icc.size = dec.getIccProfileSize(JXL_COLOR_PROFILE_TARGET_DATA)) > 0) {
    color.icc.data = JXLTK_MAKE_UNIQUE_FOR_OVERWRITE<uint8_t[]>(color.icc.size);
    vector<uint8_t> icc = dec.getIccProfile(JXL_COLOR_PROFILE_TARGET_DATA);
    memcpy(color.icc.data.get(), icc.data(), color.icc.size);
  }
  return color;
}

/**
 * Extract the color profile from the named file, which may be a raw ICC profile
 * or a JXL.
 */
ColorProfile getColorProfile(const char* filename) {
  std::ifstream inf(filename, std::ios::binary);
  if (!inf) {
    JXLTK_ERROR("Can't open %s for reading.", shellQuote(filename).c_str());
    return {};
  }
  vector<uint8_t> buff(40);
  inf.read(reinterpret_cast<char*>(buff.data()), buff.size());
  size_t got = inf.gcount();
  JxlSignature sig = JxlSignatureCheck(buff.data(), got);
  if (sig == JXL_SIG_NOT_ENOUGH_BYTES) {
    JXLTK_ERROR("Can't get a color profile from %s - file is too small.",
                shellQuote(filename).c_str());
    return {};
  }
  if (sig == JXL_SIG_CODESTREAM || sig == JXL_SIG_CONTAINER) {
    JXLTK_TRACE("Getting color profile from existing JXL.");
    jxlazy::Decoder dec;
    inf.seekg(0);
    dec.openStream(inf, 0, jxlazy::DecoderHint::NoPixels);
    return getColorProfile(dec);
  }
  if (sig == JXL_SIG_INVALID) {
    if (got < 40 ||
        buff[36] != 'a' || buff[37] != 'c' || buff[38] != 's' || buff[39] != 'p') {
      JXLTK_WARNING("Can't get a color profile from %s - "
                    "it doesn't look like either an ICC or a JXL file.",
                    shellQuote(filename).c_str());
      return {};
    }
    JXLTK_DEBUG("%s is an ICC profile.", shellQuote(filename).c_str());
    inf.seekg(0);
    loadFile(inf, &buff);
    ColorProfile color;
    color.icc.data = JXLTK_MAKE_UNIQUE_FOR_OVERWRITE<uint8_t[]>(buff.size());
    color.icc.size = buff.size();
    memcpy(color.icc.data.get(), buff.data(), buff.size());
    return color;
  }
  JXLTK_ERROR("Unexpected return value from JxlSignatureCheck: %d",
              static_cast<int>(sig));
  return {};
}

/**
 * Scan a set of JXL files and return a suitable format to use for processing
 * their pixels.  i.e. the smallest format that preserves the declared bit depth
 * and channel count of all inputs.
 * NOTE: out-of-range samples are not detected, so it's possible this will suggest
 * a pixel format that causes these values to be clamped.
 */
JxlPixelFormat suggestFormat(vector<std::unique_ptr<jxlazy::Decoder> >& decoders) {
  JxlPixelFormat format = {.num_channels = 1,
                           .data_type = JXL_TYPE_UINT8,
                           .endianness = JXL_NATIVE_ENDIAN,
                           .align = 0};
  bool needAlpha = false;
  for (auto& decPtr : decoders) {
    if (!decPtr) continue;
    JxlPixelFormat thisFormat;
    decPtr->suggestPixelFormat(&thisFormat);
    needAlpha = needAlpha ||
                (thisFormat.num_channels == 2 || thisFormat.num_channels == 4);
    format.num_channels = std::max(format.num_channels, thisFormat.num_channels);
    format.data_type =
        dataTypeRank(thisFormat.data_type) > dataTypeRank(format.data_type) ?
            thisFormat.data_type : format.data_type;
  }
  if (needAlpha && (format.num_channels == 1 || format.num_channels == 3)) {
    format.num_channels++;
  }
  return format;
}

/**
 * Get the ticks per second required for the resulting animation.
 *
 * @param[in] mergeCfg Merge configuration, which can specify ticks per second
 * explicitly OR implicitly via frame durations given in milliseconds.
 * @param[out] numerator,denominator Ticks per second required.
 *
 * @return true if the configuration is valid, else return false and set
 * `*numerator` and `*denominator` to 100 and 1, respectively.
 */
bool suggestTicksPerSecond(const MergeConfig& mergeCfg,
                           uint32_t* numerator, uint32_t* denominator) {
  *numerator = 100;
  *denominator = 1;

  if (mergeCfg.tps) {
    // TPS given explicitly in the merge config
    *numerator = mergeCfg.tps->first;
    *denominator = mergeCfg.tps->second;
    return true;
  }

  // Calculate min TPS based on frame durations
  vector<uint32_t> durationsMs;
  durationsMs.reserve(mergeCfg.frames.size());
  for (const auto& frmCfg : mergeCfg.frames) {
    uint32_t effectiveDurationMs =
        frmCfg.durationMs ? *frmCfg.durationMs :
        mergeCfg.frameDefaults.durationMs.value_or(0);
    if (effectiveDurationMs > 0) durationsMs.push_back(effectiveDurationMs);
  }

  uint32_t gcd = jxltk::greatestCommonDivisor(durationsMs);
  if (gcd > 0) {
    // gcd is the smallest possible tick duration in milliseconds, so the
    // minimum possible ticks per second is 1000 / gcd.  Simplify this
    // fraction as much as possible.
    uint32_t num = 1000;
    uint32_t den = gcd;
    if ((gcd = std::gcd(num, den)) > 1) {
      num /= gcd;
      den /= gcd;
    }
    *numerator = num;
    *denominator = den;
  }
  return true;
}

/**
 * Add a box to the encoder, and run the encoder as far as it can go.
 */
void writeBox(JxlEncoder* enc, const JxlBoxType boxType, const uint8_t* content,
              size_t size, bool compress, bool isLast, uint8_t* buffer,
              size_t bufferSize, std::ostream& fout) {
  if (JxlEncoderAddBox(enc, boxType, content, size, compress) != JXL_ENC_SUCCESS) {
    throw JxltkError("%s: Failed to add box to output", __func__);
  }
  if (isLast) {
    JxlEncoderCloseBoxes(enc);
  }
  JxlEncoderStatus st = encodeUntilSuccess(enc, buffer, bufferSize, fout);
  if (st != JXL_ENC_SUCCESS) {
    throw JxltkError("%s: Unexpected encoder status while writing box: %s",
                     __func__, encoderStatusName(st));
  }
}

}  // namespace


/**
 * Combine one or more JXLs into a single JXL.
 *
 * Ancilliary boxes on the first input file are copied to the output, but boxes
 * on all other inputs are ignored. Any boxes explicitly specified in @p
 * mergeConfig will also be included in the output.
 *
 * @param[in] mergeCfg Merge configuration, normally parsed from JSON file.
 * @param[in] output Name of the merged output file to create/overwrite.
 * @param[in] numThreads Number of threads to use, or 0 to decide automatically.
 * @param[in] forceDataType Force a specific data type to be used during
 * processing.
 *
 */
void merge(const MergeConfig& mergeCfg, const std::string& output,
           size_t numThreads = 0,
           const std::optional<JxlDataType>& forceDataType = {}) {
  JXLTK_TRACE("Entered %s", __func__);
  const vector<FrameConfig>& inputs = mergeCfg.frames;

  if (inputs.empty()) throw JxltkError("Cannot merge zero images");
  if (static_cast<bool>(mergeCfg.xsize) ^ static_cast<bool>(mergeCfg.ysize)) {
    throw JxltkError(
        "If either of xsize and ysize is set, both must be set");
  }
  if (static_cast<bool>(mergeCfg.intrinsicXsize) ^
      static_cast<bool>(mergeCfg.intrinsicYsize)) {
    throw JxltkError(
        "If either of intrinsicXsize and intrinsicYsize are specified, both "
        "must be specified");
  }
  if (mergeCfg.tps && mergeCfg.tps->second == 0) {
    throw JxltkError("Ticks-per-second denominator can't be 0");
  }

  JXLTK_INFO("Merging %zu images", inputs.size());

  // Start with a simple JxlBasicInfo for the output
  JxlBasicInfo encInfo;
  JxlEncoderInitBasicInfo(&encInfo);
  encInfo.bits_per_sample = 1;
  encInfo.exponent_bits_per_sample = 0;
  encInfo.alpha_bits = 0;
  encInfo.alpha_exponent_bits = 0;
  encInfo.num_color_channels = 1;
  encInfo.num_extra_channels = 0;
  encInfo.intrinsic_xsize = mergeCfg.intrinsicXsize.value_or(0);
  encInfo.intrinsic_ysize = mergeCfg.intrinsicYsize.value_or(0);
  encInfo.uses_original_profile = JXL_FALSE; // flipped if we have lossless frames
  // Specifying the canvas size is optional - if unspecified it's set
  // to fit all frames that are added (in positive axes only)
  encInfo.xsize = mergeCfg.xsize.value_or(0);
  encInfo.ysize = mergeCfg.ysize.value_or(0);
  bool autoSizeCanvas = encInfo.xsize == 0;

  // If not given explicitly, we'll use the color profile of the
  // first (non-empty) input.
  std::optional<ColorProfile> color;
  if (mergeCfg.color) {
    if (mergeCfg.color->type == ColorSpecType::Enum) {
      ColorProfile& cp = color.emplace();
      cp.enc.emplace(mergeCfg.color->cicp);
    } else if (mergeCfg.color->type == ColorSpecType::File) {
      JXLTK_DEBUG("Copying color profile from %s.",
                  shellQuote(mergeCfg.color->name, true).c_str());
      color = getColorProfile(mergeCfg.color->name.c_str());
      if (!color->enc && color->icc.size == 0) {
        throw JxltkError("Failed to determine color profile");
      }
    } else if (mergeCfg.color->type != ColorSpecType::None) {
      JXLTK_WARNING("Setting color profile of this type isn't implemented yet.");
    }
  }
  bool checkColorProfiles = !color;
  bool savedRef3 = false;
  bool patchesRequested = false;
  size_t totalBoxes = mergeCfg.boxes.size();

  vector<std::unique_ptr<jxlazy::Decoder> > frameDecoders;
  frameDecoders.reserve(inputs.size());
  vector<FrameConfig> frameConfigs;
  frameConfigs.reserve(inputs.size());

  // First pass over inputs:
  // - Coalesce individual frame settings with frameDefaults.
  // - Create a Decoder for each input.
  // - Decide on basic info for the output.
  for (size_t frameIdx = 0; frameIdx < inputs.size(); ++frameIdx) {
    FrameConfig& frameCfg = frameConfigs.emplace_back(mergeCfg.frameDefaults);
    frameCfg.update(inputs[frameIdx]);
    bool isZeroDuration = frameCfg.durationMs.value_or(0) == 0 &&
                          frameCfg.durationTicks.value_or(0) == 0;
    if (isZeroDuration) {
      // Zero duration frames MUST be saved (except for the final frame),
      // so warn if this is happening implicitly.
      if (frameConfigs.size() != inputs.size() &&
          !frameCfg.saveAsReference) {
        JXLTK_NOTICE("Frame %zu is implicitly saved as reference 0.",
                     frameConfigs.size()-1);
      }
    } else {
      encInfo.have_animation = JXL_TRUE;
    }
    if (!savedRef3 && frameCfg.saveAsReference && *frameCfg.saveAsReference == 3) {
      savedRef3 = true;
    }
    if (!patchesRequested && frameCfg.patches && *frameCfg.patches == 1) {
      patchesRequested = true;
    }
    encInfo.uses_original_profile =
        encInfo.uses_original_profile ||
        frameCfg.distance.value_or(0) < kLosslessDistanceThreshold;
    if (!frameCfg.file || frameCfg.file->empty()) {
      frameDecoders.emplace_back();
    } else {
      auto frameDecoder = std::make_unique<jxlazy::Decoder>();
      bool copyBoxes = frameCfg.copyBoxes.value_or(false);
      uint32_t hints = copyBoxes ?
                           static_cast<uint32_t>(jxlazy::DecoderHint::WantBoxes) : 0;
      // TODO: allow control over buffering argument
      // TODO: allow control over unpremultiply alpha (but make sure all inputs use the same setting)
      frameDecoder->openFile(frameCfg.file->c_str(),
                             jxlazy::DecoderFlag::UnpremultiplyAlpha,
                             hints);
      if (copyBoxes) {
        size_t boxCount = countNonReservedBoxes(*frameDecoder);
        if (boxCount > 0) {
          JXLTK_DEBUG("Will copy %zu boxes from input %zu.", boxCount, frameIdx);
        }
        totalBoxes += boxCount;
      }
      std::vector<jxlazy::ExtraChannelInfo> eci =
          frameDecoder->getExtraChannelInfo();
      if (eci.size() > 1 ||
          (eci.size() == 1 && eci.at(0).info.type != JXL_CHANNEL_ALPHA)) {
        JXLTK_WARNING("File %s has (non-main-alpha) extra channels - "
                      "these will be ignored.", shellQuote(*frameCfg.file, true).c_str());
      }
      JxlBasicInfo bi = frameDecoder->getBasicInfo();
      encInfo.bits_per_sample = std::max(encInfo.bits_per_sample, bi.bits_per_sample);
      encInfo.exponent_bits_per_sample = std::max(encInfo.exponent_bits_per_sample, bi.exponent_bits_per_sample);
      encInfo.alpha_bits = std::max(encInfo.alpha_bits, bi.alpha_bits);
      if (encInfo.alpha_bits > 0) {
        encInfo.num_extra_channels = 1;
      }
      encInfo.num_color_channels = std::max(encInfo.num_color_channels, bi.num_color_channels);
      encInfo.alpha_exponent_bits = std::max(encInfo.alpha_exponent_bits, bi.alpha_exponent_bits);
      if (checkColorProfiles) {
        ColorProfile thisColor = getColorProfile(*frameDecoder.get());
        if (!color) {
          color = std::move(thisColor);
        } else if (!colorProfilesMatch(*color, thisColor)) {
          JXLTK_WARNING("Input files have differing color profiles - pixels will be "
                        "reinterpreted based on the profile of the first input.");
          checkColorProfiles = false;
        }
      }
      frameDecoders.emplace_back(std::move(frameDecoder));
    }
  }

  if (savedRef3) {
    const char* msg = "Reference frame 3 in use, so disabling patches for all frames.";
    if (patchesRequested) {
      JXLTK_WARNING("%s", msg);
    } else {
      JXLTK_DEBUG("%s", msg);
    }
    for (auto& frameCfg : frameConfigs) {
      frameCfg.patches = 0;
    }
  }

  // If for some reason every input is a null frame, default the color profile
  if (!color) {
    ColorProfile& cp = color.emplace();
    JxlColorEncoding& ce = cp.enc.emplace();
    JxlColorEncodingSetToSRGB(&ce, encInfo.num_color_channels == 1);
    JXLTK_NOTICE("Using default sRGB color profile.");
  }

  // Decide the best common pixel format to use
  JxlPixelFormat pixelFormat = suggestFormat(frameDecoders);
  if (forceDataType) {
    pixelFormat.data_type = *forceDataType;
  } else if (mergeCfg.dataType) {
    pixelFormat.data_type = *mergeCfg.dataType;
  }
  JXLTK_DEBUG("Working with pixel format %s", toString(pixelFormat).c_str());

  if (encInfo.have_animation) {
    encInfo.animation.num_loops = mergeCfg.loops.value_or(0);
    if (!suggestTicksPerSecond(mergeCfg,
                               &encInfo.animation.tps_numerator,
                               &encInfo.animation.tps_denominator)) {
      throw JxltkError("Invalid frame timings / ticks-per-second specified");
    }
    // TODO: support timecodes
  }

  // Define (lazy-loaded) frame buffers we'll pass to the encoder
  vector<Pixmap> frameBuffers;
  frameBuffers.reserve(inputs.size());
  for (size_t i = 0; i < inputs.size(); i++) {
    std::unique_ptr<jxlazy::Decoder>& frameDecPtr = frameDecoders.at(i);
    if (frameDecPtr) {
      frameBuffers.emplace_back(std::move(frameDecPtr), 0, pixelFormat);
    } else {
      // Missing decoders are inputs that had no filename.
      // Construct 1x1 black transparent frames for these.
      frameBuffers.push_back(Pixmap::blackPixel(pixelFormat));
    }
    if (autoSizeCanvas) {
      const auto& optOffset = frameConfigs[i].offset;
      int32_t cropX0 = optOffset ? optOffset->first : 0;
      int32_t cropY0 = optOffset ? optOffset->second : 0;
      encInfo.xsize = std::max(encInfo.xsize, cropX0 + frameBuffers.back().getXsize());
      encInfo.ysize = std::max(encInfo.ysize, cropY0 + frameBuffers.back().getYsize());
    }
  }

  if (autoSizeCanvas) {
    JXLTK_DEBUG("Canvas size automatically set to %" PRIu32 "x%" PRIu32,
                encInfo.xsize, encInfo.ysize);
    // Changing the canvas size affects frames' have_crop field - we sort it
    // out later.
  }

  // Init encoder
  JxlMemoryManager* memoryManager = nullptr;
  JxlEncoderPtr encp = JxlEncoderMake(memoryManager);
  JxlEncoder* enc = encp.get();
  JxlThreadParallelRunnerPtr runner;
  if (numThreads != 1) {
    numThreads = numThreads > 0 ? numThreads :
                 JxlThreadParallelRunnerDefaultNumWorkerThreads();
    runner = JxlThreadParallelRunnerMake(memoryManager, numThreads);
    if (JxlEncoderSetParallelRunner(enc, JxlThreadParallelRunner, runner.get())
        != JXL_ENC_SUCCESS) {
      throw JxltkError("%s: Failed to set parallel runner for encoder", __func__);
    }
  }
  if (mergeCfg.codestreamLevel && *mergeCfg.codestreamLevel >= 0 &&
      JxlEncoderSetCodestreamLevel(enc, *mergeCfg.codestreamLevel) != JXL_ENC_SUCCESS) {
    throw JxltkError("%s: Failed in JxlEncoderSetCodestreamLevel", __func__);
  }

  if (totalBoxes > 0) {
    JXLTK_DEBUG("Forcing container format, since we have %zu metadata boxes to add.",
                totalBoxes);
    if (JxlEncoderUseBoxes(enc) != JXL_ENC_SUCCESS) {
      throw JxltkError("%s: Failed to enable container format", __func__);
    }
  }
  JXLTK_INFO("Writing basic info: %s", toString(encInfo).c_str());
  if (JxlEncoderSetBasicInfo(enc, &encInfo) != JXL_ENC_SUCCESS) {
    throw JxltkError("%s: Failed in JxlEncoderSetBasicInfo", __func__);
  }
  if (color->enc) {
    if (JxlEncoderSetColorEncoding(enc, &*color->enc) != JXL_ENC_SUCCESS) {
      throw JxltkError("%s: Failed in JxlEncoderSetColorEncoding", __func__);
    }
  } else {
    if (JxlEncoderSetICCProfile(enc, color->icc.data.get(), color->icc.size)
        != JXL_ENC_SUCCESS) {
      throw JxltkError("%s: Failed in JxlEncoderSetICCProfile", __func__);
    }
  }

  std::ofstream fout(output.c_str(), std::ios::binary);
  if (!fout) {
    throw JxltkError("%s: Failed to open %s for writing", __func__,
                     output.c_str());
  }

  std::unique_ptr<uint8_t[]> buffer =
      JXLTK_MAKE_UNIQUE_FOR_OVERWRITE<uint8_t[]>(kBufferSize);

  // Write boxes from mergeCfg
  JXLTK_TRACE("Writing %zu boxes from mergeCfg.", mergeCfg.boxes.size());
  std::vector<uint8_t> boxContent;
  size_t nextBox = 0;
  for (const BoxConfig& inBoxCfg : mergeCfg.boxes) {
    BoxConfig boxCfg(mergeCfg.boxDefaults);
    boxCfg.update(inBoxCfg);
    if (!boxCfg.type || boxCfg.type->size() != 4) {
      throw JxltkError("%s: Invalid box type %s", __func__,
                       shellQuote(boxCfg.type.value_or(""), true).c_str());
    }
    if (boxCfg.file && !boxCfg.file->empty()) {
      loadFile(*boxCfg.file, &boxContent);
    } else {
      boxContent.clear();
    }
    bool compress = boxCfg.compress.value_or(false);
    JXLTK_INFO("Writing box [%zu/%zu]: %s%s", nextBox+1, totalBoxes,
               compress ? "'brob'/" : "",
               shellQuote(simplifyString(*boxCfg.type), true).c_str());
    writeBox(enc, boxCfg.type->data(), boxContent.data(), boxContent.size(),
             compress, nextBox == totalBoxes - 1, buffer.get(), kBufferSize, fout);
    ++nextBox;
  }
  // Write boxes copied from input JXLs
  JXLTK_TRACE("Copying %zu boxes from %zu inputs.", totalBoxes - nextBox,
              frameConfigs.size());
  for (size_t frameIdx = 0; frameIdx < frameConfigs.size(); ++frameIdx) {
    const FrameConfig& frameCfg = frameConfigs[frameIdx];
    // We've moved our Decoder object to the corresponding Pixmap; borrow it back.
    jxlazy::Decoder* dec;
    if (!frameCfg.copyBoxes.value_or(false) ||
        !(dec = frameBuffers[frameIdx].getDecoder())) {
      continue;
    }
    auto nonReservedBoxes = getNonReservedBoxes(*dec);
    bool compress = mergeCfg.boxDefaults.compress.value_or(false);
    for (const std::pair<size_t,jxlazy::BoxInfo>& boxToCopy : nonReservedBoxes) {
      dec->getBoxContent(boxToCopy.first, &boxContent, SIZE_MAX, compress);
      JXLTK_INFO("Writing box [%zu/%zu]: (copied) %s%s", nextBox+1, totalBoxes,
                 compress ? "'brob'/" : "",
                 shellQuote(simplifyString(std::string_view(boxToCopy.second.type, 4)),
                            true).c_str());
      writeBox(enc, boxToCopy.second.type, boxContent.data(), boxContent.size(),
               compress, nextBox == totalBoxes - 1, buffer.get(), kBufferSize, fout);
      ++nextBox;
    }
  }

  // Write frames
  for (size_t frameIdx = 0; frameIdx < inputs.size(); ++frameIdx) {
    Pixmap& frameBuffer = frameBuffers.at(frameIdx);
    const FrameConfig& frameCfg = frameConfigs[frameIdx];

    JXLTK_INFO("Writing frame [%zu/%zu]: %s", frameIdx+1, inputs.size(),
               frameCfg
                   .toString(frameBuffer.getXsize(), frameBuffer.getYsize())
                   .c_str());
    JxlEncoderFrameSettings* settings =
        frameConfigToJxlEncoderFrameSettings(enc, encInfo, frameCfg,
                                                  encInfo.animation.tps_numerator,
                                                  encInfo.animation.tps_denominator,
                                                  frameBuffer.getXsize(),
                                                  frameBuffer.getYsize(),
                                                  mergeCfg.brotliEffort);
    if (JxlEncoderAddImageFrame(settings, &frameBuffer.getPixelFormat(),
                                frameBuffer.data(),
                                frameBuffer.getBufferSize()) != JXL_ENC_SUCCESS) {
      throw JxltkError("%s: Failed to add frame %zu", __func__, frameIdx);
    }
    if (frameIdx == inputs.size() - 1) {
      JxlEncoderCloseFrames(enc);
    }

    JxlEncoderStatus st = encodeUntilSuccess(enc, buffer.get(), kBufferSize, fout);
    if (st != JXL_ENC_SUCCESS) {
      throw JxltkError("%s: Unexpected encoder status while writing frame %zu: %s",
                       __func__, frameIdx, encoderStatusName(st));
    }

    // Frees the pixels and the Decoder
    frameBuffer.close();
  }

  JXLTK_NOTICE("Finished writing %s.", shellQuote(output, true).c_str());
}


}  // namespace jxltk
