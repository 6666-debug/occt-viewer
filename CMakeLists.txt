cmake_minimum_required(VERSION 3.16)

project(untitled2 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Qt 6
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)

# ---------------------------------------------------------------------------
# OpenCASCADE
# ---------------------------------------------------------------------------
set(OCCT_ROOT "C:/OpenCASCADE-7.8.0/occt-vc143-64" CACHE PATH "OpenCASCADE 安装根目录")
set(OCCT_3RDPARTY "C:/OpenCASCADE-7.8.0/3rdparty-vc14-64" CACHE PATH "OpenCASCADE 第三方依赖根目录")

if(NOT OpenCASCADE_DIR)
    if(DEFINED ENV{OpenCASCADE_DIR} AND EXISTS "$ENV{OpenCASCADE_DIR}/OpenCASCADEConfig.cmake")
        set(OpenCASCADE_DIR "$ENV{OpenCASCADE_DIR}")
    else()
        set(OpenCASCADE_DIR "${OCCT_ROOT}/cmake")
    endif()
endif()

find_package(OpenCASCADE REQUIRED)
if(NOT OpenCASCADE_FOUND)
    message(FATAL_ERROR "Cannot find OpenCASCADE. Please set OpenCASCADE_DIR.")
endif()

set(OCCT_CORE_LIBS
    TKernel TKMath
    TKService TKV3d TKOpenGl
    TKG2d TKG3d TKGeomBase TKBRep
    TKGeomAlgo TKTopAlgo TKPrim
    TKXSBase TKDESTEP TKDEIGES TKDE
    TKXCAF TKLCAF TKCAF
    TKVCAF
    TKBinXCAF
    TKXmlXCAF
    TKCDF
    TKStd
    TKStdL
    TKTObj
    TKBinTObj
    TKXmlTObj
    TKXmlL
    TKBin
    TKBinL
    TKXml
)

set(OCCT_LIB_FILES "")
foreach(_lib ${OCCT_CORE_LIBS})
    list(APPEND OCCT_LIB_FILES "${OpenCASCADE_LIBRARY_DIR}/${_lib}.lib")
endforeach()

# ---------------------------------------------------------------------------
# 可执行目标
# ---------------------------------------------------------------------------
qt_add_executable(untitled2
    src/main.cpp
    src/OcctViewport.cpp
    src/OcctViewport.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/Types.h
)

# ---------------------------------------------------------------------------
# Qt 资源：应用图标 + 内嵌示例 STEP
# 资源内路径前缀为 ":/resources/..."，例如 ":/resources/samples/as1_motor.step"。
# （resources.qrc 是等价的 qrc 写法，供 rcc 单独使用；此处使用 qt_add_resources，
#   二者不要同时参与构建，否则资源会重复注册。）
# ---------------------------------------------------------------------------
qt_add_resources(untitled2 "app_resources"
    PREFIX "/"
    FILES
        resources/app.ico
        resources/samples/as1_motor.step
)

target_include_directories(untitled2 PRIVATE ${OpenCASCADE_INCLUDE_DIR})

target_link_libraries(untitled2 PRIVATE
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    ${OCCT_LIB_FILES}
    "${OCCT_3RDPARTY}/tbb2021.5-vc14-x64/lib/tbb12.lib"
    "${OCCT_3RDPARTY}/tbb2021.5-vc14-x64/lib/tbbmalloc.lib"
    "${OCCT_3RDPARTY}/freetype-2.5.5-vc14-64/lib/freetype.lib"
    "${OCCT_3RDPARTY}/jemalloc-vc14-64/lib/jemalloc.lib"
    advapi32 gdi32 user32 wsock32 psapi opengl32
)

if(MSVC)
    # 源码与字符串字面量均为 UTF-8 无 BOM，必须显式告知 MSVC
    target_compile_options(untitled2 PRIVATE /utf-8)
endif()

