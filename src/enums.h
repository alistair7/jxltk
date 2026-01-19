/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#ifndef JXLTK_ENUMS_H_
#define JXLTK_ENUMS_H_

#include <jxl/decode.h>
#include <jxl/encode.h>

#include <ostream>
#include <sstream>

#if JPEGXL_NUMERIC_VERSION > JPEGXL_COMPUTE_NUMERIC_VERSION(0, 9, 2)
#define JXLTK_MAX_EFFORT 10
#else
#define JXLTK_MAX_EFFORT 9
#endif

namespace jxltk {

bool blendModeFromName(const char* name, JxlBlendMode* mode);

const char* blendModeName(JxlBlendMode b);

const char* channelTypeName(JxlExtraChannelType t);

bool colorSpaceFromName(const char* name, JxlColorSpace* s);

const char* colorSpaceName(JxlColorSpace s);

const char* dataTypeName(JxlDataType t);

const char* decoderStatusName(JxlDecoderStatus s);

const char* encoderStatusName(JxlEncoderStatus s);

bool orientationFromName(const char* name, JxlOrientation* o);

const char* orientationName(JxlOrientation o);

bool primariesFromName(const char* name, JxlPrimaries* p);

const char* primariesName(JxlPrimaries p);

bool renderingIntentFromName(const char* name, JxlRenderingIntent* r);

const char* renderingIntentName(JxlRenderingIntent r);

bool transferFunctionFromName(const char* name, JxlTransferFunction* t);

const char* transferFunctionName(JxlTransferFunction t);

bool whitePointFromName(const char* name, JxlWhitePoint* w);

const char* whitePointName(JxlWhitePoint w);


/**
 * Print the listenable decoder events in @p events to @c out,
 * as a '|'-separated list.
 */
void printDecoderEventNames(std::ostream& out, int events);

std::ostream& operator<<(std::ostream& out, const JxlBasicInfo& bi);

std::ostream& operator<<(std::ostream& out, const JxlPixelFormat& pf);

std::ostream& operator<<(std::ostream& out, const JxlColorEncoding& ce);

template<class T>
std::string toString(const T& t) {
  std::ostringstream oss;
  oss << t;
  return oss.str();
}

}  // namespace jxltk

#endif  // JXLTK_ENUMS_H_
