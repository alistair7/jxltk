/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#include <cinttypes>
#include <iomanip>
#include <sstream>

#include "../contrib/nlohmann/json.hpp"

#include "enums.h"
#include "except.h"
#include "mergeconfig.h"
#include "util.h"

using namespace std;

namespace jxltk {

namespace {

std::string getBoxPrefix(std::optional<size_t> pos) {
  if (!pos) {
    return "boxDefaults";
  }
  char prefix[20];
  snprintf(prefix, sizeof prefix, "boxes[%zu]", *pos);
  return prefix;
}

/**
 * @param[in] pos Current index in the `boxes` array, or std::nullopt if parsing
 * the `boxDefaults`.
 */
BoxConfig boxConfigFromJson(const nlohmann::json& box, std::optional<size_t> pos) {
  BoxConfig boxCfg;

  for (const auto& [key, val] : box.items()) {
    if (key == "type") {
      boxCfg.type = val.get<string>();
      if (boxCfg.type->size() != 4) {
        JXLTK_ERROR_AND_THROW(InvalidConfigError,
                              "%s: Invalid type: %s", getBoxPrefix(pos).c_str(),
                              shellQuote(*boxCfg.type, true).c_str());
      }

    } else if (key == "file") {
      boxCfg.file = val.get<string>();

    } else if (key == "compress") {
      boxCfg.compress = val.get<bool>();

    } else if (key != "comment") {
      JXLTK_ERROR_AND_THROW(InvalidConfigError, "%s: Unknown key %s.",
                            getBoxPrefix(pos).c_str(), shellQuote(key, true).c_str());
    }
  }

  return boxCfg;
}


nlohmann::json boxConfigToJson(const BoxConfig& boxCfg, bool full) {
  nlohmann::json boxObject;
  if (boxCfg.type) {
    if (boxCfg.type->size() != 4) {
      JXLTK_ERROR_AND_THROW(JxltkError, "Invalid box type %s.",
                            shellQuote(*boxCfg.type, true).c_str());
    }
    boxObject["type"] = *boxCfg.type;
  }
  if (boxCfg.file) {
    boxObject["file"] = *boxCfg.file;
  }
  if (boxCfg.compress || full) {
    boxObject["compress"] = boxCfg.compress.value_or(false);
  }
  return boxObject;
}


JxlColorEncoding jxlColorEncodingFromJson(const nlohmann::json& encoding) {
  JxlColorEncoding result;
  JxlColorEncodingSetToSRGB(&result, JXL_FALSE);
  for (const auto& [key, val] : encoding.items()) {
    if (key == "colorSpace") {
      if (!colorSpaceFromName(val.get<string>().c_str(), &result.color_space)) {
        JXLTK_ERROR_AND_THROW(InvalidConfigError,
                              "Invalid color space in 'color/cicp': %s.",
                              shellQuote(key, true).c_str());
      }
    } else if (key == "whitePoint") {
      if (!whitePointFromName(val.get<string>().c_str(), &result.white_point)) {
        JXLTK_ERROR_AND_THROW(InvalidConfigError,
                              "Invalid white point in 'color/cicp': %s.",
                              shellQuote(key, true).c_str());
      }
    } else if (key == "whitePointXy") {
      result.white_point = JXL_WHITE_POINT_CUSTOM;
      if (!val.is_array() || val.size() != 2) {
        JXLTK_ERROR_AND_THROW(InvalidConfigError,
                              "Expected whitePointXy value to be an array of 2 doubles");
      }
      result.white_point_xy[0] = val[0].get<double>();
      result.white_point_xy[1] = val[1].get<double>();
    } else if (key == "primaries") {
      if (!primariesFromName(val.get<string>().c_str(), &result.primaries)) {
        JXLTK_ERROR_AND_THROW(InvalidConfigError,
                              "Invalid primaries in 'color/cicp': %s.",
                              shellQuote(key, true).c_str());
      }
    } else if (key == "primariesRgbXy") {
      result.primaries = JXL_PRIMARIES_CUSTOM;
      if (!val.is_array() || val.size() != 6) {
        JXLTK_ERROR_AND_THROW(InvalidConfigError,
                              "Expected whitePointXy value to be an array of 6 doubles");
      }
      result.primaries_red_xy[0] = val[0].get<double>();
      result.primaries_red_xy[1] = val[1].get<double>();
      result.primaries_green_xy[0] = val[2].get<double>();
      result.primaries_green_xy[1] = val[3].get<double>();
      result.primaries_blue_xy[0] = val[4].get<double>();
      result.primaries_blue_xy[1] = val[5].get<double>();
    } else if (key == "transferFunction") {
      if (!transferFunctionFromName(val.get<string>().c_str(),
                                    &result.transfer_function)) {
        JXLTK_ERROR_AND_THROW(InvalidConfigError,
                              "Invalid transfer function in 'color/cicp': %s.",
                              shellQuote(key, true).c_str());
      }
    } else if (key == "gamma") {
      result.transfer_function = JXL_TRANSFER_FUNCTION_GAMMA;
      result.gamma = val.get<double>();
    } else if (key == "renderingIntent") {
      if (!renderingIntentFromName(val.get<string>().c_str(), &result.rendering_intent)) {
        JXLTK_ERROR_AND_THROW(InvalidConfigError,
                              "Invalid rendering intent in 'color/cicp': %s.",
                              shellQuote(key, true).c_str());
      }
    } else if (key != "comment") {
      JXLTK_ERROR_AND_THROW(InvalidConfigError, "Unknown key in 'color/cicp': %s.",
                            shellQuote(key, true).c_str());
    }
  }
  return result;
}

ColorConfig colorConfigFromJson(const nlohmann::json& color) {
  ColorConfig cc;
  for (const auto& [key, val] : color.items()) {
    if (cc.type != ColorSpecType::None) {
      JXLTK_ERROR_AND_THROW(InvalidConfigError, "Conflicting color encodings specified.");
    }
    if (key == "cicp") {
      cc.type = ColorSpecType::Enum;
      cc.cicp = jxlColorEncodingFromJson(val);
    } else if (key == "file") {
      cc.type = ColorSpecType::File;
      cc.name = val.get<std::string>();
    } else if (key != "comment") {
      JXLTK_ERROR_AND_THROW(InvalidConfigError, "Unknown key in 'color': %s.",
                            shellQuote(key, true).c_str());
    }
  }
  return cc;
}

nlohmann::json colorConfigToJson(const ColorConfig& cc, bool full) {
  nlohmann::ordered_json colorObject;

  if (cc.type == ColorSpecType::File) {
    colorObject["file"] = cc.name;
    return colorObject;
  }
  if (cc.type == ColorSpecType::None && !full) {
    return colorObject;
  }

  JxlColorEncoding defaultColorEncoding;
  JxlColorEncodingSetToSRGB(&defaultColorEncoding, JXL_FALSE);

  auto cicpObject = nlohmann::json::object();
  const JxlColorEncoding& cicp = cc.cicp;
  if (cicp.color_space != defaultColorEncoding.color_space || full) {
    cicpObject["colorSpace"] = colorSpaceName(cicp.color_space);
  }
  if (cicp.white_point != defaultColorEncoding.white_point || full) {
    cicpObject["whitePoint"] = whitePointName(cicp.white_point);
  }
  if (cicp.white_point == JXL_WHITE_POINT_CUSTOM) {
    auto xyArray = nlohmann::json::array();
    xyArray.push_back(cicp.white_point_xy[0]);
    xyArray.push_back(cicp.white_point_xy[1]);
    cicpObject["whitePointXy"] = xyArray;
  }
  if (cicp.primaries != defaultColorEncoding.primaries || full) {
    cicpObject["primaries"] = primariesName(cicp.primaries);
  }
  if (cicp.primaries == JXL_PRIMARIES_CUSTOM) {
    auto xyArray = nlohmann::json::array();
    xyArray.push_back(cicp.primaries_red_xy[0]);
    xyArray.push_back(cicp.primaries_red_xy[1]);
    xyArray.push_back(cicp.primaries_green_xy[0]);
    xyArray.push_back(cicp.primaries_green_xy[1]);
    xyArray.push_back(cicp.primaries_blue_xy[0]);
    xyArray.push_back(cicp.primaries_blue_xy[1]);
    cicpObject["primariesRgbXy"] = xyArray;
  }
  if (cicp.transfer_function != defaultColorEncoding.transfer_function || full) {
    cicpObject["transferFunction"] =
        transferFunctionName(cicp.transfer_function);
  }
  if (cicp.transfer_function == JXL_TRANSFER_FUNCTION_GAMMA) {
    cicpObject["gamma"] = cicp.gamma;
  }
  if (cicp.rendering_intent != defaultColorEncoding.rendering_intent || full) {
    cicpObject["renderingIntent"] = renderingIntentName(cicp.rendering_intent);
  }
  colorObject["cicp"] = cicpObject;
  return colorObject;
}

FrameConfig frameConfigFromJson(const nlohmann::json& frameObj,
                                const char* nodeName) {
  FrameConfig frame;

  for (const auto& [key, val] : frameObj.items()) {
    if (key == "blendMode") {
      string valString = val.get<string>();
      JxlBlendMode blendMode;
      if (!blendModeFromName(valString.c_str(), &blendMode)) {
        JXLTK_ERROR_AND_THROW(InvalidConfigError, "Invalid value for %s in %s: %s",
                             shellQuote(key).c_str(), nodeName,
                             shellQuote(valString).c_str());
      }
      frame.blendMode = blendMode;

    } else if (key == "blendSource") {
      frame.blendSource = val.get<uint32_t>();

    } else if (key == "copyBoxes") {
      frame.copyBoxes = val.get<bool>();

    } else if (key == "cropX0") {
      auto cropX0 = val.get<int32_t>();
      if (!frame.offset)
        frame.offset.emplace(cropX0, 0);
      else
        frame.offset->first = cropX0;

    } else if (key == "cropY0") {
      auto cropY0 = val.get<int32_t>();
      if (!frame.offset)
        frame.offset.emplace(0, cropY0);
      else
        frame.offset->second = cropY0;

    } else if (key == "distance") {
      frame.distance = val.get<float>();

    } else if (key == "durationMs") {
      frame.durationMs = val.get<uint32_t>();

    } else if (key == "durationTicks") {
      frame.durationTicks = val.get<uint32_t>();

    } else if (key == "effort") {
      frame.effort = val.get<int32_t>();

    } else if (key == "file") {
      frame.file = val.get<string>();

    } else if (key == "maPrevChannels") {
      frame.maPrevChannels = val.get<int32_t>();

    } else if (key == "maTreeLearnPct") {
      frame.maTreeLearnPct = val.get<int32_t>();

    } else if (key == "name") {
      frame.name = val.get<string>();

    } else if (key == "patches") {
      frame.patches = val.get<int32_t>();

    } else if (key == "saveAsReference") {
      frame.saveAsReference = val.get<uint32_t>();

    } else if (key != "comment") {
      JXLTK_ERROR_AND_THROW(InvalidConfigError, "Unknown key in %s: %s", nodeName,
                            shellQuote(key, true).c_str());
    }
  }

  return frame;
}

nlohmann::json frameConfigToJson(const FrameConfig& frame, bool full,
                                 const FrameConfig& frameDefaults = {}) {
  auto frameObject = nlohmann::json::object();

  if (frame.blendMode) {
    frameObject["blendMode"] = blendModeName(*frame.blendMode) + 10;
  } else if (full) {
    frameObject["blendMode"] = blendModeName(
      frameDefaults.blendMode.value_or(JXL_BLEND_BLEND)) + 10;
  }
  if (frame.blendSource) {
    frameObject["blendSource"] = *frame.blendSource;
  } else if (full) {
    frameObject["blendSource"] = frameDefaults.blendSource.value_or(0);
  }
  if (frame.copyBoxes) {
    frameObject["copyBoxes"] = *frame.copyBoxes;
  } else if (full) {
    frameObject["copyBoxes"] = frameDefaults.copyBoxes.value_or(false);
  }
  if (frame.distance) {
    frameObject["distance"] = *frame.distance;
  } else if (full) {
    frameObject["distance"] = frameDefaults.distance.value_or(0);
  }
  if (frame.durationMs) {
    frameObject["durationMs"] = *frame.durationMs;
  } else if (frame.durationTicks) {
    frameObject["durationTicks"] = *frame.durationTicks;
  }
  if (frame.effort) {
    frameObject["effort"] = *frame.effort;
  } else if (full) {
    frameObject["effort"] = frameDefaults.effort.value_or(-1);
  }
  if (frame.file) {
    frameObject["file"] = *frame.file;
  } else if (full) {
    frameObject["file"] = frameDefaults.file.value_or("");
  }
  if (frame.maPrevChannels) {
    frameObject["maPrevChannels"] = *frame.maPrevChannels;
  } else if (full) {
    frameObject["maPrevChannels"] =
        frameDefaults.maPrevChannels.value_or(-1);
  }
  if (frame.maTreeLearnPct) {
    frameObject["maTreeLearnPct"] = *frame.maTreeLearnPct;
  } else if (full) {
    frameObject["maTreeLearnPct"] =
        frameDefaults.maTreeLearnPct.value_or(-1);
  }
  if (frame.name) {
    frameObject["name"] = *frame.name;
  } else if (full) {
    frameObject["name"] = frameDefaults.name.value_or("");
  }
  if (frame.offset) {
    frameObject["cropX0"] = frame.offset->first;
    frameObject["cropY0"] = frame.offset->second;
  } else if (full) {
    if (frameDefaults.offset) {
      frameObject["cropX0"] = frameDefaults.offset->first;
      frameObject["cropY0"] = frameDefaults.offset->second;
    } else {
      frameObject["cropX0"] = 0;
      frameObject["cropY0"] = 0;
    }
  }
  if (frame.patches) {
    frameObject["patches"] = *frame.patches;
  } else if (full) {
    frameObject["patches"] = frameDefaults.patches.value_or(-1);
  }
  if (frame.saveAsReference) {
    frameObject["saveAsReference"] = *frame.saveAsReference;
  } else if (full) {
    frameObject["saveAsReference"] = frameDefaults.saveAsReference.value_or(0);
  }
  return frameObject;
}

}  // namespace


bool BoxConfig::isAllDefault() const {
  return !compress && !file && !type;
}

BoxConfig &BoxConfig::update(const BoxConfig &b) {
  if (b.compress) compress = b.compress;
  if (b.file) file = b.file;
  if (b.type) type = b.type;
  return *this;
}

FrameConfig& FrameConfig::update(const FrameConfig& f) {
  if (f.blendMode) blendMode = f.blendMode;
  if (f.blendSource) blendSource = f.blendSource;
  if (f.copyBoxes) copyBoxes = f.copyBoxes;
  if (f.distance) distance = f.distance;
  if (f.durationMs) durationMs = f.durationMs;
  if (f.durationTicks) durationTicks = f.durationTicks;
  if (f.effort) effort = f.effort;
  if (f.file) file = f.file;
  if (f.maPrevChannels) maPrevChannels = f.maPrevChannels;
  if (f.maTreeLearnPct) maTreeLearnPct = f.maTreeLearnPct;
  if (f.name) name = f.name;
  if (f.offset) offset = f.offset;
  if (f.patches) patches = f.patches;
  if (f.saveAsReference) saveAsReference = f.saveAsReference;
  return *this;
}

bool FrameConfig::isAllDefault() const {
  return !blendMode &&
         !blendSource &&
         !copyBoxes &&
         !distance &&
         !durationMs &&
         !durationTicks &&
         !effort &&
         !file &&
         !maPrevChannels &&
         !maTreeLearnPct &&
         !name &&
         !offset &&
         !patches &&
         !saveAsReference;
}

std::string FrameConfig::toString(uint32_t frameXsize,
                                  uint32_t frameYsize) const {
  std::ostringstream oss;
  if (frameXsize && frameYsize) {
    oss << frameXsize << 'x' << frameYsize;
  }
  if (offset) {
    int32_t cropX0 = offset->first;
    int32_t cropY0 = offset->second;
    if (cropX0 != 0 || cropY0 != 0) {
      oss << (cropX0 < 0 ? "" : "+") << offset->first
          << (cropY0 < 0 ? "" : "+") << offset->second;
    }
  }
  if (distance) oss << " d" << *distance;
  if (effort) oss << " e" << *effort;
  if (maPrevChannels) oss << " E" << *maPrevChannels;
  if (maTreeLearnPct) oss << " I" << *maTreeLearnPct;
  if (durationMs) {
    oss << " duration=" << *durationMs << "ms";
  } else if (durationTicks) {
    oss << " duration=" << *durationTicks << "t";
  }
  if (patches) oss << " patches=" << *patches;
  JxlBlendMode blendMode = this->blendMode.value_or(JXL_BLEND_REPLACE);
  oss << " blend={mode="
      << blendModeName(blendMode) + 10
      << " source=" << blendSource.value_or(0);
  uint32_t saveAsReference = this->saveAsReference.value_or(0);
  if (saveAsReference > 0 ||
      (durationMs.value_or(0) == 0 && durationTicks.value_or(0) == 0)) {
    oss << " save=" << saveAsReference;
  }
  oss << '}';
  if (copyBoxes.value_or(false)) oss << " copyBoxes";
  if (name && !name->empty()) oss << " name=" << shellQuote(*name);
  if (file && !file->empty()) oss << " file=" << shellQuote(*file);
  return oss.str();
}

void FrameConfig::normalize() {
  if (effort && *effort == -1) {
    effort.reset();
  }
  if (distance && *distance == -1) {
    distance.reset();
  }
  if (maPrevChannels && *maPrevChannels == -1) {
    maPrevChannels.reset();
  }
  if (maTreeLearnPct && *maTreeLearnPct == -1) {
    maTreeLearnPct.reset();
  }
  if (patches && *patches == -1) {
    patches.reset();
  }
}

MergeConfig fromJson_(istream& in) {
  nlohmann::json json;
  in >> json;

  MergeConfig opts;

  for (const auto& [key, val] : json.items()) {
    if (key == "boxDefaults") {
      opts.boxDefaults = boxConfigFromJson(val, {});

    } else if (key == "boxes") {
      size_t pos = 0;
      for (const auto& box : val) {
        opts.boxes.push_back(boxConfigFromJson(box, pos++));
      }

    } else if (key == "brotliEffort") {
      opts.brotliEffort = val.get<uint32_t>();

    } else if (key == "codestreamLevel") {
      opts.codestreamLevel = val.get<int>();

    } else if (key == "color" || key == "colour") {
      opts.color = colorConfigFromJson(val);

    } else if (key == "frameDefaults") {
      opts.frameDefaults = frameConfigFromJson(val, "frameDefaults");

    } else if (key == "frames") {
      size_t pos = 0;
      for (const auto& input : val) {
        char nodeName[20];
        snprintf(nodeName, sizeof nodeName, "frames[%zu]", pos++);
        opts.frames.emplace_back(frameConfigFromJson(input, nodeName));
      }

    } else if (key == "intrinsicXsize") {
      if (uint32_t size = val.get<uint32_t>()) opts.intrinsicXsize = size;

    } else if (key == "intrinsicYsize") {
      if (uint32_t size = val.get<uint32_t>()) opts.intrinsicYsize = size;

    } else if (key == "loops") {
      opts.loops = val.get<uint32_t>();

    } else if (key == "orientation") {
      string valString = val.get<string>();
      JxlOrientation o;
      if (!orientationFromName(val.get<string>().c_str(), &o)) {
        JXLTK_ERROR_AND_THROW(InvalidConfigError, "Invalid value for orientation: %s",
                              shellQuote(valString).c_str());
      }
      opts.orientation = o;

    } else if (key == "ticksPerSecond") {
      string tpsString = val.get<string>();
      auto tpsRational = parseRational(tpsString.c_str());
      if (!tpsRational) {
        JXLTK_ERROR_AND_THROW(InvalidConfigError, "Invalid ticks-per-second value: %s",
                              tpsString.c_str());
      }
      opts.tps.emplace(*tpsRational);

    } else if (key == "xsize") {
      opts.xsize = val.get<uint32_t>();
      if (*opts.xsize == 0) {
        JXLTK_ERROR_AND_THROW(InvalidConfigError, "Invalid value for xsize: %" PRIu32,
                              *opts.xsize);
      }

    } else if (key == "ysize") {
      opts.ysize = val.get<uint32_t>();
      if (*opts.ysize == 0) {
        JXLTK_ERROR_AND_THROW(InvalidConfigError, "Invalid value for ysize: %" PRIu32,
                              *opts.ysize);
      }

    } else if (key != "comment") {
      JXLTK_ERROR_AND_THROW(InvalidConfigError, "Unknown key at top level: %s.",
                            shellQuote(key, true).c_str());
    }
  }

  return opts;
}

MergeConfig MergeConfig::fromJson(istream& in) {
  try {
    return fromJson_(in);
  } catch (const nlohmann::json::exception& ex) {
    throw InvalidConfigError("Generic JSON parsing error: %s", ex.what());
  }
}

bool MergeConfig::toJson(ostream& to, bool full /*=false*/) const {
  nlohmann::ordered_json json;

  if (loops)
    json["loops"] = *loops;
  else if (full)
    json["loops"] = 0u;

  if (tps) {
    std::ostringstream oss;
    oss << tps->first;
    if (tps->second != 1) {
      oss << '/' << tps->second;
    }
    json["ticksPerSecond"] = oss.str();
  }

  if (intrinsicXsize) json["intrinsicXsize"] = *intrinsicXsize;
  if (intrinsicYsize) json["intrinsicYsize"] = *intrinsicYsize;

  if (orientation)
    json["orientation"] = orientationName(*orientation) + 11;
  else if (full)
    json["orientation"] = orientationName(JXL_ORIENT_IDENTITY) + 11;

  if (xsize) json["xsize"] = *xsize;
  if (ysize) json["ysize"] = *ysize;

  if (color) {
    json["color"] = colorConfigToJson(*color, full);
  }

  if (codestreamLevel) {
    json["codestreamLevel"] = *codestreamLevel;
  }

  if (full || !frameDefaults.isAllDefault()) {
    json["frameDefaults"] = frameConfigToJson(frameDefaults, full);
  }

  if (full || !boxDefaults.isAllDefault()) {
    json["boxDefaults"] = boxConfigToJson(boxDefaults, full);
  }

  auto framesArray = nlohmann::json::array();
  for (const FrameConfig& frame : frames) {
    nlohmann::json frameObject = frameConfigToJson(frame, full, frameDefaults);
    framesArray.push_back(std::move(frameObject));
  }
  json["frames"] = framesArray;

  auto boxesArray = nlohmann::json::array();
  for (const BoxConfig& box : boxes) {
    boxesArray.push_back(boxConfigToJson(box, full));
  }

  if (!boxesArray.empty()) json["boxes"] = boxesArray;

  to << std::setw(2) << json;
  return true;
}

void MergeConfig::normalize() {
  if (brotliEffort && *brotliEffort == -1) {
    brotliEffort.reset();
  }
  for (auto& frame : frames) {
    frame.normalize();
  }
  frameDefaults.normalize();
}

}  // namespace jxltk
