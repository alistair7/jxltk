/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#include <cstring>
#include <iomanip>

#include "enums.h"

namespace jxltk {

namespace {

int strncasecmp(const char* a, const char* b, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    int ca = tolower(static_cast<unsigned char>(a[i]));
    int cb = tolower(static_cast<unsigned char>(b[i]));
    int diff = ca - cb;
    if (diff != 0 || ca == 0) {
      return diff;
    }
  }
  return 0;
}

int strcasecmp(const char* a, const char* b) {
  return jxltk::strncasecmp(a, b, SIZE_MAX);
}

}  // namespace

bool blendModeFromName(const char* name, JxlBlendMode* mode) {
  if (jxltk::strncasecmp(name, "JXL_BLEND_", 10) == 0) {
    name += 10;
  }
  if (jxltk::strcasecmp(name, "REPLACE") == 0) *mode = JXL_BLEND_REPLACE;
  else if (jxltk::strcasecmp(name, "BLEND") == 0) *mode = JXL_BLEND_BLEND;
  else if (jxltk::strcasecmp(name, "ADD") == 0) *mode = JXL_BLEND_ADD;
  else if (jxltk::strcasecmp(name, "MUL") == 0) *mode = JXL_BLEND_MUL;
  else if (jxltk::strcasecmp(name, "MULADD") == 0) *mode = JXL_BLEND_MULADD;
  else {
    *mode = JXL_BLEND_REPLACE;
    return false;
  }
  return true;
}

const char* blendModeName(JxlBlendMode b) {
  switch (b) {
    case JXL_BLEND_REPLACE: return "JXL_BLEND_REPLACE";
    case JXL_BLEND_BLEND: return "JXL_BLEND_BLEND";
    case JXL_BLEND_ADD: return "JXL_BLEND_ADD";
    case JXL_BLEND_MUL: return "JXL_BLEND_MUL";
    case JXL_BLEND_MULADD: return "JXL_BLEND_MULADD";
  }
  return "Unknown blend mode";
}

const char* channelTypeName(JxlExtraChannelType t) {
  switch (t) {
    case JXL_CHANNEL_ALPHA: return "JXL_CHANNEL_ALPHA";
    case JXL_CHANNEL_BLACK: return "JXL_CHANNEL_BLACK";
    case JXL_CHANNEL_CFA: return "JXL_CHANNEL_CFA";
    case JXL_CHANNEL_DEPTH: return "JXL_CHANNEL_DEPTH";
    case JXL_CHANNEL_OPTIONAL: return "JXL_CHANNEL_OPTIONAL";
    case JXL_CHANNEL_SELECTION_MASK: return "JXL_CHANNEL_SELECTION_MASK";
    case JXL_CHANNEL_SPOT_COLOR: return "JXL_CHANNEL_SPOT_COLOR";
    case JXL_CHANNEL_THERMAL: return "JXL_CHANNEL_THERMAL";
    case JXL_CHANNEL_UNKNOWN: return "JXL_CHANNEL_UNKNOWN";
    case JXL_CHANNEL_RESERVED0:
    case JXL_CHANNEL_RESERVED1:
    case JXL_CHANNEL_RESERVED2:
    case JXL_CHANNEL_RESERVED3:
    case JXL_CHANNEL_RESERVED4:
    case JXL_CHANNEL_RESERVED5:
    case JXL_CHANNEL_RESERVED6:
    case JXL_CHANNEL_RESERVED7:
      return "JXL_CHANNEL_RESERVED";
  }
  return "Unknown channel type";
}

bool colorSpaceFromName(const char* name, JxlColorSpace* s) {
  if (jxltk::strncasecmp(name, "JXL_COLOR_SPACE_", 16) == 0) {
    name += 16;
  }
  if (jxltk::strcasecmp(name, "RGB") == 0) *s = JXL_COLOR_SPACE_RGB;
  else if (jxltk::strcasecmp(name, "GRAY") == 0) *s = JXL_COLOR_SPACE_GRAY;
  else if (jxltk::strcasecmp(name, "XYB") == 0) *s = JXL_COLOR_SPACE_XYB;
  else if (jxltk::strcasecmp(name, "UNKNOWN") == 0) *s = JXL_COLOR_SPACE_UNKNOWN;
  else {
    *s = JXL_COLOR_SPACE_UNKNOWN;
    return false;
  }
  return true;
}

