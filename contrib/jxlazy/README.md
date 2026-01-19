# JXLazy

High(er) level decoding API for JPEG-XL images.

This is a thin wrapper around libjxl's decoding functionality, providing an easy C++
interface to most of its features.  Not really intended as a stand-alone library, but I
decided to split it into its own thing for easy sharing between projects.

## Features
JXLazy hides the streaming, event-driven nature of the libjxl decoding logic and presents
the image as a simple object from which you can query the metadata, frames and boxes.

- Reads input in chunks from a `std::istream` and manages the buffering internally.
- Provides random access to frames and image properties.
- Provides random access to ISO/IEC 18181-2 boxes.

(Although it's always more efficient to access things in their natural sequence.)

## Example Usage

```c++
#include <iostream>
#include <vector>

#include <jxlazy/decoder.h>

using namespace std;
using namespace jxlazy;

void func() {

  const JxlPixelFormat pixelFormat = {
    .data_type = JXL_TYPE_UINT8,
    .num_channels = 4,
  };
  vector<uint8_t> pixels;

  Decoder jxl;
  jxl.openFile("picture.jxl");

  // Iterate through frames in sequence.  This provides the JxlFrameHeader and the frame's
  // name, if any.  Pixels can optionally be decoded via `getFramePixels`.
  for (auto& frame : jxl) {

    if (!frame.name.empty()) {
      cout << "This frame is named: " << frame.name << '\n';
    }

    // Get the pixels for this frame
    size_t size = jxl.getFrameBufferSize(frame, pixelFormat);
    pixels.resize(size);
    jxl.getFramePixels(frame, pixelFormat, pixels.data(), size);

    do_something_with(pixels, frame.header.xsize, frame.header.ysize);
  }

  // Note, `frameCount` requires no extra work at this point because we've already counted
  // them along the way.  However, because the file wasn't opened with `Hint::WantBoxes`,
  // `boxCount` forces the decoder to rewind and visit every box.
  cout << "Total frames: " << jxl.frameCount() << '\n';
  cout << "Total boxes: " << jxl.boxCount() << '\n';
}
```

## Building
This library is theoretically portable, but so far it's only been built and tested on
Linux, and compiled using gcc and clang.

Build dependencies:

- cmake
- libjxl
- googletest (if you want to build and run the unit tests)

### Building with cmake

To build as a static library:

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
# Optionally install to default locations:
sudo cmake --install .
```

To build as a shared library, add `-DBUILD_SHARED_LIBS` to the first `cmake` command.

To build unit tests, add `-DBUILD_TESTING=ON` to the first `cmake` command.  To run them,
change directory to the top of the source tree and execute jxlazy_decoder_test and
jxlazy_util_test from the build directory.
