/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <vector>

#ifdef JXLAZY_DEBUG
#include <cstdarg>
#include <sstream>
#endif

#include <jxl/decode_cxx.h>
#include <jxl/thread_parallel_runner_cxx.h>

#include <jxlazy/exception.h>
#include <jxlazy/decoder.h>
#include "info.h"
#include "util.h"

// Wrappers for compiling against pre- or post- 0.9.0 API
#if JPEGXL_NUMERIC_VERSION < JPEGXL_COMPUTE_NUMERIC_VERSION(0,9,0)
#define JXLAZY_GET_ENCODED_PROFILE(dec, target, color_encoding) \
  JxlDecoderGetColorAsEncodedProfile((dec), nullptr, (target), (color_encoding))
#define JXLAZY_GET_ICC_PROFILE_SIZE(dec, target, size) \
  JxlDecoderGetICCProfileSize((dec), nullptr, (target), (size))
#define JXLAZY_GET_ICC_PROFILE(dec, target, icc_profile, size) \
  JxlDecoderGetColorAsICCProfile((dec), nullptr, (target), (icc_profile), (size))
#else
#define JXLAZY_GET_ENCODED_PROFILE  JxlDecoderGetColorAsEncodedProfile
#define JXLAZY_GET_ICC_PROFILE_SIZE JxlDecoderGetICCProfileSize
#define JXLAZY_GET_ICC_PROFILE      JxlDecoderGetColorAsICCProfile
#endif

// >= C++20
#if __cplusplus >= 202002L
#define JXLAZY_MAKE_UNIQUE_FOR_OVERWRITE std::make_unique_for_overwrite
#else
#define JXLAZY_MAKE_UNIQUE_FOR_OVERWRITE std::make_unique
#endif


