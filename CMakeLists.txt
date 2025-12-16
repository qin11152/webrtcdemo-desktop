cmake_minimum_required(VERSION 3.15)

project(twebrtc)

aux_source_directory(. srcs)
file(GLOB_RECURSE module ${CMAKE_SOURCE_DIR}/module/*.cpp)
file(GLOB_RECURSE ui ${CMAKE_SOURCE_DIR}/ui/*.cpp)

SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
SET(CMAKE_INSTALL_RPATH "\${ORIGIN}/lib")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
# set(CMAKE_BUILD_TYPE "RelWithDebInfo")

add_executable(${PROJECT_NAME} ${srcs} ${module} ${ui})

find_package(Qt5 REQUIRED COMPONENTS Gui Widgets Network WebSockets)
find_package(absl CONFIG REQUIRED)
# 添加平台宏定义和pthread链接
target_compile_definitions(${PROJECT_NAME} PRIVATE WEBRTC_POSIX)
# target_compile_definitions(${PROJECT_NAME} PRIVATE WEBRTC_USE_X11)

target_link_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/3rd/lib)

# target_include_directories(${PROJECT_NAME} PRIVATE /home/qin/rtc/webrtc-checkout/src/third_party/jsoncpp/source/include)
# target_include_directories(${PROJECT_NAME} PRIVATE /home/qin/rtc/webrtc-checkout/src)
# target_include_directories(${PROJECT_NAME} PRIVATE /home/qin/rtc/webrtc-checkout/src/third_party/abseil-cpp)

# target_include_directories(${PROJECT_NAME} PRIVATE /home/qin/rtc/webrtc-checkout/src/third_party/jsoncpp/source/include)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/3rd/include/rtc)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/3rd/include)
# target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/include/rtc/third_party/abseil-cpp)

#查看是debug还是release


if("Debug" STREQUAL "${CMAKE_BUILD_TYPE}")
    message(STATUS "Debug mode: Adding debug libraries")
    target_link_libraries(${PROJECT_NAME} PRIVATE webrtc_d)
else()
    message(STATUS "Release mode: Adding release libraries")
    target_link_libraries(${PROJECT_NAME} PRIVATE webrtc)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE stdc++ pthread dl 
X11 
Xfixes
Xdamage
Xext
Xrandr
Xcomposite
Xtst
    glib-2.0 
    gobject-2.0
    # dbus-2.0 
    gio-2.0
    drm
    gbm
    Qt5::Core 
    Qt5::Gui 
    Qt5::Widgets 
    Qt5::Network 
    Qt5::WebSockets
    absl::flags)