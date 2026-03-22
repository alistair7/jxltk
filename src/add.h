/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#ifndef JXLTK_ADD_H_
#define JXLTK_ADD_H_
#include <iostream>

#include "../contrib/jxlazy/include/jxlazy/decoder.h"
#include "mergeconfig.h"
#include "util.h"

namespace jxltk {

/**
 * Subtract one full image from another and write the result as a new JXL.
 *
 * @param leftImage Decoder for an existing JXL file.
 * @param rightImage Decoder for an existing JXL file that will be added to or subtracted
 *   from @p leftImage.
 * @param adding true if the samples should be added; false if they should be subtracted.
 * @param[in,out] fout Stream to write JXL bytes to, or nullptr to consume encoder output
 *   without writing it anywhere.
 * @param frameConfig Override encoding options for all frames.
 * @param numThreads Max number of threads to use for encoding, or 0 to pick a default.
 * @param[out] written Number of bytes output from the encoder, or nullptr if you don't
 *   care.
 * @return 0 on success.
 */
int addOrSubtract(jxlazy::Decoder& leftImage, jxlazy::Decoder& rightImage, bool adding,
                  std::ostream* fout, const FrameConfig& frameConfig = {},
                  size_t numThreads = 0, size_t* written = nullptr);

}  // namespace jxltk

#endif  //JXLTK_ADD_H_
