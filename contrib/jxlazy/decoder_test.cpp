/**
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
*/
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include <gtest/gtest.h>

#include <jxlazy/decoder.h>
#include <jxlazy/exception.h>

using namespace std;

static string getPath(const string& relative) {
  const char* dataDir = getenv("JXLAZY_TEST_DATA_DIR");
  dataDir = dataDir ? dataDir : "testfiles";
  return string(dataDir) + '/' + relative;
}

static void loadFileAppend(const string& filename, vector<uint8_t>* v) {
  ifstream in(filename, ios::in|ios::binary);
  size_t fileSize = std::filesystem::file_size(filename);
  size_t oldSize = v->size();
  v->resize(oldSize + fileSize);
  in.read(reinterpret_cast<char*>(v->data() + oldSize), fileSize);
}

static vector<uint8_t> loadFile(const string& filename) {
  vector<uint8_t> result;
  loadFileAppend(filename, &result);
  return result;
}


TEST(Decoder, OpenFile) {
  jxlazy::Decoder jxl;
  EXPECT_THROW(jxl.openFile(getPath("file-that-does-not-exist").c_str()),
               jxlazy::ReadError);
  EXPECT_THROW(jxl.openFile(getPath("not_a_jxl.png").c_str(),
                            /*flags=*/0, /*hints=*/0,
                            /*inBufferSize=*/0), jxlazy::ReadError);
  EXPECT_NO_THROW(jxl.openFile(getPath("frame0.jxl").c_str(), 0, 0, 0));
  EXPECT_NO_THROW(jxl.openFile(getPath("frame0.jxl").c_str(), 0, 0, 0));
  EXPECT_NO_THROW(jxl.getBasicInfo());
}

TEST(Decoder, OpenStream) {
  jxlazy::Decoder jxl;
  ifstream frame0(getPath("frame0.jxl").c_str(), ios::in|ios::binary);
  EXPECT_NO_THROW(jxl.openStream(frame0, jxlazy::DecoderFlag::NoCoalesce, 0, 512));
  ifstream frame1(getPath("frame1 (-2-1).jxl").c_str(), ios::in|ios::binary);
  EXPECT_NO_THROW(jxl.openStream(frame1, 0, jxlazy::DecoderHint::NoPixels, 0));
  EXPECT_NO_THROW(jxl.getBasicInfo());

  // Make sure doing multiple passes over a stream rewinds to the initial position.
  string s;
  {
    vector<uint8_t> jxlfile(16);
    memcpy(jxlfile.data(), "NonsenseDataHere", 16);
    loadFileAppend(getPath("generated.jxl").c_str(), &jxlfile);
    s.assign(reinterpret_cast<char*>(jxlfile.data()), jxlfile.size());
  }

  JxlPixelFormat pf = {
    .num_channels = 4,
    .data_type = JXL_TYPE_UINT8,
  };
  vector<uint8_t> pixels;

  // Stream that starts at the JXL
  istringstream is1(s.substr(16), ios::in|ios::binary);
  jxl.openStream(is1, jxlazy::DecoderFlag::NoCoalesce);

  pixels.resize(jxl.getFrameBufferSize(1, pf));
  jxl.getFramePixels(1, pf, pixels.data(), pixels.size());
  pixels.resize(jxl.getFrameBufferSize(0, pf));
  jxl.getFramePixels(0, pf, pixels.data(), pixels.size());


  // Stream that starts before the JXL
  istringstream is(s, ios::in|ios::binary);
  is.ignore(16);
  EXPECT_EQ(is.gcount(), 16);
  jxl.openStream(is, jxlazy::DecoderFlag::NoCoalesce);

  pixels.resize(jxl.getFrameBufferSize(1, pf));
  jxl.getFramePixels(1, pf, pixels.data(), pixels.size());
  pixels.resize(jxl.getFrameBufferSize(0, pf));
  jxl.getFramePixels(0, pf, pixels.data(), pixels.size());
}

TEST(DecoderOpen, OpenMemory) {
  vector<uint8_t> frame = loadFile(getPath("frame0.jxl"));
  jxlazy::Decoder jxl;
  EXPECT_NO_THROW(jxl.openMemory(frame.data(), frame.size(), 0, 0));
  EXPECT_NO_THROW(jxl.openMemory(frame.data(), frame.size(), 0, 0));
  EXPECT_THROW(jxl.openMemory(frame.data()+1, frame.size()-1, 0, 0), jxlazy::ReadError);
  EXPECT_NO_THROW(jxl.openMemory(frame.data(), frame.size(), 0, 0));
  EXPECT_NO_THROW(jxl.getBasicInfo());
}

TEST(Decoder, OpenBehavior) {
  jxlazy::Decoder jxl;
  // Attempting an operation before a file is open should throw an exception.
  EXPECT_THROW(jxl.getBasicInfo(), jxlazy::UsageError);
  EXPECT_THROW(jxl.openFile(getPath("file-that-does-not-exist").c_str()),
               jxlazy::ReadError);
  // Open existing non-jxl and catch exception.
  EXPECT_THROW(jxl.openFile(getPath("not_a_jxl.png").c_str()), jxlazy::ReadError);

  EXPECT_NO_THROW(jxl.openFile(getPath("frame0.jxl").c_str()));
  ifstream frame0(getPath("frame0.jxl"), ios::in|ios::binary);
  EXPECT_NO_THROW(jxl.openStream(frame0, jxlazy::DecoderFlag::NoCoalesce, 0, 12));
  EXPECT_NO_THROW(jxl.close());
  EXPECT_THROW(jxl.getBasicInfo(), jxlazy::UsageError);
  EXPECT_NO_THROW(jxl.openFile(getPath("frame1 (-2-1).jxl").c_str()));
  jxl.getBasicInfo();
}

