cmake_minimum_required(VERSION 3.16)
project(nitv-cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(ZLIB REQUIRED)

add_executable(nitv
  src/main.cpp
  src/mmap_file.cpp
  src/nitf.cpp
  src/png_write.cpp
  src/sicd_xml.cpp
)

target_include_directories(nitv PRIVATE src third_party/argparse)
target_link_libraries(nitv PRIVATE ZLIB::ZLIB)

target_compile_options(nitv PRIVATE -Wall -Wextra)
