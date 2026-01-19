/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
/**
 * @file decoder.h
 * @brief Defines the Decoder class for reading JXL files.
 */
#ifndef JXLAZY_JXLDECODER_H_
#define JXLAZY_JXLDECODER_H_
#ifndef __cplusplus
#error "This is a C++ header"
#endif

/// \cond
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <jxl/decode_cxx.h>
#include <jxl/thread_parallel_runner_cxx.h>
/// \endcond

#include <jxlazy/exception.h>

namespace jxlazy {

struct BoxInfo {
  JxlBoxType type; // Always the decompressed type, not "brob"
  bool compressed; // True if this box was compressed in the codestream.

  /**
   * Size of the (possibly compressed) box content (exluding type/size box headers).
   *
   * For uncompressed boxes, or compressed boxes decoded with `decompress = false`,
   * this is the exact number of bytes required to store the output from `getBoxContent`.
   * It does NOT tell you the decompressed size of a compressed box.
   */
  uint64_t size;
};

struct FrameInfo {
  JxlFrameHeader header = {};
  std::string name{};
};

struct ExtraChannelInfo {
  JxlExtraChannelInfo info = {};
  // Optional name of this channel (UTF-8)
  std::string name{};
};

/**
 * A request for the pixels of an extra channel to be written to a target buffer,
 * used by Decoder::getFramePixels.
 */
struct ExtraChannelRequest {
  size_t channelIndex;
  JxlPixelFormat format; // num_channels is ignored
  void* target;
  size_t capacity; // max bytes to write to @c target
};

enum class StopAtIndex : uint8_t {
  None, All, Specific
};

/**
 * Hints for fine-tuning the behavior of the decoder.
 *
 * Hints are only hints - they don't prevent the decoder from doing anything, or allow
 * you to do anything you can't do anyway. They MAY slightly improve performance if
 * they're set accurately, and slightly hurt performance if they're lies, but there are
 * no guarantees.
 *
 * The one exception is when reading a non-seekable input file, where, for example,
 * setting `NoPixels` might actually prevent you from decoding any pixels should you
 * change your mind, depending on exactly what is requested and how much of the input
 * is buffered.
 */
enum DecoderHint : uint32_t {
  /**
   * Hint to the decoder that you want to access ISO BMFF-style boxes directly.
   *
   * Use this if you're planning to call any of the `boxCount`, `getBox*` methods.
   */
  WantBoxes = 0x1,

  /**
   * Hint to the decoder that you don't want to decode any frames to pixels.
   *
   * Use this if you're not planning to call `getFramePixels`.
   */
  NoPixels = 0x2,

  /**
   * Hint to the decoder that you're not going to read the image's color profile(s).
   *
   * Use this if you're not planning to call `getIccProfile` or `getEncodedProfile`.
   */
  NoColorProfile = 0x4,

  /**
   * Hint to the decoder that you're interested in reconstructing a transcoded JPEG.
   *
   * Use this if you're planning to call `getReconstructedJpeg` or
   * `haveJpegReconstruction`.
   */
  WantJpeg = 0x8
};

/**
 * Decoder options that can be set at the point of opening a JXL file.
 *
 * Unlike the `DecoderHint`s these have a significant effect on the decoded results.
 */
enum DecoderFlag : uint32_t {
  /**
   * Don't blend layers together into full-image-sized animation frames; decode individual
   * (possibly cropped) zero-duration layers if available.
   */
  NoCoalesce = 0x1,

  /**
   * Don't automatically correct the image orientation based on the JXL metadata.
   * By default, the orientation is automatically adjusted, and the reported orientation
   * is always reported as `JXL_ORIENT_IDENTITY`.
   */
  KeepOrientation = 0x2,

  /**
   * Automatically convert premultiplied (associated) alpha to straight alpha on decode.
   */
  UnpremultiplyAlpha = 0x4,
};


class Decoder {
public:

  static constexpr size_t kDefaultBufferKiB = size_t{64} * 1024; // 64MiB
  struct FrameIterator;

