/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#include <iomanip>
#include <iostream>

#include <jxl/decode.h>

using std::ostream;

namespace jxlazy {


namespace {

const char* dataTypeName(JxlDataType t) {
  switch(t) {
  case JXL_TYPE_UINT8:   return "uint8";
  case JXL_TYPE_UINT16:  return "uint16";
  case JXL_TYPE_FLOAT:   return "float32";
  case JXL_TYPE_FLOAT16: return "float16";
  }
  return "unknown";
}

}  // namespace


const char* decoderEventName(JxlDecoderStatus s) {
  switch(s) {
  case JXL_DEC_BASIC_INFO:              return "JXL_DEC_BASIC_INFO";
  case JXL_DEC_BOX:                     return "JXL_DEC_BOX";
  case JXL_DEC_BOX_NEED_MORE_OUTPUT:    return "JXL_DEC_BOX_NEED_MORE_OUTPUT";
  case JXL_DEC_COLOR_ENCODING:          return "JXL_DEC_COLOR_ENCODING";
  case JXL_DEC_ERROR:                   return "JXL_DEC_ERROR";
#if JPEGXL_NUMERIC_VERSION < JPEGXL_COMPUTE_NUMERIC_VERSION(0,9,0)
  case JXL_DEC_EXTENSIONS:              return "JXL_DEC_EXTENSIONS";
#endif
  case JXL_DEC_FRAME:                   return "JXL_DEC_FRAME";
  case JXL_DEC_FRAME_PROGRESSION:       return "JXL_DEC_FRAME_PROGRESSION";
  case JXL_DEC_FULL_IMAGE:              return "JXL_DEC_FULL_IMAGE";
  case JXL_DEC_JPEG_NEED_MORE_OUTPUT:   return "JXL_DEC_JPEG_NEED_MORE_OUTPUT";
  case JXL_DEC_JPEG_RECONSTRUCTION:     return "JXL_DEC_JPEG_RECONSTRUCTION";
  case JXL_DEC_NEED_IMAGE_OUT_BUFFER:   return "JXL_DEC_NEED_IMAGE_OUT_BUFFER";
  case JXL_DEC_NEED_MORE_INPUT:         return "JXL_DEC_NEED_MORE_INPUT";
  case JXL_DEC_NEED_PREVIEW_OUT_BUFFER: return "JXL_DEC_NEED_PREVIEW_OUT_BUFFER";
  case JXL_DEC_PREVIEW_IMAGE:           return "JXL_DEC_PREVIEW_IMAGE";
#if JPEGXL_NUMERIC_VERSION >= JPEGXL_COMPUTE_NUMERIC_VERSION(0,11,0)
  case JXL_DEC_BOX_COMPLETE:            return "JXL_DEC_BOX_COMPLETE";
#endif
  case JXL_DEC_SUCCESS:                 return "JXL_DEC_SUCCESS";
  }
  return "JXL_DEC_???";
}

void printDecoderEventNames(ostream& out, int events) {
  bool first = true;
  static const JxlDecoderStatus statuses[] = {
    JXL_DEC_BASIC_INFO,
#if JPEGXL_NUMERIC_VERSION < JPEGXL_COMPUTE_NUMERIC_VERSION(0,9,0)
    JXL_DEC_EXTENSIONS,
#endif
    JXL_DEC_COLOR_ENCODING,
    JXL_DEC_PREVIEW_IMAGE,
    JXL_DEC_FRAME,
    JXL_DEC_FULL_IMAGE,
    JXL_DEC_JPEG_RECONSTRUCTION,
    JXL_DEC_BOX,
    JXL_DEC_FRAME_PROGRESSION,
  };

  for(auto s : statuses) {
    if((events & s)) {
      if(!first)
        out << '|';
      out << decoderEventName(s);
      first = false;
    }
  }
}

inline namespace operators {

ostream& operator<<(ostream& out, const JxlBasicInfo& bi) {
  out << bi.xsize << 'x' << bi.ysize;
  if (bi.xsize > 0 && (bi.intrinsic_xsize != bi.xsize || bi.intrinsic_ysize != bi.ysize))
    out << " (intrinsic " << bi.intrinsic_xsize << 'x' << bi.intrinsic_ysize << ')';
  out << ", " << bi.num_color_channels << "+"
      << bi.num_extra_channels << " channels (" << bi.bits_per_sample << "-bit color";
  if(bi.alpha_bits > 0)
    out << ", " << bi.alpha_bits << "-bit alpha";
  out << "), uses_original_profile=" << (bi.uses_original_profile?"yes":"no");

  if(bi.have_animation) {
    out << " animated: ";
    if(bi.animation.num_loops == 0)
      out << "infinite";
    else
      out << bi.animation.num_loops;
    out << " loops ";
    if (bi.animation.tps_denominator == 1) {
      out << bi.animation.tps_numerator << "t/s";
    } else if (bi.animation.tps_denominator != 0) {
      out << '(' << bi.animation.tps_numerator << '/' << bi.animation.tps_denominator
          << ") = " << std::fixed << std::setprecision(2)
          << (static_cast<float>(bi.animation.tps_numerator) /
              static_cast<float>(bi.animation.tps_denominator))
          << "t/s";
    }
  }

  return out;
}

ostream& operator<<(ostream& out, const JxlPixelFormat& pf) {
  out << pf.num_channels << '*' << dataTypeName(pf.data_type);

  if(pf.data_type != JXL_TYPE_UINT8) {
    if(pf.endianness == JXL_BIG_ENDIAN)
      out << "-be";
    else if(pf.endianness == JXL_LITTLE_ENDIAN)
      out << "-le";
    // else JXL_NATIVE_ENDIAN
  }

  if(pf.align > 1)
    out << '@' << pf.align << 'B';
  return out;
}

ostream& operator<<(ostream& out, const JxlColorEncoding& ec) {
  switch(ec.color_space) {
  case JXL_COLOR_SPACE_RGB:  out << "RGB";     break;
  case JXL_COLOR_SPACE_GRAY: out << "Gray";    break;
  case JXL_COLOR_SPACE_XYB:  out << "XYB";     break;
  default:                   out << "?ColorSpace?"; break;
  }

  switch(ec.white_point) {
  case JXL_WHITE_POINT_D65: out << " D65"; break;
  case JXL_WHITE_POINT_DCI: out << " DCI"; break;
  case JXL_WHITE_POINT_E:   out << " E";   break;
  case JXL_WHITE_POINT_CUSTOM:
    out << " (" << ec.white_point_xy[0] << "," << ec.white_point_xy[1] << ")";
    break;
  default: out << " ?WhitePoint?"; break;
  }

  switch(ec.primaries) {
  case JXL_PRIMARIES_SRGB: out << " sRGB"; break;
  case JXL_PRIMARIES_P3:   out << " P3";   break;
  case JXL_PRIMARIES_2100: out << " 2100"; break;
  case JXL_PRIMARIES_CUSTOM:
    out << " [(" << ec.primaries_red_xy[0]   << ',' << ec.primaries_red_xy[1]   << "),"
             "(" << ec.primaries_green_xy[0] << ',' << ec.primaries_green_xy[1] << "),"
             "(" << ec.primaries_blue_xy[0]  << ',' << ec.primaries_blue_xy[1]  << ")]";
    break;
  default: out << " ?Primaries?"; break;
  }

  switch(ec.transfer_function) {
  case JXL_TRANSFER_FUNCTION_SRGB:   out << " sRGB";               break;
  case JXL_TRANSFER_FUNCTION_GAMMA:  out << " gamma" << ec.gamma;  break;
  case JXL_TRANSFER_FUNCTION_709:    out << " 709";                break;
  case JXL_TRANSFER_FUNCTION_DCI:    out << " DCI";                break;
  case JXL_TRANSFER_FUNCTION_HLG:    out << " HLG";                break;
  case JXL_TRANSFER_FUNCTION_LINEAR: out << " linear";             break;
  case JXL_TRANSFER_FUNCTION_PQ:     out << " PQ";                 break;
  default:                           out << " ?TransferFunction?"; break;
  }

  return out;
}

} // namespace operators


}  // namespace jxlazy
