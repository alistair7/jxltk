/**
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */

#include <filesystem>
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

#include <jxlazy/decoder.h>
#include "merge.h"

static std::string getPath(std::string_view s) {
  return std::string(JXLTK_TEST_DIR) + '/' + std::string(s);
}

TEST(Merge, AutoCrop) {
  jxltk::MergeConfig mergeCfg;
  {
    std::ifstream mergeJson(getPath("crop/croptest.json"), std::ios::binary);
    mergeCfg = jxltk::MergeConfig::fromJson(mergeJson);
  }
  // Adjust paths so they're relative to the json directory.
  auto jsonDir = std::filesystem::path(getPath("crop"));
  for (auto& frameCfg : mergeCfg.frames) {
    if (!frameCfg.file || frameCfg.file->empty()) continue;
    std::filesystem::path inpPath(*frameCfg.file);
    if (inpPath.is_absolute()) continue;
    frameCfg.file = (jsonDir / inpPath).string();
  }

  std::string jxlBytes;
  {
    std::ostringstream oss;
    jxltk::merge(mergeCfg, oss, 0, false);
    jxlBytes = oss.str();
  }

  jxlazy::Decoder dec;
  dec.openMemory(reinterpret_cast<const uint8_t*>(jxlBytes.data()), jxlBytes.size(),
                 jxlazy::DecoderFlag::NoCoalesce,
                 jxlazy::DecoderHint::NoColorProfile|jxlazy::DecoderHint::NoPixels);
  jxlazy::FrameInfo frameInfo = dec.getFrameInfo(0);
  EXPECT_EQ(frameInfo.header.layer_info.crop_x0, 8);
  EXPECT_EQ(frameInfo.header.layer_info.crop_y0, 7);
  EXPECT_EQ(frameInfo.header.layer_info.xsize, 24);
  EXPECT_EQ(frameInfo.header.layer_info.ysize, 25);
  for (size_t frameIdx : {1,2,3}) {
    frameInfo = dec.getFrameInfo(frameIdx);
    EXPECT_EQ(frameInfo.header.layer_info.crop_x0, 0);
    EXPECT_EQ(frameInfo.header.layer_info.crop_y0, 0);
    EXPECT_EQ(frameInfo.header.layer_info.xsize, 32);
    EXPECT_EQ(frameInfo.header.layer_info.ysize, 32);
  }


  {
    std::ostringstream oss;
    jxltk::merge(mergeCfg, oss, 0, true);
    jxlBytes = oss.str();
  }

  dec.openMemory(reinterpret_cast<const uint8_t*>(jxlBytes.data()), jxlBytes.size(),
                 jxlazy::DecoderFlag::NoCoalesce,
                 jxlazy::DecoderHint::NoColorProfile|jxlazy::DecoderHint::NoPixels);
  frameInfo = dec.getFrameInfo(0);
  EXPECT_EQ(frameInfo.header.layer_info.crop_x0, 12);
  EXPECT_EQ(frameInfo.header.layer_info.crop_y0, 11);
  EXPECT_EQ(frameInfo.header.layer_info.xsize, 16);
  EXPECT_EQ(frameInfo.header.layer_info.ysize, 16);
  frameInfo = dec.getFrameInfo(1);
  EXPECT_EQ(frameInfo.header.layer_info.crop_x0, 5);
  EXPECT_EQ(frameInfo.header.layer_info.crop_y0, 5);
  EXPECT_EQ(frameInfo.header.layer_info.xsize, 23);
  EXPECT_EQ(frameInfo.header.layer_info.ysize, 24);
  frameInfo = dec.getFrameInfo(2);
  EXPECT_EQ(frameInfo.header.layer_info.crop_x0, 0);
  EXPECT_EQ(frameInfo.header.layer_info.crop_y0, 0);
  EXPECT_EQ(frameInfo.header.layer_info.xsize, 1);
  EXPECT_EQ(frameInfo.header.layer_info.ysize, 1);
  frameInfo = dec.getFrameInfo(3);
  EXPECT_EQ(frameInfo.header.layer_info.crop_x0, 0);
  EXPECT_EQ(frameInfo.header.layer_info.crop_y0, 0);
  EXPECT_EQ(frameInfo.header.layer_info.xsize, 32);
  EXPECT_EQ(frameInfo.header.layer_info.ysize, 32);
}

template<class T>
void assertLastChannelUniform(T* samples, uint32_t xsize, uint32_t ysize,
                              size_t numChannels, T expect) {
  for (uint32_t y = 0; y < ysize; ++y) {
    for (uint32_t x = 0; x < xsize; ++x, samples += numChannels) {
      ASSERT_EQ(samples[numChannels - 1], expect);
    }
  }
}

