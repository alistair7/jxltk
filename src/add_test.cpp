/**
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */

#include <gtest/gtest.h>
#include "src/util.h"

#include "add.h"

#ifndef JXLTK_TEST_DIR
#define JXLTK_TEST_DIR "testfiles"
#endif

static std::string getPath(std::string_view s) {
  return std::string(JXLTK_TEST_DIR) + '/' + std::string(s);
}

bool operator==(const JxlAnimationHeader& a, const JxlAnimationHeader& b) {
  return a.have_timecodes == b.have_timecodes &&
         a.num_loops == b.num_loops &&
         a.tps_numerator == b.tps_numerator &&
         a.tps_denominator == b.tps_denominator;
}

void expectBasicInfosEqualIsh(const JxlBasicInfo& a, const JxlBasicInfo& b) {
  EXPECT_EQ(a.alpha_bits, b.alpha_bits);
  EXPECT_EQ(a.alpha_exponent_bits, b.alpha_exponent_bits);
  EXPECT_EQ(a.alpha_premultiplied, b.alpha_premultiplied);
  EXPECT_EQ(a.bits_per_sample, b.bits_per_sample);
  EXPECT_EQ(a.exponent_bits_per_sample, b.exponent_bits_per_sample);
  EXPECT_EQ(a.have_animation, b.have_animation);
  //EXPECT_EQ(a.have_container, b.have_container);
  EXPECT_EQ(a.have_preview, b.have_preview);
  EXPECT_EQ(a.intensity_target, b.intensity_target);
  EXPECT_EQ(a.intrinsic_xsize, b.intrinsic_xsize);
  EXPECT_EQ(a.intrinsic_ysize, b.intrinsic_ysize);
  EXPECT_EQ(a.linear_below, b.linear_below);
  EXPECT_EQ(a.min_nits, b.min_nits);
  EXPECT_EQ(a.num_color_channels, b.num_color_channels);
  EXPECT_EQ(a.num_extra_channels, b.num_extra_channels);
  EXPECT_EQ(a.orientation, b.orientation);
  EXPECT_EQ(a.relative_to_max_display, b.relative_to_max_display);
  EXPECT_EQ(a.uses_original_profile, b.uses_original_profile);
  if (a.have_animation) {
    EXPECT_EQ(a.animation, b.animation);
  }
}

bool operator==(const JxlBlendInfo& a, const JxlBlendInfo& b) {
  return a.blendmode == b.blendmode &&
         a.source == b.source &&
         (a.clamp == b.clamp ||
          (a.blendmode != JXL_BLEND_BLEND && a.blendmode != JXL_BLEND_MUL &&
           a.blendmode != JXL_BLEND_MULADD)) &&
         (a.alpha == b.alpha ||
          (a.blendmode != JXL_BLEND_BLEND && a.blendmode != JXL_BLEND_MULADD));
}

bool operator==(const JxlLayerInfo& a, const JxlLayerInfo& b) {
  return a.have_crop == b.have_crop &&
         a.save_as_reference == b.save_as_reference &&
         a.blend_info == b.blend_info &&
         (!a.have_crop ||
           (a.crop_x0 == b.crop_x0 &&
            a.crop_y0 == b.crop_y0 &&
            a.xsize == b.xsize &&
            a.ysize == b.ysize));
}

void expectFrameHeadersEqualIsh(const JxlFrameHeader& a, const JxlFrameHeader& b,
                                bool haveTimecodes) {
  EXPECT_EQ(a.duration, b.duration);
  //EXPECT_EQ(a.is_last, b.is_last);
  if (haveTimecodes) {
    EXPECT_EQ(a.timecode, b.timecode);
  }
  EXPECT_EQ(a.layer_info, b.layer_info);
}

void expectFrameInfosEqual(const jxlazy::FrameInfo& a, const jxlazy::FrameInfo& b,
                           bool haveTimeCodes) {
  EXPECT_EQ(a.ecBlendInfo, b.ecBlendInfo);
  expectFrameHeadersEqualIsh(a.header, b.header, haveTimeCodes);
  EXPECT_EQ(a.name, b.name);
}

