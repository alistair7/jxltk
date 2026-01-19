/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#ifndef JXLTK_SPLIT_H_
#define JXLTK_SPLIT_H_

#include <optional>
#include <string_view>

#include "mergeconfig.h"

namespace jxltk {

/**
 * Split the named JXL file into its individual frames and boxes.
 *
 * Each frame extracted from the input is written to its own single-frame JXL.
 * Each non-JXL-reserved ISO-BMFF box in the input has its contents written
 * to a new file.
 * Finally, a JSON file containing a recipe for reassembling the file is written.
 *
 * @param[in] input Path to a JXL file to split.
 * @param[in] poutputDir Path to a directory for the output files (created if it
 * doesn't exist).
 * @param[in] coalesce If true, blend layers together and output only full-sized
 * animation frames.
 * @param[in] numThreads Maximum threads to use for encoding/decoding, or 0 to use
 * the default.
 * @param[in] frameConfig Encoding settings for all output files.
 * @param[in] forceDataType Force a specific data type to be used during
 * processing.
 * @param[in] wantPixels Enable (true; default) or disable (false) extracting
 * pixels.
 * @param[in] wantBoxes Enable (true; default) or disable (false) extracting box
 * content.
 * @param[in] configFile Path where the merge config will be written.  May be
 * `""` to suppress writing it, or `"-"` to write it to stdout.  Otherwise
 * interpreted as a path relative to @p poutputDir.
 * @param[in] useTicks If true, output animation timing information using ticks.
 * If false, use milliseconds (possibly rounded).
 * @param[in] full If true, the merge config is written in a more verbose way,
 * with fewer implied defaults.
 */
void split(std::string_view input, std::string_view poutputDir,
           bool coalesce = false, size_t numThreads = 0,
           const FrameConfig& frameConfig = {},
           const std::optional<JxlDataType>& forceDataType = {},
           bool wantPixels = true, bool wantBoxes = true,
           std::string_view configFile = "merge.json",
           bool useTicks = true, bool full = false);

}  // namespace jxltk

#endif  // JXLTK_SPLIT_H_