const char* colorSpaceName(JxlColorSpace s) {
  switch (s) {
    case JXL_COLOR_SPACE_RGB: return "JXL_COLOR_SPACE_RGB";
    case JXL_COLOR_SPACE_GRAY: return "JXL_COLOR_SPACE_GRAY";
    case JXL_COLOR_SPACE_XYB: return "JXL_COLOR_SPACE_XYB";
    case JXL_COLOR_SPACE_UNKNOWN: return "JXL_COLOR_SPACE_UNKNOWN";
  }
  return "Unknown color space";
}

const char* dataTypeName(JxlDataType t) {
  switch (t) {
    case JXL_TYPE_UINT8: return "uint8";
    case JXL_TYPE_UINT16: return "uint16";
    case JXL_TYPE_FLOAT: return "float32";
    case JXL_TYPE_FLOAT16: return "float16";
  }
  return "unknown";
}

const char* decoderStatusName(JxlDecoderStatus s) {
  switch (s) {
    case JXL_DEC_BASIC_INFO:
      return "JXL_DEC_BASIC_INFO";
    case JXL_DEC_BOX:
      return "JXL_DEC_BOX";
    case JXL_DEC_BOX_NEED_MORE_OUTPUT:
      return "JXL_DEC_BOX_NEED_MORE_OUTPUT";
    case JXL_DEC_COLOR_ENCODING:
      return "JXL_DEC_COLOR_ENCODING";
    case JXL_DEC_ERROR:
      return "JXL_DEC_ERROR";
#if JPEGXL_NUMERIC_VERSION < JPEGXL_COMPUTE_NUMERIC_VERSION(0, 9, 0)
    case JXL_DEC_EXTENSIONS:
      return "JXL_DEC_EXTENSIONS";
#endif
    case JXL_DEC_FRAME:
      return "JXL_DEC_FRAME";
    case JXL_DEC_FRAME_PROGRESSION:
      return "JXL_DEC_FRAME_PROGRESSION";
    case JXL_DEC_FULL_IMAGE:
      return "JXL_DEC_FULL_IMAGE";
    case JXL_DEC_JPEG_NEED_MORE_OUTPUT:
      return "JXL_DEC_JPEG_NEED_MORE_OUTPUT";
    case JXL_DEC_JPEG_RECONSTRUCTION:
      return "JXL_DEC_JPEG_RECONSTRUCTION";
    case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
      return "JXL_DEC_NEED_IMAGE_OUT_BUFFER";
    case JXL_DEC_NEED_MORE_INPUT:
      return "JXL_DEC_NEED_MORE_INPUT";
    case JXL_DEC_NEED_PREVIEW_OUT_BUFFER:
      return "JXL_DEC_NEED_PREVIEW_OUT_BUFFER";
    case JXL_DEC_PREVIEW_IMAGE:
      return "JXL_DEC_PREVIEW_IMAGE";
    case JXL_DEC_SUCCESS:
      return "JXL_DEC_SUCCESS";
#if JPEGXL_NUMERIC_VERSION >= JPEGXL_COMPUTE_NUMERIC_VERSION(0, 11, 0)
    case JXL_DEC_BOX_COMPLETE:
      return "JXL_DEC_BOX_COMPLETE";
#endif
  }
  return "Unknown decoder status";
}

const char* encoderStatusName(JxlEncoderStatus s) {
  switch (s) {
    case JXL_ENC_ERROR: return "JXL_ENC_ERROR";
    case JXL_ENC_NEED_MORE_OUTPUT: return "JXL_ENC_NEED_MORE_OUTPUT";
#if JPEGXL_NUMERIC_VERSION < JPEGXL_COMPUTE_NUMERIC_VERSION(0, 9, 0)
    case JXL_ENC_NOT_SUPPORTED: return "JXL_ENC_NOT_SUPPORTED";
#endif
    case JXL_ENC_SUCCESS: return "JXL_ENC_SUCCESS";
  }
  return "Unknown encoder status";
}

bool orientationFromName(const char* name, JxlOrientation* o) {
  if (jxltk::strncasecmp(name, "JXL_ORIENT_", 11) == 0) {
    name += 11;
  }
  if (jxltk::strcasecmp(name, "IDENTITY") == 0) *o = JXL_ORIENT_IDENTITY;
  else if (jxltk::strcasecmp(name, "ROTATE_90_CCW") == 0) *o = JXL_ORIENT_ROTATE_90_CCW;
  else if (jxltk::strcasecmp(name, "ROTATE_180") == 0) *o = JXL_ORIENT_ROTATE_180;
  else if (jxltk::strcasecmp(name, "ROTATE_90_CW") == 0) *o = JXL_ORIENT_ROTATE_90_CW;
  else if (jxltk::strcasecmp(name, "FLIP_HORIZONTAL") == 0) *o = JXL_ORIENT_FLIP_HORIZONTAL;
  else if (jxltk::strcasecmp(name, "FLIP_VERTICAL") == 0) *o = JXL_ORIENT_FLIP_VERTICAL;
  else if (jxltk::strcasecmp(name, "TRANSPOSE") == 0) *o = JXL_ORIENT_TRANSPOSE;
  else if (jxltk::strcasecmp(name, "ANTI_TRANSPOSE") == 0) *o = JXL_ORIENT_ANTI_TRANSPOSE;
  else {
    *o = JXL_ORIENT_IDENTITY;
    return false;
  }
  return true;
}