if(WIN32)
    # GUI 子系统（双击运行不弹控制台），并使用 resources/app.ico 作为 exe 图标
    set_target_properties(untitled2 PROPERTIES WIN32_EXECUTABLE TRUE)

    set(_app_icon_rc "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
    file(WRITE "${_app_icon_rc}"
         "IDI_ICON1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico\"\n")
    target_sources(untitled2 PRIVATE "${_app_icon_rc}")
endif()

# ---------------------------------------------------------------------------
# 运行时依赖 DLL 复制
# ---------------------------------------------------------------------------
file(GLOB OCCT_RUNTIME_DLLS "${OpenCASCADE_BINARY_DIR}/*.dll")
file(GLOB MSVC_RUNTIME_DLLS "${OCCT_3RDPARTY}/msvc-vc14-64/*.dll")
file(GLOB FFMPEG_RUNTIME_DLLS "${OCCT_3RDPARTY}/ffmpeg-3.3.4-64/bin/*.dll")
file(GLOB OPENVR_RUNTIME_DLLS "${OCCT_3RDPARTY}/openvr-1.14.15-64/bin/win64/*.dll")

add_custom_command(TARGET untitled2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OCCT_RUNTIME_DLLS} "$<TARGET_FILE_DIR:untitled2>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OCCT_3RDPARTY}/tbb2021.5-vc14-x64/bin/tbb12.dll"
        "${OCCT_3RDPARTY}/tbb2021.5-vc14-x64/bin/tbbmalloc.dll"
        "${OCCT_3RDPARTY}/freetype-2.5.5-vc14-64/bin/freetype.dll"
        "${OCCT_3RDPARTY}/jemalloc-vc14-64/bin/jemalloc.dll"
        "${OCCT_3RDPARTY}/gl2ps-1.3.8-vc14-64/bin/gl2ps.dll"
        "${OCCT_3RDPARTY}/freeimage-3.17.0-vc14-64/bin/FreeImage.dll"
        "$<TARGET_FILE_DIR:untitled2>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_RUNTIME_DLLS} "$<TARGET_FILE_DIR:untitled2>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OPENVR_RUNTIME_DLLS} "$<TARGET_FILE_DIR:untitled2>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MSVC_RUNTIME_DLLS} "$<TARGET_FILE_DIR:untitled2>"
    COMMENT "Copying OpenCASCADE runtime DLLs next to the executable"
)

# ---------------------------------------------------------------------------
# Qt Test 单元测试（可选）
# ---------------------------------------------------------------------------
option(UNTITLED2_BUILD_TESTS "构建 Qt Test 单元测试" ON)

if(UNTITLED2_BUILD_TESTS)
    find_package(Qt6 QUIET COMPONENTS Test)
    if(TARGET Qt6::Test)
        enable_testing()

        qt_add_executable(tst_measure tests/tst_measure.cpp)

        target_include_directories(tst_measure PRIVATE ${OpenCASCADE_INCLUDE_DIR})
        target_link_libraries(tst_measure PRIVATE
            Qt6::Test
            ${OCCT_LIB_FILES}
            "${OCCT_3RDPARTY}/tbb2021.5-vc14-x64/lib/tbb12.lib"
            "${OCCT_3RDPARTY}/tbb2021.5-vc14-x64/lib/tbbmalloc.lib"
            advapi32 gdi32 user32 wsock32 psapi opengl32
        )
        if(MSVC)
            target_compile_options(tst_measure PRIVATE /utf-8)
        endif()

        add_test(NAME tst_measure COMMAND tst_measure)

        # 测试通过 PATH 找到 Qt 运行库（不把 Qt DLL 复制到构建目录，
        # 否则同一目录下的 untitled2.exe 会按「exe 同目录/plugins」找插件而失败）
        set_tests_properties(tst_measure PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>\\;$ENV{PATH}")
    else()
        message(WARNING "Qt6::Test 未找到，跳过单元测试目标（主程序不受影响）")
    endif()
endif()