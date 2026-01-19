/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#ifndef JXLTK_MERGE_H_
#define JXLTK_MERGE_H_

#include <optional>
#include <string>

#include <jxl/types.h>

#include "mergeconfig.h"

namespace jxltk {

/**
 * Combine one or more JXLs into a single JXL.
 *
 * Ancilliary boxes on the first input file are copied to the output, but boxes
 * on all other inputs are ignored. Any boxes explicitly specified in @p
 * mergeConfig will also be included in the output.
 *
 * @param[in] mergeCfg Merge configuration, normally parsed from JSON file.
 * @param[in] output Name of the merged output file to create/overwrite.
 * @param[in] numThreads Number of threads to use, or 0 to decide automatically.
 * @param[in] forceDataType Force a specific data type to be used during
 * processing.
 */
void merge(const MergeConfig& mergeCfg, const std::string& output,
           size_t numThreads = 0,
           const std::optional<JxlDataType>& forceDataType = {});

}  // namespace jxltk

#endif  // JXLTK_MERGE_H_