TEST(Decoder, BuffersWholeFileWhenPossible) {
  jxlazy::Decoder jxl;
  // Buffer limit too low, so never buffers the whole file
  EXPECT_NO_THROW(jxl.openFile(getPath("generated.jxl").c_str(),
                               jxlazy::DecoderFlag::NoCoalesce, 0, 2));
  EXPECT_FALSE(jxl.jxlIsFullyBuffered());
  EXPECT_EQ(jxl.frameCount(), 3);
  EXPECT_FALSE(jxl.jxlIsFullyBuffered());

  // Buffer size exactly the same as the file size
  EXPECT_NO_THROW(jxl.openFile(getPath("generated.jxl").c_str(), 0, 0, 3));
  EXPECT_TRUE(jxl.jxlIsFullyBuffered());

  // Synthesize a large jxl to force dynamic buffer growth
  vector<uint8_t> bigjxl = loadFile(getPath("generated.jxl"));
  size_t origSize = 3072;
  EXPECT_EQ(bigjxl.size(), origSize);
  size_t fullSize = 512 * 1024;
  size_t extraSize = fullSize - origSize;
  uint8_t sizeHeader[] = { 0, static_cast<uint8_t>((extraSize>>16)&0xFF),
                              static_cast<uint8_t>((extraSize>>8)&0xFF),
                              static_cast<uint8_t>(extraSize&0xFF),
                           'x', 't', 'r', 'a' };
  bigjxl.resize(fullSize);
  memcpy(bigjxl.data() + origSize, sizeHeader, sizeof sizeHeader);
  std::string bigjxlStr(reinterpret_cast<const char*>(bigjxl.data()), bigjxl.size());
  std::istringstream sstream(std::move(bigjxlStr));
  // Allow large buffer, but as jxlazy doesn't know the size, it should start small.
  EXPECT_NO_THROW(jxl.openStream(sstream, 0, jxlazy::DecoderHint::WantBoxes,
                                 1024));
  EXPECT_FALSE(jxl.jxlIsFullyBuffered());
  vector<uint8_t> content;
  jxl.getBoxContent(8, &content);
  EXPECT_EQ(content.size(), extraSize - 8);
  EXPECT_TRUE(jxl.jxlIsFullyBuffered());
}


TEST(Decoder, CanGetBasicInfo) {
  jxlazy::Decoder jxl;
  jxl.openFile(getPath("frame0.jxl").c_str());
  JxlBasicInfo bi = jxl.getBasicInfo();
  EXPECT_EQ(bi.xsize, 16);
  EXPECT_EQ(bi.ysize, 16);
  EXPECT_EQ(jxl.xsize(), 16);
  EXPECT_EQ(jxl.ysize(), 16);
  EXPECT_EQ(bi.num_color_channels, 3);
  EXPECT_EQ(bi.num_extra_channels, 1);
  EXPECT_EQ(bi.alpha_bits, 8);

  jxl.openFile(getPath("frame1 (-2-1).jxl").c_str());
  EXPECT_EQ(jxl.xsize(), 20);
  EXPECT_EQ(jxl.ysize(), 18);
  bi = jxl.getBasicInfo();
  EXPECT_EQ(bi.xsize, 20);
  EXPECT_EQ(bi.ysize, 18);
  EXPECT_EQ(bi.num_color_channels, 3);
  EXPECT_EQ(bi.num_extra_channels, 1);
  EXPECT_EQ(bi.alpha_bits, 8);
  
  jxl.openFile(getPath("generated.jxl").c_str());
  bi = jxl.getBasicInfo();
  EXPECT_EQ(bi.xsize, 16);
  EXPECT_EQ(bi.ysize, 16);
  EXPECT_EQ(bi.num_color_channels, 3);
  EXPECT_EQ(bi.num_extra_channels, 2);
  EXPECT_EQ(bi.alpha_bits, 8);
  EXPECT_EQ(bi.have_animation, JXL_FALSE);
}

TEST(Decoder, CanGetFrameInfoCoalesced) {
  jxlazy::Decoder jxl;
  jxl.openFile(getPath("generated.jxl").c_str());
  JxlBasicInfo bi = jxl.getBasicInfo();
  EXPECT_EQ(bi.xsize, 16);
  EXPECT_EQ(bi.ysize, 16);
  EXPECT_EQ(bi.num_color_channels, 3);
  EXPECT_EQ(bi.num_extra_channels, 2);
  EXPECT_EQ(bi.alpha_bits, 8);

  EXPECT_EQ(jxl.frameCount(), 1);
  jxlazy::FrameInfo frameInfo = jxl.getFrameInfo(0);
  EXPECT_EQ(frameInfo.name, string());
  EXPECT_TRUE(frameInfo.ecBlendInfo.empty());
  const JxlLayerInfo& layerInfo = frameInfo.header.layer_info;
  EXPECT_EQ(layerInfo.xsize, bi.xsize);
  EXPECT_EQ(layerInfo.ysize, bi.ysize);
  EXPECT_EQ(layerInfo.have_crop, JXL_FALSE);
  EXPECT_EQ(layerInfo.blend_info.blendmode, JXL_BLEND_REPLACE);
}