const char* orientationName(JxlOrientation o) {
  switch (o) {
    case JXL_ORIENT_IDENTITY: return "JXL_ORIENT_IDENTITY";
    case JXL_ORIENT_ROTATE_90_CCW: return "JXL_ORIENT_ROTATE_90_CCW";
    case JXL_ORIENT_ROTATE_180: return "JXL_ORIENT_ROTATE_180";
    case JXL_ORIENT_ROTATE_90_CW: return "JXL_ORIENT_ROTATE_90_CW";
    case JXL_ORIENT_FLIP_HORIZONTAL: return "JXL_ORIENT_FLIP_HORIZONTAL";
    case JXL_ORIENT_FLIP_VERTICAL: return "JXL_ORIENT_FLIP_VERTICAL";
    case JXL_ORIENT_TRANSPOSE: return "JXL_ORIENT_TRANSPOSE";
    case JXL_ORIENT_ANTI_TRANSPOSE: return "JXL_ORIENT_ANTI_TRANSPOSE";
  }
  return "Unknown orientation";
}

bool primariesFromName(const char* name, JxlPrimaries* p) {
  if (jxltk::strncasecmp(name, "JXL_PRIMARIES_", 14) == 0) {
    name += 14;
  }
  if (jxltk::strcasecmp(name, "SRGB") == 0) *p = JXL_PRIMARIES_SRGB;
  else if (jxltk::strcasecmp(name, "2100") == 0) *p = JXL_PRIMARIES_2100;
  else if (jxltk::strcasecmp(name, "P3") == 0) *p = JXL_PRIMARIES_P3;
  else if (jxltk::strcasecmp(name, "CUSTOM") == 0) *p = JXL_PRIMARIES_CUSTOM;
  else {
    *p = JXL_PRIMARIES_SRGB;
    return false;
  }
  return true;
}

const char* primariesName(JxlPrimaries p) {
  switch (p) {
    case JXL_PRIMARIES_SRGB: return "JXL_PRIMARIES_SRGB";
    case JXL_PRIMARIES_2100: return "JXL_PRIMARIES_2100";
    case JXL_PRIMARIES_P3: return "JXL_PRIMARIES_P3";
    case JXL_PRIMARIES_CUSTOM: return "JXL_PRIMARIES_CUSTOM";
  }
  return "Unknown primaries";
}

bool renderingIntentFromName(const char* name, JxlRenderingIntent* r) {
  if (jxltk::strncasecmp(name, "JXL_RENDERING_INTENT_", 21) == 0) {
    name += 21;
  }
  if (jxltk::strcasecmp(name, "RELATIVE") == 0) *r = JXL_RENDERING_INTENT_RELATIVE;
  else if (jxltk::strcasecmp(name, "PERCEPTUAL") == 0) *r = JXL_RENDERING_INTENT_PERCEPTUAL;
  else if (jxltk::strcasecmp(name, "ABSOLUTE") == 0) *r = JXL_RENDERING_INTENT_ABSOLUTE;
  else if (jxltk::strcasecmp(name, "SATURATION") == 0) *r = JXL_RENDERING_INTENT_SATURATION;
  else {
    *r = JXL_RENDERING_INTENT_RELATIVE;
    return false;
  }
  return true;
}

const char* renderingIntentName(JxlRenderingIntent r) {
  switch (r) {
    case JXL_RENDERING_INTENT_RELATIVE: return "JXL_RENDERING_INTENT_RELATIVE";
    case JXL_RENDERING_INTENT_PERCEPTUAL: return "JXL_RENDERING_INTENT_PERCEPTUAL";
    case JXL_RENDERING_INTENT_ABSOLUTE: return "JXL_RENDERING_INTENT_ABSOLUTE";
    case JXL_RENDERING_INTENT_SATURATION: return "JXL_RENDERING_INTENT_SATURATION";
  }
  return "Unknown rendering intent";
}

