/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "cmdline.h"
#include "enums.h"
#include "log.h"
#include "util.h"

// https://github.com/skeeto/optparse
#define OPTPARSE_IMPLEMENTATION
#define OPTPARSE_API static
#include "../contrib/optparse/optparse.h"
#undef OPTPARSE_API
#undef OPTPARSE_IMPLEMENTATION

using namespace std;

namespace jxltk {

enum HelpSection : uint32_t {
  Merge = 1,
  Split = 2,
  Gen =   4,
  Icc =   8,

  MergeSplitGen =   7,
  All =   15,
};

struct CommandLineOption {
  const char* longName{nullptr};
  char shortName{'\0'};
  uint32_t sections{0};
  const char* metaVar{nullptr};
  const char* help{nullptr};
};

constexpr CommandLineOption commandLineOptions[] = {
  {"help", 'h', HelpSection::All, nullptr,
    "Display this help message and exit." },
  {"verbose", 'v', HelpSection::All, nullptr,
   "More detailed console output - use twice for debug, thrice for trace."},
  {"quiet", 'q', HelpSection::All, nullptr,
   "Less console output - use twice to see only errors, thrice for silence."},
  {"merge-config", 'M', HelpSection::Merge, "FILE",
    "Path to a JSON merge config file to read." },
  {"coalesce", 'c', HelpSection::Split, nullptr,
   "Flatten layers and output only full frames."},
  {"config-only", 'C', HelpSection::Split, nullptr,
   "Just generate the JSON merge config on stdout and don't write any files."},
  {"distance", 'd', HelpSection::MergeSplitGen, "FLOAT",
   "Butteraugli distance for encoded files. Default is 0 (lossless)." },
  {"effort", 'e', HelpSection::MergeSplitGen, "1-" JXLTK_ITOA(JXLTK_MAX_EFFORT),
   "Encoding effort.  Default is whatever libjxl decides." },
  {"compress-boxes", '\0', HelpSection::Merge|HelpSection::Gen, "0|1",
   "Globally disable (0) or enable (1) Brotli compression of metadata boxes." },
  {"brotli-effort", '\0', HelpSection::Merge|HelpSection::Gen, "0-11",
   "Effort for Brotli compression of metadata." },
  {"best", '\0', HelpSection::MergeSplitGen, nullptr,
   "Equivalent to `--effort=" JXLTK_ITOA(JXLTK_MAX_EFFORT)
   "--compress-boxes=1 --brotli-effort=11`." },
  {"modular-nb-prev-channels", 'E', HelpSection::MergeSplitGen, "INT",
   "Number of previous channels modular mode is allowed to reference."},
  {"iterations", 'I', HelpSection::MergeSplitGen, "0-100",
   "Percentage of pixels used to learn MA trees in modular mode. "
   "Default is whatever libjxl decides."},
  {"patches", '\0', HelpSection::MergeSplitGen, "0|1",
   "Enable (1) or disable (0) automatic patch generation for all frames. "
   "Default is whatever libjxl decides."},
  {"duration-ms", '\0', HelpSection::Merge|HelpSection::Gen, "INT",
   "Duration of each frame in milliseconds." },
  {"duration-ticks", '\0', HelpSection::Merge|HelpSection::Gen, "INT",
   "Duration of each frame in ticks." },
  {"ticks-per-second", 'r', HelpSection::Merge|HelpSection::Gen, "N[/D]",
   "Number of animation ticks per second, given as an integer or rational. "
   "Default is 100 if processing an animation."},
  {"blend-mode", '\0', HelpSection::Merge|HelpSection::Gen, "REPLACE/BLEND/ADD/MUL/MULADD",
   "Blend mode for all frames.  Default is REPLACE."},
  {"data-type", '\0', HelpSection::Merge|HelpSection::Split, "u8|u16|f32",
   "Force processing samples as uint8, uint16, or float type."},
  {"ms", '\0', HelpSection::Split, nullptr,
   "Output frame durations in (possibly rounded) milliseconds instead of ticks."},
  {"full", '\0', HelpSection::Split|HelpSection::Gen, nullptr,
   "Generate \"full\" merge config, with fewer implied defaults."},
  {"overwrite", 'Y', HelpSection::Split|HelpSection::Merge|HelpSection::Icc, nullptr,
   "Overwrite existing files without asking."},
  {"color-from", '\0', HelpSection::Merge|HelpSection::Gen, "FILE",
   "Assign the color profile from the named JXL or ICC file." },
  {"colour-from", '\0', HelpSection::Merge|HelpSection::Gen, "FILE", nullptr },
  {"level", '\0', HelpSection::Merge|HelpSection::Gen, "5|10",
   "Explicitly set the codestream conformance level." },
  {"threads", '\0', HelpSection::All, "N", "Maximum number of threads to use. Default is"
   " '0', meaning choose automatically." },
  {"no-754", '\0', HelpSection::All, nullptr, nullptr },
};


/**
 * Print all options that have ALL bits of @p sec set and
 * NOT ALL bits of @p exclude set (if exclude > 0).
 */
static void printSection(uint32_t sec, uint32_t exclude = 0) {
  for (const auto& opt : commandLineOptions) {
    if ((exclude != 0 && (opt.sections & exclude) == exclude) ||
        (opt.sections & sec) != sec) {
      continue;
    }
    if (!opt.help) {
      continue;
    }
    cerr << "  ";
    if (opt.shortName) {
      cerr << '-' << opt.shortName;
      if (opt.metaVar) {
        cerr << ' ' << opt.metaVar;
      }
      if (opt.longName) {
        cerr << ", ";
      }
    }
    if (opt.longName) {
      cerr << "--" << opt.longName;
      if (opt.metaVar) {
        cerr << '=' << opt.metaVar;
      }
      cerr << "\n\t" << opt.help << "\n\n";
    }
  }
}

static void printHelp(HelpSection sec) {
  cerr << "Usage:\n"
            "\tjxltk MODE [options]\n\n"
            "  Split, merge, or examine JPEG XL files.\n\n"
            "  Global options:\n\n";
  printSection(HelpSection::All);
  if ((sec & HelpSection::MergeSplitGen)) {
    cerr << "  Common options for split, merge, and gen modes:\n\n";
    printSection(HelpSection::MergeSplitGen, HelpSection::All);
  }
  if ((sec & HelpSection::Split)) {
    cerr << "\nSPLIT MODE\n\n"
            "\tjxltk split [opts] [input.jxl] [outputdir]\n\n"
            "  Deconstruct a multi-frame JXL into multiple single-frame\n"
            "  images.\n\n"
            "  Options for split mode:\n\n";
    printSection(HelpSection::Split, HelpSection::MergeSplitGen);
  }
  if ((sec & HelpSection::Merge)) {
    cerr << "\nMERGE MODE\n\n"
            "\tjxltk merge [opts] [inputs...] [output.jxl]\n\n"
            "  Construct a multi-frame JXL.\n\n"
            "  Options for merge mode:\n\n"
            "  (Encoding options given on the command line apply to all\n"
            "  frames, and override any settings in merge config files.)\n\n";
    printSection(HelpSection::Merge, HelpSection::MergeSplitGen);
  }
  if ((sec & HelpSection::Gen)) {
    cerr << "\nGEN MODE\n\n"
            "\tjxltk gen [opts] [inputs...]\n\n"
            "  Convenience function that writes a merge config template to\n"
            "  stdout for the named inputs.\n\n"
            "  Options for gen mode:\n\n";
    printSection(HelpSection::Gen, HelpSection::MergeSplitGen);
  }
  if ((sec & HelpSection::Icc)) {
    cerr << "\nICC MODE\n\n"
            "\tjxltk icc [input.jxl] [output.icc]\n\n"
            "  Extract (or synthesize) the ICC profile of a JXL.  The output\n"
            "  name can be omitted or \"-\" to write the ICC to stdout.\n\n";
  }
}

/**
 * Check whether @p file refers to an existing file, and if so, ask
 * the user whether it's OK to overwrite it.  Calls exit() if it's
 * not OK.
 * @param[in] file Path whose existence we're checking.
 * @param[in] usedStdin Whether we're using stdin for one of the input files
 * (and hence we can't read the user's response to our prompt - if the file
 * exists, we'll always exit()).
 * @param isDir True if we're expecting @p file to be the name of a directory
 * to create.
 */
static void confirmOverwrite(string_view file, bool usedStdin, bool isDir) {
  filesystem::file_type ftype = filesystem::status(file).type();
  if (ftype == filesystem::file_type::not_found) {
    return;
  }
  if (usedStdin) {
    JXLTK_ERROR("%s exists - pass `-Y` to %s it.", shellQuote(file).c_str(),
                isDir ? "write into" : "overwrite");
    exit(EXIT_FAILURE);
  }
  if (isDir) {
    if (ftype != filesystem::file_type::directory) {
      JXLTK_ERROR("Can't create directory at %s - file exists.",
                  shellQuote(file).c_str());
      exit(EXIT_FAILURE);
    }
    cerr << "Write output files into existing directory ";
  } else {
    cerr << "Overwrite existing file ";
  }
  cerr << shellQuote(file, true) << "? [y/n] ";
  cerr.flush();
  string answer;
  cin >> answer;
  if (!answer.starts_with("y") && !answer.starts_with("Y")) {
    JXLTK_NOTICE("Not overwriting existing files.");
    exit(EXIT_FAILURE);
  }
}

/**
 * Generate an option list in the format used by optparse, for
 * the given HelpSection.
 *
 * @param[in] exact If true, only include options whose @c sections property
 * is equal to @p section, else include all options whose @c sections property
 * shares any set bit with @p section.
 */
static vector<struct optparse_long> buildOptList(HelpSection section, bool exact) {
  vector<struct optparse_long> longopts;
  constexpr size_t optionCount = sizeof commandLineOptions / sizeof commandLineOptions[0];
  longopts.reserve(optionCount + 1);
  for (const auto& opt : commandLineOptions) {
    if ((exact && opt.sections == section) ||
        (!exact && (opt.sections & section))) {
      longopts.emplace_back(opt.longName, opt.shortName,
                            opt.metaVar ? OPTPARSE_REQUIRED : OPTPARSE_NONE);
    }
  }
  longopts.emplace_back();
  return longopts;
}

CmdlineOpts parseArgs(int argc, char** argv) {
  CmdlineOpts opts;
  HelpSection sec = HelpSection::All;
  if (argc > 1) {
    opts.mode = argv[1];
    if (opts.mode == "merge") {
      sec = HelpSection::Merge;
    } else if (opts.mode == "split") {
      sec = HelpSection::Split;
    } else if (opts.mode == "gen") {
      sec = HelpSection::Gen;
    } else if (opts.mode == "icc") {
      sec = HelpSection::Icc;
    } else  {
      if (opts.mode != "-h" && opts.mode != "--help") {
        JXLTK_ERROR("Invalid mode %s.", shellQuote(opts.mode, true).c_str());
      }
      opts.mode.clear();
    }
  }
  if (opts.mode.empty()) {
    printHelp(HelpSection::All);
    exit(EXIT_FAILURE);
  }

  vector<struct optparse_long> longOpts = buildOptList(sec, false);
  struct optparse options;
  optparse_init(&options, argv + 1);

  int verbosity = 3;  // Notice
  bool usedStdin = false;
  bool usedStdout = false;
  bool overwriteFiles = false;

  int longidx;
  int option;
  while ((option = optparse_long(&options, longOpts.data(), &longidx)) != -1) {
    switch (option) {
      case 'h':
        printHelp(sec);
        exit(EXIT_SUCCESS);

      case 'v':
        ++verbosity;
        continue;

      case 'q':
        --verbosity;
        continue;

      case 'M':
        if (strcmp(options.optarg, "-") == 0) {
          if (usedStdin) {
            JXLTK_ERROR("Can't read multiple things from stdin.");
            exit(EXIT_FAILURE);
          }
          usedStdin = true;
        }
        opts.mergeCfgFilename = options.optarg;
        continue;

      case 'C':
        opts.configOnly = true;
        continue;

      case 'c':
        opts.coalesce = true;
        continue;

      case 'd':
        opts.overrideFrameConfig.distance = stof(options.optarg);
        continue;

      case 'E':
        opts.overrideFrameConfig.maPrevChannels = stoi(options.optarg);
        continue;

      case 'e':
        opts.overrideFrameConfig.effort = stoi(options.optarg);
        continue;

      case 'I':
        /* Modular MA tree learning percent (equivalent to the
         * corresponding cjxl option) */
        opts.overrideFrameConfig.maTreeLearnPct = atoi(options.optarg);
        continue;

      case 'T':
        opts.overrideTps = parseRational(options.optarg);
        if (!opts.overrideTps) {
          JXLTK_ERROR("Invalid argument to --ticks-per-second: %s",
                      shellQuote(options.optarg, true).c_str());
          exit(EXIT_FAILURE);
        }
        continue;

      case 'Y':
        overwriteFiles = true;
        continue;

      case '?':
        JXLTK_ERROR("%s", options.errmsg);
        exit(EXIT_FAILURE);
    }

    const char* longName = longidx >= 0 ? longOpts[longidx].longname : nullptr;
    if (!longName) {
      JXLTK_ERROR("Unknown option.\n");
      exit(EXIT_FAILURE);
    }

    if (strcmp(longName, "best") == 0) {
      opts.overrideFrameConfig.effort = JXLTK_MAX_EFFORT;
      opts.overrideBoxConfig.compress = true;
      opts.overrideBrotliEffort = 11;

    } else if (strcmp(longName, "brotli-effort") == 0) {
      opts.overrideBrotliEffort = atoi(options.optarg);

    } else if (strcmp(longName, "patches") == 0) {
      if ((options.optarg[0] != '1' && options.optarg[0] != '0') ||
          options.optarg[1] != '\0') {
        JXLTK_ERROR("Invalid argument to --patches: %s",
                    shellQuote(options.optarg, true).c_str());
        exit(EXIT_FAILURE);
      }
      opts.overrideFrameConfig.patches = atoi(options.optarg);

    } else if (strcmp(longName, "duration-ms") == 0) {
      int duration = atoi(options.optarg);
      if (duration < 0) {
        JXLTK_ERROR("Invalid argument to --%s: %s", longName,
                    shellQuote(options.optarg, true).c_str());
        exit(EXIT_FAILURE);
      }
      opts.overrideFrameConfig.durationMs = duration;

    } else if (strcmp(longName, "duration-ticks") == 0) {
      int duration = atoi(options.optarg);
      if (duration < 0) {
        JXLTK_ERROR("Invalid argument to --%s: %s", longName,
                    shellQuote(options.optarg, true).c_str());
        exit(EXIT_FAILURE);
      }
      opts.overrideFrameConfig.durationTicks = duration;

    } else if (strcmp(longName, "blend-mode") == 0) {
      JxlBlendMode mode;
      if (!blendModeFromName(options.optarg, &mode)) {
        JXLTK_ERROR("Invalid argument to --blend-mode: %s;\n"
                    "Options are: REPLACE, BLEND, ADD, MUL, MULADD",
                    shellQuote(options.optarg, true).c_str());
        exit(EXIT_FAILURE);
      }
      opts.overrideFrameConfig.blendMode = mode;

    } else if (strcmp(longName, "color-from") == 0 ||
               strcmp(longName, "colour-from") == 0) {
      ColorConfig& cc = opts.overrideColor.emplace();
      cc.type = ColorSpecType::File;
      cc.name = options.optarg;

    } else if (strcmp(longName, "compress-boxes") == 0) {
      int compress = atoi(options.optarg);
      if (compress < -1 || compress > 1) {
        JXLTK_ERROR("Invalid argument to --%s.", longName);
        exit(EXIT_FAILURE);
      }
      if (compress != -1) {
        opts.overrideBoxConfig.compress = compress;
      }

    } else if (strcmp(longName, "data-type") == 0) {
      if (strcmp(options.optarg, "u8") == 0) {
        opts.overrideDataType = JXL_TYPE_UINT8;
      } else if (strcmp(options.optarg, "u16") == 0) {
        opts.overrideDataType = JXL_TYPE_UINT16;
      } else if (strcmp(options.optarg, "f32") == 0) {
        opts.overrideDataType = JXL_TYPE_FLOAT;
      } else {
        JXLTK_ERROR("Invalid argument to --data-type: %s;\n"
                    "Options are: u8, u16, f32",
                    shellQuote(options.optarg, true).c_str());
        exit(EXIT_FAILURE);
      }

    } else if (strcmp(longName, "level") == 0) {
      opts.codestreamLevel = atoi(options.optarg);

    } else if (strcmp(longName, "ms") == 0) {
      opts.useMilliseconds = true;

    } else if (strcmp(longName, "full") == 0) {
      opts.fullConfig = true;

    } else if (strcmp(longName, "threads") == 0) {
      opts.numThreads = stoi(options.optarg);

#ifndef JXLTK_FLOATS_ARE_IEEE754
    } else if (strcmp(longName, "no-754") == 0) {
      opts.no754 = true;
#endif

    }


  }

  jxltkLogThreshold = (verbosity <= 1) ? LogLevel::Error :
                      (verbosity == 2) ? LogLevel::Warning :
                      (verbosity == 3) ? LogLevel::Notice :
                      (verbosity == 4) ? LogLevel::Info :
                      (verbosity == 5) ? LogLevel::Debug :
                      LogLevel::Trace;
  JXLTK_DEBUG("Log level: %s",
              (jxltkLogThreshold == LogLevel::Debug ? "Debug" : "Trace"));

  if (opts.overrideFrameConfig.durationMs && opts.overrideFrameConfig.durationTicks) {
    JXLTK_ERROR("--duration-ms and --duration-ticks are mutually exclusive.");
    exit(EXIT_FAILURE);
  }
  if (opts.overrideFrameConfig.durationMs && opts.overrideTps) {
    JXLTK_ERROR("--duration-ms and --ticks-per-second are mutually exclusive.");
    exit(EXIT_FAILURE);
  }

  // Append remaining positional arguments
  const char* arg;
  while ((arg = optparse_arg(&options)))
    opts.positional.emplace_back(arg);

  if (opts.mode == "merge") {
    if (!opts.mergeCfgFilename.empty() && opts.positional.size() != 1) {
      JXLTK_ERROR("merge mode requires a single output file.");
      exit(EXIT_FAILURE);
    }
    if (opts.mergeCfgFilename.empty() && opts.positional.size() < 2) {
      JXLTK_ERROR("merge mode requires at least one input and exactly one output "
                  "file.");
      exit(EXIT_FAILURE);
    }

    auto fnames = opts.positional.crbegin();
    // Output file
    if (*fnames == "-") {
      if (usedStdout) {
        JXLTK_ERROR("stdout can't be used for more than one type of output in "
                    "the same command.");
        exit(EXIT_FAILURE);
      }
      usedStdout = true;
    } else if (!overwriteFiles) {
      confirmOverwrite(*fnames, usedStdin, false);
    }

    // Input files
    while (++fnames != opts.positional.crend()) {
      if (*fnames == "-") {
        if (usedStdin) {
          JXLTK_ERROR("stdin can't be used for more than one output in "
                      "the same command.");
          exit(EXIT_FAILURE);
        }
        usedStdin = true;
      }
    }

  } else if (opts.mode == "split") {
    if (opts.configOnly && opts.positional.size() != 1) {
      JXLTK_ERROR("%s mode requires a single input file.", opts.mode.c_str());
      exit(EXIT_FAILURE);
    }
    else if (!opts.configOnly && opts.positional.size() != 2) {
      JXLTK_ERROR("split mode requires an input file and an output directory.");
      exit(EXIT_FAILURE);
    }
    if (!opts.configOnly && !overwriteFiles) {
      confirmOverwrite(opts.positional[1], usedStdin, true);
    }
  } else if (opts.mode == "icc") {
    if (opts.positional.size() > 2) {
      JXLTK_ERROR("%s mode requires at most 2 arguments.", opts.mode.c_str());
      exit(EXIT_FAILURE);
    }
  }

  return opts;
}

}  // namespace jxltk