TEST(AddOrSubtract, SelfSubtract) {
  for (uint32_t decoderFlags : {0, static_cast<int>(jxlazy::DecoderFlag::NoCoalesce)}) {

    // Subtracting a file from itself should result in zeros in every channel
    jxlazy::Decoder leftDec, rightDec, resultDec, restoreDec;
    leftDec.openFile(getPath("../contrib/jxlazy/testfiles/generated.jxl").c_str(),
                     decoderFlags);
    rightDec.openFile(getPath("../contrib/jxlazy/testfiles/generated.jxl").c_str(),
                      decoderFlags);
    std::string result;
    {
      std::ostringstream sresult;
      EXPECT_EQ(jxltk::addOrSubtract(leftDec, rightDec, false, sresult), 0);
      result = sresult.str();
    }
    resultDec.openMemory(reinterpret_cast<const uint8_t*>(result.data()), result.size(),
                         decoderFlags);
    JxlBasicInfo leftInfo = leftDec.getBasicInfo();
    JxlBasicInfo resultInfo = resultDec.getBasicInfo();
    EXPECT_EQ(leftInfo.num_color_channels, 3);
    EXPECT_EQ(leftInfo.num_extra_channels, 2);
    expectBasicInfosEqualIsh(leftInfo, resultInfo);
    size_t frameCount = leftDec.frameCount();
    EXPECT_EQ(resultDec.frameCount(), frameCount);
    bool haveTimecodes = leftInfo.have_animation && leftInfo.animation.have_timecodes;

    jxlazy::FramePixels<float> resultFramePixels;
    jxlazy::FramePixels<float> expect;
    // If coalescing, every frame is the same size
    if (!(decoderFlags & jxlazy::DecoderFlag::NoCoalesce)) {
      size_t numPixels = static_cast<size_t>(leftInfo.xsize) * leftInfo.ysize;
      expect.color.resize(numPixels * leftInfo.num_color_channels);
      for (size_t ec = 0; ec < leftInfo.num_extra_channels; ++ec) {
        expect.ecs.emplace_hint(expect.ecs.end(), ec,
                                std::vector<float>(static_cast<size_t>(leftInfo.xsize) *
                                                   leftInfo.ysize));
      }
    } else {
      for (size_t ec = 0; ec < leftInfo.num_extra_channels; ++ec) {
        expect.ecs.emplace_hint(expect.ecs.end(), ec, std::vector<float>());
      }
    }
    for (size_t frameIdx = 0; frameIdx < frameCount; ++frameIdx) {
      jxlazy::FrameInfo leftFrameInfo = leftDec.getFrameInfo(frameIdx);
      jxlazy::FrameInfo resultFrameInfo = resultDec.getFrameInfo(frameIdx);
      expectFrameInfosEqual(leftFrameInfo, resultFrameInfo, haveTimecodes);
      // If not coalescing, each frame can have a different size
      if ((decoderFlags & jxlazy::DecoderFlag::NoCoalesce)) {
        size_t numPixels = static_cast<size_t>(leftFrameInfo.header.layer_info.xsize) *
                           leftFrameInfo.header.layer_info.ysize;
        expect.color.resize(numPixels * leftInfo.num_color_channels);
        for (auto& ecNode : expect.ecs) {
          ecNode.second.resize(numPixels);
        }
      }

      resultDec.getFramePixels(&resultFramePixels, frameIdx,
                               resultInfo.num_color_channels, std::span<const int>({-1}));
      EXPECT_EQ(resultFramePixels, expect);
    }


    // Adding this zeroed image back to the original should leave it unchanged.
    std::string restore;
    {
      std::ostringstream srestore;
      EXPECT_EQ(jxltk::addOrSubtract(leftDec, resultDec, true, srestore), 0);
      restore = srestore.str();
    }
    restoreDec.openMemory(reinterpret_cast<const uint8_t*>(restore.data()),
                          restore.size(), decoderFlags);
    EXPECT_TRUE(jxltk::haveSamePixels(restoreDec, leftDec));
  }
}

TEST(AddOrSubtract, AddIsCommutative) {

  jxlazy::FramePixels<float> expectPixels;
  uint32_t numColorChannels;
  {
    jxlazy::Decoder expectDec;
    expectDec.openFile(getPath("gray256_h+v.jxl").c_str());
    expectDec.getFramePixels(&expectPixels, 0,
                             numColorChannels = expectDec.getBasicInfo().num_color_channels,
                             std::span<const int>({-1}));
  }

  jxlazy::FramePixels<float> resultFramePixels;

  for (bool horizontalFirst : {true, false}) {

    jxlazy::Decoder leftDec, rightDec;
    if (horizontalFirst) {
      leftDec.openFile(getPath("gray256_horizontal.jxl").c_str());
      rightDec.openFile(getPath("gray256_vertical.jxl").c_str());
    } else {
      leftDec.openFile(getPath("gray256_vertical.jxl").c_str());
      rightDec.openFile(getPath("gray256_horizontal.jxl").c_str());
    }
    std::string result;
    {
      std::ostringstream sresult;
      EXPECT_EQ(jxltk::addOrSubtract(leftDec, rightDec, true, sresult), 0);
      result = sresult.str();
    }
    jxlazy::Decoder resultDec;
    resultDec.openMemory(reinterpret_cast<const uint8_t*>(result.data()), result.size());
    resultDec.getFramePixels(&resultFramePixels, 0,
                             numColorChannels, std::span<const int>({-1}));
    EXPECT_EQ(resultFramePixels, expectPixels);
  }
}

