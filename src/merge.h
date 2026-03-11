/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#ifndef JXLTK_MERGE_H_
#define JXLTK_MERGE_H_

#include <iostream>

#include <jxl/types.h>

#include "mergeconfig.h"

namespace jxltk {

/**
 * Combine one or more JXLs into a single JXL.
 *
 * @param[in] mergeCfg Merge configuration, normally parsed from JSON file.
 * @param[in] fout Open stream where the result will be written.
 * @param[in] numThreads Number of threads to use, or 0 to decide automatically.
 * @param[in] autoCrop Trim fully-transparent borders (for alpha-blended frames) and
 *   all-zero borders (for kAdded frames), reducing the number of pixels without altering
 *   the appearance of the result.
 */
void merge(const MergeConfig& mergeCfg, std::ostream& fout, size_t numThreads = 0,
           bool autoCrop = false);

}  // namespace jxltk

#endif  // JXLTK_MERGE_H_