TEST(Decoder, CanGetFrameInfoNonCoalesced) {
  jxlazy::Decoder jxl;
  jxl.openFile(getPath("generated.jxl").c_str(), jxlazy::DecoderFlag::NoCoalesce);
  const JxlBasicInfo bi = jxl.getBasicInfo();
  EXPECT_EQ(bi.xsize, 16);
  EXPECT_EQ(bi.ysize, 16);
  EXPECT_EQ(bi.num_color_channels, 3);
  EXPECT_EQ(bi.num_extra_channels, 2);
  EXPECT_EQ(bi.alpha_bits, 8);

  EXPECT_EQ(jxl.frameCount(), 3);

  jxlazy::FrameInfo frameInfo = jxl.getFrameInfo(0);
  EXPECT_EQ(frameInfo.name, string());
  const JxlLayerInfo* layerInfo = &frameInfo.header.layer_info;
  EXPECT_EQ(layerInfo->xsize, bi.xsize);
  EXPECT_EQ(layerInfo->ysize, bi.ysize);
  EXPECT_EQ(layerInfo->have_crop, JXL_FALSE);
  EXPECT_EQ(layerInfo->blend_info.blendmode, JXL_BLEND_REPLACE);
  // Extra channel blend modes appear to default to the color channels' blend mode
  // TODO: create some more exotic files to test with.
  ASSERT_EQ(frameInfo.ecBlendInfo.size(), bi.num_extra_channels);
  for (size_t ec = 0; ec < bi.num_extra_channels; ++ec) {
    EXPECT_EQ(frameInfo.ecBlendInfo[ec].blendmode, JXL_BLEND_REPLACE);
    EXPECT_EQ(frameInfo.ecBlendInfo[ec].source, 0);
    EXPECT_EQ(frameInfo.ecBlendInfo[ec].alpha, 0);
    EXPECT_EQ(frameInfo.ecBlendInfo[ec].clamp, 0);
  }

  frameInfo = jxl.getFrameInfo(1);
  EXPECT_EQ(frameInfo.name, string("Name"));
  layerInfo = &frameInfo.header.layer_info;
  EXPECT_EQ(layerInfo->xsize, 20);
  EXPECT_EQ(layerInfo->ysize, 18);
  EXPECT_EQ(layerInfo->have_crop, JXL_TRUE);
  EXPECT_EQ(layerInfo->crop_x0, -2);
  EXPECT_EQ(layerInfo->crop_y0, -1);
  EXPECT_EQ(layerInfo->blend_info.blendmode, JXL_BLEND_BLEND);
  ASSERT_EQ(frameInfo.ecBlendInfo.size(), bi.num_extra_channels);
  for (size_t ec = 0; ec < bi.num_extra_channels; ++ec) {
    EXPECT_EQ(frameInfo.ecBlendInfo[ec].blendmode, JXL_BLEND_BLEND);
    EXPECT_EQ(frameInfo.ecBlendInfo[ec].source, 0);
    EXPECT_EQ(frameInfo.ecBlendInfo[ec].alpha, 0);
    EXPECT_EQ(frameInfo.ecBlendInfo[ec].clamp, 0);
  }

  frameInfo = jxl.getFrameInfo(2);
  EXPECT_EQ(frameInfo.name, string());
  layerInfo = &frameInfo.header.layer_info;
  EXPECT_EQ(layerInfo->xsize, 9);
  EXPECT_EQ(layerInfo->ysize, 9);
  EXPECT_EQ(layerInfo->have_crop, JXL_TRUE);
  EXPECT_EQ(layerInfo->crop_x0, 6);
  EXPECT_EQ(layerInfo->crop_y0, 1);
  EXPECT_EQ(layerInfo->blend_info.blendmode, JXL_BLEND_ADD);
  ASSERT_EQ(frameInfo.ecBlendInfo.size(), bi.num_extra_channels);
  for (size_t ec = 0; ec < bi.num_extra_channels; ++ec) {
    EXPECT_EQ(frameInfo.ecBlendInfo[ec].blendmode, JXL_BLEND_ADD);
    EXPECT_EQ(frameInfo.ecBlendInfo[ec].source, 0);
    EXPECT_EQ(frameInfo.ecBlendInfo[ec].alpha, 0);
    EXPECT_EQ(frameInfo.ecBlendInfo[ec].clamp, 0);
  }

  // Reopen and make sure we can skip straight to frame 2
  jxl.openFile(getPath("generated.jxl").c_str(), jxlazy::DecoderFlag::NoCoalesce);
  frameInfo = jxl.getFrameInfo(2);

  // Make sure we can go backwards too
  frameInfo = jxl.getFrameInfo(1);
}

TEST(Decoder, GetRowStride) {
  struct {
    uint32_t xsize;
    JxlDataType dataType;
    size_t align;
    uint32_t numChannels;
    size_t expectReturn;
    size_t expectPadding;
  } tests[] = {
    { 0, JXL_TYPE_FLOAT, 8, 3, 0, 0 },
    { 1, JXL_TYPE_UINT8, 0, 3, 3, 0 },
    { 1, JXL_TYPE_UINT8, 4, 3, 4, 1 },
    { 2, JXL_TYPE_UINT8, 4, 3, 8, 2 },
    { 10, JXL_TYPE_UINT16, 16, 2, 48, 8 },
    { 1, JXL_TYPE_FLOAT, 4, 1, 4, 0 },
    { 1920, JXL_TYPE_FLOAT, 3, 4, 30720, 0 },
    { 1920, JXL_TYPE_FLOAT, 7, 4, 30723, 3 },
  };

  for (const auto& test : tests) {
    JxlPixelFormat format = {
      .num_channels = test.numChannels,
      .data_type = test.dataType,
      .endianness = JXL_NATIVE_ENDIAN,
      .align = test.align,
    };
    size_t padding;
    EXPECT_EQ(jxlazy::Decoder::getRowStride(test.xsize, format, &padding),
              test.expectReturn);
    EXPECT_EQ(padding, test.expectPadding);
  }
}

TEST(Decoder, ChoosesCorrectBufferSize) {
  jxlazy::Decoder jxl;
  jxl.openFile(getPath("frame2 (+6+1).jxl").c_str());
  const JxlBasicInfo bi = jxl.getBasicInfo();

  size_t pixelCount = bi.xsize * bi.ysize;

  JxlPixelFormat format = {
    .num_channels = 3,
    .data_type = JXL_TYPE_UINT8,
    .endianness = JXL_NATIVE_ENDIAN,
    .align = 0
  };

  size_t expect = (9 * 9) * 3 * 1;
  EXPECT_EQ(expect, pixelCount * format.num_channels * sizeof(uint8_t));
  EXPECT_EQ(jxl.getFrameBufferSize(0, format), expect);

  format.num_channels = 4;
  expect = (9 * 9) * 4 * 1;
  EXPECT_EQ(expect, pixelCount * format.num_channels * sizeof(uint8_t));
  EXPECT_EQ(jxl.getFrameBufferSize(0, format), expect);

  format.num_channels = 1;
  expect = (9 * 9) * 1 * 1;
  EXPECT_EQ(expect, pixelCount * format.num_channels * sizeof(uint8_t));
  EXPECT_EQ(jxl.getFrameBufferSize(0, format), expect);

  format.num_channels = 4;
  format.data_type = JXL_TYPE_FLOAT;
  expect = (9 * 9) * 4 * 4;
  EXPECT_EQ(expect, pixelCount * format.num_channels * sizeof(float));
  EXPECT_EQ(jxl.getFrameBufferSize(0, format), expect);

  format.endianness = JXL_BIG_ENDIAN;
  EXPECT_EQ(jxl.getFrameBufferSize(0, format), expect);
  format.endianness = JXL_LITTLE_ENDIAN;
  EXPECT_EQ(jxl.getFrameBufferSize(0, format), expect);

  format.align = 7;
  size_t rowStride = bi.xsize * format.num_channels * sizeof(float);
  size_t remain = rowStride % format.align;
  size_t padding = (remain != 0) ? format.align - remain : 0;
  rowStride += padding;
  EXPECT_EQ(rowStride, 147);
  expect = rowStride * bi.ysize - padding;
  EXPECT_EQ(expect, 1320);
  EXPECT_EQ(jxl.getFrameBufferSize(0, format), expect);
}

