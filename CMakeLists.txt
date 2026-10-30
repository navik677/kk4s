# kk4s - Kirikiri for Nintendo Switch
# Engine: krkrs by uyjulian (engine/), Switch layer: platform/switch/
cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{DEVKITPRO})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/Switch.cmake" CACHE PATH "toolchain file")
    else()
        message(FATAL_ERROR "Please define DEVKITPRO to point to your devkitPro path (or use the Dockerfile)")
    endif()
endif()

project(kk4s C CXX)

if(NOT DEVKITPRO)
    set(DEVKITPRO $ENV{DEVKITPRO})
endif()

set(KK4S_VERSION "0.0.1")
set(SWITCH_APP_NAME "kk4s")
set(SWITCH_AUTHOR   "navik677 (krkrs: uyjulian, Kirikiri: W.Dee)")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Build id for the log file
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE KK4S_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT KK4S_GIT_HASH)
    set(KK4S_GIT_HASH "unknown")
endif()

set(C_FLAGS
    -DTVP_ARCHIVE_ENABLE_XP3
    -DTVP_AUDIO_ENABLE_FFMPEG
    -DTVP_AUDIO_ENABLE_OPUS
    -DTVP_AUDIO_ENABLE_VORBIS
    -DTVP_AUDIO_ENABLE_WAVE
    -DTVP_COMPRESSION_ENABLE_ZLIB
    -DTVP_IMAGE_ENABLE_BMP
    -DTVP_IMAGE_ENABLE_JPEG
    -DTVP_IMAGE_ENABLE_PNG
    -DTVP_IMAGE_ENABLE_TLG
    -DTVP_IMAGE_ENABLE_WEBP
    -DTVP_RENDERER_ENABLE_FFMPEG
    -DTVP_TEXT_READ_ANSI_MBCS
    # always keep symbols: the .elf is used to resolve Atmosphere crash reports
    -g
)

# old upstream C code: keep newer GCC from turning these into hard errors
set(C_ONLY_FLAGS
    -Wno-error=implicit-function-declaration
    -Wno-error=incompatible-pointer-types
    -Wno-error=int-conversion
)

set(CXX_FLAGS ${C_FLAGS}
    -fno-delete-null-pointer-checks
    -fpermissive
)

set(OPTIMIZATION_FLAGS
    -O2
)

set(INCLUDES
    platform/switch
    engine/source
    engine/source/core
    engine/source/core/base
    engine/source/core/base/android
    engine/source/core/base/win32
    engine/source/core/environ
    engine/source/core/environ/android
    engine/source/core/environ/win32
    engine/source/core/extension
    engine/source/core/movie
    engine/source/core/movie/ffmpeg
    engine/source/core/msg
    engine/source/core/msg/win32
    engine/source/core/sdl2
    engine/source/core/sound
    engine/source/core/sound/win32
    engine/source/core/tjs2
    engine/source/core/utils
    engine/source/core/utils/encoding
    engine/source/core/utils/win32
    engine/source/core/visual
    engine/source/core/visual/gl
    engine/source/core/visual/win32
    engine/source/plugins
    engine/source/plugins/ncbind
)

