/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#include <cassert>
#include <cerrno>
#include <cinttypes>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "enums.h"
#include "except.h"
#include "pixmap.h"
#include "util.h"

using std::ifstream;
using std::istream;
using std::ofstream;
using std::ostream;
using std::pair;
using std::span;
using std::string;
using std::vector;

namespace jxltk {

namespace {

template<typename T>
size_t addInterleavedChannel(const T* inSamples, T* outSamples, size_t outMax, T init,
                             size_t inChannels, uint32_t align, uint32_t xsize,
                             uint32_t ysize, size_t index) {
  size_t inStride = sizeof(T) * inChannels * xsize;
  size_t remainder = inStride % align;
  if (remainder > 0) {
    inStride += align - remainder;
  }
  // TODO: Validate that alignment is OK for `T`, but in practice it's always 0 anyway.
  size_t outChannels = inChannels + 1;
  size_t outLastStride = sizeof(T) * outChannels * xsize;
  size_t outStride = outLastStride;
  remainder = outStride % align;
  if (remainder > 0) {
    outStride += align - remainder;
  }
  size_t required = (ysize - 1) * outStride + outLastStride;
  if (inSamples == nullptr && outSamples == nullptr && outMax == 0)
    return required;
  if (required > outMax)
    return 0;

  for (uint32_t y = 0; y < ysize; ++y) {
    const T* inRow = reinterpret_cast<const T*>(
        reinterpret_cast<const char*>(inSamples) + y*inStride);
    T* outRow = reinterpret_cast<T*>(
        reinterpret_cast<char*>(outSamples) + y*outStride);
    for (uint32_t x = 0; x < xsize; ++x) {
      const T* inPixel = inRow + x * inChannels;
      T* outPixel = outRow + x * outChannels;
      for (size_t c = 0; c < outChannels; ++c) {
        if (c == index) {
          *outPixel = init;
        } else {
          *outPixel = *(inPixel++);
        }
        ++outPixel;
      }
    }
  }
  return required;
}

size_t addInterleavedChannel(const void* pOldSamples, void* pNewSamples, size_t outMax,
                             float init, const JxlPixelFormat& format, uint32_t xsize,
                             uint32_t ysize, size_t index) {
  if (format.data_type == JXL_TYPE_UINT8) {
    return addInterleavedChannel(static_cast<const uint8_t*>(pOldSamples),
                                 static_cast<uint8_t*>(pNewSamples), outMax,
                                 static_cast<uint8_t>(roundf(init * 255.f)),
                                 format.num_channels, format.align,
                                 xsize, ysize, index);
  }
  if (format.data_type == JXL_TYPE_UINT16) {
    return addInterleavedChannel(static_cast<const uint16_t*>(pOldSamples),
                                 static_cast<uint16_t*>(pNewSamples), outMax,
                                 static_cast<uint16_t>(roundf(init * 65535.f)),
                                 format.num_channels, format.align,
                                 xsize, ysize, index);
  }
  if (format.data_type == JXL_TYPE_FLOAT) {
    return addInterleavedChannel(static_cast<const float*>(pOldSamples),
                                 static_cast<float*>(pNewSamples), outMax,
                                 init, format.num_channels, format.align,
                                 xsize, ysize, index);
  }
  throw JxltkError("Unsupported data type");
}

/**
 * Throw an exception if @p bytes is less than the minimum required buffer size.
 */
void validateSize(uint32_t xsize, uint32_t ysize, const JxlPixelFormat& format,
                  size_t bytes) {
  size_t minSize = jxlazy::Decoder::getFrameBufferSize(xsize, ysize, format);
  if (bytes < minSize) {
    throw JxltkError("Invalid buffer size: expected at least %zu, but only have %zu",
                     minSize, bytes);
  }
}

template<typename T>
bool isFullyOpaque(const T* inSamples, const JxlPixelFormat& format,
                   uint32_t xsize, uint32_t ysize, T fullOpacity) {
  if (format.num_channels != 2 && format.num_channels != 4) {
    return true;
  }
  size_t _;
  const T* row = inSamples;
  size_t stride = jxlazy::Decoder::getRowStride(xsize, format, &_);
  for (uint32_t y = 0; y < ysize; ++y) {
    const T* sample = row + format.num_channels - 1;
    for (uint32_t x = 0; x < xsize; ++x) {
      if (*sample != fullOpacity)
        return false;
      sample += format.num_channels;
    }
    row += stride;
  }
  return true;
}


}  // namespace


PixelPtr makePixelPtr(uint32_t xsize, uint32_t ysize, const JxlPixelFormat &format) {
  size_t size = jxlazy::Decoder::getFrameBufferSize(xsize, ysize, format);
  return {malloc(size), free};
}

/*static */Pixmap Pixmap::blackPixel(const JxlPixelFormat& format) {
  uint8_t zeros[4 * sizeof(float)] = {};
  return {1, 1, format, zeros, jxlazy::Decoder::getFrameBufferSize(1, 1, format)};
}

Pixmap::Pixmap(uint32_t xsize, uint32_t ysize, const JxlPixelFormat& format,
               const void* pixels, size_t size) {
  setPixelsCopy(xsize, ysize, format, pixels, size);
}

Pixmap::Pixmap(uint32_t xsize, uint32_t ysize, const JxlPixelFormat& format,
               PixelPtr&& pixels) {
  setPixelsMove(xsize, ysize, format, std::move(pixels));
}

Pixmap::Pixmap(std::string filename, size_t frameIdx, const JxlPixelFormat& format) {
  setPixelsFile(std::move(filename), frameIdx, format);
}

Pixmap::Pixmap(std::unique_ptr<jxlazy::Decoder>&& decoder, size_t frameIdx,
               const JxlPixelFormat& format) {
  setPixelsDecoder(std::move(decoder), frameIdx, format);
}

void Pixmap::close_() {
  pixels_.reset();
  xsize_ = 0;
  ysize_ = 0;
  pixelFormat_ = kDefaultPixelFormat;
  filename_.clear();
  decoderFrameIdx_ = 0;
}

void Pixmap::close() {
  close_();
  decoder_.reset();
}

std::unique_ptr<jxlazy::Decoder> Pixmap::releaseDecoder() {
  close_();
  return std::move(decoder_);
}

void Pixmap::setPixelsCopy(uint32_t xsize, uint32_t ysize, const JxlPixelFormat& format,
                          const void* pixels, size_t size) {
  validateSize(xsize, ysize, format, size);
  pixels_ = makePixelPtr(xsize, ysize, format);
  xsize_ = xsize;
  ysize_ = ysize;
  pixelFormat_ = format;
  filename_.clear();
  decoder_.reset();
  memcpy(pixels_.get(), pixels, size);
}

void Pixmap::setPixelsMove(uint32_t xsize, uint32_t ysize, const JxlPixelFormat& format,
                           PixelPtr&& pixels) {
  pixels_ = std::move(pixels);
  xsize_ = xsize;
  ysize_ = ysize;
  pixelFormat_ = format;
  filename_.clear();
  decoder_.reset();
}

void Pixmap::setPixelsFile(std::string filename, size_t frameIdx,
                           const JxlPixelFormat& format) {
  pixels_.reset();
  xsize_ = 0;
  ysize_ = 0;
  pixelFormat_ = format;
  filename_ = std::move(filename);
  decoder_ = std::make_unique<jxlazy::Decoder>();
  decoderFrameIdx_ = frameIdx;
}

void Pixmap::setPixelsDecoder(std::unique_ptr<jxlazy::Decoder>&& decoder,
                              size_t frameIdx, const JxlPixelFormat& format) {
  pixels_.reset();
  xsize_ = 0;
  ysize_ = 0;
  pixelFormat_ = format;
  filename_.clear();
  decoder_ = std::move(decoder);
  decoderFrameIdx_ = frameIdx;
}

bool Pixmap::addInterleavedAlpha() {
  // If nothing buffered yet, just set format to include alpha.
  if (!pixels_) {
    pixelFormat_.num_channels =
        (pixelFormat_.num_channels == 1 || pixelFormat_.num_channels == 3) ?
            pixelFormat_.num_channels + 1 : pixelFormat_.num_channels;
    return false;
  }
  // Already have alpha
  if (pixelFormat_.num_channels == 2 || pixelFormat_.num_channels == 4)
    return false;

  // Physically add alpha and update pixelFormat_
  ensureBuffered();
  JxlPixelFormat newFormat = pixelFormat_;
  ++newFormat.num_channels;
  size_t required = jxlazy::Decoder::getFrameBufferSize(xsize_, ysize_, newFormat);
  PixelPtr newPixels = makePixelPtr(xsize_, ysize_, newFormat);
  if (addInterleavedChannel(pixels_.get(), newPixels.get(), required, 1.f,
                            pixelFormat_, xsize_, ysize_, pixelFormat_.num_channels)
      != required) {
    throw JxltkError("Unexpected return value from addInterleavedChannel");
  }
  pixelFormat_ = newFormat;
  pixels_ = std::move(newPixels);
  return true;
}

/*static */bool Pixmap::isFullyOpaque(const void *pixels, uint32_t xsize, uint32_t ysize,
                                      const JxlPixelFormat &format) {
  // If format has no alpha channel, it's opaque
  if (format.num_channels == 1 || format.num_channels == 3) {
    return true;
  }

  if (format.data_type == JXL_TYPE_UINT8) {
    return jxltk::isFullyOpaque(reinterpret_cast<const uint8_t*>(pixels),
                                format, xsize, ysize, uint8_t{255});
  }
  if (format.data_type == JXL_TYPE_UINT16) {
    return jxltk::isFullyOpaque(reinterpret_cast<const uint16_t*>(pixels),
                                format, xsize, ysize, uint16_t{65535});
  }
  if (format.data_type == JXL_TYPE_FLOAT) {
    return jxltk::isFullyOpaque(reinterpret_cast<const float*>(pixels),
                                format, xsize, ysize, 1.f);
  }
  throw NotImplemented(
        "Checking for opacity for this data type (%s) is not implemented",
        dataTypeName(format.data_type));

}


bool Pixmap::isFullyOpaque() const {
  // If format has no alpha channel, it's opaque
  if (pixelFormat_.num_channels == 1 || pixelFormat_.num_channels == 3) {
    return true;
  }

  ensureBuffered();
  return Pixmap::isFullyOpaque(pixels_.get(), xsize_, ysize_, pixelFormat_);
}

void Pixmap::ensureDecoder_() const {
  if (!decoder_) {
    if (filename_.empty()) {
      throw JxltkError("No pixels buffered, and no file to read pixels from");
    }
    decoder_ = std::make_unique<jxlazy::Decoder>();
    decoder_->openFile(filename_.c_str());
  }
}

void Pixmap::ensureBuffered() const {
  if (pixels_)
    return;
  ensureDecoder_();
  jxlazy::FrameInfo frameInfo = decoder_->getFrameInfo(decoderFrameIdx_);
  xsize_ = frameInfo.header.layer_info.xsize;
  ysize_ = frameInfo.header.layer_info.ysize;
  pixels_ = makePixelPtr(xsize_, ysize_, pixelFormat_);
  size_t size = getBufferSize();
  decoder_->getFramePixels(decoderFrameIdx_, pixelFormat_, pixels_.get(), size);
}

const JxlPixelFormat& Pixmap::getPixelFormat() const {
  return pixelFormat_;
}

uint32_t Pixmap::getXsize() const {
  if (xsize_ > 0) {
    return xsize_;
  }
  ensureDecoder_();
  return xsize_ = decoder_->getFrameInfo(decoderFrameIdx_).header.layer_info.xsize;
}

uint32_t Pixmap::getYsize() const {
  if (ysize_ > 0) {
    return ysize_;
  }
  ensureDecoder_();
  return ysize_ = decoder_->getFrameInfo(decoderFrameIdx_).header.layer_info.ysize;
}

bool Pixmap::isEmpty() const {
  return xsize_ == 0 && !decoder_ && filename_.empty();
}

jxlazy::Decoder* Pixmap::getDecoder() const {
  if (decoder_) {
    return decoder_.get();
  }
  if (filename_.empty()) {
    return nullptr;
  }
  ensureDecoder_();
  return decoder_.get();
}

void* Pixmap::data() {
  ensureBuffered();
  return pixels_.get();
}
const void* Pixmap::data() const {
  ensureBuffered();
  return pixels_.get();
}

size_t Pixmap::getBufferSize() const {
  getXsize();
  getYsize();
  return jxlazy::Decoder::getFrameBufferSize(xsize_, ysize_, pixelFormat_);
}


PixelPtr&& Pixmap::releasePixels() {
  ensureBuffered();
  return std::move(pixels_);
}


ostream& operator<<(ostream& out, const Pixmap& p) {
  out << "Pixmap<size=" << p.getXsize() << "x" << p.getYsize();
  out << "; format=" << p.pixelFormat_;
  return out;
}

}  // namespace jxltk
