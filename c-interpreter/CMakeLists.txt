# Lumi 인터프리터 — 윈도우 / 리눅스 / 맥 공통 빌드
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#
# 결과물은 bin/lumi(.exe) 입니다.
# (윈도우에서 MSVC 만 쓰겠다면 build.bat 도 그대로 씁니다.)

cmake_minimum_required(VERSION 3.16)
project(lumi C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# builtins.inc 는 interp.c 안으로 그대로 이어 붙는 조각이라 목록에 넣지 않습니다.
add_executable(lumi
  src/platform.c
  src/value.c
  src/regex.c
  src/unicode.c
  src/lexer.c
  src/parser.c
  src/interp.c
  src/pkg.c
  src/lsp.c
  src/dap.c
  src/lint.c
  src/fmt.c
  src/ffi.c
  src/bundle.c
  src/main.c
)

set_target_properties(lumi PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY             "${CMAKE_CURRENT_SOURCE_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG       "${CMAKE_CURRENT_SOURCE_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE     "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

if(MSVC)
  # /we4013 : 선언 없이 쓴 함수는 오류로 (64비트에서 포인터가 잘립니다)
  target_compile_options(lumi PRIVATE /W3 /we4013 /utf-8)
  target_compile_definitions(lumi PRIVATE _CRT_SECURE_NO_WARNINGS)
  # 한글 파일 이름을 UTF-16 으로 받으려면 진입점이 wmain 이어야 합니다
  target_link_options(lumi PRIVATE /STACK:8388608 /ENTRY:wmainCRTStartup)
  target_link_libraries(lumi PRIVATE ws2_32)   # 작은 웹 서버가 쓰는 소켓
else()
  target_compile_options(lumi PRIVATE -Wall -Wextra -Wno-unused-parameter
                                     -Werror=implicit-function-declaration)
  target_link_libraries(lumi PRIVATE m)
  # 따로 도는 일감(start/wait)이 pthread 를 씁니다.  glibc 2.34 부터는 libc 안에
  # 들어와 있지만, 그 전 판과 musl·BSD·맥에서는 따로 이어 주어야 합니다.
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)
  target_link_libraries(lumi PRIVATE Threads::Threads)
  # 윈도우는 기본 스택이 1MB 라 /STACK 으로 8MB 를 따로 부탁하지만,
  # 리눅스·맥은 기본이 이미 8MB (`ulimit -s`) 라 손댈 것이 없습니다.
  # 재귀 깊이는 인터프리터가 MAX_DEPTH(2000)로 따로 막습니다.
endif()

# cp949 / euc-kr 변환.  리눅스(glibc)는 libc 안에 있지만 맥·Cygwin·musl·BSD 는
# libiconv 가 따로입니다.  CMake 가 알아서 가려 주도록 맡깁니다.
if(NOT WIN32)
  find_package(Iconv)
  if(Iconv_FOUND AND NOT Iconv_IS_BUILT_IN)
    target_link_libraries(lumi PRIVATE Iconv::Iconv)
  endif()
endif()