TEST(Decoder, GetsCorrectPixelsSingleFrame) {
  jxlazy::Decoder jxl;
  jxl.openFile(getPath("frame2 (+6+1).jxl").c_str());
  JxlPixelFormat format {
    .num_channels = 3,
    .data_type = JXL_TYPE_UINT8,
  };
  size_t size = 9 * 9 * format.num_channels * 1;
  auto pixels = make_unique<uint8_t[]>(size);
  jxl.getFramePixels(0, format, pixels.get(), size);
  static const char expect[] =
  "\0\0\0\003\003\003\a\a\a\f\f\f\021\021\021\025\025\025\032\032\032\037\037\037###\0\0\0"
  "K\004\004\306\r\r`\r\r\022\022\022\224\025\025\264\027\027K\037\037$$$\0\0\0\340\016"
  "\016\365\020\020\357\020\020\022\022\022\316\023\023\351\022\022\274\031\031%%%\0\0\0\323"
  "\016\016\244\r\r\244\017\017\023\023\023a\027\027\316\025\025\202\036\036%%%\0\0\0\005"
  "\005\005\n\n\n\016\016\016\024\024\024\031\031\031\035\035\035\"\"\"'''\002\002\002\016\006\011"
  "\022\v\016\031\017\023\034\023\027 \030\033'\032\037'!#(((\003\003\003\037\b\022\065\v\037"
  "\070\f\"\070\r\"\071\017#\070\022#\062\032$(((\003\003\003\a\a\a\f\f\f\022\022\022\026"
  "\026\026\033\033\033   $$$)))\004\004\004\b\b\b\r\r\r\022\022\022\026\026\026"
  "\033\033\033!!!%%%***";
  EXPECT_EQ(memcmp(pixels.get(), expect, size), 0);


  // Retry but request alpha channel from JXL with no alpha
  format.num_channels = 4;
  size = 9 * 9 * format.num_channels * 1;
  pixels = make_unique<uint8_t[]>(2 * size);
  uint8_t* out = pixels.get();
  size_t outCount = 0;
  for (size_t i = 0; i < sizeof expect - 1; ++i) {
    //cout << "out[" << (out-pixels.get()) << "] = expect[" << i << "]" << endl;
    *(out++) = expect[i];
    outCount++;
    if ((i+1)%3 == 0) {
      //cout << "out[" << (out-pixels.get()) << "] = UINT8_MAX" << endl;
      *(out++) = UINT8_MAX;
      outCount++;
    }
  }
  ASSERT_EQ(sizeof expect - 1, 9 * 9 * 3);
  ASSERT_EQ(size, 9 * 9 * 4);
  ASSERT_EQ(outCount, size);
  ASSERT_EQ(out, pixels.get() + size);
  jxl.getFramePixels(0, format, out, size);
  EXPECT_EQ(memcmp(out, pixels.get(), size), 0);
}



TEST(Decoder, GetsCorrectPixelsMultiLayer) {
  jxlazy::Decoder jxl;
  JxlPixelFormat format {
    .num_channels = 4,
    .data_type = JXL_TYPE_UINT8,
  };

  // Alternating between reading frame metadata and pixels in sequence.
  // Assuming this is the most common usage pattern.
  jxl.openFile(getPath("generated.jxl").c_str(), jxlazy::DecoderFlag::NoCoalesce);
  for (size_t i = 0; i < 3; ++i) {
    size_t size = jxl.getFrameBufferSize(i, format);
    auto pixels = make_unique<uint8_t[]>(size);
    jxl.getFramePixels(i, format, pixels.get(), size);
  }
#if 0
  // require a single pass.
  EXPECT_EQ(jxl.rewindCount_, 0);
#endif

  // Getting frame metadata only, in sequence.
  jxl.openFile(getPath("generated.jxl").c_str(), jxlazy::DecoderFlag::NoCoalesce);
  for (size_t i = 0; i < 3; ++i) {
    jxl.getFrameBufferSize(i, format);
  }
#if 0
  EXPECT_EQ(jxl.rewindCount_, 0);
#endif

  // Getting pixels only, in sequence.
  jxl.openFile(getPath("generated.jxl").c_str(), jxlazy::DecoderFlag::NoCoalesce);
  size_t size = 20 * 18 * 4;
  auto pixels = make_unique<uint8_t[]>(size);
  for (size_t i = 0; i < 3; ++i) {
    jxl.getFramePixels(i, format, pixels.get(), size);
  }
#if 0
  EXPECT_EQ(jxl.rewindCount_, 0);
#endif

  // TODO: Test how Box events can interfere with this!
}

