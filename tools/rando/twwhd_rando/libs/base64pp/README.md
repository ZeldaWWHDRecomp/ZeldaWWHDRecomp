[![Build Status](https://dev.azure.com/Matheusgarcia28/Base64pp/_apis/build/status%2Fmatheusgomes28.base64pp?branchName=main)](https://dev.azure.com/Matheusgarcia28/Base64pp/_build/latest?definitionId=3&branchName=main) [![codecov](https://codecov.io/github/matheusgomes28/base64pp/branch/main/graph/badge.svg?token=5Q5G5MNFLO)](https://codecov.io/github/matheusgomes28/base64pp)

# Base64pp - A Modern & Safe C++ Encoder

Base64pp is a C++ library that provides an efficient and easy-to-use interface,
for encoding and decoding data in Base64 format. The library is lightweight
and designed to be portable, making it an ideal choice for applications that
need to support multiple platforms.

If you want to find out more about this implementation, check out the blog post
about [writing a Base64 encoder in C++](https://matgomes.com/base64-encode-decode-cpp/)

- [Features](#features-and-goals)
- [Example Usage](#usage)
- [Installation](#installation)
- [Building](#building-the-project)
- [License](#license)
- [Contributing](#contributions)

## Features And Goals
- [x] A safe implementation of Base64 Encoding & Decoding.
- [x] Tested with many edge test cases, unlike most Base64 snippets out there.
- [x] Supports `std::span<std::uint8_t>`, meaning you can encode any blob of data.
- [x] High testing coverage.
- [ ] High testing standards (FuzzTesting, AddressSanitizers, etc).
- [x] Lightweight & easy to include - A single header & source file.
- [x] Easy to use API (see usage).
- [ ] Integration with package managers (`conan` & `vcpkg`).

## Usage

Here's an example of using the `base64pp` library for a round-trip encoding
of a file given as an argument.

```cpp
#include "base64pp.h"

#include <iterator>
#include <iostream>
#include <fstream>
#include <vector>

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    std::cout << "Usage: program FILE_PATH\n"; 
  }

  std::ifstream file_to_encode{argv[1]};
  std::vector<std::uint8_t> const bytes{std::istreambuf_iterator<std::uint8_t>{file_to_encode}, {}};
  
  auto const base64_str = base64pp::encode({begin(bytes), end(bytes)});
  std::cout << base64_str << '\n';

  // decoding & testing for equality
  auto const decoded_bytes = base64::decode(base64_str);
  if (!decoded_bytes)
  {
    std::cout << "could not decode.\n";
  }

  return *decoded_bytes == bytes;
}
```

## Installation

Currently, you can use this library in a few different ways. Although
limited, I am working on making `base64pp` easily accessible through
package managers.

### Raw Includes

Build the project (seebuild section), and make sure you link against
`base64pp`, while also copying the header `base64pp/include/base64pp` to
your build system.

### CMake

As of now, you can either include this project into your `CMakeLists.txt`
via `FetchContent{Declare,MakeAvailable}`, via submodule, or copying
the directory `base64pp` into your own project.

However, if you are including the project with submodule or copying
the `base64pp` directory, make sure to call
`add_subdirectory(path-to/base64pp)` in your `CMakeLists.txt`.

### Package Managers

The plan is to make this library available through package managers.

Currently, I'm working on getting a working `conan` package for this
repo. If you feel like you could help, feel free to create a fork and
a PR!


## Building The Project

This project can be build with CMake. Make sure you have a
recent enough version of CMake (>= 3.21).

In addition, we build with the C++20 standard, so make sure
your compiler supports this version.

### project dependencies
Base64pp doesn't rely on any third-party libraries per se,
but the unit tests were implemented with GTest. However,
the dependencies were managed with [Conan](https://conan.io)
so you do need to install that first.

You can use the following script to install `conan` dependencies:

```bash
./scripts/helpers/conan-install.sh [Debug|Release]
```

### Build Instructions

Run the build bash script provided in the repo. This
assumes you have installed `conan` dependencies.

On Linux, call:

```bash
cd ${PROJECT_ROOT_DIR}
./scripts/build-x86-64.sh unix-rel
``` 

On Powershell (Windows), you can call:
```pwsh
.\scripts\build_win_x64.ps1 -Preset vs2022-rel
```

Note that `unix-rel` and `vs2022-rel` are presets provided
in the `CMakePresets.json`. Take a look at the build scripts
for more info.

**All the build files will be compiled to `build/<PRESET-NAME>/base64pp`.**

## License
Base64pp is licensed under the MIT license. See the LICENSE file for more details.

## Contributions

Contributions to base64pp are welcome. To contribute, please fork the repository,
make your changes, and submit a pull request. Please ensure that your code follows
the existing coding style and conventions, and include appropriate tests where
necessary.