  /**
   * Create a new Decoder object that uses a threaded parallel runner.
   *
   * @param[in] numThreads Maximum number of worker threads to use for decoding. Pass 0 to
   * pick a sensible default based on the available CPUs.
   * @param[in] memManager Optional custom memory manager for libjxl - refer to libjxl
   * documentation for details. Note that jxlazy itself doesn't use this for its own
   * allocations.
   */
  Decoder(size_t numThreads, const JxlMemoryManager* memManager = nullptr) :
    Decoder(numThreads, memManager, nullptr, nullptr) {}

  /**
   * Create a new Decoder object.
   *
   * @param[in] parallelRunner,parallelRunnerOpaque Custom parallel runner implementation
   * for libjxl to use. See libjxl documentation.
   * @param[in] memManager Optional custom memory manager for libjxl - refer to libjxl
   * documentation for details. Note that jxlazy itself doesn't use this for its own
   * allocations.
   */
  Decoder(JxlParallelRunner parallelRunner, void* parallelRunnerOpaque,
          const JxlMemoryManager* memManager = nullptr) :
    Decoder(0, memManager, parallelRunner, parallelRunnerOpaque) {}

  /**
   * Create a new Decoder object with default settings.
   *
   * The default settings are: automatically choose how many threads to use; do not use
   * a custom memory manager.
   */
  Decoder() : Decoder(0, nullptr, nullptr, nullptr) {}

  Decoder(const Decoder&) = delete;
  Decoder& operator=(const Decoder&) = delete;
  Decoder(Decoder&&) noexcept = default;
  Decoder& operator=(Decoder&&) noexcept = default;
  virtual ~Decoder();

  /**
   * Open a JPEG XL image from the named file.
   *
   * It's safe to call this even if there's already a file open - in that case, all state
   * related to the previous file is discarded.
   *
   * @param[in] filename Name of an existing JXL file to read.
   * @param[in] flags Bitwise combination of decoder flags - @see DecoderFlag.
   * @param[in] hints Bitwise combination of decoder hints - @see DecoderHint.
   * @param[in] bufferKiB Maximum amount of the input file to store in memory at one time,
   * in KiB. `0` means use the default size. We won't necessarily allocate the full
   * amount. Note that this doesn't control the (normally much larger) amount of memory
   * used by libjxl. Set a memory manager in the constructor if you want some control over
   * that.
   */
  void openFile(const char* filename, uint32_t flags = 0, uint32_t hints = 0,
                size_t bufferKiB = kDefaultBufferKiB);

  /**
   * Open a JPEG XL image from an open input stream.
   *
   * It's safe to call this even if there's already a file open - in that case, all state
   * related to the previous file is discarded.
   *
   * Reading starts at the stream's current position.
   *
   * In some cases, we will need to do multiple passes over the input over the lifetime of
   * this object, so the stream must remain open and unmodified until this object is
   * destroyed or a different file is opened. The stream is released back to the caller
   * when this Decoder is destroyed or any of the following methods are called: `close`,
   * `openFile`, `openStream`, `openMemory`.
   *
   * If the stream isn't seekable, you may get an exception if the decoder needs to rewind
   * later. One way to avoid this is to set a large enough @p inBuffSize to fit the whole
   * file (or buffer it yourself and use `openMemory`). However, if you're careful to
   * access each frame once, in the correct sequence, no seeking is necessary :).
   * It may be impossible to access both frames and boxes without causing a rewind.
   *
   * The stream remains open when this object has finished with it, and its input position
   * is indeterminate. (In particular, if the stream continues after the end of the JXL
   * file, we may have read past the end.)
   *
   * @param[in] in Binary input stream to read JXL from.
   * @param[in] flags Bitwise combination of decoder flags - @see DecoderFlag.
   * @param[in] hints Bitwise combination of decoder hints - @see DecoderHint.
   * @param[in] bufferKiB Maximum amount of the input file to store in memory at one time,
   * in KiB. `0` means use the default size. We won't necessarily allocate the full
   * amount. Note that this doesn't control the (normally much larger) amount of memory
   * used by libjxl. Set a memory manager in the constructor if you want some control over
   * that.
   */
  void openStream(std::istream& in, uint32_t flags = 0, uint32_t hints = 0,
                  size_t bufferKiB = kDefaultBufferKiB);