TEST(Decoder, CanGetExtraChannelsOnly) {
  jxlazy::Decoder jxl;
  jxl.openFile(getPath("generated.jxl").c_str(), jxlazy::DecoderFlag::NoCoalesce);

  std::vector<jxlazy::ExtraChannelInfo> extraInfo = jxl.getExtraChannelInfo();
  EXPECT_EQ(extraInfo.size(), 2);

  JxlPixelFormat alphaFormat {
    .num_channels = 1,
    .data_type = JXL_TYPE_FLOAT,
    .endianness = JXL_NATIVE_ENDIAN,
    .align = 0,
  };
  std::vector<uint8_t> planarAlpha(jxl.getFrameBufferSize(0, alphaFormat));
  JxlPixelFormat depthFormat {
    .num_channels = 1,
    .data_type = JXL_TYPE_UINT8,
    .endianness = JXL_NATIVE_ENDIAN,
    .align = 64,
  };
  std::vector<uint8_t> planarDepth(jxl.getFrameBufferSize(0, depthFormat));

  std::vector<jxlazy::ExtraChannelRequest> extra{
    { 0, alphaFormat, planarAlpha.data(), planarAlpha.size() },
    { 1, depthFormat, planarDepth.data(), planarDepth.size() },
  };
  jxl.getFramePixels(0, {}, nullptr, 0, extra);

  jxlazy::Decoder expectDecoder;
  vector<uint8_t> expectDepth(planarDepth.size());
  expectDecoder.openFile(getPath("frame0_depthonly.jxl").c_str());
  expectDecoder.getFramePixels(0, depthFormat, expectDepth.data(), expectDepth.size());
  EXPECT_EQ(planarDepth, expectDepth);

  vector<uint8_t> expectAlpha(planarAlpha.size());
  expectDecoder.openFile(getPath("frame0_alphaonly.jxl").c_str());
  expectDecoder.getFramePixels(0, alphaFormat, expectAlpha.data(), expectAlpha.size());
  EXPECT_EQ(expectAlpha, planarAlpha);
}

TEST(Decoder, CanGetColorAndExtra) {
  jxlazy::Decoder jxl;
  jxl.openFile(getPath("generated.jxl").c_str(), jxlazy::DecoderFlag::NoCoalesce);

  std::vector<jxlazy::ExtraChannelInfo> extraInfo = jxl.getExtraChannelInfo();
  EXPECT_EQ(extraInfo.size(), 2);

  JxlPixelFormat alphaFormat {
    .num_channels = 1,
    .data_type = JXL_TYPE_UINT16,
    .endianness = JXL_NATIVE_ENDIAN,
    .align = 0,
  };
  std::vector<uint8_t> planarAlpha(jxl.getFrameBufferSize(0, alphaFormat));
  JxlPixelFormat depthFormat {
    .num_channels = 1,
    .data_type = JXL_TYPE_UINT8,
    .endianness = JXL_NATIVE_ENDIAN,
    .align = 0,
  };
  std::vector<uint8_t> planarDepth(jxl.getFrameBufferSize(0, depthFormat));

  std::vector<jxlazy::ExtraChannelRequest> extra{
    { 0, alphaFormat, planarAlpha.data(), planarAlpha.size() },
    { 1, depthFormat, planarDepth.data(), planarDepth.size() },
  };

  JxlPixelFormat colorFormat {
    .num_channels = 3,
    .data_type = JXL_TYPE_UINT8,
    .endianness = JXL_NATIVE_ENDIAN,
    .align = 0,
  };
  std::vector<uint8_t> interleavedColor(jxl.getFrameBufferSize(0, colorFormat));
  jxl.getFramePixels(0, colorFormat, interleavedColor.data(), interleavedColor.size(),
                     extra);

  jxlazy::Decoder expectDecoder;
  vector<uint8_t> expectDepth(planarDepth.size());
  expectDecoder.openFile(getPath("frame0_depthonly.jxl").c_str());
  expectDecoder.getFramePixels(0, depthFormat, expectDepth.data(), expectDepth.size());
  EXPECT_EQ(planarDepth, expectDepth);

  vector<uint8_t> expectAlpha(planarAlpha.size());
  expectDecoder.openFile(getPath("frame0_alphaonly.jxl").c_str());
  expectDecoder.getFramePixels(0, alphaFormat, expectAlpha.data(), expectAlpha.size());
  EXPECT_EQ(expectAlpha, planarAlpha);

  vector<uint8_t> expectColor(interleavedColor.size());
  expectDecoder.openFile(getPath("frame0.jxl").c_str());
  expectDecoder.getFramePixels(0, colorFormat, expectColor.data(), expectColor.size());
  EXPECT_EQ(expectColor, interleavedColor);
}

TEST(Decoder, GetsBoxes) {

  size_t boxCount;
  {
    jxlazy::Decoder jxl;
    jxl.openFile(getPath("generated.jxl").c_str());
    boxCount = jxl.boxCount();
    EXPECT_EQ(boxCount, 8);
    bool seenLazyBox = false;

    jxlazy::BoxInfo boxInfo;
    EXPECT_THROW(boxInfo = jxl.getBoxInfo(boxCount),
                 jxlazy::IndexOutOfRange);

    for (bool decompress : {false, true}) {
      for (size_t i = 0; i < boxCount; ++i) {
        boxInfo = jxl.getBoxInfo(i);
        vector<uint8_t> boxdata;
        EXPECT_TRUE(jxl.getBoxContent(i, &boxdata, SIZE_MAX, decompress));
        if (!decompress || !boxInfo.compressed) {
          EXPECT_EQ(boxdata.size(), boxInfo.size);
        }

        // First box should always be "JXL " with 4-byte payload
        if (i == 0) {
          EXPECT_EQ(boxdata.size(), 4);
          EXPECT_EQ(boxInfo.type, string("JXL "));
          EXPECT_EQ(memcmp(boxdata.data(), "\r\n\x87\n", 4), 0);
        } else if (memcmp(boxInfo.type, "Lazy", 4) == 0) {
          EXPECT_EQ(boxdata.size(), 627);
          EXPECT_EQ(memcmp(boxdata.data(), "This JPEG XL file", 17), 0);
          EXPECT_EQ(boxdata.back(), '\n');
          seenLazyBox = true;
        } else if (memcmp(boxInfo.type, "Lzip", 4) == 0) {
          EXPECT_TRUE(boxInfo.compressed);
          if (decompress) {
            EXPECT_EQ(boxdata.size(), 250);
            EXPECT_EQ(memcmp(boxdata.data(), "This box is brotli compressed", 29), 0);
          } else {
            EXPECT_EQ(boxdata.size(), 40);
            EXPECT_EQ(memcmp(boxdata.data(), "Lzip", 4), 0);
          }
        }
      }
      EXPECT_TRUE(seenLazyBox);
    }
#if 0
    EXPECT_EQ(jxl.rewindCount_, 2);
#endif
  }

  {
    // Repeat, but without the initial count, and access boxes in the wrong order.
    jxlazy::Decoder jxl;
    jxl.openFile(getPath("generated.jxl").c_str());

    vector<uint8_t> boxdata;
    for (size_t i = boxCount; i > 0; --i) {
      EXPECT_TRUE(jxl.getBoxContent(i-1, &boxdata, SIZE_MAX, false));
      // First box should always be "JXL " with 4-byte payload
      if (i-1 == 0) {
        EXPECT_EQ(boxdata.size(), 4);
      }
    }
#if 0
    EXPECT_EQ(jxl.rewindCount_, boxCount);
#endif

    // Limit size
    EXPECT_FALSE(jxl.getBoxContent(0, &boxdata, 2, false));
    EXPECT_EQ(boxdata.size(), 2);
    // Strictly speaking, the API doesn't promise to give 2 bytes here. Maybe
    // change to EXPECT_LE.
    EXPECT_EQ(memcmp(boxdata.data(), "\r\n", 2), 0);
  }
}

