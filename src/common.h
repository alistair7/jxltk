/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#ifndef JXLTK_COMMON_H_
#define JXLTK_COMMON_H_

#include <iostream>

#include <jxl/encode.h>

#include "../contrib/jxlazy/include/jxlazy/decoder.h"
#include "mergeconfig.h"

namespace jxltk {

// If the requested distance is < this, treat it as distance 0.
constexpr float kLosslessDistanceThreshold = .001f;

// Block size for IO
constexpr size_t kBufferSize = size_t{128} * 1024;

/**
 * Process encoder input and write output to a file until JXL_ENC_SUCCESS.
 *
 * Returns when the encoder returns anything that isn't JXL_ENC_NEED_MORE_OUTPUT.
 *
 * @param[in,out] buffer Pre-allocated scratch buffer.
 * @param[in] bufferSize Size of @p buffer.
 * @param[in,out] fout Stream to write JXL bytes to.
 * @return JXL_ENC_SUCCESS if everything passed to the encoder so far was
 * successfully encoded and written out, else return the unexpected encoder
 * status (i.e. JXL_ENC_ERROR).
 */
JxlEncoderStatus encodeUntilSuccess(JxlEncoder* enc, uint8_t* buffer, size_t bufferSize,
                                    std::ostream& fout);

/**
 * Initialise frame settings for the next frame based on a FrameConfig.
 *
 * @param[in,out] enc Encoder the frame settings will be linked to
 * @param[in] basicInfo Info about the resulting image.
 * @param[in] frameConfig Jxltk frame settings.
 * @param[in] tpsNumerator,tpsDenominator Ticks per second fraction (used to
 * convert milliseconds to ticks when applicable).
 * @param[in] frameXsize,frameYsize Dimensions of this frame.
 * @param[in] brotliEffort Effort to use when compressing brob boxes.
 * (Not sure why this is a frame setting, or how the order of operations
 * affects which boxes are affected by it, but setting it the same for
 * every frame shouldn't hurt.)
 * @return A pointer to the new frame settings object.
 */
JxlEncoderFrameSettings* frameConfigToJxlEncoderFrameSettings(
    JxlEncoder* enc,
    const JxlBasicInfo& basicInfo,
    const FrameConfig& frameConfig,
    uint32_t tpsNumerator, uint32_t tpsDenominator,
    uint32_t frameXsize, uint32_t frameYsize,
    const std::optional<int32_t>& brotliEffort = {});

/**
 * Return (index,info) pairs for all non-JXL-reserved ISO BMFF boxes.
 */
std::vector<std::pair<size_t, jxlazy::BoxInfo> >
  getNonReservedBoxes(jxlazy::Decoder& dec);

/**
 * Return the number of non-reserved metadata boxes in the JXL.
 */
size_t countNonReservedBoxes(jxlazy::Decoder& dec);

}  // namespace jxltk

#endif  // JXLTK_COMMON_H_