  /**
   * Open a JPEG XL image from a fully-buffered file in memory.
   *
   * It's safe to call this even if there's already a file open - in that case, all state
   * related to the previous file is discarded.
   *
   * The caller owns the input buffer, which must remain valid and unmodified until this
   * Decoder is destroyed or any of the following methods are called: `close`, `openFile`,
   * `openStream`, `openMemory`.
   *
   * @param[in] mem Pointer to full JXL file in memory.
   * @param[in] size Number of bytes that make up the file.
   * @param[in] flags Bitwise combination of decoder flags - @see DecoderFlag.
   * @param[in] hints Bitwise combination of decoder hints - @see DecoderHint.
   */
  void openMemory(const uint8_t* mem, size_t size, uint32_t flags = 0,
                  uint32_t hints = 0);

  /**
   * Close the file that's currently being decoded, if any.
   *
   * Calling this is never required, and in fact it wastes time if the object is about to
   * be destroyed anyway, or is about to be reopened.
   *
   * If you do call it, it closes the input file handle (if it was opened via
   * `openFile`) and shrinks some internal data structures to minimise heap usage.
   */
  void close();

  /**
   * Get the JxlBasicInfo object for the open file.
   *
   * Throws ReadError on failure.
   */
  JxlBasicInfo getBasicInfo();

  /**
   * Return the width of the image in pixels.
   *
   * Convenience function, equivalent to `getBasicInfo().xsize`.
   */
  uint32_t xsize();

  /**
   * Return the height of the image in pixels.
   *
   * Convenience function, equivalent to `getBasicInfo().ysize`.
   */
  uint32_t ysize();

  /**
   * Return information about all "extra" channels that exist in the open file.
   *
   * This includes an entry for the main Alpha channel, if any.
   *
   * Throws ReadError on failure.
   */
  std::vector<ExtraChannelInfo> getExtraChannelInfo();

  /**
   * Set the preferred color profile for decoded pixels.
   *
   * This has identical semantics and restrictions to libjxl's
   * `JxlDecoderSetOutputColorProfile` - except you don't have to worry about the
   * decoder's current state. As long as no pixels have been decoded yet, it's fine.
   *
   * If colorEncoding is not nullptr, iccData and iccSize must be nullptr and 0,
   * respectively. If iccData is not nullptr, iccSize must be > 0 and colorEncoding must
   * be nullptr.
   * It's an error to call this after decoding any pixels.
   * It's an error to pass an ICC profile without first setting a CMS (which isn't yet
   * implemented in this API!).
   *
   * @param[in] colorEncoding Encoded color profile, or nullptr.
   * @param[in] icc ICC data, or nullptr.
   * @param[in] iccSize Length of ICC data, or 0.
   *
   * Throws UsageError if you request an ICC without setting a CMS first.
   * Throws UsageError if you request both ICC and an encoded profile, or neither.
   * Throws UsageError if you call this when you've already decoded at least one frame.
   *
   * @return true if the profile was set successfully, false if the profile
   * could not be set due to being invalid or unsupported for this image type.
   */
  bool setPreferredOutputColorProfile(const JxlColorEncoding* colorEncoding,
                                      const uint8_t* iccData, size_t iccSize);

  /**
   * Return the ICC profile for this image, if available, or an empty vector if not.
   *
   * @param[in] target `JXL_COLOR_PROFILE_TARGET_DATA` to get the ICC that applies to the
   * pixels that will be decoded through this object. `JXL_COLOR_PROFILE_TARGET_ORIGINAL`
   * to get the "original" ICC the image was tagged with on encoding.
   */
  std::vector<uint8_t> getIccProfile(JxlColorProfileTarget target);