static const uint8_t JXL_ftyp[] = {  0,    0,    0,    0xc,  0x4a, 0x58, 0x4c, 0x20,
                                     0xd,  0xa,  0x87, 0xa,
                                     0,    0,    0,    0x14, 0x66, 0x74, 0x79, 0x70,
                                     0x6a, 0x78, 0x6c, 0x20, 0,    0,    0,    0,
                                     0x6a, 0x78, 0x6c, 0x20 };

TEST(Decoder, UnfinishedBox) {
  static const uint8_t emptBox[] = { 0, 0, 0, 0x8, 0x65, 0x6d, 0x70, 0x74 };
  static const uint8_t jxlpHeader[] = { 0, 0, 0, 0, 0x6a, 0x78, 0x6c, 0x70 };
  std::vector<uint8_t> boxes(sizeof JXL_ftyp + sizeof emptBox + sizeof jxlpHeader);
  memcpy(boxes.data(), JXL_ftyp, sizeof JXL_ftyp);
  memcpy(boxes.data() + sizeof JXL_ftyp, emptBox, sizeof emptBox);
  memcpy(boxes.data() + sizeof JXL_ftyp + sizeof emptBox, jxlpHeader, sizeof jxlpHeader);

  jxlazy::Decoder dec;
  dec.openMemory(boxes.data(), boxes.size(), 0, jxlazy::DecoderHint::WantBoxes);
  // Empty box
  jxlazy::BoxInfo emptInfo = dec.getBoxInfo(2);
  EXPECT_EQ(emptInfo.size, 0);
  EXPECT_FALSE(emptInfo.unbounded);
  // Implicitly-sized box
  jxlazy::BoxInfo jxlpInfo = dec.getBoxInfo(3);
  EXPECT_EQ(jxlpInfo.size, 0);
  EXPECT_TRUE(jxlpInfo.unbounded);

  // Truncate the header of the last box
  boxes.resize(boxes.size() - 1);
  dec.openMemory(boxes.data(), boxes.size(), 0, jxlazy::DecoderHint::WantBoxes);
  EXPECT_THROW(dec.getBoxInfo(3), jxlazy::JxlazyException);
}

TEST(Decoder, GetCodestreamLevel) {
  const uint32_t hints = (jxlazy::DecoderHint::NoPixels|jxlazy::DecoderHint::WantBoxes);
  jxlazy::Decoder jxl;

  // Container with jxll
  jxl.openFile(getPath("generated.jxl").c_str(), 0, hints);
  EXPECT_EQ(jxl.getCodestreamLevel(), 10);
  // Container without jxll
  jxl.openFile(getPath("jpeg.jpg.jxl").c_str(), 0, hints);
  EXPECT_EQ(jxl.getCodestreamLevel(), -1);
  // Container with jxll at the end
  jxl.openFile(getPath("frame0.jxl").c_str(), 0, hints);
  EXPECT_EQ(jxl.getCodestreamLevel(), 5);
  // Non-container
  jxl.openFile(getPath("orient.jxl").c_str(), 0, hints);
  EXPECT_EQ(jxl.getCodestreamLevel(), -1);
}


TEST(Decoder, FrameIterator) {
  jxlazy::Decoder jxl;

  // Simple iteration with syntatic sugar
  jxl.openFile(getPath("generated.jxl").c_str(), jxlazy::DecoderFlag::NoCoalesce);
  for (auto& frm : jxl) {
    cout << "a frame: [" << frm.name << "]" << endl;
  }
#if 0
  EXPECT_EQ(jxl.rewindCount_, 0);
#endif

  // Simple iteration with explicit types
  jxl.openFile(getPath("generated.jxl").c_str(), jxlazy::DecoderFlag::NoCoalesce);
  for (jxlazy::Decoder::FrameIterator fi = jxl.begin(); fi != jxl.end(); fi++) {
    cout << "a frame: [" << fi->name << "]" << endl;
  }
#if 0
  EXPECT_EQ(jxl.rewindCount_, 0);
#endif

  // Simple iteration initial count
  jxl.openFile(getPath("generated.jxl").c_str(), jxlazy::DecoderFlag::NoCoalesce);
  jxl.frameCount();
  for (auto frm : jxl) {
    cout << "a frame: [" << frm.name << "]" << endl;
  }
#if 0
  EXPECT_EQ(jxl.rewindCount_, 0);
#endif

  jxl.openFile(getPath("generated.jxl").c_str(), jxlazy::DecoderFlag::NoCoalesce);
  jxlazy::Decoder::FrameIterator fi = jxl.begin();
  fi++;
  cout << "Should be frame 1: " << fi->name << '/' << (*fi).name << endl;
  ++fi;
  EXPECT_NE(fi++, jxl.end());
  EXPECT_EQ(fi, jxl.end());
#if 0
  EXPECT_EQ(jxl.rewindCount_, 0);
#endif

  jxl.openFile(getPath("generated.jxl").c_str(), jxlazy::DecoderFlag::NoCoalesce);
  fi = jxl.begin();
  EXPECT_THROW(fi += -1, jxlazy::UsageError);
  fi += 2;
  EXPECT_EQ(++fi, jxl.end());
  EXPECT_EQ(fi, jxl.end());
  EXPECT_EQ(++fi, jxl.end());
  fi += 100;
  EXPECT_EQ(++fi, jxl.end());
#if 0
  EXPECT_EQ(jxl.rewindCount_, 0);
#endif
}