TEST(Merge, AlphaFill) {
  jxltk::MergeConfig mergeCfg;
  mergeCfg.frameDefaults.effort = 1;
  mergeCfg.dataType = JXL_TYPE_FLOAT;

  // Frame 0 has alpha, which should be unmodified
  jxltk::FrameConfig& frameCfg0 = mergeCfg.frames.emplace_back();
  frameCfg0.file = getPath("subtract/frame0.jxl");
  // Frame 1 has alpha, and we're overriding it
  jxltk::FrameConfig& frameCfg1 = mergeCfg.frames.emplace_back();
  frameCfg1.file = getPath("subtract/frame0.jxl");
  frameCfg1.alphaFill = 2.f; // Whole number to avoid float issues - the temp JXL is 8 bit
  // Frame 2 has no alpha, and it should default to 1.0
  jxltk::FrameConfig& frameCfg2 = mergeCfg.frames.emplace_back();
  frameCfg2.file = getPath("subtract/frame1.jxl");
  // Frame 3 has no alpha, and it should default to 0.0
  jxltk::FrameConfig& frameCfg3 = mergeCfg.frames.emplace_back();
  frameCfg3.file = getPath("subtract/frame1.jxl");
  frameCfg3.blendMode = JXL_BLEND_ADD;
  // Frame 4 has no alpha, but we're overriding the default
  jxltk::FrameConfig& frameCfg4 = mergeCfg.frames.emplace_back();
  frameCfg4.file = getPath("subtract/frame1.jxl");
  frameCfg4.alphaFill = 0.0f;
  // Frame 5 has no alpha, but we're overriding the default
  jxltk::FrameConfig& frameCfg5 = mergeCfg.frames.emplace_back();
  frameCfg5.file = getPath("subtract/frame1.jxl");
  frameCfg3.blendMode = JXL_BLEND_ADD;
  frameCfg5.alphaFill = 1.0f;
  // Frame 6 has no alpha, and we're redundantly setting it to its default value
  jxltk::FrameConfig& frameCfg6 = mergeCfg.frames.emplace_back();
  frameCfg6.file = getPath("subtract/frame1.jxl");
  frameCfg6.alphaFill = 1.0f;

  std::string jxlBytes;
  {
    std::ostringstream oss;
    jxltk::merge(mergeCfg, oss);
    jxlBytes = oss.str();
  }
  jxlazy::Decoder dec;
  dec.openMemory(reinterpret_cast<const uint8_t*>(jxlBytes.data()), jxlBytes.size(),
                 jxlazy::DecoderFlag::NoCoalesce, jxlazy::DecoderHint::NoColorProfile);

  ASSERT_EQ(dec.frameCount(), 7);

  const size_t numChannels = 4;

  jxlazy::FrameInfo frameInfo = dec.getFrameInfo(0);
  jxlazy::FramePixels<float> framePixels = dec.getFramePixels<float>(0, numChannels);
  // Check for at least two distinct alpha values to show it hasn't been
  // defaulted/overwritten
  float seenAlpha;
  bool multipleAlphaValues = false;
  const float* pixel = framePixels.color.data();
  for (uint32_t y = 0;
       y < frameInfo.header.layer_info.ysize && !multipleAlphaValues;
       ++y) {
    for (uint32_t x = 0;
         x < frameInfo.header.layer_info.xsize;
         ++x, pixel += numChannels) {
      if (y == 0 && x == 0) {
        seenAlpha = pixel[numChannels - 1];
      } else {
        if (seenAlpha != pixel[numChannels - 1]) {
          multipleAlphaValues = true;
          break;
        }
      }
    }
  }
  EXPECT_TRUE(multipleAlphaValues);

  frameInfo = dec.getFrameInfo(1);
  framePixels = dec.getFramePixels<float>(1, numChannels);
  assertLastChannelUniform(framePixels.color.data(), frameInfo.header.layer_info.xsize,
                           frameInfo.header.layer_info.ysize, numChannels, 2.f);

  frameInfo = dec.getFrameInfo(2);
  framePixels = dec.getFramePixels<float>(2, numChannels);
  assertLastChannelUniform(framePixels.color.data(), frameInfo.header.layer_info.xsize,
                           frameInfo.header.layer_info.ysize, numChannels, 1.f);

  frameInfo = dec.getFrameInfo(3);
  framePixels = dec.getFramePixels<float>(3, numChannels);
  assertLastChannelUniform(framePixels.color.data(), frameInfo.header.layer_info.xsize,
                           frameInfo.header.layer_info.ysize, numChannels, 0.f);

  frameInfo = dec.getFrameInfo(4);
  framePixels = dec.getFramePixels<float>(4, numChannels);
  assertLastChannelUniform(framePixels.color.data(), frameInfo.header.layer_info.xsize,
                           frameInfo.header.layer_info.ysize, numChannels, 0.f);

  frameInfo = dec.getFrameInfo(5);
  framePixels = dec.getFramePixels<float>(5, numChannels);
  assertLastChannelUniform(framePixels.color.data(), frameInfo.header.layer_info.xsize,
                           frameInfo.header.layer_info.ysize, numChannels, 1.f);

  frameInfo = dec.getFrameInfo(6);
  framePixels = dec.getFramePixels<float>(6, numChannels);
  assertLastChannelUniform(framePixels.color.data(), frameInfo.header.layer_info.xsize,
                           frameInfo.header.layer_info.ysize, numChannels, 1.f);
}
