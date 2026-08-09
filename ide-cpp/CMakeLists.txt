cmake_minimum_required(VERSION 3.16)
project(Lumina LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt6 발견 — CMAKE_PREFIX_PATH (build.bat / build.sh 에서 Qt 설치 경로 지정)
find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)
qt_standard_project_setup()

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)

# 윈도우에서만 있는 것: 아이콘·판 번호를 담은 .rc, 그리고 콘솔 창을 띄우지 않는 WIN32 표시.
# 리눅스·맥에는 둘 다 뜻이 없어서 붙이면 오히려 설정이 깨집니다.
set(PLATFORM_SOURCES "")
if(WIN32)
    list(APPEND PLATFORM_SOURCES assets/lumina.rc)
endif()

add_executable(Lumina
    ${APP_SOURCES}
    resources.qrc
    ${PLATFORM_SOURCES}
)

target_include_directories(Lumina PRIVATE src)
target_link_libraries(Lumina PRIVATE Qt6::Widgets Qt6::Svg)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

target_compile_options(Lumina PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W3 /EHsc /O2>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -O2>
)

set_target_properties(Lumina PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../bin
)

if(WIN32)
    # 콘솔 창 없이 뜨는 GUI 프로그램
    set_target_properties(Lumina PROPERTIES WIN32_EXECUTABLE ON)
elseif(APPLE)
    # 맥은 .app 묶음이라야 Dock 과 메뉴 막대가 제대로 붙습니다
    set_target_properties(Lumina PROPERTIES MACOSX_BUNDLE ON)
endif()