bool transferFunctionFromName(const char* name, JxlTransferFunction* t) {
  if (jxltk::strncasecmp(name, "JXL_TRANSFER_FUNCTION_", 22) == 0) {
    name += 22;
  }
  if (jxltk::strcasecmp(name, "SRGB") == 0) *t = JXL_TRANSFER_FUNCTION_SRGB;
  else if (jxltk::strcasecmp(name, "GAMMA") == 0) *t = JXL_TRANSFER_FUNCTION_GAMMA;
  else if (jxltk::strcasecmp(name, "LINEAR") == 0) *t = JXL_TRANSFER_FUNCTION_LINEAR;
  else if (jxltk::strcasecmp(name, "709") == 0) *t = JXL_TRANSFER_FUNCTION_709;
  else if (jxltk::strcasecmp(name, "DCI") == 0) *t = JXL_TRANSFER_FUNCTION_DCI;
  else if (jxltk::strcasecmp(name, "HLG") == 0) *t = JXL_TRANSFER_FUNCTION_HLG;
  else if (jxltk::strcasecmp(name, "PQ") == 0) *t = JXL_TRANSFER_FUNCTION_PQ;
  else if (jxltk::strcasecmp(name, "UNKNOWN") == 0) *t = JXL_TRANSFER_FUNCTION_UNKNOWN;
  else {
    *t = JXL_TRANSFER_FUNCTION_UNKNOWN;
    return false;
  }
  return true;
}

const char* transferFunctionName(JxlTransferFunction t) {
  switch (t) {
    case JXL_TRANSFER_FUNCTION_SRGB: return "JXL_TRANSFER_FUNCTION_SRGB";
    case JXL_TRANSFER_FUNCTION_GAMMA: return "JXL_TRANSFER_FUNCTION_GAMMA";
    case JXL_TRANSFER_FUNCTION_LINEAR: return "JXL_TRANSFER_FUNCTION_LINEAR";
    case JXL_TRANSFER_FUNCTION_709: return "JXL_TRANSFER_FUNCTION_709";
    case JXL_TRANSFER_FUNCTION_DCI: return "JXL_TRANSFER_FUNCTION_DCI";
    case JXL_TRANSFER_FUNCTION_HLG: return "JXL_TRANSFER_FUNCTION_HLG";
    case JXL_TRANSFER_FUNCTION_PQ: return "JXL_TRANSFER_FUNCTION_PQ";
    case JXL_TRANSFER_FUNCTION_UNKNOWN: return "JXL_TRANSFER_FUNCTION_UNKNOWN";
  }
  return "Unknown transfer function";
}

bool whitePointFromName(const char* name, JxlWhitePoint* w) {
  if (jxltk::strncasecmp(name, "JXL_WHITE_POINT_", 16) == 0) {
    name += 16;
  }
  if (jxltk::strcasecmp(name, "D65") == 0) *w = JXL_WHITE_POINT_D65;
  else if (jxltk::strcasecmp(name, "DCI") == 0) *w = JXL_WHITE_POINT_DCI;
  else if (jxltk::strcasecmp(name, "E") == 0) *w = JXL_WHITE_POINT_E;
  else if (jxltk::strcasecmp(name, "CUSTOM") == 0) *w = JXL_WHITE_POINT_CUSTOM;
  else {
    *w = JXL_WHITE_POINT_D65;;
    return false;
  }
  return true;
}

const char* whitePointName(JxlWhitePoint w) {
  switch (w) {
    case JXL_WHITE_POINT_D65: return "JXL_WHITE_POINT_D65";
    case JXL_WHITE_POINT_DCI: return "JXL_WHITE_POINT_DCI";
    case JXL_WHITE_POINT_E: return "JXL_WHITE_POINT_E";
    case JXL_WHITE_POINT_CUSTOM: return "JXL_WHITE_POINT_CUSTOM";
  }
  return "Unknown white point";
}

void printDecoderEventNames(std::ostream& out, int events) {
  bool first = true;
  static const JxlDecoderStatus statuses[] = {
      JXL_DEC_BASIC_INFO,
#if JPEGXL_NUMERIC_VERSION < JPEGXL_COMPUTE_NUMERIC_VERSION(0, 9, 0)
      JXL_DEC_EXTENSIONS,
#endif
      JXL_DEC_COLOR_ENCODING,    JXL_DEC_PREVIEW_IMAGE,       JXL_DEC_FRAME,
      JXL_DEC_FULL_IMAGE,        JXL_DEC_JPEG_RECONSTRUCTION, JXL_DEC_BOX,
      JXL_DEC_FRAME_PROGRESSION,
#if JPEGXL_NUMERIC_VERSION >= JPEGXL_COMPUTE_NUMERIC_VERSION(0, 11, 0)
      JXL_DEC_BOX_COMPLETE,
#endif
  };

  for (auto s : statuses) {
    if ((events & s)) {
      if (!first) out << '|';
      out << decoderStatusName(s);
      first = false;
    }
  }
}

