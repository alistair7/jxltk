/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#ifndef JXLTK_PIXMAP_H_
#define JXLTK_PIXMAP_H_

#include <memory>
#include <ostream>

#include <jxl/encode.h>

#include "../contrib/jxlazy/include/jxlazy/decoder.h"

namespace jxltk {

constexpr JxlPixelFormat kDefaultPixelFormat = {
  .num_channels = 4,
  .data_type = JXL_TYPE_FLOAT,
  .endianness = JXL_NATIVE_ENDIAN,
  .align = 0,
};

using PixelPtr = std::unique_ptr<void, void(*)(void*)>;

/**
 * Allocate a buffer for pixels and return a std::unique_ptr to it.
 *
 * Memory is allocated using `malloc` and automatically deallocated later using `free`.
 */
PixelPtr makePixelPtr(uint32_t xsize, uint32_t ysize, const JxlPixelFormat& format);

/**
 * Wrapper for a rectangular array of pixels.
 *
 * The pixels can either be stored immediately in this object, or
 * loaded lazily from a file.  Currently only JXL files can be decoded.
 */
class Pixmap {

  friend std::ostream& operator<<(std::ostream& out, const Pixmap& r);

 public:

  /**
   * Return a Pixmap instance representing a 1x1 black frame.
   * If the format permits it, this will be fully transparent.
   */
  static Pixmap blackPixel(const JxlPixelFormat& format);

  /**
   * Construct a Pixmap with no pixels.
   */
  Pixmap() = default;

  /**
   * Construct a Pixmap, initialising its pixels from a buffer.
   * The @p pixels buffer is copied internally.
   */
  Pixmap(uint32_t xsize, uint32_t ysize, const JxlPixelFormat& format,
         const void* pixels, size_t size);

  /**
   * Construct a Pixmap, initialising its pixels from a buffer.
   * This object takes ownership of the buffer and will free it later using
   * the Deleter in the PixelPtr.
   *
   * See @ref makePixelPtr for a convenient way of instantiating a PixelPtr
   * to a newly-allocated buffer.
   */
  Pixmap(uint32_t xsize, uint32_t ysize, const JxlPixelFormat& format,
         PixelPtr&& pixels);

  /**
   * Construct a Pixmap, configured to read its pixels from the named file.
   *
   * The pixels won't be buffered until needed.
   *
   * @param[in] filename Path to the file to read (must be JXL)
   * @param[in] frameIdx Index of the (coalesced) frame to decode.
   */
  Pixmap(std::string filename, size_t frameIdx, const JxlPixelFormat& format);

  Pixmap(std::unique_ptr<jxlazy::Decoder>&& decoder, size_t frameIdx,
         const JxlPixelFormat& format);

  Pixmap(Pixmap&& move) = default;
  Pixmap& operator=(Pixmap&& move) = default;
  Pixmap(const Pixmap& other) = delete;
  Pixmap& operator=(const Pixmap& other) = delete;
  ~Pixmap() = default;

  /**
   * Reset to empty, as if newly default-constructed.
   */
  void close();

  /**
   * Identical to @ref close, but IF this object owns a Decoder,
   * transfer ownership to the caller instead of destroying it.
   * Otherwise, returns a null unique_ptr.
   */
  std::unique_ptr<jxlazy::Decoder> releaseDecoder();

  /**
   * Return the pixel format currently being used to store the pixels in this object.
   */
  const JxlPixelFormat& getPixelFormat() const;
  /**
   * Change the pixel format used to decode pixels.
   * This can only be called if pixels have not yet been decoded.
   */
  void setPixelFormat();

  uint32_t getXsize() const;
  uint32_t getYsize() const;
  /* True if the object isn't initialised */
  bool isEmpty() const;

  /**
   * Replace the current pixel buffer with a new one copied from @p pixels.
   * Any existing buffer / input source is forgotten.
   */
  void setPixelsCopy(uint32_t xsize, uint32_t ysize, const JxlPixelFormat& format,
                     const void* pixels, size_t size);

  /**
   * Replace the current pixel buffer with a new one using @p pixels.
   * Any existing buffer / input source is forgotten.
   */
  void setPixelsMove(uint32_t xsize, uint32_t ysize, const JxlPixelFormat& format,
                     PixelPtr&& pixels);

  void setPixelsFile(std::string filename, size_t frameIdx,
                     const JxlPixelFormat& format);

  void setPixelsDecoder(std::unique_ptr<jxlazy::Decoder>&& decoder, size_t frameIdx,
                        const JxlPixelFormat& format);

  /**
   * Add a fully-opaque alpha channel to this Pixmap, if it doesn't have alpha already.
   *
   * If pixels aren't buffered yet, calling this function ensures that an
   * alpha channel is included when they are.
   *
   * @return @c true if a new alpha channel was added and set to full opacity.
   * @return @c false if there was already an alpha channel (it has has NOT been
   * changed).
   */
  bool addInterleavedAlpha();

  /**
   * Remove the interleaved alpha channel, if it exists.
   *
   * If pixels aren't buffered yet, calling this function ensures that an
   * alpha channel is not included when they are.  This preference is reset
   * if you open a new input.
   */
  void removeInterleavedAlpha();

  static bool isFullyOpaque(const void* pixels, uint32_t xsize, uint32_t ysize,
                            const JxlPixelFormat& format);
  /**
   * Return true if the frame is fully opaque.
   */
  bool isFullyOpaque() const;

  /**
   * Get a reference to the internal data buffer.
   * The format of the data is given by getPixelFormat().
   */
  void* data();
  const void* data() const;

  /**
   * Return the size in bytes of the data buffer based on the currently
   * configured dimensions and pixel format.
   */
  size_t getBufferSize() const;

  /**
   * Make sure all pixels are in memory.
   */
  void ensureBuffered() const;

  /**
   * Try to buffer all pixels and return them as a PixelPtr, transferring
   * ownership to the caller.  If this object is getting pixels from a JXL file,
   * it can decode them again if you call a method that needs them.
   * If we only have raw pixels, this object can't be used again until you
   * call one of the `setPixels` methods.
   */
  PixelPtr&& releasePixels();

  /**
   * Break this class's abstraction and access the internal Decoder object,
   * which lets you query boxes, color profiles, etc.
   * Returns nullptr if pixels are not being decoded from a JXL.
   */
  jxlazy::Decoder* getDecoder() const;

 private:

  // Mutable to support lazy loading
  mutable PixelPtr pixels_{nullptr, free};
  JxlPixelFormat pixelFormat_{kDefaultPixelFormat};
  mutable uint32_t xsize_{0};
  mutable uint32_t ysize_{0};

  std::string filename_{};
  mutable std::unique_ptr<jxlazy::Decoder> decoder_{};
  size_t decoderFrameIdx_{0};

  void close_();
  /**
   * If possible, drop buffered pixels, keeping only frame
   * metadata.  Next time the actual data is needed, it will be
   * automatically decoded again.
   *
   * This saves memory, possibly at the expense of speed if you need the
   * data later.
   */
  void unbuffer_();
  void ensureDecoder_() const;
};

/**
 * Dump a brief description of the object.
 */
std::ostream& operator<<(std::ostream& out, const Pixmap& r);

}  // namespace jxltk

#endif  // JXLTK_PIXMAP_H_