  /**
   * Return the size in bytes of the image's ICC profile.
   *
   * @param[in] target `JXL_COLOR_PROFILE_TARGET_DATA` to get the ICC that applies to the
   * pixels that will be decoded through this object. `JXL_COLOR_PROFILE_TARGET_ORIGINAL`
   * to get the "original" ICC the image was tagged with on encoding.
   *
   * @return The size of the ICC profile in bytes, or 0 if no ICC profile is available.
   */
  size_t getIccProfileSize(JxlColorProfileTarget target);

  /**
   * Return the JXL encoded color profile for this image, if available.
   *
   * @param[in] target `JXL_COLOR_PROFILE_TARGET_DATA` to get the ICC that applies to the
   * pixels that will be decoded through this object. `JXL_COLOR_PROFILE_TARGET_ORIGINAL`
   * to get the "original" ICC the image was tagged with on encoding.
   * @param[out] colorEncoding Struct to write the result to.
   *
   * @return @c true if the color profile was available, else @p false.
   */
  bool getEncodedColorProfile(JxlColorProfileTarget target,
                              JxlColorEncoding* colorEncoding);

  /**
   * Get the total number of frames in this image.
   *
   * If coalescing is enabled, this is the number of animation frames.
   * If coalescing is disabled, the count includes all (non-internal) layers.
   *
   * Calling this function requires the decoder to "go past" all frames if it hasn't
   * already counted them on a previous pass. If you want to decode the pixels, it's
   * normally more efficient to use the frame iterator methods rather than counting
   * the frames in advance.
   */
  size_t frameCount();

  /**
   * Get the number of bytes required for each scanline of the given size and format.
   *
   * @param[in] xsize Horizontal size in pixels.
   * @param[in] format Channel count and alignment.
   * @param[out] rowPadding The number of bytes used for aligment padding (this is
   * included in the return value).  May be nullptr.
   * @return The size of a scanline in bytes, or 0 on arithmetic overflow.
   */
  static size_t getRowStride(uint32_t xsize, const JxlPixelFormat& format,
                             size_t* rowPadding);

  /**
   * Get the required buffer size (in bytes) for storing the specified pixels.
   *
   * @param[in] xsize,ysize Dimensions in pixels.
   * @param[in] pixelFormat Desired format for the pixels.
   * @return The number of bytes required to store the pixels.
   */
  static size_t getFrameBufferSize(uint32_t xsize, uint32_t ysize,
                                   const JxlPixelFormat& pixelFormat);
  /**
   * Get the number of bytes required to store all pixels of the specified frame.
   *
   * This is the minimum number you must provide to @ref getFramePixels when requesting
   * this frame with the same pixelFormat.
   *
   * @param[in] index Frame index - the first frame is 0.
   * @return The number of bytes required to store the frame's pixels.
  */
  size_t getFrameBufferSize(size_t index, const JxlPixelFormat& pixelFormat);

  /**
   * Suggest an appropriate pixel format for decoding frames from this JXL.
   *
   * The returned format always has `.endianness = JXL_NATIVE_ENDIAN' and '.align = 0'.
   * '.num_channels' and `.data_type` are chosen based on the image's Basic Info, with the
   * aim of using the smallest buffer possible while preserving the declared precision of
   * the input. Extra channels, other than the main alpha channel if present, are not
   * considered.
   *
   * NOTE: if the image contains samples outside the nominal range [0,1], we can't
   * detect that before decoding the pixels, and you might need to force the use of a
   * floating-point data type to avoid clamping.
   */
  void suggestPixelFormat(JxlPixelFormat* pixelFormat);

  /**
   * Suggest an appropriate pixel format for decoding the specified data.
   */
  static void suggestPixelFormat(uint32_t bitsPerSample, uint32_t exponentBitsPerSample,
                                 uint32_t numChannels, JxlPixelFormat* format);