TEST(Decoder, Orientation) {
  jxlazy::Decoder jxl;
  jxl.openFile(getPath("orient.jxl").c_str(), 0);
  const JxlBasicInfo bi = jxl.getBasicInfo();
  EXPECT_EQ(bi.xsize, 2);
  EXPECT_EQ(bi.ysize, 1);

  jxl.openFile(getPath("orient.jxl").c_str(), jxlazy::DecoderFlag::KeepOrientation);
  const JxlBasicInfo bi2 = jxl.getBasicInfo();
  EXPECT_EQ(bi2.xsize, 1);
  EXPECT_EQ(bi2.ysize, 2);
}


TEST(Decoder, GetReconstructedJpegVector) {
  jxlazy::Decoder jxl;
  jxl.openFile(getPath("orient.jxl").c_str());
  EXPECT_FALSE(jxl.hasJpegReconstruction());

  jxl.openFile(getPath("jpeg.jpg.jxl").c_str());
  EXPECT_TRUE(jxl.hasJpegReconstruction());
  std::vector<uint8_t> jpegBytes;
  EXPECT_TRUE(jxl.getReconstructedJpeg(&jpegBytes));
  vector<uint8_t> original = loadFile(getPath("jpeg.jpg"));
  EXPECT_EQ(jpegBytes, original);

  // Restrict length to exactly the right length
  EXPECT_TRUE(jxl.getReconstructedJpeg(&jpegBytes, original.size()));
  EXPECT_EQ(jpegBytes, original);
  // Restrict length to less than the right length
  EXPECT_FALSE(jxl.getReconstructedJpeg(&jpegBytes, original.size()-1));
  // libjxl won't necessarily fill the buffer if it's too small, so
  // just check the size hasn't gone over
  EXPECT_LE(jpegBytes.size(), original.size()-1);
  original.resize(jpegBytes.size());
  EXPECT_EQ(jpegBytes, original);
}

TEST(Decoder, GetReconstructedJpegOstream) {
  vector<uint8_t> original = loadFile(getPath("jpeg.jpg"));

  jxlazy::Decoder jxl;
  jxl.openFile(getPath("jpeg.jpg.jxl").c_str());
  std::ostringstream jpegBytes;
  size_t jpegSize;
  EXPECT_TRUE(jxl.getReconstructedJpeg(jpegBytes, &jpegSize));
  EXPECT_EQ(jpegSize, original.size());
  EXPECT_EQ(memcmp(jpegBytes.str().data(), original.data(), original.size()), 0);
}

TEST(Decoder, UnpremultipliesAlpha) {
  jxlazy::Decoder jxl;
  jxl.openFile(getPath("premul.jxl").c_str());
  JxlBasicInfo info = jxl.getBasicInfo();
  EXPECT_EQ(info.alpha_premultiplied, JXL_TRUE);
  JxlPixelFormat format = {.num_channels = 2, .data_type = JXL_TYPE_UINT8 };
  uint8_t pixels[4];
  uint8_t expect_pixels[] = { 255, 255, 128, 128 };
  jxl.getFramePixels(0, format, pixels, sizeof pixels);
  EXPECT_EQ(memcmp(pixels, expect_pixels, sizeof pixels), 0);

  jxl.openFile(getPath("premul.jxl").c_str(), jxlazy::DecoderFlag::UnpremultiplyAlpha);
  info = jxl.getBasicInfo();
  // apparently the basic info still reports premultiplied even when we're unpremultiplying
  //EXPECT_EQ(basicInfo->alpha_premultiplied, JXL_FALSE);
  jxl.getFramePixels(0, format, pixels, sizeof pixels);
  expect_pixels[2] = 255;
  EXPECT_EQ(pixels[0], expect_pixels[0]);
  EXPECT_EQ(pixels[1], expect_pixels[1]);
  EXPECT_EQ(pixels[2], expect_pixels[2]);
  EXPECT_EQ(pixels[3], expect_pixels[3]);
  EXPECT_EQ(memcmp(pixels, expect_pixels, sizeof pixels), 0);
}

TEST(Decoder, GetIccProfile) {
  jxlazy::Decoder jxl;
  jxl.openFile(getPath("generated.jxl").c_str());
  size_t size = jxl.getIccProfileSize(JXL_COLOR_PROFILE_TARGET_DATA);
  EXPECT_EQ(size, 3144);
  std::vector<uint8_t> icc = jxl.getIccProfile(JXL_COLOR_PROFILE_TARGET_DATA);
  EXPECT_EQ(icc.size(), size);
  vector<uint8_t> icc2 = loadFile(getPath("snibgoGBR.icc"));
  EXPECT_EQ(icc, icc2);
}

TEST(Decoder, GetEncodedColorProfile) {
  jxlazy::Decoder jxl;
  JxlColorEncoding color;
  // Non-standard ICC profile
  jxl.openFile(getPath("generated.jxl").c_str());
  EXPECT_FALSE(jxl.getEncodedColorProfile(JXL_COLOR_PROFILE_TARGET_DATA, &color));
  jxl.openFile(getPath("frame0.jxl").c_str());
  EXPECT_TRUE(jxl.getEncodedColorProfile(JXL_COLOR_PROFILE_TARGET_DATA, &color));
  EXPECT_EQ(color.color_space, JXL_COLOR_SPACE_RGB);
  EXPECT_EQ(color.white_point, JXL_WHITE_POINT_D65);
  EXPECT_EQ(color.primaries, JXL_PRIMARIES_SRGB);
  EXPECT_EQ(color.transfer_function, JXL_TRANSFER_FUNCTION_SRGB);
  EXPECT_EQ(color.rendering_intent, JXL_RENDERING_INTENT_RELATIVE);
  memset(&color, 0, sizeof color);
  EXPECT_TRUE(jxl.getEncodedColorProfile(JXL_COLOR_PROFILE_TARGET_ORIGINAL, &color));
  EXPECT_EQ(color.color_space, JXL_COLOR_SPACE_RGB);
  EXPECT_EQ(color.white_point, JXL_WHITE_POINT_D65);
  EXPECT_EQ(color.primaries, JXL_PRIMARIES_SRGB);
  EXPECT_EQ(color.transfer_function, JXL_TRANSFER_FUNCTION_SRGB);
  EXPECT_EQ(color.rendering_intent, JXL_RENDERING_INTENT_RELATIVE);
}

