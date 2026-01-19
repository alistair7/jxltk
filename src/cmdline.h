/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#ifndef JXLTK_CMDLINE_H_
#define JXLTK_CMDLINE_H_

#include <string>
#include <vector>

#include <jxl/types.h>

#include "mergeconfig.h"
#include "util.h"

namespace jxltk {

struct CmdlineOpts {
  std::string mode{};
  bool coalesce{false};
  int codestreamLevel{-1};
  bool configOnly{false};
  bool useMilliseconds{false};
  bool fullConfig{false};
  size_t numThreads{0};
  std::string mergeCfgFilename{};
  std::vector<std::string> positional{};

  /* Global encode settings - overrides per-frame settings */
  FrameConfig overrideFrameConfig{};
  BoxConfig overrideBoxConfig{};
  std::optional<uint32_t> overrideBrotliEffort{};
  std::optional<ColorConfig> overrideColor{};
  std::optional<JxlDataType> overrideDataType{};
  std::optional<std::pair<uint32_t,uint32_t> > overrideTps{};
#ifndef JXLTK_FLOATS_ARE_IEEE754
  bool no754{false}; // Ignore lack of IEEE 754 float support
#endif
};

CmdlineOpts parseArgs(int argc, char** argv);

}  // namespace jxltk

#endif  // JXLTK_CMDLINE_H_