  /**
   * Get information about the frame at index @p index.
   *
   * `FrameInfo` provides the frame's name (if any) and the `JxlFrameHeader` struct
   * describing its crop, blending, duration, etc.
   *
   * You can access frames in any order, but for maximum efficiency, they should be
   * accessed in sequence.
   *
   * Throws IndexOutOfRange if @p index >= `frameCount()`
   */
  FrameInfo getFrameInfo(size_t index);

  struct FrameIterator {
    using iterator_category = std::input_iterator_tag;
    using difference_type   = std::ptrdiff_t;
    using value_type        = FrameInfo;
    using pointer           = const value_type*;
    using reference         = const value_type&;

    FrameIterator(Decoder* parent);

    reference operator*() const { return parent_->frames_.at(index_); }
    pointer operator->() const { return &*(*this); }

    FrameIterator& operator++();
    FrameIterator operator++(int);
    FrameIterator& operator+=(difference_type count);
    FrameIterator operator+(difference_type count);

    friend bool operator==(const FrameIterator& a, const FrameIterator& b) {
      return a.index_ == b.index_;
    }
    friend bool operator!=(const FrameIterator& a, const FrameIterator& b) {
      return a.index_ != b.index_;
    }

    size_t getFrameBufferSize(const JxlPixelFormat& pixelFormat) {
      return parent_->getFrameBufferSize(index_, pixelFormat);
    }
    void getFramePixels(const JxlPixelFormat& pixelFormat, void* buffer, size_t max) {
      return parent_->getFramePixels(index_, pixelFormat, buffer, max);
    }


  private:
    void ensurePopulatedTo_(size_t);
    size_t index_; // Not using a pointer to the element as the vector data might move
    Decoder* parent_;
  };

  /**
   * Return a new forward iterator for the frames of this image.
   *
   * Dereferencing the iterator gives a `const FrameInfo&` providing the frame's metadata.
   * You can call `.getFramePixels` on the iterator object to extract the raw pixels.
   *
   * Before dereferencing the returned iterator, you should check whether it is equal to
   * `end()`.
   *
   * The returned iterator remains valid until this file is closed.
   */
  FrameIterator begin() {
    return {this};
  }
  /**
   * Return a FrameIterator that compares equal to an iterator that has been incremented
   * one position beyond the last frame. It's an error to attempt to dereference this.
   */
  FrameIterator end() {
    return {nullptr};
  }

  /**
   * Decode the pixels of the frame at position @p index.
   *
   * This can be used to get the main color channels, plus optional alpha, in an
   * interleaved format, and/or zero or more extra channels in planar format.
   *
   * "Main" channels:
   * The pixels are written to @p buffer, in the format described by @p pixelFormat.
   * `getFrameBufferSize` will return the number of bytes required to store the pixels,
   * which is the minimum acceptable value for @p capacity.
   *
   * @param[in] frameIndex The index of the frame to decode, the first frame being 0.
   * @param[in] pixelFormat Format used to output the main interleaved channels.
   * @param[out] buffer Target buffer for decoded pixels.  May be nullptr if you
   * don't want the main color channels to be decoded.
   * @param[in] capacity Maximum number of bytes to write to @p buffer.
   *
   * "Extra" channels:
   * In addition to, or instead of, the main color channels, zero or more extra channels
   * can be requested: each element of @p extraChannels identifies a channel index, a
   * pixel format, and a target buffer/capacity, analogous to the main color arguments
   * above. The data is always planar, and the pixel format's `num_channels` value is
   * ignored.
   * `getFrameBufferSize` can be used to determine the required capacity for each
   * extra channel buffer (as long as you ensure that your pixel format's `num_channels`
   * value is set to 1).
   *
   * Note that if @c coalesce is disabled, the dimensions of the frame may differ from
   * the dimensions of the image. It may be larger or smaller. With or without coalescing,
   * the frame's real dimensions can be found via `getFrameInfo(index).header.layer_info`.
   *
   * You can access frames in any order, but for maximum efficiency, they should be
   * accessed in their natural sequence.
   */
  void getFramePixels(size_t frameIndex, const JxlPixelFormat& pixelFormat,
                      void* buffer, size_t capacity,
                      const std::vector<ExtraChannelRequest>& extraChannels = {});