TEST(Decoder, GetExtraChannelInfo) {
  jxlazy::Decoder jxl;
  jxl.openFile(getPath("generated.jxl").c_str());
  std::vector<jxlazy::ExtraChannelInfo> ecInfo = jxl.getExtraChannelInfo();
  EXPECT_EQ(ecInfo.size(), 2);
  const jxlazy::ExtraChannelInfo& alphaChannel = ecInfo[0];
  EXPECT_EQ(alphaChannel.info.type, JXL_CHANNEL_ALPHA);
  EXPECT_EQ(alphaChannel.info.alpha_premultiplied, JXL_FALSE);
  EXPECT_EQ(alphaChannel.info.name_length, 0);
  EXPECT_EQ(alphaChannel.info.bits_per_sample, 8);
  EXPECT_EQ(alphaChannel.info.exponent_bits_per_sample, 0);
  EXPECT_EQ(alphaChannel.name, std::string());
  const jxlazy::ExtraChannelInfo& depthChannel = ecInfo[1];
  EXPECT_EQ(depthChannel.info.type, JXL_CHANNEL_DEPTH);
  EXPECT_EQ(depthChannel.info.alpha_premultiplied, JXL_FALSE);
  EXPECT_EQ(depthChannel.info.name_length, 16);
  EXPECT_EQ(depthChannel.info.bits_per_sample, 8);
  EXPECT_EQ(depthChannel.info.exponent_bits_per_sample, 0);
  EXPECT_EQ(depthChannel.name, "My Depth Channel");
}

TEST(Decoder, SetPreferredOutputColorProfile) {
  std::vector<uint8_t> wantIcc = loadFile(getPath("SwappedRedAndGreen.icc"));
  jxlazy::Decoder jxl;

  jxl.openFile(getPath("generated.jxl").c_str());
  EXPECT_THROW(jxl.setPreferredOutputColorProfile(nullptr, nullptr, 0), jxlazy::UsageError);
  // As no CMS is set, can't ask for a specific ICC profile
  EXPECT_THROW(jxl.setPreferredOutputColorProfile(nullptr, wantIcc.data(),
                                                  wantIcc.size()), jxlazy::UsageError);
  JxlColorEncoding wantEncoding {.color_space = JXL_COLOR_SPACE_RGB,
                                 .white_point = JXL_WHITE_POINT_E,
                                 .primaries = JXL_PRIMARIES_P3,
                                 .transfer_function = JXL_TRANSFER_FUNCTION_GAMMA,
                                 .gamma = .6f,
                                 .rendering_intent = JXL_RENDERING_INTENT_SATURATION
                                };
  // Requesting an encoded profile for a non-XYB image with no CMS fails
  EXPECT_FALSE(jxl.setPreferredOutputColorProfile(&wantEncoding, nullptr, 0));

  jxl.openFile(getPath("lossy.jxl").c_str());
  // As no CMS is set, can't ask for a specific ICC profile
  EXPECT_THROW(jxl.setPreferredOutputColorProfile(nullptr, wantIcc.data(),
                                                  wantIcc.size()), jxlazy::UsageError);
  // Requesting an encoded profile for an XYB image should succeed
  EXPECT_TRUE(jxl.setPreferredOutputColorProfile(&wantEncoding, nullptr, 0));
  JxlColorEncoding gotEncoding;
  EXPECT_TRUE(jxl.getEncodedColorProfile(JXL_COLOR_PROFILE_TARGET_DATA, &gotEncoding));
  EXPECT_EQ(wantEncoding.color_space, gotEncoding.color_space);
  EXPECT_EQ(wantEncoding.primaries, gotEncoding.primaries);
  EXPECT_EQ(wantEncoding.transfer_function, gotEncoding.transfer_function);
  EXPECT_EQ(wantEncoding.white_point, gotEncoding.white_point);
  EXPECT_NEAR(wantEncoding.gamma, gotEncoding.gamma, 0.001);
  EXPECT_EQ(wantEncoding.rendering_intent, gotEncoding.rendering_intent);

  JxlPixelFormat format{.num_channels = 3, .data_type = JXL_TYPE_UINT8};
  std::vector<uint8_t> pixels(jxl.getFrameBufferSize(0, format));
  jxl.getFramePixels(0, format, pixels.data(), pixels.size());
  // Can't set preferred profile after getting pixels.
  EXPECT_THROW(jxl.setPreferredOutputColorProfile(&wantEncoding, nullptr,0), jxlazy::UsageError);
}

TEST(Decoder, SuggestPixelFormatStatic) {
  // Note, float16 isn't supported
  struct {
    uint32_t bitsPerSample;
    uint32_t exponentBitsPerSample;
    uint32_t numChannels;
    JxlDataType expectType;
  } tests[] = {
    { 1, 0, 1, JXL_TYPE_UINT8 },
    { 8, 0, 3, JXL_TYPE_UINT8 },
    { 9, 0, 4, JXL_TYPE_UINT16 },
    { 16, 0, 5, JXL_TYPE_UINT16 },
    { 17, 0, 5, JXL_TYPE_FLOAT },
    { 8, 2, 1, JXL_TYPE_FLOAT },
  };
  for (const auto& test : tests) {
    JxlPixelFormat result;
    jxlazy::Decoder::suggestPixelFormat(test.bitsPerSample, test.exponentBitsPerSample,
                                        test.numChannels, &result);
    EXPECT_EQ(result.num_channels, test.numChannels);
    EXPECT_EQ(result.data_type, test.expectType);
    EXPECT_EQ(result.endianness, JXL_NATIVE_ENDIAN);
    EXPECT_EQ(result.align, 0);
  }
}