set(SOURCES
    platform/switch/kk4s_platform.cpp
    engine/source/core/base/BinaryStream.cpp
    engine/source/core/base/CharacterSet.cpp
    engine/source/core/base/EventIntf.cpp
    engine/source/core/base/PluginIntf.cpp
    engine/source/core/base/ScriptMgnIntf.cpp
    engine/source/core/base/StorageIntf.cpp
    engine/source/core/base/SysInitIntf.cpp
    engine/source/core/base/SystemIntf.cpp
    engine/source/core/base/TextStream.cpp
    engine/source/core/base/UtilStreams.cpp
    engine/source/core/base/XP3Archive.cpp
    engine/source/core/base/android/GetLocalFileListAt.cpp
    engine/source/core/base/android/StorageImpl.cpp
    engine/source/core/base/win32/EventImpl.cpp
    engine/source/core/base/win32/FileSelector.cpp
    engine/source/core/base/win32/NativeEventQueue.cpp
    engine/source/core/base/win32/PluginImpl.cpp
    engine/source/core/base/win32/ScriptMgnImpl.cpp
    engine/source/core/base/win32/SysInitImpl.cpp
    engine/source/core/base/win32/SystemImpl.cpp
    engine/source/core/environ/Application.cpp
    engine/source/core/environ/android/SystemControl.cpp
    engine/source/core/extension/Extension.cpp
    engine/source/core/movie/ffmpeg/krffmpeg.cpp
    engine/source/core/movie/krmovie.cpp
    engine/source/core/msg/MsgIntf.cpp
    engine/source/core/msg/win32/MsgImpl.cpp
    engine/source/core/sdl2/stubs.cpp
    engine/source/core/sound/CDDAIntf.cpp
    engine/source/core/sound/FFWaveDecoder.cpp
    engine/source/core/sound/MIDIIntf.cpp
    engine/source/core/sound/MathAlgorithms.cpp
    engine/source/core/sound/PhaseVocoderDSP.cpp
    engine/source/core/sound/PhaseVocoderFilter.cpp
    engine/source/core/sound/RealFFT.cpp
    engine/source/core/sound/SoundBufferBaseImpl.cpp
    engine/source/core/sound/SoundBufferBaseIntf.cpp
    engine/source/core/sound/VorbisWaveDecoder.cpp
    engine/source/core/sound/WaveFormatConverter.cpp
    engine/source/core/sound/WaveIntf.cpp
    engine/source/core/sound/WaveLoopManager.cpp
    engine/source/core/sound/WaveSegmentQueue.cpp
    engine/source/core/sound/win32/CDDAImpl.cpp
    engine/source/core/sound/win32/MIDIImpl.cpp
    engine/source/core/sound/win32/WaveImpl.cpp
    engine/source/core/sound/win32/WaveMixer.cpp
    engine/source/core/tjs2/tjs.cpp
    engine/source/core/tjs2/tjs.tab.cpp
    engine/source/core/tjs2/tjsArray.cpp
    engine/source/core/tjs2/tjsBinarySerializer.cpp
    engine/source/core/tjs2/tjsByteCodeLoader.cpp
    engine/source/core/tjs2/tjsCompileControl.cpp
    engine/source/core/tjs2/tjsConfig.cpp
    engine/source/core/tjs2/tjsConstArrayData.cpp
    engine/source/core/tjs2/tjsDate.cpp
    engine/source/core/tjs2/tjsDateParser.cpp
    engine/source/core/tjs2/tjsDebug.cpp
    engine/source/core/tjs2/tjsDictionary.cpp
    engine/source/core/tjs2/tjsDisassemble.cpp
    engine/source/core/tjs2/tjsError.cpp
    engine/source/core/tjs2/tjsException.cpp
    engine/source/core/tjs2/tjsGlobalStringMap.cpp
    engine/source/core/tjs2/tjsInterCodeExec.cpp
    engine/source/core/tjs2/tjsInterCodeGen.cpp
    engine/source/core/tjs2/tjsInterface.cpp
    engine/source/core/tjs2/tjsLex.cpp
    engine/source/core/tjs2/tjsMT19937ar-cok.cpp
    engine/source/core/tjs2/tjsMath.cpp
    engine/source/core/tjs2/tjsMessage.cpp
    engine/source/core/tjs2/tjsNamespace.cpp
    engine/source/core/tjs2/tjsNative.cpp
    engine/source/core/tjs2/tjsObject.cpp
    engine/source/core/tjs2/tjsObjectExtendable.cpp
    engine/source/core/tjs2/tjsOctPack.cpp
    engine/source/core/tjs2/tjsRandomGenerator.cpp
    engine/source/core/tjs2/tjsRegExp.cpp
    engine/source/core/tjs2/tjsScriptBlock.cpp
    engine/source/core/tjs2/tjsScriptCache.cpp
    engine/source/core/tjs2/tjsSnprintf.cpp
    engine/source/core/tjs2/tjsString.cpp
    engine/source/core/tjs2/tjsUtils.cpp
    engine/source/core/tjs2/tjsVariant.cpp
    engine/source/core/tjs2/tjsVariantString.cpp
    engine/source/core/tjs2/tjsdate.tab.cpp
    engine/source/core/tjs2/tjspp.tab.cpp
    engine/source/core/utils/ClipboardIntf.cpp
    engine/source/core/utils/DebugIntf.cpp
    engine/source/core/utils/KAGParser.cpp
    engine/source/core/utils/MiscUtility.cpp
    engine/source/core/utils/PadIntf.cpp
    engine/source/core/utils/Random.cpp
    engine/source/core/utils/TVPTimer.cpp
    engine/source/core/utils/ThreadIntf.cpp
    engine/source/core/utils/TickCount.cpp
    engine/source/core/utils/TimerIntf.cpp
    engine/source/core/utils/TimerThread.cpp
    engine/source/core/utils/VelocityTracker.cpp
    engine/source/core/utils/cp932_uni.cpp
    engine/source/core/utils/encoding/gbk2unicode.c
    engine/source/core/utils/encoding/jis2unicode.c
    engine/source/core/utils/md5.c
    engine/source/core/utils/uni_cp932.cpp
    engine/source/core/utils/win32/ClipboardImpl.cpp
    engine/source/core/visual/BitmapIntf.cpp
    engine/source/core/visual/BitmapLayerTreeOwner.cpp
    engine/source/core/visual/CharacterData.cpp
    engine/source/core/visual/ComplexRect.cpp
    engine/source/core/visual/FontSystem.cpp
    engine/source/core/visual/FreeType.cpp
    engine/source/core/visual/FreeTypeFontRasterizer.cpp
    engine/source/core/visual/GraphicsLoadThread.cpp
    engine/source/core/visual/GraphicsLoaderIntf.cpp
    engine/source/core/visual/ImageFunction.cpp
    engine/source/core/visual/LayerBitmapIntf.cpp
    engine/source/core/visual/LayerIntf.cpp
    engine/source/core/visual/LayerManager.cpp
    engine/source/core/visual/LayerTreeOwnerImpl.cpp
    engine/source/core/visual/LoadJPEG.cpp
    engine/source/core/visual/LoadPNG.cpp
    engine/source/core/visual/LoadTLG.cpp
    engine/source/core/visual/LoadWEBP.cpp
    engine/source/core/visual/MenuItemIntf.cpp
    engine/source/core/visual/PrerenderedFont.cpp
    engine/source/core/visual/RectItf.cpp
    engine/source/core/visual/RenderManager.cpp
    engine/source/core/visual/SaveTLG5.cpp
    engine/source/core/visual/SaveTLG6.cpp
    engine/source/core/visual/TransIntf.cpp
    engine/source/core/visual/VideoOvlIntf.cpp
    engine/source/core/visual/WindowIntf.cpp
    engine/source/core/visual/gl/ResampleImage.cpp
    engine/source/core/visual/gl/WeightFunctor.cpp
    engine/source/core/visual/gl/blend_function.cpp
    engine/source/core/visual/tvpgl.cpp
    engine/source/core/visual/win32/BasicDrawDevice.cpp
    engine/source/core/visual/win32/BitmapBitsAlloc.cpp
    engine/source/core/visual/win32/BitmapInfomation.cpp
    engine/source/core/visual/win32/DInputMgn.cpp
    engine/source/core/visual/win32/DrawDevice.cpp
    engine/source/core/visual/win32/GraphicsLoaderImpl.cpp
    engine/source/core/visual/win32/LayerBitmapImpl.cpp
    engine/source/core/visual/win32/LayerImpl.cpp
    engine/source/core/visual/win32/MenuItemImpl.cpp
    engine/source/core/visual/win32/PassThroughDrawDevice.cpp
    engine/source/core/visual/win32/TVPScreen.cpp
    engine/source/core/visual/win32/VideoOvlImpl.cpp
    engine/source/core/visual/win32/WindowImpl.cpp
    engine/source/plugins/InternalPlugins.cpp
    engine/source/plugins/LayerExBase.cpp
    engine/source/plugins/csvParser.cpp
    engine/source/plugins/dirlist.cpp
    engine/source/plugins/fftgraph.cpp
    engine/source/plugins/getSample.cpp
    engine/source/plugins/layerExPerspective.cpp
    engine/source/plugins/ncbind/ncbind.cpp
    engine/source/plugins/saveStruct.cpp
    engine/source/plugins/varfile.cpp
    engine/source/plugins/win32dialog.cpp
    engine/source/plugins/wutcwf.cpp
)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE2 REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(LIBAVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(LIBAVFILTER REQUIRED IMPORTED_TARGET libavfilter)
pkg_check_modules(LIBAVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(LIBAVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(LIBPNG REQUIRED IMPORTED_TARGET libpng)
pkg_check_modules(LIBSWSCALE REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(LIBTURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)
pkg_check_modules(LIBWEBP REQUIRED IMPORTED_TARGET libwebp)
pkg_check_modules(OGG REQUIRED IMPORTED_TARGET ogg)
pkg_check_modules(ONIGURUMA REQUIRED IMPORTED_TARGET oniguruma)
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
pkg_check_modules(OPUSFILE REQUIRED IMPORTED_TARGET opusfile)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(VORBIS REQUIRED IMPORTED_TARGET vorbis)
pkg_check_modules(VORBISFILE REQUIRED IMPORTED_TARGET vorbisfile)
pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)

add_executable(${PROJECT_NAME} ${SOURCES})
target_link_libraries(${PROJECT_NAME}
    PkgConfig::FREETYPE2
    PkgConfig::LIBAVCODEC
    PkgConfig::LIBAVFILTER
    PkgConfig::LIBAVFORMAT
    PkgConfig::LIBAVUTIL
    PkgConfig::LIBPNG
    PkgConfig::LIBSWSCALE
    PkgConfig::LIBTURBOJPEG
    PkgConfig::LIBWEBP
    PkgConfig::OGG
    PkgConfig::ONIGURUMA
    PkgConfig::OPUS
    PkgConfig::OPUSFILE
    PkgConfig::SDL2
    PkgConfig::VORBIS
    PkgConfig::VORBISFILE
    PkgConfig::ZLIB
)

target_include_directories(${PROJECT_NAME} PUBLIC ${INCLUDES})
target_compile_options(${PROJECT_NAME} PUBLIC $<$<COMPILE_LANGUAGE:C>:${C_FLAGS}>)
target_compile_options(${PROJECT_NAME} PUBLIC $<$<COMPILE_LANGUAGE:C>:${C_ONLY_FLAGS}>)
target_compile_definitions(${PROJECT_NAME} PRIVATE KK4S_VERSION="${KK4S_VERSION}" KK4S_GIT_HASH="${KK4S_GIT_HASH}")
target_compile_options(${PROJECT_NAME} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:${CXX_FLAGS}>)
target_compile_options(${PROJECT_NAME} PUBLIC $<$<CONFIG:RELEASE>:${OPTIMIZATION_FLAGS}>)
target_compile_options(${PROJECT_NAME} PUBLIC $<$<CONFIG:RELWITHDEBINFO>:${OPTIMIZATION_FLAGS}>)

# --- .nro packaging (keeps the .elf next to it for crash analysis) ---
set(KK4S_ICON ${CMAKE_SOURCE_DIR}/platform/switch/icon.jpg)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/kk4s.nacp
    COMMAND ${DEVKITPRO}/tools/bin/nacptool --create "${SWITCH_APP_NAME}" "${SWITCH_AUTHOR}" "${KK4S_VERSION}-${KK4S_GIT_HASH}" ${CMAKE_BINARY_DIR}/kk4s.nacp
    VERBATIM
)
add_custom_target(kk4s_nro ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_BINARY_DIR}/kk4s.elf
    COMMAND ${DEVKITPRO}/tools/bin/elf2nro ${CMAKE_BINARY_DIR}/kk4s.elf ${CMAKE_BINARY_DIR}/kk4s.nro --icon=${KK4S_ICON} --nacp=${CMAKE_BINARY_DIR}/kk4s.nacp
    DEPENDS ${PROJECT_NAME} ${CMAKE_BINARY_DIR}/kk4s.nacp ${KK4S_ICON}
    BYPRODUCTS ${CMAKE_BINARY_DIR}/kk4s.elf ${CMAKE_BINARY_DIR}/kk4s.nro
    VERBATIM
)