  /**
   * Return the number of Boxes available in this image's container.
   *
   * This includes metadata boxes and boxes that are reserved for the JXL container
   * format. If the input is a raw JPEG XL codestream with no container, this returns 0.
   *
   * TODO: make box iterator
   */
  size_t boxCount();

  /**
   * Get information about the box at index @p index.
   *
   * `BoxInfo` provides the box's 4-byte type and a flag indicating whether it is stored
   * brotli-compressed in the file. If `compressed` is true, the `type` always refers to
   * the decompressed inner box type, not the outer `brob`.
   *
   * You can access boxes in any order, but for maximum efficiency, they should be
   * accessed in sequence.
   *
   * @param[in] index Box index, starting at 0.
   *
   * @return @c true if the BoxInfo was retrieved, @c false if @p index was out of range.
   *
   * Throws IndexOutOfRange if @p index >= `boxCount()`
   */
  BoxInfo getBoxInfo(size_t index);

  /**
   * Get the content of a box.
   *
   * @param[in] index The index of the box to decode (< boxCount()).
   * @param[out] destination Buffer for the decoded contents.
   * @param[in] max Maximum number of bytes to write to @p destination.
   * @param[in] decompress If this box is compressed, `true` causes the content to be
   * automatically decompressed, giving you the payload of the inner box; and `false`
   * causes the raw, compressed `brob` payload to be output. If the box is not a `brob`
   * type, this argument is ignored.
   * @param[out] written The number of bytes actually written to @p destination.
   *
   * Note, when decompressing a `brob` box, there's no way to predict how much space will
   * be needed before choosing @p max, so you should check the return value to detect
   * truncated output. See also the other overloads of `getBoxContent` that return a
   * dynamically-sized buffer.
   *
   * If @p decompress is true and libjxl was built without Brotli support, this function
   * throws `NoBrotliError`.
   *
   * @return @c true if the box was fully decoded.
   * @return @c false if @p max was too small to decode the whole box; @p destination will
   * contain the first @p written bytes of decoded content.
   */
  bool getBoxContent(size_t index, uint8_t* destination, size_t max, size_t *written,
                     bool decompress = true);

  /**
   * Get the content of a box.
   *
   * As above, but the result is a dynamically-sized std::vector.
   *
   * @param[in] index The index of the box to decode (< boxCount()).
   * @param[out] destination vector whose contents will be replaced with the decoded box
   * content.
   * @param[in] max Optional limit on destination.size(), to avoid arbitrarily large
   * memory allocation in case of decompressed boxes. Pass -1 for no limit.
   * @param[in] decompress If this box is a `brob` type, `true` causes the content to be
   * automatically decompressed, giving you the payload of the inner box; and `false`
   * causes the raw, compressed `brob` payload to be output. If the box is not a `brob`
   * type, this argument is ignored.
   *
   * @return `true` if the box was fully decoded and its contents written to @p
   * destination.
   * @return `false` if the content required more than @p max bytes, so was truncated.
   * @p destination will contain the first `destination->size()` bytes of the box.
   */
  bool getBoxContent(size_t index, std::vector<uint8_t>* destination,
                     std::streamsize max = -1, bool decompress = true);

  /**
   * Return the declared codestream level, if any, or -1 if not.
   */
  int getCodestreamLevel();

  /**
   * Return true if this JXL contains JPEG reconstruction data.
   */
  bool hasJpegReconstruction();

  /**
   * Get reconstruted JPEG if available.
   *
   * @param[in,out] destination The bytes of the reconstructed JPEG are written to this
   * stream.
   * @param[out] jpegSize Number of bytes that were written to @p destination.
   *
   * @return @c true if a reconstructed JPEG was present and was fully
   * written to @p destination.
   * @return @c false if no JPEG reconstruction data exists.
   */
  bool getReconstructedJpeg(std::ostream& destination, size_t* jpegSize = nullptr);

