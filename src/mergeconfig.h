/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#ifndef JXLTK_MERGECONFIG_H_
#define JXLTK_MERGECONFIG_H_

#include <jxl/codestream_header.h>
#include <jxl/color_encoding.h>

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace jxltk {

struct BoxConfig {
  BoxConfig() = default;
  std::optional<std::string> type{};
  std::optional<std::string> file{};
  std::optional<bool> compress{};

  bool isAllDefault() const;

  /**
   * For all fields in @p overrides that contain a value, copy that value
   * into the corresponding field of this object.
   * Other fields are left untouched.
   * @return `*this`
   */
  BoxConfig& update(const BoxConfig& overrides);
};

enum class ColorSpecType : uint8_t {
  None,
  File,
  //String,
  Enum,
};

struct ColorConfig {
  ColorSpecType type{ColorSpecType::None};
  /*
   * File name if type == File, compact profile description if type == String
   */
  std::string name{};
  JxlColorEncoding cicp{};
};


/**
 * Encoding settings for a single frame.
 * Also used to pass global overrides from command line.
 */
struct FrameConfig {
  std::optional<JxlBlendMode> blendMode{};
  std::optional<uint32_t> blendSource{};
  std::optional<bool> copyBoxes{};
  std::optional<float> distance{};
  std::optional<uint32_t> durationMs{};
  std::optional<uint32_t> durationTicks{};
  std::optional<int32_t> effort{};
  std::optional<std::string> file{};
  std::optional<int32_t> maPrevChannels{};
  std::optional<int32_t> maTreeLearnPct{};
  std::optional<std::string> name{};
  std::optional<std::pair<int32_t,int32_t> > offset{};
  std::optional<int32_t> patches{};
  std::optional<uint32_t> saveAsReference{};

  /**
   * For all fields in @p overrides that contain a value, copy that value
   * into the corresponding field of this object.
   * Other fields are left untouched.
   * @return `*this`
   */
  FrameConfig& update(const FrameConfig& overrides);

  bool isAllDefault() const;

  /**
   * Return a brief description of this frame config.
   * @param[in] frameXsize,frameYsize Dimensions of this frame to include in
   * the result (as the size is normally implicit and not part of the FrameConfig).
   */
  std::string toString(uint32_t frameXsize = 0,
                       uint32_t frameYsize = 0) const;

  /**
   * Find optional fields that are set to -1, meaning "use the library default",
   * and unset these fields, since passing -1 to the encoder doesn't work.
   */
  void normalize();
};

struct MergeConfig {
  std::optional<uint32_t> loops{};
  std::optional<std::pair<uint32_t,uint32_t> > tps{};
  std::optional<JxlOrientation> orientation{};
  std::optional<ColorConfig> color{};
  std::optional<JxlDataType> dataType{};
  std::optional<uint32_t> intrinsicXsize{};
  std::optional<uint32_t> intrinsicYsize{};
  std::optional<uint32_t> xsize{};
  std::optional<uint32_t> ysize{};
  BoxConfig boxDefaults{};
  FrameConfig frameDefaults{};
  std::optional<int> codestreamLevel{};
  std::optional<int32_t> brotliEffort{};

  std::vector<FrameConfig> frames{};
  std::vector<BoxConfig> boxes{};

  /**
   * Parse a JSON merge config file
   *
   * @param[in,out] in Stream from which the JSON is read.
   */
  static MergeConfig fromJson(std::istream& in);

  /**
   * Serialise to JSON
   *
   * @param[in,out] to Stream where the JSON is written.
   * @param[in] full If true, include keys that have their default value.
   * @return true if the JSON was written successfully, false if the config
   * is invalid.
   */
  bool toJson(std::ostream& to, bool full = false) const;

  /**
   * Find optional fields that are set to -1, meaning "use the library default",
   * and unset these fields, since passing -1 to the encoder doesn't work.
   */
  void normalize();
};

}  // namespace jxltk

#endif  // JXLTK_MERGECONFIG_H_