TEST(AddOrSubtract, Roundtrip) {

  const jxltk::FrameConfig frameConfig { .effort = 1 };

  struct {
    const char* minuend;
    const char* subtrahend;
    const char* expectSub;
    const char* expectAdd;
  } tests[] = {
    { "gray256_horizontal.jxl", "gray256_vertical.jxl",
      "gray256_h-v.jxl", "gray256_h+v.jxl" },
    { "gray256_vertical.jxl", "gray256_horizontal.jxl",
      "gray256_v-h.jxl", "gray256_h+v.jxl" },
  };

  for (const auto& test : tests) {

    jxlazy::Decoder minuendDec;
    minuendDec.openFile(getPath(test.minuend).c_str());

    jxlazy::Decoder subtrahendDec;
    subtrahendDec.openFile(getPath(test.subtrahend).c_str());

    // Subtract
    std::string subtractedJxl;
    {
      std::ostringstream ss;
      EXPECT_EQ(jxltk::addOrSubtract(minuendDec, subtrahendDec, false, ss, frameConfig),
                0);
      subtractedJxl = ss.str();
    }
    uint32_t numColorChannels;
    jxlazy::FramePixels<float> expectPixels;
    {
      jxlazy::Decoder dec;
      dec.openFile(getPath(test.expectSub).c_str());
      dec.getFramePixels(&expectPixels, 0,
                         numColorChannels = dec.getBasicInfo().num_color_channels,
                         std::span<const int>({-1}));
    }
    jxlazy::Decoder subtractedDec;
    subtractedDec.openMemory(reinterpret_cast<uint8_t*>(subtractedJxl.data()), subtractedJxl.size());
    jxlazy::FramePixels<float> subtractedPixels =
      subtractedDec.getFramePixels<float>(0, numColorChannels, std::span<const int>({-1}));
    EXPECT_EQ(subtractedPixels, expectPixels);

    // Add back
    std::string addedJxl;
    {
      std::ostringstream ss;
      EXPECT_EQ(jxltk::addOrSubtract(subtractedDec, subtrahendDec, true, ss, frameConfig),
                0);
      addedJxl = ss.str();
    }
    jxlazy::Decoder addedDec;
    addedDec.openMemory(reinterpret_cast<uint8_t*>(addedJxl.data()), addedJxl.size());
    jxlazy::FramePixels<float> addedPixels =
      addedDec.getFramePixels<float>(0, numColorChannels, std::span<const int>({-1}));
    jxlazy::FramePixels<float> minuendPixels =
        minuendDec.getFramePixels<float>(0, numColorChannels, std::span<const int>({-1}));
    EXPECT_EQ(addedPixels, minuendPixels);


    // Add original frames together
    {
      std::ostringstream ss;
      EXPECT_EQ(jxltk::addOrSubtract(minuendDec, subtrahendDec, true, ss, frameConfig),
                0);
      addedJxl = ss.str();
    }
    addedDec.openMemory(reinterpret_cast<uint8_t*>(addedJxl.data()), addedJxl.size());
    addedDec.getFramePixels<float>(&addedPixels, 0, numColorChannels,
                                   std::span<const int>({-1}));
    {
      jxlazy::Decoder dec;
      dec.openFile(getPath(test.expectAdd).c_str());
      dec.getFramePixels(&expectPixels, 0,
                         numColorChannels,
                         std::span<const int>({-1}));
    }
    EXPECT_EQ(addedPixels, expectPixels);

    // Subtract back
    {
      std::ostringstream ss;
      EXPECT_EQ(jxltk::addOrSubtract(addedDec, subtrahendDec, false, ss, frameConfig), 0);
      subtractedJxl = ss.str();
    }
    subtractedDec.openMemory(reinterpret_cast<uint8_t*>(subtractedJxl.data()),
                             subtractedJxl.size());
    subtractedDec.getFramePixels<float>(&subtractedPixels, 0, numColorChannels,
                                        std::span<const int>({-1}));
    EXPECT_EQ(subtractedPixels, minuendPixels);
  }

}