  /**
   * Get reconstruted JPEG if available.
   *
   * @param[out] destination The contents of this vector are replaced by the bytes of the
   * reconstructed JPEG.
   * @param[in] max Maximum number of bytes to store, or -1 for no limit.

   * @return @c true if a reconstructed JPEG was present and was fully written to
   * @p destination.
   * @return @c false if no JPEG reconstruction data exists, or more than @p max bytes
   * were needed to store it. The contents of @p destination are indeterminate.
   */
  bool getReconstructedJpeg(std::vector<uint8_t>* destination, std::streamsize max = -1);

  /**
   * Return true iff all bytes of the input file are in memory, which means
   * you can access all image features in any sequence as many times as you like.
   */
  bool jxlIsFullyBuffered() const;

protected:
  Decoder(size_t numThreads, const JxlMemoryManager* memManager,
          JxlParallelRunner parallelRunner, void* parallelRunnerOpaque);

private:
  std::unique_ptr<std::ifstream> inStreamPrivate_{};
  std::istream* inStreamPtr_{nullptr}; // &*inStreamPrivate_ or user stream or nullptr
  std::istream::pos_type inStreamStart_{}; // position we'll rewind to if reading a stream
  std::vector<uint8_t> inBufferPrivate_{};
  const uint8_t* inBufferPtr_{nullptr}; // inBufferPrivate_.data() or user buffer
  size_t inBufferLength_{0};
  size_t inBufferCap_{0};
  size_t inBufferMax_{0};  // inBufferCap_ may grow to this limit
  size_t inBufferOffset_{0}; // Offset of inBufferPtr_[0] from start of file
  // Last input for JxlDecoderSetInput was inBufferPtr_ + inBufferDecOffset_
  size_t inBufferDecOffset_{0};
  JxlDecoderPtr dec_;
  JxlThreadParallelRunnerPtr pr_{nullptr}; // only used if we created our own
  JxlParallelRunner clientPr_; // only used if client passed their own
  void* parallelRunnerOpaque_;

  enum StateFlag : uint16_t {
    IsOpen       =      1 << 0,
    IsCoalescing =      1 << 1,
    GotBasicInfo =      1 << 2,
    GotColor =          1 << 3,
    GotOrigColorEnc =   1 << 4,
    GotDataColorEnc =   1 << 5,
    SeenAllBoxes =      1 << 6,
    SeenAllFrames =     1 << 7,
    SeenAllJpeg =       1 << 8,
    DecodedSomePixels = 1 << 9,
    WholeFileBuffered = 1 << 10,
    HaveCms =           1 << 11,
  };
  uint16_t stateFlags_{0};

  JxlBasicInfo basicInfo_{};

  std::vector<uint8_t> origIcc_{};
  JxlColorEncoding origColorEnc_{};
  std::vector<uint8_t> dataIcc_{};
  JxlColorEncoding dataColorEnc_{};

  int eventsSubbed_{0};
  JxlDecoderStatus status_{JXL_DEC_ERROR};

  std::vector<BoxInfo> boxes_{};
  size_t nextBoxIndex_{0};

  std::vector<FrameInfo> frames_{};
  size_t nextFrameIndex_{0}; // Updated immediately when we see JXL_DEC_FRAME

  size_t jpegCount_{0};
  size_t nextJpegIndex_{0};

  std::vector<ExtraChannelInfo> extra_{};

  void open_(uint32_t,uint32_t,size_t,bool,const uint8_t*);
  void close_(bool);
  JxlDecoderStatus processInput_(int,StopAtIndex=StopAtIndex::None,size_t=0,
                                 StopAtIndex=StopAtIndex::None,size_t=0,
                                 StopAtIndex=StopAtIndex::None,size_t=0);
  void checkOpen_() const;
  void ensureBasicInfo_();
  void ensureColor_(bool);
  void ensureExtraChannelInfo_();
  void rewind_(int);
  void goToFrame_(size_t);
  void goToBox_(size_t);
  void goToJpeg_(size_t);
};


}  // namespace jxlazy

#endif  // JXLAZY_JXLDECODER_H_
