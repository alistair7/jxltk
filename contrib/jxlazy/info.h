/*
 * Copyright (c) Alistair Barrow. All rights reserved.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file.
 */
#ifndef JXLAZY_INFO_H_
#define JXLAZY_INFO_H_
#ifndef __cplusplus
#error "This is a C++ header"
#endif

#include <iostream>

#include <jxl/decode.h>

namespace jxlazy {

const char* decoderEventName(JxlDecoderStatus s);

/**
 * Print the listenable decoder events in @p events to @c out,
 * as a '|'-separated list.
 */
void printDecoderEventNames(std::ostream& out, int events);

inline namespace operators {

std::ostream& operator<<(std::ostream& out, const JxlBasicInfo& bi);

std::ostream& operator<<(std::ostream& out, const JxlPixelFormat& pf);

std::ostream& operator<<(std::ostream& out, const JxlColorEncoding& ce);

} // namespace operators

}  // namespace jxlazy

#endif  // JXLAZY_INFO_H_