namespace jxlazy {

using namespace std;

#ifdef JXLAZY_DEBUG_TRACE

/**
Print a message to stderr, tagged with the current location in the source file.

Only for debug builds.
*/
#define JXLAZY_DPRINTF(...) \
  ::jxlazy::debugPrintf(stderr, __FILE__, __func__, __LINE__, __VA_ARGS__)


void debugPrintf(FILE *to, const char *file, const char *func, unsigned line,
                 const char *format, ...) JXLAZY_FORMAT_PRINTF(5, 6);
void debugPrintf(FILE *to, const char *file, const char *func, unsigned line,
                 const char *format, ...)
{
  fprintf(to, "%s: in '%s':%u: ", file, func, line);
  va_list args;
  va_start(args, format);
  vfprintf(to, format, args);
  va_end(args);
  fputc('\n', to);
}

#else
#define JXLAZY_DPRINTF(...)
#endif

namespace {

constexpr size_t kMaxBufferBytes =
    std::min<uintmax_t>(std::numeric_limits<size_t>::max(),
                        std::numeric_limits<std::streamsize>::max());

constexpr size_t kDefaultChunkBytes = size_t{128} * 1024;

size_t bytesPerSample(JxlDataType dataType) {
  switch (dataType) {
  case JXL_TYPE_UINT8:  return 1;
  case JXL_TYPE_UINT16: return 2;
  case JXL_TYPE_FLOAT:  return 4;
  case JXL_TYPE_FLOAT16: return 2;
  }
  throw JxlazyException("Unknown data type \"%d\".", static_cast<int>(dataType));
}

template<typename T>
typename enable_if<is_unsigned<T>::value, bool>::type safeMul(T a, T b, T* product) {
  *product = a * b;
  return (a == 0 || a == 1 || b == 1 || *product / a == b);
}

}  // namespace

Decoder::Decoder(size_t numThreads/* = 0*/,
                 const JxlMemoryManager* memManager/* = nullptr*/,
                 JxlParallelRunner parallelRunner/* = nullptr*/,
                 void* parallelRunnerOpaque/* = nullptr*/) :
  dec_(JxlDecoderMake(memManager)),
  clientPr_(parallelRunner),
  parallelRunnerOpaque_(parallelRunnerOpaque)
{
  if (!dec_)
    throw LibraryError("Failed to create decoder.");

  if (!clientPr_ && numThreads != 1) {
    pr_ = JxlThreadParallelRunnerMake(memManager, numThreads > 0 ? numThreads : 
                                      JxlThreadParallelRunnerDefaultNumWorkerThreads());
    if (!pr_)
      throw LibraryError("Failed to create parallel runner (%zu threads).", numThreads);
  }
  // We call JxlDecoderReset on opening a file, so apply settings afterwards.
}

Decoder::~Decoder() {
  JXLAZY_DPRINTF("[%p] Destroyed.", static_cast<void*>(this));
}


/**
Code shared by several open functions.

Before calling this, the caller must:
- Call `close_(true)`.
- Set `inStreamPtr_` appropriately (to nullptr in the case of openMemory).
@param[in] bufferB Max buffer size to use for JXL bytes.  If `fromMemory`
                   != `nullptr`, this is the exact size of the existing buffer.
                   Not validated.
@param[in] allocateFull If true, allocate the full bufferB immediately,
                        otherwise, grow the buffer as needed, limited to bufferB.
*/
void Decoder::open_(uint32_t flags, uint32_t hints, size_t bufferB,
                    bool allocateFull, const uint8_t* fromMemory) {

  // Memory manager remains from construction, but must set the parallel runner again
  JxlDecoderStatus status = JXL_DEC_SUCCESS;
  if (clientPr_) {
    status = JxlDecoderSetParallelRunner(dec_.get(), clientPr_, parallelRunnerOpaque_);
  } else if (pr_) {
    status = JxlDecoderSetParallelRunner(dec_.get(), JxlThreadParallelRunner, pr_.get());
  }
  if (status != JXL_DEC_SUCCESS)
    throw LibraryError("Failed to set parallel runner.");

  if (!(flags & DecoderFlag::NoCoalesce)) {
    stateFlags_ |= StateFlag::IsCoalescing;
  }
  if (!(stateFlags_ & StateFlag::IsCoalescing) &&
      JxlDecoderSetCoalescing(dec_.get(), JXL_FALSE) != JXL_DEC_SUCCESS) {
    throw LibraryError("Failed to disable coalescing.");
  }
  if ((flags & DecoderFlag::KeepOrientation) &&
      JxlDecoderSetKeepOrientation(dec_.get(), JXL_TRUE) != JXL_DEC_SUCCESS) {
    throw LibraryError("Failed to set Keep Orientation flags.");
  }

  if ((flags & DecoderFlag::UnpremultiplyAlpha) &&
      JxlDecoderSetUnpremultiplyAlpha(dec_.get(), JXL_TRUE) != JXL_DEC_SUCCESS) {
    throw LibraryError("Failed to set Unpremultiply Alpha.");
  }

  // Set the size of the input buffer
  inBufferMax_ = bufferB;
  inBufferCap_ = allocateFull ? bufferB : std::min(bufferB, kDefaultChunkBytes);
  if (fromMemory) {
    inBufferLength_ = bufferB;
    inBufferPtr_ = fromMemory;
    stateFlags_ |= StateFlag::WholeFileBuffered;
  } else {
    inBufferPrivate_.resize(inBufferCap_);
    uint8_t* inBufferMutable = inBufferPrivate_.data();
    inBufferPtr_ = inBufferMutable;
    if (inStreamPtr_->read(reinterpret_cast<char*>(inBufferMutable),
                           static_cast<streamsize>(inBufferCap_)).bad()) {
      throw ReadError("Failed to read from input.");
    }
    inBufferLength_ = inStreamPtr_->gcount();
    inStreamPtr_->peek(); // ensure eofbit is set if we read the exact size.
    if (inStreamPtr_->eof()) {
      stateFlags_ |= StateFlag::WholeFileBuffered;
    }
    JXLAZY_DPRINTF("[%p] Read %zu bytes from disk%s.", static_cast<void*>(this),
                   inBufferLength_, wholeFileBuffered_ ? " (whole file)" : "");
  }

  // Verify JXL signature
  JxlSignature sig = JxlSignatureCheck(inBufferPtr_, inBufferLength_);
  if (sig != JXL_SIG_CODESTREAM && sig != JXL_SIG_CONTAINER) {
    static const uint8_t pngMagic[] = { 0x89, 0x50, 0x4e, 0x47,
                                        0x0d, 0x0a, 0x1a, 0x0a };
    if(inBufferLength_ >= sizeof pngMagic &&
       memcmp(inBufferPtr_, pngMagic, sizeof pngMagic) == 0) {
      throw ReadError("This is a PNG - convert inputs to JXL first.");
    }
    throw ReadError("Input is not a JXL file");
  }
  if (sig == JXL_SIG_CODESTREAM) {
    // There won't be any boxes
    stateFlags_ |= StateFlag::SeenAllBoxes;
  }

  if (JxlDecoderSetInput(dec_.get(), inBufferPtr_, inBufferLength_) != JXL_DEC_SUCCESS)
    throw ReadError("Failed to set first %zu bytes of input", inBufferLength_);

  if ((stateFlags_ & StateFlag::WholeFileBuffered)) {
    JxlDecoderCloseInput(dec_.get());
    // If the ifstream belongs to us, close it
    inStreamPrivate_.reset();
  }
  stateFlags_ |= StateFlag::IsOpen;

  int eventsWanted = JXL_DEC_BASIC_INFO | JXL_DEC_FRAME |
                     ((hints & DecoderHint::WantBoxes) ? JXL_DEC_BOX : 0) |
                     ((hints & DecoderHint::NoPixels) ? 0 : JXL_DEC_FULL_IMAGE) |
                     ((hints & DecoderHint::WantJpeg) ?
                                (JXL_DEC_JPEG_RECONSTRUCTION|JXL_DEC_FULL_IMAGE) : 0) |
                     ((hints & DecoderHint::NoColorProfile) ? 0 :
                                                               JXL_DEC_COLOR_ENCODING);
#ifdef JXLAZY_DEBUG
  ostringstream oss;
  printDecoderEventNames(oss, eventsWanted);
  JXLAZY_DPRINTF("Subscribing to (%s).", oss.str().c_str());
#endif
  if (JxlDecoderSubscribeEvents(dec_.get(), eventsWanted) != JXL_DEC_SUCCESS)
    throw LibraryError("Failed to subscribe to decoder events");
  eventsSubbed_ = eventsWanted;
}

void Decoder::openStream(istream& stream, uint32_t flags, uint32_t hints,
                         size_t bufferKiB) {
  // Close and reset (almost) everything
  close_(true);
  inStreamPtr_ = &stream;
  inStreamStart_ = stream.tellg();
  inStreamPrivate_.reset();

  bufferKiB = bufferKiB > 0 ? bufferKiB : kDefaultBufferKiB;
  size_t bufferB;
  if (!safeMul(bufferKiB, size_t{1024}, &bufferB)) {
    bufferB = SIZE_MAX;
  }
  bufferB = std::min<uintmax_t>(bufferB, kMaxBufferBytes);

  open_(flags, hints, bufferB, /*allocateFull=*/false, nullptr);
}

void Decoder::openMemory(const uint8_t* mem, size_t size, uint32_t flags,
                         uint32_t hints) {
  close_(true);
  inStreamPrivate_.reset();
  inStreamPtr_ = nullptr;
  open_(flags, hints, size, false, mem);
  inBufferPrivate_.shrink_to_fit();
}

void Decoder::openFile(const char* filename, uint32_t flags, uint32_t hints,
                       size_t bufferKiB) {
  // Close and reset (almost) everything
  close_(true);

  if (!inStreamPrivate_) {
    inStreamPrivate_ = std::make_unique<std::ifstream>(filename, ios::binary);
  } else {
    inStreamPrivate_->open(filename, ios::binary);
  }
  if (!inStreamPrivate_->good())
    throw ReadError("Can't open %s for reading.", filename);
  inStreamPtr_ = &*inStreamPrivate_;

  bufferKiB = bufferKiB > 0 ? bufferKiB : kDefaultBufferKiB;
  size_t bufferB;
  if (!safeMul(bufferKiB, size_t{1024}, &bufferB)) {
    bufferB = SIZE_MAX;
  }
  bool allocateFull = false;
  size_t fileSizeB = getFileSize(filename);
  if (fileSizeB > 0) {
    allocateFull = true;
    if (fileSizeB < bufferB) {
      bufferB = fileSizeB;
    }
  }
  bufferB = std::min<uintmax_t>(bufferB, kMaxBufferBytes);

  open_(flags, hints, bufferB, allocateFull, nullptr);
}

void Decoder::close_(bool reopening) {
  stateFlags_ = 0;
  inBufferLength_ = 0;
  inBufferOffset_ = 0;
  inBufferDecOffset_ = 0;
  inBufferPrivate_.clear();
  inBufferPtr_ = nullptr;
  JxlDecoderReset(dec_.get());
  eventsSubbed_ = 0;
  status_ = JXL_DEC_ERROR;
  boxes_.clear();
  nextBoxIndex_ = 0;
  frames_.clear();
  nextFrameIndex_ = 0;
  jpegCount_ = 0;
  nextJpegIndex_ = 0;
  extra_.clear();
  if (reopening) {
    if (inStreamPrivate_) {
      inStreamPrivate_->close();
    }
  } else {
    inBufferPrivate_.shrink_to_fit();
    inStreamPrivate_.reset();
    inStreamPtr_ = nullptr;
    origIcc_.clear();
    origIcc_.shrink_to_fit();
    dataIcc_.clear();
    dataIcc_.shrink_to_fit();
    boxes_.shrink_to_fit();
    frames_.shrink_to_fit();
    extra_.shrink_to_fit();
  }
}


void Decoder::close() {
  close_(false);
}

void Decoder::checkOpen_() const {
  if (!(stateFlags_ & StateFlag::IsOpen)) {
    throw UsageError("No file open.");
  }
}

void Decoder::ensureBasicInfo_() {
  checkOpen_();
  if (!(stateFlags_ & StateFlag::GotBasicInfo)) {
    /* JXL_DEC_BASIC_INFO is not always the first event - we can get BOX and
       JPEG_RECONSTRUCTION first. */
    JxlDecoderStatus status = processInput_(JXL_DEC_BASIC_INFO, StopAtIndex::None, 0,
                                            StopAtIndex::None, 0);
    if (status != JXL_DEC_BASIC_INFO) {
      throw ReadError("Unexpected status: %s; expected JXL_DEC_BASIC_INFO",
                      decoderEventName(status));
    }
  }
}

JxlBasicInfo Decoder::getBasicInfo() {
  ensureBasicInfo_();
  return basicInfo_;
}

uint32_t Decoder::xsize() {
  ensureBasicInfo_();
  return basicInfo_.xsize;
}

uint32_t Decoder::ysize() {
  ensureBasicInfo_();
  return basicInfo_.ysize;
}

void Decoder::ensureExtraChannelInfo_() {
  ensureBasicInfo_();
  // Get extra channel info if missing
  if (extra_.size() < basicInfo_.num_extra_channels) {
    extra_.resize(basicInfo_.num_extra_channels);
    for (uint32_t i = 0; i < basicInfo_.num_extra_channels; ++i) {
      JxlExtraChannelInfo& info = extra_[i].info;
      if (JxlDecoderGetExtraChannelInfo(dec_.get(), i, &info) != JXL_DEC_SUCCESS) {
        throw ReadError("Failed to get info for extra channel %" PRIu32, i);
      }
      if (info.name_length > 0) {
        auto tmpName = JXLAZY_MAKE_UNIQUE_FOR_OVERWRITE<char[]>(info.name_length + 1);
        if (JxlDecoderGetExtraChannelName(dec_.get(), i, tmpName.get(),
                                          info.name_length + 1) != JXL_DEC_SUCCESS) {
          throw ReadError("Can't get name for extra channel %" PRIu32 ".", i);
        }
        extra_[i].name = tmpName.get();
      }
    }
  }
}

vector<ExtraChannelInfo> Decoder::getExtraChannelInfo() {
  ensureExtraChannelInfo_();
  return extra_;
}

bool Decoder::setPreferredOutputColorProfile(const JxlColorEncoding* colorEncoding,
                                             const uint8_t *iccData, size_t iccSize) {
  if ((stateFlags_ & StateFlag::DecodedSomePixels)) {
    throw UsageError("Can't set a color profile after decoding has started");
  }
  // Currently we have no way of setting a CMS
  if (iccData && !(stateFlags_ & StateFlag::HaveCms)) {
    throw UsageError("Can't request an ICC profile without setting a CMS");
  }
  if (!colorEncoding && !iccData) {
    throw UsageError("No color profile provided");
  }
  bool result = true;
  stateFlags_ &= ~StateFlag::GotDataColorEnc;
  dataIcc_.clear();
  ensureColor_(true);
  if (JxlDecoderSetOutputColorProfile(dec_.get(), colorEncoding, iccData, iccSize)
      != JXL_DEC_SUCCESS) {
    result = false;
  }
  /*
  if no cms is set:
    - requesting an ICC causes JxlDecoderSetOutputColorProfile to return an error.
    - requesting an encoded profile for a non-XYB-encoded file fails silently.
  */

  // Refetch data profile
  if (JXLAZY_GET_ENCODED_PROFILE(dec_.get(), JXL_COLOR_PROFILE_TARGET_DATA,
                                 &dataColorEnc_) == JXL_DEC_SUCCESS) {
    stateFlags_ |= StateFlag::GotDataColorEnc;
  } else if (colorEncoding) {
    result = false;
  }
  JxlDecoderGetICCProfileSize(dec_.get(), JXL_COLOR_PROFILE_TARGET_DATA, &iccSize);
  dataIcc_.resize(iccSize);
  if (iccSize > 0 && JXLAZY_GET_ICC_PROFILE(dec_.get(),
                                            JXL_COLOR_PROFILE_TARGET_DATA,
                                            dataIcc_.data(),
                                            dataIcc_.size()) != JXL_DEC_SUCCESS) {
    throw ReadError("Unexpected failure while checking output ICC");
  }
  if (iccData && dataIcc_.empty()) {
    result = false;
  }
  if (!(stateFlags_ & StateFlag::GotDataColorEnc) && dataIcc_.empty()) {
    throw ReadError("Unexpected failure while checking output encoded color profile");
  }
  return result;
}

/**
Populate ICC and encoded profiles or throw ReadError.
@param[in] goThereNow If true, go exactly to the JXL_DEC_COLOR_ENCODING event.
                      If false, it's fine if we've gone past it.
*/
void Decoder::ensureColor_(bool goThereNow) {
  checkOpen_();
  if (status_ == JXL_DEC_COLOR_ENCODING) {
    return;
  }
  bool pastColor = (stateFlags_ & StateFlag::GotColor) != 0;

  if (goThereNow || !pastColor) {
    if ((goThereNow && pastColor) || !(eventsSubbed_ & JXL_DEC_COLOR_ENCODING))
      rewind_(eventsSubbed_ | JXL_DEC_COLOR_ENCODING);
    // This populates dataIcc_ and origIcc_ if they're available.
    if (processInput_(JXL_DEC_COLOR_ENCODING, StopAtIndex::None, 0, StopAtIndex::None, 0)
        != JXL_DEC_COLOR_ENCODING) {
      stateFlags_ &= ~(StateFlag::GotOrigColorEnc | StateFlag::GotDataColorEnc);
      throw ReadError("No color encoding returned from decoder");
    }
    stateFlags_ |= StateFlag::GotColor;
  }
}

std::vector<uint8_t> Decoder::getIccProfile(JxlColorProfileTarget target) {
  JXLAZY_DPRINTF("[%p] %s", static_cast<void*>(this), colorProfileTargetName(target));
  ensureColor_(false);
  return (target == JXL_COLOR_PROFILE_TARGET_DATA) ? dataIcc_ : origIcc_;
}

size_t Decoder::getIccProfileSize(JxlColorProfileTarget target) {
  ensureColor_(false);
  return (target == JXL_COLOR_PROFILE_TARGET_DATA) ?
             dataIcc_.size() : origIcc_.size();
}

bool Decoder::getEncodedColorProfile(JxlColorProfileTarget target,
                                     JxlColorEncoding* colorEncoding) {
  ensureColor_(false);
  if (target == JXL_COLOR_PROFILE_TARGET_DATA) {
    if (!(stateFlags_ & StateFlag::GotDataColorEnc)) {
      return false;
    }
    *colorEncoding = dataColorEnc_;
  } else {
    if (!(stateFlags_ & StateFlag::GotOrigColorEnc)) {
      return false;
    }
    *colorEncoding = origColorEnc_;
  }
  return true;
}

size_t Decoder::frameCount() {
  JXLAZY_DPRINTF("[%p]", static_cast<void*>(this));
  if ((stateFlags_ & StateFlag::SeenAllFrames)) {
    return frames_.size();
  }
  if ((stateFlags_ & StateFlag::GotBasicInfo) &&
      (stateFlags_ & StateFlag::IsCoalescing) && !basicInfo_.have_animation) {
    return 1;
  }

  checkOpen_();
  if (!(eventsSubbed_ & JXL_DEC_FRAME)) {
    rewind_(eventsSubbed_ | JXL_DEC_FRAME);
  }

  // Fast forward to the last known frame
  if (nextFrameIndex_ < frames_.size()) {
    JXLAZY_DPRINTF("[%p] Skip %zu frames to frame %zu.", static_cast<void*>(this),
                   frames_.size() - nextFrameIndex_, frames_.size());
    JxlDecoderSkipFrames(dec_.get(), frames_.size() - nextFrameIndex_);
    nextFrameIndex_ = frames_.size();
  }
  // Scan all remaining frame headers
  processInput_(0, /*stopAtFrame=*/StopAtIndex::All, 0, StopAtIndex::None, 0);
  return frames_.size();
}

void Decoder::rewind_(int resubscribeTo) {
  JxlDecoder* dec = dec_.get();
  JxlDecoderRewind(dec);
#ifdef JXLAZY_DEBUG
  ostringstream oss;
  printDecoderEventNames(oss, resubscribeTo);
  JXLAZY_DPRINTF("[%p] Subscribing to (%s).", static_cast<void*>(this),
                 oss.str().c_str());
#endif
  if (JxlDecoderSubscribeEvents(dec, resubscribeTo) != JXL_DEC_SUCCESS)
    throw ReadError("Failed to resubscribe events after rewind.");
  eventsSubbed_ = resubscribeTo;
  nextFrameIndex_ = 0;
  nextBoxIndex_ = 0;
  nextJpegIndex_ = 0;
  status_ = JXL_DEC_ERROR;
  if (inBufferOffset_ == 0) {
    // Start of file is already buffered.  Simulate what openFile does with this first chunk
    inBufferDecOffset_ = 0;
    if (JxlDecoderSetInput(dec, inBufferPtr_, inBufferLength_) != JXL_DEC_SUCCESS) {
      throw ReadError("Failed to set first %zu bytes of input after rewind",
                      inBufferLength_);
    }
    if (stateFlags_ & StateFlag::WholeFileBuffered) {
      JxlDecoderCloseInput(dec);
    }
  } else {
    inBufferLength_ = 0;
    inBufferOffset_= 0;
    inBufferDecOffset_ = 0;
    inStreamPtr_->clear();
    inStreamPtr_->seekg(inStreamStart_);
    if (!inStreamPtr_->good()) {
      throw ReadError("Input is not seekable - can't read image features out of sequence.");
    }
  }
}

/*static*/size_t Decoder::getRowStride(uint32_t xsize, const JxlPixelFormat& format,
                                       size_t* rowPadding) {
  size_t bytesPerPixel;
  if (!safeMul<size_t>(bytesPerSample(format.data_type), format.num_channels,
                       &bytesPerPixel)) {
    return 0;
  }
  size_t bytesPerRow;
  if (!safeMul<size_t>(bytesPerPixel, xsize, &bytesPerRow)) {
    return 0;
  }
  size_t lrowPadding = 0;
  if (format.align > 1) {
    size_t remainder = bytesPerRow % format.align;
    lrowPadding = remainder > 0 ? (format.align - remainder) : 0;
  }
  if (rowPadding) {
    *rowPadding = lrowPadding;
  }
  return bytesPerRow + lrowPadding;
}


/*static*/size_t Decoder::getFrameBufferSize(uint32_t xsize, uint32_t ysize,
                                             const JxlPixelFormat& pixelFormat) {
  size_t rowPadding;
  size_t stride = Decoder::getRowStride(xsize, pixelFormat, &rowPadding);
  if (stride == 0) {
    throw JxlazyException("Buffer memory requirement is too large.");
  }

  size_t requiredBytes;
  if (!safeMul(stride, static_cast<size_t>(ysize), &requiredBytes)) {
    throw JxlazyException("Buffer memory requirement is too large.");
  }
  // Don't insist on padding the last row
  return requiredBytes - rowPadding;
}

size_t Decoder::getFrameBufferSize(size_t index, const JxlPixelFormat& pixelFormat) {
  const JxlLayerInfo& li = getFrameInfo(index).header.layer_info;
  return Decoder::getFrameBufferSize(li.xsize, li.ysize, pixelFormat);
}

void Decoder::suggestPixelFormat(uint32_t bitsPerSample, uint32_t exponentBitsPerSample,
                                 uint32_t numChannels, JxlPixelFormat* format) {
  format->data_type = (exponentBitsPerSample > 0 || bitsPerSample > 16) ? JXL_TYPE_FLOAT :
                      bitsPerSample > 8 ? JXL_TYPE_UINT16 : JXL_TYPE_UINT8;
  format->num_channels = numChannels;
  format->endianness = JXL_NATIVE_ENDIAN;
  format->align = 0;
}

void Decoder::suggestPixelFormat(JxlPixelFormat* pixelFormat) {
  ensureBasicInfo_();
  suggestPixelFormat(std::max(basicInfo_.bits_per_sample, basicInfo_.alpha_bits),
                     std::max(basicInfo_.exponent_bits_per_sample,
                              basicInfo_.alpha_exponent_bits),
                     basicInfo_.num_color_channels + (basicInfo_.alpha_bits > 0),
                     pixelFormat);
}

/**
Do whatever needs to be done to reach the JXL_DEC_FRAME event for the specified index.

This may include rewinding and skipping frames.

On return, `this->status_` will be `JXL_DEC_FRAME`, and `frames_` will be populated at
least up to the requested frame.
*/
void Decoder::goToFrame_(size_t index) {
  if ((stateFlags_ & StateFlag::SeenAllFrames) && index >= frames_.size()) {
    throw IndexOutOfRange("%s: Frame at index %zu doesn't exist "
                          "- image only has %zu frames.",
                          __func__, index, frames_.size());
  }
  // If we happen to be in exactly the right state already, return.
  if (status_ == JXL_DEC_FRAME && index == nextFrameIndex_ - 1) {
    JXLAZY_DPRINTF("[%p] Already at JXL_DEC_FRAME for frame %zu",
                   static_cast<void*>(this), index);
    return;
  }

  JXLAZY_DPRINTF("[%p] index[%zu]", static_cast<void*>(this), index);

  // - We've gone past the target frame.
  bool decodedTooFar = nextFrameIndex_ > index;
  // - We aren't subscribed to JXL_DEC_FRAME.
  bool mustSubToDecFrame = !(eventsSubbed_ & JXL_DEC_FRAME);
  if (mustSubToDecFrame || decodedTooFar) {
    JXLAZY_DPRINTF("[%p] Must rewind to get frame %zu because: we've gone past it [%c"
                   "]; we're not subscribed to JXL_DEC_FRAME [%c].",
                   static_cast<void*>(this), index, decodedTooFar ? 'x' : ' ',
                   mustSubToDecFrame ? 'x' : ' ');
    rewind_(eventsSubbed_ | JXL_DEC_FRAME);
  }

  size_t skipToFrame = min(index, frames_.size());
  if (nextFrameIndex_ != skipToFrame) {
    JXLAZY_DPRINTF("[%p] Skip %zu frames to frame %zu.", static_cast<void*>(this),
                   skipToFrame - nextFrameIndex_, skipToFrame);
    JxlDecoderSkipFrames(dec_.get(), skipToFrame - nextFrameIndex_);
    nextFrameIndex_ = skipToFrame;
  }

  // We are now before the required frame. Iterate until we reach the one we want.
  if (processInput_(0, StopAtIndex::Specific, index, StopAtIndex::None, 0) !=
      JXL_DEC_FRAME || nextFrameIndex_-1 != index) {
    throw ReadError("Failed to find frame %zu.", index);
  }
}

FrameInfo Decoder::getFrameInfo(size_t index) {
  if (index >= frames_.size()) {
    checkOpen_();
    goToFrame_(index);
  }
  return frames_.at(index);
}

void Decoder::getFramePixels(size_t frameIndex, const JxlPixelFormat& pixelFormat,
                             void* buffer, size_t max,
                             const std::vector<ExtraChannelRequest>& extraChannels) {
  JXLAZY_DPRINTF("[%p] frameIndex[%zu]", static_cast<void*>(this), frameIndex);

  if (!buffer && extraChannels.empty()) {
    return;
  }

  if ((stateFlags_ & StateFlag::SeenAllFrames) && frameIndex >= frames_.size()) {
    throw IndexOutOfRange("%s: Frame at index %zu doesn't exist - image only "
                          "has %zu frames.", __func__, frameIndex, frames_.size());
  }

  // Go to the appropriate JXL_DEC_FRAME event
  if (!(eventsSubbed_ & JXL_DEC_FULL_IMAGE))
    rewind_(eventsSubbed_ | JXL_DEC_FULL_IMAGE);
  goToFrame_(frameIndex);
  const JxlLayerInfo& layerInfo = frames_.at(frameIndex).header.layer_info;

  if (!extraChannels.empty()) {
    ensureExtraChannelInfo_();
    for (const ExtraChannelRequest& req : extraChannels) {
      if (req.channelIndex >= extra_.size()) {
        throw IndexOutOfRange("%s: Extra channel index %zu doesn't exist - "
                              "image only has %zu extra channels.",
                              __func__, req.channelIndex, extra_.size());
      }
      JxlPixelFormat realFormat = {
        .num_channels = 1,
        .data_type = req.format.data_type,
        .endianness = req.format.endianness,
        .align = req.format.align,
      };
      size_t requiredBytes = getFrameBufferSize(layerInfo.xsize, layerInfo.ysize,
                                                realFormat);
      if (req.capacity < requiredBytes) {
        throw ReadError("Buffer of %zu bytes isn't large to store extra channel %zu - "
                        "require at least %zu.", req.capacity, req.channelIndex,
                        requiredBytes);
      }
    }
    for (const ExtraChannelRequest& req : extraChannels) {
      JxlPixelFormat realFormat = {
        .num_channels = 1,
        .data_type = req.format.data_type,
        .endianness = req.format.endianness,
        .align = req.format.align,
      };
      if (JxlDecoderSetExtraChannelBuffer(dec_.get(), &realFormat, req.target,
                                          req.capacity, req.channelIndex) !=
          JXL_DEC_SUCCESS) {
        throw LibraryError("Failed to set image output buffer for frame %zu "
                           "extra channel %zu.", frameIndex, req.channelIndex);
      }
    }
  }

  // Block any changes to output color profile
  stateFlags_ |= StateFlag::DecodedSomePixels;

  std::unique_ptr<uint8_t[]> dummyBuffer;

  if (buffer) {
    size_t requiredBytes = getFrameBufferSize(layerInfo.xsize, layerInfo.ysize,
                                              pixelFormat);
    if (max < requiredBytes) {
      throw ReadError("Buffer of %zu bytes isn't large to store this frame - require at "
                      "least %zu.", max, requiredBytes);
    }

    if (JxlDecoderSetImageOutBuffer(dec_.get(), &pixelFormat, buffer, max) !=
        JXL_DEC_SUCCESS) {
      throw LibraryError("Failed to set image output buffer for frame %zu.", frameIndex);
    }
  } else {
    // FIXME: stupidly inefficient, but I can't see how to get the extra channels
    // without setting an output buffer for the main color channels too...
    // Otherwise the decoder just returns JXL_DEC_NEED_IMAGE_OUT_BUFFER.
    JxlPixelFormat dummyFormat = {
      .num_channels = basicInfo_.num_color_channels,
      .data_type = JXL_TYPE_UINT8,
      .endianness = JXL_NATIVE_ENDIAN,
      .align = 0,
    };
    size_t dummySize = getFrameBufferSize(layerInfo.xsize, layerInfo.ysize, dummyFormat);
    dummyBuffer = JXLAZY_MAKE_UNIQUE_FOR_OVERWRITE<uint8_t[]>(dummySize);
    if (JxlDecoderSetImageOutBuffer(dec_.get(), &dummyFormat, dummyBuffer.get(),
                                    dummySize) != JXL_DEC_SUCCESS) {
      throw LibraryError("Failed to set dummy output buffer");
    }
  }

  if (processInput_(JXL_DEC_FULL_IMAGE, StopAtIndex::None, 0, StopAtIndex::None, 0) !=
      JXL_DEC_FULL_IMAGE || nextFrameIndex_-1 != frameIndex) {
    throw ReadError("Failed to read pixels for frame %zu.", frameIndex);
  }
}


Decoder::FrameIterator::FrameIterator(Decoder* parent) :
  index_(parent ? 0 : SIZE_MAX),
  parent_(parent) {
  if (parent)
    ensurePopulatedTo_(0);
}

void Decoder::FrameIterator::ensurePopulatedTo_(size_t index) {
  if (index_ == SIZE_MAX)
    return;
  if (index >= parent_->frames_.size() &&
      ((parent_->stateFlags_ & StateFlag::SeenAllFrames) ||
      parent_->processInput_(0, StopAtIndex::Specific, index, StopAtIndex::None, 0)
      != JXL_DEC_FRAME)) {
    index_ = SIZE_MAX;
  } else {
    index_ = index;
  }
}

Decoder::FrameIterator& Decoder::FrameIterator::operator++() {
  ensurePopulatedTo_(index_ + 1);
  return *this;
}

Decoder::FrameIterator Decoder::FrameIterator::operator++(int) {
  FrameIterator tmp = *this;
  ++(*this);
  return tmp;
}

Decoder::FrameIterator& Decoder::FrameIterator::operator+=(difference_type count) {
  if (count < 0)
    throw UsageError("FrameIterator can only be incremented, not decremented.");
  ensurePopulatedTo_(index_ + static_cast<size_t>(count));
  return *this;
}

Decoder::FrameIterator Decoder::FrameIterator::operator+(difference_type count) {
  if (count < 0)
    throw UsageError("FrameIterator can only be incremented, not decremented.");
  FrameIterator tmp = *this;
  tmp.ensurePopulatedTo_(tmp.index_ + static_cast<size_t>(count));
  return tmp;
}


BoxInfo Decoder::getBoxInfo(size_t index) {
  if (index >= boxes_.size()) {
    goToBox_(index);
  }
  return boxes_.at(index);
}

size_t Decoder::boxCount() {
  if ((stateFlags_ & StateFlag::SeenAllBoxes)) {
    return boxes_.size();
  }
  checkOpen_();

  // If we're not subbed, rewind (even if we haven't started yet), as we assume we've missed some.
  if (!(eventsSubbed_ & JXL_DEC_BOX))
    rewind_(eventsSubbed_ | JXL_DEC_BOX);

  processInput_(0, StopAtIndex::None, 0, /*stopAtBox=*/StopAtIndex::All, 0);
  return boxes_.size();
}

void Decoder::goToBox_(size_t index) {
  if ((stateFlags_ & StateFlag::SeenAllBoxes) && index >= boxes_.size()) {
    throw IndexOutOfRange("%s: Box at index %zu doesn't exist - image only has %zu "
                          "boxes.", __func__, index, boxes_.size());
  }
  // If we happen to be in exactly the right state already, return.
  if (status_ == JXL_DEC_BOX && index == nextBoxIndex_ - 1) {
    JXLAZY_DPRINTF("[%p] Already at JXL_DEC_BOX for box %zu",
                   static_cast<void*>(this), index);
    return;
  }
  // Rewind if necessary
  if (index < nextBoxIndex_ || !(eventsSubbed_ & JXL_DEC_BOX))
    rewind_(eventsSubbed_ | JXL_DEC_BOX);
  // Run until the box we want
  if (processInput_(0, StopAtIndex::None, 0, StopAtIndex::Specific, index) != JXL_DEC_BOX)
    throw IndexOutOfRange("%s: Failed to find box %zu.", __func__, index);
}

bool Decoder::getBoxContent(size_t index, uint8_t* destination, size_t max,
                            size_t* written, bool decompress) {
  if (written)
    *written = 0;
  checkOpen_();

  goToBox_(index);

  JxlDecoder* dec = dec_.get();
  BoxInfo& boxInfo = boxes_.at(index);
  if (boxInfo.compressed &&
      JxlDecoderSetDecompressBoxes(dec, decompress) != JXL_DEC_SUCCESS) {
    throw NoBrotliError("%s: Failed to %s box decompression%s.", __func__,
                        decompress ? "enable" : "disable",
                        decompress ? " (libjxl built without brotli support?)" : "");
  }
  if (JxlDecoderSetBoxBuffer(dec, destination, max) != JXL_DEC_SUCCESS) {
    throw ReadError("%s: Failed to set output buffer for box %zu "
                    "(previous buffer not released?).", __func__, index);
  }

  // Run decoder until this box is finished or we run out of space
  JxlDecoderStatus result = processInput_(JXL_DEC_SUCCESS | JXL_DEC_BOX |
                                          JXL_DEC_BOX_NEED_MORE_OUTPUT,
                                          StopAtIndex::None, 0, StopAtIndex::None, 0);
  size_t notWritten = JxlDecoderReleaseBoxBuffer(dec);
  size_t dataSize = max - notWritten;
  if (written)
    *written = dataSize;
  // If this box has unbounded size, we might now know its actual size
  if (result == JXL_DEC_SUCCESS && boxInfo.unbounded) {
    boxInfo.size = dataSize;
  }
  return (result == JXL_DEC_SUCCESS || result == JXL_DEC_BOX);
}

bool Decoder::getBoxContent(size_t index, vector<uint8_t>* destination,
                            std::streamsize max, bool decompress) {
  destination->clear();
  checkOpen_();
  goToBox_(index);
  BoxInfo& boxInfo = boxes_.at(index);
  bool isCompressed = boxInfo.compressed;
  size_t realMax = max >= 0 ? static_cast<size_t>(max) : SIZE_MAX;
  size_t expectedBoxSize = boxInfo.size;
  destination->resize(std::min(realMax, std::max(expectedBoxSize,size_t{32})));

  JxlDecoder* dec = dec_.get();

  if (isCompressed &&
      JxlDecoderSetDecompressBoxes(dec, decompress) != JXL_DEC_SUCCESS) {
    throw LibraryError("%s: Failed to %s box decompression%s.", __func__,
                       decompress ? "enable" : "disable",
                       decompress ? " (libjxl built without brotli support?)" : "");
  }

  size_t totalWritten = 0;
  uint8_t* nextOut = destination->data();
  size_t availOut = destination->size();
  while (true) {
    if (JxlDecoderSetBoxBuffer(dec, nextOut, availOut) != JXL_DEC_SUCCESS) {
      throw ReadError("%s: Failed to set output buffer for box %zu "
                      "(previous buffer not released?).", __func__, index);
    }
    JxlDecoderStatus result = processInput_(JXL_DEC_SUCCESS | JXL_DEC_BOX |
                                            JXL_DEC_BOX_NEED_MORE_OUTPUT,
                                            StopAtIndex::None, 0, StopAtIndex::None, 0);
    size_t notWritten = JxlDecoderReleaseBoxBuffer(dec);
    totalWritten += availOut - notWritten;
    if (result == JXL_DEC_SUCCESS || result == JXL_DEC_BOX) {
      if (expectedBoxSize > 0 && totalWritten != expectedBoxSize) {
        throw ReadError("%s: Unexpected length for box %zu data - expected %zu, got %zu.",
                        __func__, index, expectedBoxSize, totalWritten);
      }
      destination->resize(totalWritten);
      // If this box has unbounded size, we might now know its actual size
      if (result == JXL_DEC_SUCCESS && boxInfo.unbounded) {
        boxInfo.size = totalWritten;
      }
      return true;
    }

    if (destination->size() == realMax) {
      // Output truncated
      destination->resize(destination->size() - notWritten);
      JXLAZY_DPRINTF("[%p] Box truncated to %zu bytes", static_cast<void*>(this),
                     destination->size());
      return false;
    }

    // Grow buffer
    size_t newSize;
    if (!safeMul(std::max<size_t>(destination->size(), 16), static_cast<size_t>(2),
                 &newSize)) {
      newSize = SIZE_MAX;
    }
    if (newSize > realMax) {
      newSize = realMax;
    }
    destination->resize(newSize);
    nextOut = destination->data() + totalWritten;
    availOut = newSize - totalWritten;
  }
}

int Decoder::getCodestreamLevel() {
  ensureBasicInfo_();
  if (!basicInfo_.have_container) {
    return -1;
  }
  // jxll can't occur before box 2
  for (size_t boxIndex = 2; ; ++boxIndex) {
    if (boxIndex >= boxes_.size()) {
      if ((stateFlags_ & StateFlag::SeenAllBoxes) ||
          processInput_(JXL_DEC_SUCCESS, StopAtIndex::None, 0,
                        StopAtIndex::Specific, boxIndex,
                        StopAtIndex::None, 0) != JXL_DEC_BOX) {
        return -1;
      }
    }
    if (memcmp(boxes_[boxIndex].type, "jxll", 4) == 0) {
      uint8_t levelByte;
      size_t written;
      if (getBoxContent(boxIndex, &levelByte, 1, &written, false) && written == 1) {
        return levelByte;
      }
      return -1;
    }
  }
  return -1; // Unreachable
}

bool Decoder::hasJpegReconstruction() {
  if (jpegCount_ > 0)
    return true;
  if ((stateFlags_ & StateFlag::SeenAllJpeg) && jpegCount_ == 0)
    return false;
  checkOpen_();
  if (!(eventsSubbed_ & JXL_DEC_JPEG_RECONSTRUCTION)) {
    rewind_(eventsSubbed_ | JXL_DEC_JPEG_RECONSTRUCTION | JXL_DEC_FULL_IMAGE);
  }
  return processInput_(JXL_DEC_JPEG_RECONSTRUCTION, StopAtIndex::None, 0,
                       StopAtIndex::None, 0, StopAtIndex::None, 0)
                      == JXL_DEC_JPEG_RECONSTRUCTION;
}

void Decoder::goToJpeg_(size_t index) {
  if ((stateFlags_ & StateFlag::SeenAllJpeg) && index >= jpegCount_) {
    throw IndexOutOfRange("%s: No reconstrutable JPEG found", __func__);
  }
  // If we happen to be in exactly the right state already, return.
  if (status_ == JXL_DEC_JPEG_RECONSTRUCTION && index == nextJpegIndex_ - 1) {
    JXLAZY_DPRINTF("[%p] Already at JXL_DEC_JPEG_RECONSTRUCTION",
                   static_cast<void*>(this));
    return;
  }
  // Rewind if necessary
  int needEvents = JXL_DEC_JPEG_RECONSTRUCTION | JXL_DEC_FULL_IMAGE;
  if (index < nextJpegIndex_ || (eventsSubbed_ & needEvents) != needEvents) {
    rewind_(eventsSubbed_ | needEvents);
  }
  // Run until the JPEG we want
  if (processInput_(0, StopAtIndex::None, 0, StopAtIndex::None, 0,
                    StopAtIndex::Specific, index) != JXL_DEC_JPEG_RECONSTRUCTION)
  {
    throw IndexOutOfRange("%s: Failed to find JPEG frame %zu.", __func__, index);
  }
}

bool Decoder::getReconstructedJpeg(ostream& destination, size_t* jpegSize) {
  const size_t index = 0;
  const size_t chunkSize = static_cast<size_t>(128) * 1024;
  checkOpen_();
  try {
    goToJpeg_(index);
  } catch(const IndexOutOfRange&) {
    return false;
  }

  auto buffp = JXLAZY_MAKE_UNIQUE_FOR_OVERWRITE<uint8_t[]>(chunkSize);
  uint8_t* const buff = buffp.get();

  JxlDecoder* dec = dec_.get();
  size_t totalWritten = 0;
  while (true) {
    uint8_t* nextOut = buff;
    size_t availOut = chunkSize;
    if (JxlDecoderSetJPEGBuffer(dec, nextOut, availOut) != JXL_DEC_SUCCESS)
      throw LibraryError("%s: Failed to set JPEG output buffer.", __func__);
    JxlDecoderStatus result = processInput_(JXL_DEC_FULL_IMAGE |
                                            JXL_DEC_JPEG_NEED_MORE_OUTPUT,
                                            StopAtIndex::None, 0, StopAtIndex::None, 0,
                                            StopAtIndex::None, 0);
    size_t notWritten = JxlDecoderReleaseJPEGBuffer(dec);
    size_t justWrote = availOut - notWritten;
    if (!destination.write(reinterpret_cast<const char*>(buff),
                           static_cast<streamsize>(justWrote))) {
      throw JxlazyException("%s: Failed to output JPEG data", __func__);
    }
    totalWritten += justWrote;
    if (result == JXL_DEC_FULL_IMAGE) {
      JXLAZY_DPRINTF("[%p] %zu-bytes JPEG reconstructed", static_cast<void*>(this),
                     totalWritten);
      if (jpegSize)
        *jpegSize = totalWritten;
      return true;
    }
    if (result != JXL_DEC_JPEG_NEED_MORE_OUTPUT) {
      throw ReadError("%s: Unexpected status from decoder: %s", __func__,
                      decoderEventName(result));
    }
  }
}

bool Decoder::getReconstructedJpeg(vector<uint8_t>* destination, streamsize max) {
  const size_t index = 0;
  size_t chunkSize = static_cast<size_t>(64) * 1024;
  destination->clear();
  checkOpen_();
  try {
    goToJpeg_(index);
  } catch(const IndexOutOfRange&) {
    return false;
  }
  if ((stateFlags_ & StateFlag::WholeFileBuffered)) {
    // TODO: take a more realistic guess at chunkSize
  }
  const size_t realMax = max >= 0 ? static_cast<size_t>(max) : SIZE_MAX;
  destination->resize(std::min(realMax, chunkSize));

  JxlDecoder* dec = dec_.get();
  size_t totalWritten = 0;
  uint8_t* nextOut = destination->data();
  size_t availOut = destination->size();
  while (true) {
    if (JxlDecoderSetJPEGBuffer(dec, nextOut, availOut) != JXL_DEC_SUCCESS)
      throw LibraryError("%s: Failed to set JPEG output buffer.", __func__);
    JxlDecoderStatus result = processInput_(JXL_DEC_FULL_IMAGE |
                                            JXL_DEC_JPEG_NEED_MORE_OUTPUT,
                                            StopAtIndex::None, 0, StopAtIndex::None, 0,
                                            StopAtIndex::None, 0);
    size_t notWritten = JxlDecoderReleaseJPEGBuffer(dec);
    totalWritten += availOut - notWritten;
    if (result == JXL_DEC_FULL_IMAGE) {
      destination->resize(totalWritten);
      JXLAZY_DPRINTF("[%p] %zu-byte JPEG reconstructed", static_cast<void*>(this),
                     totalWritten);
      return true;
    }
    if (result != JXL_DEC_JPEG_NEED_MORE_OUTPUT) {
      destination->resize(totalWritten);
      throw ReadError("%s: Unexpected status from decoder: %s", __func__,
                      decoderEventName(result));
    }

    if (destination->size() == realMax) {
      // Output truncated
      destination->resize(destination->size() - notWritten);
      JXLAZY_DPRINTF("[%p] JPEG truncated to %zu bytes", static_cast<void*>(this),
                     destination->size());
      return false;
    }

    // Double buffer size
    size_t newSize;
    if (!safeMul(std::max<size_t>(destination->size(), 16), static_cast<size_t>(2),
                 &newSize)) {
      newSize = SIZE_MAX;
    }
    if (newSize > realMax)
      newSize = realMax;
    destination->resize(newSize);
    nextOut = destination->data() + totalWritten;
    availOut = newSize - totalWritten;
  }
}

bool Decoder::jxlIsFullyBuffered() const {
  return (stateFlags_ & StateFlag::WholeFileBuffered);
}

/**
Run the decoder until the specified condition is met.

- If libjxl returns JXL_DEC_ERROR, this function always throws JxlazyException and
  attempts to rewind the file, leaving most of this object's state intact.
- Outside of that error condition, this function never rewinds the input.
- This function never returns JXL_DEC_NEED_MORE_INPUT - chunks of input are read and
  buffered internally.

The following members are automatically updated as decoding progresses, depending on which
events we're subscribed to:
- status_
- seen_
- basicInfo_
- frames_, nextFrameIndex_
- boxes_, nextBoxIndex_
- origIcc_, haveOrigColorEnc_, origColorEnc_
- dataIcc_, haveDataColorEnc_, dataColorEnc_

Events JXL_DEC_BASIC_INFO and JXL_DEC_COLOR_PROFILE are removed from `this->eventsSubbed_`
as soon as they occur, so that we never re-subscribe to them on rewind.

Returns `this->status_` as soon as one of the following is true:

- libjxl returns JXL_DEC_SUCCESS.
- We reached any of the requested event types: `(status_ & untilStatus) != 0`.
- `stopAtFrame == Specific`, and we received the JXL_DEC_FRAME for frame index @p
  `specificFrame'.  (For this to ever happen, the caller must ensure we're subscribed to
  JXL_DEC_FRAME and we haven't already gone past this frame.)
- `stopAtFrame == All`, and we've got the metadata of every frame.  (No guarantees about
  where this leaves the decoder.  We might not process any input.  We might stop on the
  last JXL_DEC_FRAME.  We might go past it.)
- `stopAtBox` and `specificBox` have the same meaning as `stopAtFrame` and
  `specificFrame`, respectively, but for boxes instead of frames.
- `stopAtJpeg` and `specificJpeg` have the same meaning as `stopAtFrame` and
  `specificFrame`, respectively, but for JPEG reconstructions instead of frames.

@return the latest status returned from JxlDecoderProcessInput.
*/
JxlDecoderStatus Decoder::processInput_(int untilStatus,
                                        StopAtIndex stopAtFrame, size_t specificFrame,
                                        StopAtIndex stopAtBox, size_t specificBox,
                                        StopAtIndex stopAtJpeg, size_t specificJpeg) {
#ifdef JXLAZY_DEBUG
  ostringstream oss;
  oss << "status JXL_DEC_SUCCESS";
  if (untilStatus != 0) {
    oss << " or any of (";
    printDecoderEventNames(oss, untilStatus|JXL_DEC_SUCCESS);
    oss << ')';
  }
  if (stopAtFrame == StopAtIndex::Specific)
    oss << ", or we reach frame " << specificFrame;
  else if (stopAtFrame == StopAtIndex::All)
    oss << ", or we've got all frame metadata";
  if (stopAtBox == StopAtIndex::Specific)
    oss << ", or we reach box " << specificBox;
  else if (stopAtBox == StopAtIndex::All)
    oss << ", or we've got all box metadata";
  if (stopAtJpeg == StopAtIndex::Specific)
    oss << ", or we reach JPEG reconstruction " << specificJpeg;
  else if (stopAtJpeg == StopAtIndex::All)
    oss << ", or we've seen all JPEG reconstruction data";

  JXLAZY_DPRINTF("[%p] Run decoder until %s.", static_cast<void*>(this),
                 oss.str().c_str());
#endif

  JxlDecoder* dec = dec_.get();

  while ((status_ = JxlDecoderProcessInput(dec)) != JXL_DEC_SUCCESS) {
    if (status_ == JXL_DEC_ERROR)
    {
      string err = "Input failed to decode.";
      try {
        rewind_(eventsSubbed_);
      } catch (const JxlazyException& ex) {
        err += "  While rewinding the file, another error occured: ";
        err += ex.what();
      }
      throw ReadError("%s.", err.c_str());
    }

    JXLAZY_DPRINTF("[%p] %s", static_cast<void*>(this), decoderEventName(status_));

    if (status_ == JXL_DEC_BASIC_INFO) {
      eventsSubbed_ &= ~JXL_DEC_BASIC_INFO;
      if (JxlDecoderGetBasicInfo(dec, &basicInfo_) != JXL_DEC_SUCCESS) {
        throw LibraryError("Failed to get basic info");
      }
      stateFlags_ |= StateFlag::GotBasicInfo;
    }

    else if (status_ == JXL_DEC_NEED_MORE_INPUT) {
      // Refill the input buffer
      uint8_t* inBufferData = inBufferPrivate_.data();
      size_t unprocessedCount = JxlDecoderReleaseInput(dec);
      if (unprocessedCount == inBufferLength_ && inBufferLength_ > 0) {
        throw ReadError("Decoder stalled - last iteration consumed 0 of %zu bytes.",
                        inBufferLength_);
      }

      if (inBufferCap_ < inBufferMax_) {
        // Buffer can grow, so append data and restart at the right offset.
        size_t newCap;
        if (!safeMul(inBufferCap_, size_t{2}, &newCap) || newCap > inBufferMax_) {
          newCap = inBufferMax_;
        }
        inBufferPrivate_.resize(inBufferLength_); // Don't copy trailing junk
        inBufferPrivate_.resize(newCap);
        inBufferPtr_ = inBufferData = inBufferPrivate_.data();
        JXLAZY_DPRINTF("Grew input buffer from %zu to %zu bytes", inBufferCap_, newCap);
        inBufferCap_ = newCap;
        inBufferDecOffset_ = inBufferLength_ - unprocessedCount;
      } else {
        // Buffer size is fixed, so shift leftover bytes to the start of inBufferData.
        if (unprocessedCount > 0) {
          const uint8_t* nextStart = inBufferData + inBufferLength_ - unprocessedCount;
          memmove(inBufferData, nextStart, unprocessedCount);
        }
        inBufferOffset_ += inBufferLength_ - unprocessedCount;
        inBufferLength_ = unprocessedCount;
        inBufferDecOffset_ = 0;
      }
      size_t spaceInBuffer = inBufferCap_ - inBufferLength_;
      if (inStreamPtr_->read(reinterpret_cast<char*>(inBufferData + inBufferLength_),
                             static_cast<streamsize>(spaceInBuffer)).bad()) {
        throw ReadError("Failed to read next chunk from input (total read: %zu bytes).",
                        inBufferOffset_ + inBufferLength_);
      }
      size_t got = inStreamPtr_->gcount();
      JXLAZY_DPRINTF("[%p] Read next %zu bytes from disk", static_cast<void*>(this),
                       got);
      inBufferLength_ += got;
      if (JxlDecoderSetInput(dec, inBufferData + inBufferDecOffset_,
                             inBufferLength_ - inBufferDecOffset_) !=
          JXL_DEC_SUCCESS) {
        throw ReadError("Failed to set next %zu bytes of input", inBufferLength_);
      }
      inStreamPtr_->peek();
      if (inStreamPtr_->eof()) {
        if (inBufferOffset_ == 0) {
          stateFlags_ |= StateFlag::WholeFileBuffered;
        }
        JXLAZY_DPRINTF("[%p] Reached EOF, wholeFileBuffered_[%d]",
                       static_cast<void*>(this), static_cast<int>(wholeFileBuffered_));
        JxlDecoderCloseInput(dec);
      }
    }

    else if (status_ == JXL_DEC_FRAME) {
      if (frames_.size() <= nextFrameIndex_) {
        // We don't have this frame's header yet
        FrameInfo frameInfo;
        if (JxlDecoderGetFrameHeader(dec, &frameInfo.header) != JXL_DEC_SUCCESS)
          throw LibraryError("Failed to get header for frame %zu.", nextFrameIndex_);
        // Extra channel blend info is only useful when coalescing is disabled.
        if (!(stateFlags_ & StateFlag::IsCoalescing)) {
          frameInfo.ecBlendInfo.resize(basicInfo_.num_extra_channels);
          for (uint32_t ec = 0; ec < basicInfo_.num_extra_channels; ++ec) {
            if (JxlDecoderGetExtraChannelBlendInfo(dec, ec, &frameInfo.ecBlendInfo[ec])
                != JXL_DEC_SUCCESS) {
              throw LibraryError("Failed to get extra channel %" PRIu32 " blend info for"
                                 " frame %zu.", ec, nextFrameIndex_);
            }
          }
        }
        if (frameInfo.header.name_length > 0) {
          auto tmpName = JXLAZY_MAKE_UNIQUE_FOR_OVERWRITE<char[]>(
              frameInfo.header.name_length + 1);
          if (JxlDecoderGetFrameName(dec, tmpName.get(),
              frameInfo.header.name_length + 1) != JXL_DEC_SUCCESS) {
            throw LibraryError("Failed to get name for frame %zu.", nextFrameIndex_);
          }
          frameInfo.name = tmpName.get();
        }
        frames_.push_back(frameInfo);
        // is_last only says it's set for the last animation frame, so only trust it if
        // we're coalescing.
        if ((stateFlags_ & StateFlag::IsCoalescing) && frameInfo.header.is_last) {
          stateFlags_ |= StateFlag::SeenAllFrames;
        }
      }
      if (++nextFrameIndex_ == 0) {
        throw LibraryError("Too many frames!");
      }

      if ((stopAtFrame == StopAtIndex::All && (stateFlags_ & StateFlag::SeenAllFrames)) ||
          (stopAtFrame == StopAtIndex::Specific && nextFrameIndex_ == specificFrame+1)) {
        return status_;
      }
    }

    else if (status_ == JXL_DEC_BOX) {
      if (boxes_.size() <= nextBoxIndex_) {
        // We don't know about this box yet
        BoxInfo boxInfo;
        if (JxlDecoderGetBoxType(dec, boxInfo.type, JXL_FALSE) != JXL_DEC_SUCCESS)
          throw LibraryError("Failed to get raw box type.");
        boxInfo.unbounded = false;
        boxInfo.compressed = (memcmp(boxInfo.type, "brob", 4) == 0);
        if(boxInfo.compressed) {
          // Get the real type.
          if (JxlDecoderGetBoxType(dec, boxInfo.type, JXL_TRUE) != JXL_DEC_SUCCESS)
            throw LibraryError("Failed to get decompressed box type.");
        }
        if (JxlDecoderGetBoxSizeContents(dec, &boxInfo.size) != JXL_DEC_SUCCESS)
          throw LibraryError("Failed to get box content size.");
        if (boxInfo.size == 0) {
          // Is it actually empty, or just unbounded?
          size_t rawSize;
          if (JxlDecoderGetBoxSizeRaw(dec, &rawSize) == JXL_DEC_SUCCESS &&
              rawSize == 0) {
            boxInfo.unbounded = true;
          }
        }
        boxes_.push_back(boxInfo);
      }
      if (++nextBoxIndex_ == 0)
        throw LibraryError("Too many boxes!");

      if ((stopAtBox == StopAtIndex::All && (stateFlags_ & StateFlag::SeenAllBoxes)) ||
          (stopAtBox == StopAtIndex::Specific && nextBoxIndex_ == specificBox+1)) {
        return status_;
      }
    }

    else if (status_ == JXL_DEC_COLOR_ENCODING) {
      eventsSubbed_ &= ~JXL_DEC_COLOR_ENCODING;
      stateFlags_ |= StateFlag::GotColor;

      struct {
        JxlColorProfileTarget target;
        vector<uint8_t>& icc;
        StateFlag flag;
        JxlColorEncoding& colorEnc;
      } targets[] = {
        { JXL_COLOR_PROFILE_TARGET_ORIGINAL,
          origIcc_, StateFlag::GotOrigColorEnc, origColorEnc_},
        { JXL_COLOR_PROFILE_TARGET_DATA,
          dataIcc_, StateFlag::GotDataColorEnc, dataColorEnc_},
      };

      for (size_t i = 0; i < sizeof targets / sizeof targets[0]; ++i) {
        if (JXLAZY_GET_ENCODED_PROFILE(dec, targets[i].target, &targets[i].colorEnc)
            == JXL_DEC_SUCCESS) {
          stateFlags_ |= targets[i].flag;
        }

        size_t iccSize;
        if (JXLAZY_GET_ICC_PROFILE_SIZE(dec, targets[i].target,
                                        &iccSize) == JXL_DEC_SUCCESS) {
          targets[i].icc.resize(iccSize);
          if (JXLAZY_GET_ICC_PROFILE(dec, targets[i].target, targets[i].icc.data(),
                                     iccSize) != JXL_DEC_SUCCESS) {
            JXLAZY_DPRINTF("Failed to get %s ICC profile.",
                           targets[i].target == JXL_COLOR_PROFILE_TARGET_ORIGINAL ?
                           "original" : "data");
            targets[i].icc.clear();
          }
        }
      }
    }

    else if (status_ == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
      // If we get here, we're subscribed to JXL_DEC_FULL_IMAGE, but the client hasn't
      // asked for the pixels of this frame.
      JXLAZY_DPRINTF("[%p] Don't want to decode pixels for frame %zu!",
                     static_cast<void*>(this), nextFrameIndex_-1);
      if (JxlDecoderSkipCurrentFrame(dec) != JXL_DEC_SUCCESS)
        throw LibraryError("Library refused to skip current frame...");
      continue;
    }

    else if (status_ == JXL_DEC_JPEG_RECONSTRUCTION) {
      ++nextJpegIndex_;
      jpegCount_ = max(jpegCount_, nextJpegIndex_);
      if ((stopAtJpeg == StopAtIndex::All && (stateFlags_ & StateFlag::SeenAllJpeg)) ||
          (stopAtJpeg == StopAtIndex::Specific && nextJpegIndex_ == specificJpeg+1)) {
        return status_;
      }
    }

    if ((status_ & untilStatus)) {
      break;
    }
  }

  if (status_ == JXL_DEC_SUCCESS) {
    if ((eventsSubbed_ & JXL_DEC_FRAME))
      stateFlags_ |= StateFlag::SeenAllFrames;
    if ((eventsSubbed_ & JXL_DEC_BOX))
      stateFlags_ |= StateFlag::SeenAllBoxes;
    if ((eventsSubbed_ & JXL_DEC_JPEG_RECONSTRUCTION))
      stateFlags_ |= StateFlag::SeenAllJpeg;
  }
  return status_;
}

}  // namespace jxlazy