std::ostream& operator<<(std::ostream& out, const JxlBasicInfo& bi) {
  out << bi.xsize << 'x' << bi.ysize;
  if (bi.intrinsic_xsize > 0 &&
      (bi.intrinsic_xsize != bi.xsize || bi.intrinsic_ysize != bi.ysize)) {
    out << " (intrinsic " << bi.intrinsic_xsize << 'x' << bi.intrinsic_ysize
        << ')';
  }
  out << ", " << bi.num_color_channels << "+" << bi.num_extra_channels
      << " channels (" << bi.bits_per_sample << "-bit color";
  if (bi.alpha_bits > 0) out << ", " << bi.alpha_bits << "-bit alpha";
  out << "), uses_original_profile="
      << (bi.uses_original_profile ? "yes" : "no");

  if (bi.have_animation) {
    out << " animated: ";
    if (bi.animation.num_loops == 0)
      out << "infinite";
    else
      out << bi.animation.num_loops;
    out << " loops ";
    if (bi.animation.tps_denominator == 1) {
      out << bi.animation.tps_numerator << "t/s";
    } else if (bi.animation.tps_denominator != 0) {
      out << '(' << bi.animation.tps_numerator << '/'
          << bi.animation.tps_denominator << ") = " << std::fixed
          << std::setprecision(2)
          << (static_cast<float>(bi.animation.tps_numerator) /
              bi.animation.tps_denominator)
          << "t/s";
    }
  }

  return out;
}

std::ostream& operator<<(std::ostream& out, const JxlPixelFormat& pf) {
  out << pf.num_channels << '*' << dataTypeName(pf.data_type);

  if (pf.data_type != JXL_TYPE_UINT8) {
    if (pf.endianness == JXL_BIG_ENDIAN)
      out << "-be";
    else if (pf.endianness == JXL_LITTLE_ENDIAN)
      out << "-le";
    // else JXL_NATIVE_ENDIAN
  }

  if (pf.align > 1) out << '@' << pf.align << 'B';
  return out;
}

std::ostream& operator<<(std::ostream& out, const JxlColorEncoding& ec) {
  switch (ec.color_space) {
    case JXL_COLOR_SPACE_RGB:
      out << "RGB";
      break;
    case JXL_COLOR_SPACE_GRAY:
      out << "Gray";
      break;
    case JXL_COLOR_SPACE_XYB:
      out << "XYB";
      break;
    default:
      out << "Unknown";
      break;
  }

  switch (ec.white_point) {
    case JXL_WHITE_POINT_D65:
      out << " D65";
      break;
    case JXL_WHITE_POINT_DCI:
      out << " DCI";
      break;
    case JXL_WHITE_POINT_E:
      out << " E";
      break;
    case JXL_WHITE_POINT_CUSTOM:
      out << " (" << ec.white_point_xy[0] << "," << ec.white_point_xy[1] << ")";
      break;
  }

  switch (ec.primaries) {
    case JXL_PRIMARIES_SRGB:
      out << " sRGB";
      break;
    case JXL_PRIMARIES_P3:
      out << " P3";
      break;
    case JXL_PRIMARIES_2100:
      out << " 2100";
      break;
    default:
      out << " [(" << ec.primaries_red_xy[0] << "," << ec.primaries_red_xy[1]
          << ")," << "[(" << ec.primaries_green_xy[0] << ","
          << ec.primaries_green_xy[1] << ")," << "[(" << ec.primaries_blue_xy[0]
          << "," << ec.primaries_blue_xy[1] << ")]";
  }

  switch (ec.transfer_function) {
    case JXL_TRANSFER_FUNCTION_SRGB:
      out << " sRGB";
      break;
    case JXL_TRANSFER_FUNCTION_GAMMA:
      out << " gamma=" << ec.gamma;
      break;
    case JXL_TRANSFER_FUNCTION_709:
      out << " 709";
      break;
    case JXL_TRANSFER_FUNCTION_DCI:
      out << " DCI";
      break;
    case JXL_TRANSFER_FUNCTION_HLG:
      out << " HLG";
      break;
    case JXL_TRANSFER_FUNCTION_LINEAR:
      out << " linear";
      break;
    case JXL_TRANSFER_FUNCTION_PQ:
      out << " PQ";
      break;
    default:
      out << " unknown";
      break;
  }

  return out;
}


}  // namespace jxltk
