/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include <jxl/encode_cxx.h>

#include "../contrib/jxlazy/include/jxlazy/decoder.h"

#include "cmdline.h"
#include "log.h"
#include "merge.h"
#include "mergeconfig.h"
#include "split.h"
#include "util.h"

using std::optional;
using std::vector;


namespace jxltk {

int main_(int argc, char** argv) {

  CmdlineOpts opts = parseArgs(argc, argv);
  JXLTK_TRACE("Finished parsing command line.");

#ifndef JXLTK_FLOATS_ARE_IEEE754
  if (!opts.no754) {
    JXLTK_WARNING("The compiler used to build jxltk has a `float` type that does not "
                  "seem to conform to IEEE 754.\n"
                  "Some operations on floating-point samples might give incorrect "
                  "results.\n"
                  "(Pass --no-754 to suppress this warning)");
  }
#endif

  if (opts.positional.empty()) {
    JXLTK_ERROR("No output file specified.");
    return EXIT_FAILURE;
  }

  if (opts.mode == "merge") {
    MergeConfig mergeOp;

    if (!opts.mergeCfgFilename.empty()) {
      if (opts.mergeCfgFilename == "-") {
        mergeOp = MergeConfig::fromJson(std::cin);
      } else {
        std::ifstream mergeCfgFile(opts.mergeCfgFilename, std::ios::binary);
        if (!mergeCfgFile) {
          JXLTK_ERROR("Failed to open %s for reading.",
                      shellQuote(opts.mergeCfgFilename, true).c_str());
          return EXIT_FAILURE;
        }
        mergeOp = MergeConfig::fromJson(mergeCfgFile);
      }

      // Adjust paths so they're relative to the json directory.
      auto jsonDir = std::filesystem::path(opts.mergeCfgFilename).remove_filename();
      for (auto& box : mergeOp.boxes) {
        if (!box.file || box.file->empty()) continue;
        std::filesystem::path boxPath(*box.file);
        if (boxPath.is_absolute()) continue;
        *box.file = jsonDir / boxPath;
      }
      for (auto& frameConfig : mergeOp.frames) {
        if (!frameConfig.file || frameConfig.file->empty()) continue;
        std::filesystem::path inpPath(*frameConfig.file);
        if (inpPath.is_absolute()) continue;
        frameConfig.file = jsonDir / inpPath;
      }

    } else {
      // No JSON file
      if (opts.positional.size() < 2) {
        JXLTK_ERROR("No input files.");
        return EXIT_FAILURE;
      }
      // Make each frame blend with its previous frame:
      uint32_t saveAsReference =
          (opts.overrideFrameConfig.durationMs.value_or(0) > 0 ||
           opts.overrideFrameConfig.durationTicks.value_or(0) > 0) ? 1 : 0;
      for (auto c = opts.positional.begin(); c < opts.positional.end() - 1; ++c) {
        FrameConfig& frm = mergeOp.frames.emplace_back();
        frm.file = *c;
        frm.update(opts.overrideFrameConfig);
        frm.saveAsReference = saveAsReference;
        frm.blendSource = saveAsReference;
      }
      // Copy any boxes from the first input
      mergeOp.frames[0].copyBoxes = true;
    }

    // Apply command line overrides
    if (opts.overrideTps) {
      mergeOp.tps = opts.overrideTps;
    }
    for (FrameConfig& frameCfg : mergeOp.frames) {
      frameCfg.update(opts.overrideFrameConfig);
    }
    if (opts.overrideColor) {
      mergeOp.color = *opts.overrideColor;
    }
    for (BoxConfig& boxCfg : mergeOp.boxes) {
      boxCfg.update(opts.overrideBoxConfig);
    }
    // Also set boxDefaults, as this is used for boxes copied from JXLs.
    mergeOp.boxDefaults = opts.overrideBoxConfig;
    if (opts.overrideBrotliEffort) {
      mergeOp.brotliEffort = *opts.overrideBrotliEffort;
    }
    if (opts.codestreamLevel >= 0) {
      mergeOp.codestreamLevel = opts.codestreamLevel;
    }

    mergeOp.normalize();

    merge(mergeOp, opts.positional.back(), opts.numThreads, opts.overrideDataType);
    return EXIT_SUCCESS;
  }

  if (opts.mode == "split") {

    split(opts.positional[0],
          opts.positional.size() > 1 ? opts.positional[1] : std::string(),
          opts.coalesce,
          opts.numThreads, opts.overrideFrameConfig, {}, !opts.configOnly,
          !opts.configOnly,
          opts.configOnly ? "-" : "merge.json", !opts.useMilliseconds,
          opts.fullConfig);
    return EXIT_SUCCESS;
  }

  if (opts.mode == "gen") {
    JXLTK_TRACE("gen mode");
    // Produce an example JSON merge file for the files provided
    MergeConfig feo;
    if (opts.fullConfig) {
      ColorConfig& cc = feo.color.emplace();
      cc.type = ColorSpecType::Enum;
      JxlColorEncodingSetToSRGB(&cc.cicp, JXL_FALSE);
    }
    feo.frameDefaults.blendMode = JXL_BLEND_BLEND;
    feo.frameDefaults.durationMs = 1000;
    if (opts.overrideFrameConfig.durationMs) {
      feo.frameDefaults.durationMs = *opts.overrideFrameConfig.durationMs;
    } else if (opts.overrideFrameConfig.durationTicks){
      feo.frameDefaults.durationTicks = *opts.overrideFrameConfig.durationTicks;
    }
    feo.boxDefaults = opts.overrideBoxConfig;
    feo.tps = opts.overrideTps;
    if (!feo.tps && feo.frameDefaults.durationTicks.value_or(0) > 0) {
      feo.tps.emplace(100, 1);
    }
    feo.frameDefaults.effort = opts.overrideFrameConfig.effort;
    feo.frameDefaults.distance = opts.overrideFrameConfig.distance;
    feo.loops = 0;
    for (size_t i = 0; i < opts.positional.size(); ++i) {
      FrameConfig frm;
      frm.file = opts.positional[i];
      if (i == 0) {
        frm.blendMode = JXL_BLEND_REPLACE;
        frm.copyBoxes = true;
      } else {
        frm.blendSource = 1;
      }
      if (i < opts.positional.size() - 1) frm.saveAsReference = 1;

      // Try to get offset from filename
      if (frm.file && !frm.file->empty()) {
        static const std::regex geoRx(R"(([+\-][0-9]+)([+\-][0-9]+))");
        std::smatch cm;
        if (std::regex_search(*frm.file, cm, geoRx)) {
          frm.offset.emplace(std::stol(cm[1]), std::stol(cm[2]));
        }
      }

      feo.frames.push_back(frm);
    }

    if (opts.overrideColor) {
      feo.color = *opts.overrideColor;
    }
    if (opts.overrideBrotliEffort) {
      feo.brotliEffort = *opts.overrideBrotliEffort;
    }

    feo.toJson(std::cout, opts.fullConfig);
    std::cout.put('\n');
    return EXIT_SUCCESS;
  }

  if (opts.mode == "icc") {
    std::ifstream ifile;
    std::istream* pifile = &std::cin;
    std::ofstream ofile;
    std::ostream* pofile = &std::cout;
    if (!opts.positional.empty() && opts.positional[0] != "-") {
      ifile.open(opts.positional[0], std::ios::binary);
      pifile = &ifile;
    }
    if (opts.positional.size() > 1 && opts.positional[1] != "-") {
      ofile.open(opts.positional[1], std::ios::binary);
      pofile = &ofile;
    }
    vector<uint8_t> icc;
    {
      jxlazy::Decoder dec;
      dec.openStream(*pifile, 0, jxlazy::DecoderHint::NoPixels, 16);
      icc = dec.getIccProfile(JXL_COLOR_PROFILE_TARGET_ORIGINAL);
      if (icc.empty()) {
        JXLTK_ERROR("Failed to get ICC profile.");
        return EXIT_FAILURE;
      }
    }
    pofile->write(reinterpret_cast<char*>(icc.data()), icc.size());
    JXLTK_DEBUG("Wrote %zu byte ICC profile.", icc.size());
    return EXIT_SUCCESS;
  }

  JXLTK_ERROR("Unknown mode %s.", shellQuote(opts.mode, true).c_str());
  return EXIT_FAILURE;
}

}  // namespace jxltk



int main(int argc, char** argv) {
  try {
    return jxltk::main_(argc, argv);
  } catch (const std::exception& e) {
    JXLTK_ERROR("Unhandled exception: %s.", e.what());
  } catch (...) {
    JXLTK_ERROR("Unhandled exception.");
  }
  return EXIT_FAILURE;
}
