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
    jxltk::merge(mergeCfg, oss, 0, {}, false);
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
    jxltk::merge(mergeCfg, oss, 0, {}, true);
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
