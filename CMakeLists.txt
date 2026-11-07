# kk4s - Kirikiri for Nintendo Switch (+ PC viewer)
# Engine: krkrs by uyjulian (engine/), platform layers: platform/
#   Switch:  aarch64-none-elf-cmake -S . -B build-switch          (or tools/build-switch.sh)
#   PC:      cmake -S . -B build-desktop -DKK4S_PLATFORM=desktop  (or tools/build-desktop.sh)
cmake_minimum_required(VERSION 3.13)

set(KK4S_PLATFORM "switch" CACHE STRING "Target platform: switch or desktop")
set_property(CACHE KK4S_PLATFORM PROPERTY STRINGS switch desktop)

if(KK4S_PLATFORM STREQUAL "switch" AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{DEVKITPRO})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/Switch.cmake" CACHE PATH "toolchain file")
    else()
        message(FATAL_ERROR "Please define DEVKITPRO to point to your devkitPro path (or use the Dockerfile)")
    endif()
endif()

project(kk4s C CXX)
if(WIN32 AND KK4S_PLATFORM STREQUAL "desktop")
    enable_language(RC) # kk4s.rc embeds the UTF-8 active-code-page manifest
endif()

if(NOT DEVKITPRO)
    set(DEVKITPRO $ENV{DEVKITPRO})
endif()

option(KK4S_ASAN "desktop: build with AddressSanitizer + UBSan" OFF)
if(KK4S_PLATFORM STREQUAL "desktop" AND KK4S_ASAN)
    # vptr check needs RTTI for classes whose key function is never defined (tTJSNI_Pad).
    # null: TJS calls tTJSVariantString methods on a null pointer (the empty
    # string) and checks `if(!this)` by design; the engine is built with
    # -fno-delete-null-pointer-checks for it. Real null dereferences still crash
    # under ASan.
    add_compile_options(-fsanitize=address,undefined -fno-sanitize=vptr,null -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined -fno-sanitize=vptr,null)
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
    platform/common
    compat
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
    platform/common/kk4s_common.cpp
    platform/common/kk4s_engines.cpp
    platform/common/kk4s_onscripter.cpp
    platform/common/kk4s_rlvm.cpp
    platform/common/kk4s_files.cpp
    platform/common/kk4s_i18n.cpp
    platform/common/kk4s_image.cpp
    platform/common/kk4s_launcher.cpp
    platform/common/kk4s_net.cpp
    platform/common/kk4s_overlay.cpp
    platform/common/kk4s_settings.cpp
    platform/common/kk4s_ui.cpp
    compat/kk4s_compat.cpp
    compat/kk4s_cx.cpp
    compat/kk4s_scripts.cpp
    compat/cxdec/cxdec.cpp
    compat/cxdec/tinyfilter.cpp
    compat/kk4s_motion.cpp
    compat/psb/psb.cpp
    # universal E-mote runtime (docs/universal_emote_runtime.md); pure C++,
    # not wired into Motion.* yet
    compat/emote/emote.cpp
    compat/emote/emote_status.cpp
    compat/emote/psb_value.cpp
    compat/emote/psb_document.cpp
    compat/emote/cipher_profile.cpp
    compat/emote/profiles/builtin_profiles.cpp
    compat/emote/profiles/nekopara_vol1.cpp
    compat/emote/emote_model.cpp
    compat/emote/model_adapter.cpp
    compat/emote/adapters/emote_v3.cpp
    compat/emote/easing.cpp
    compat/emote/animation_runtime.cpp
    compat/emote/scene.cpp
    compat/emote/render_contract.cpp
    compat/emote/emote_assets.cpp
    compat/pbd/pbd.cpp
    compat/kk4s_plugins.cpp
    compat/kk4s_layerfx.cpp
    compat/kk4s_layerex.cpp
    compat/kk4s_json.cpp
    compat/kk4s_xp3crypt.cpp
    compat/kk4s_softmode.cpp
    compat/kk4s_tpm.cpp
    compat/x86/x86emu.cpp
    compat/x86/pe32.cpp
    compat/x86/tpm_plugin.cpp
    compat/cxdec/scheme_recover.cpp
    platform/${KK4S_PLATFORM}/kk4s_platform.cpp
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
    engine/source/core/movie/kk4s_videoplayer.cpp
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
    engine/source/plugins/AlphaMovie.cpp
    engine/source/plugins/csvParser.cpp
    engine/source/plugins/KAGParserEx/kagparserex.cpp
    engine/source/plugins/psdfile/main.cpp
    engine/source/plugins/psdfile/psdclass.cpp
    engine/source/plugins/psdfile/psdclass_loadstream.cpp
    engine/source/plugins/psdfile/psdclass_meta.cpp
    engine/source/plugins/psdfile/psdclass_render.cpp
    engine/source/plugins/psdfile/psdparse/bmp.cpp
    engine/source/plugins/psdfile/psdparse/psdadjust.cpp
    engine/source/plugins/psdfile/psdparse/psdchannels.cpp
    engine/source/plugins/psdfile/psdparse/psdcomposite.cpp
    engine/source/plugins/psdfile/psdparse/psddesc.cpp
    engine/source/plugins/psdfile/psdparse/psdengine.cpp
    engine/source/plugins/psdfile/psdparse/psdfile.cpp
    engine/source/plugins/psdfile/psdparse/psdfx.cpp
    engine/source/plugins/psdfile/psdparse/psdfx_adjust.cpp
    engine/source/plugins/psdfile/psdparse/psdfx_effects.cpp
    engine/source/plugins/psdfile/psdparse/psdfx_paint.cpp
    engine/source/plugins/psdfile/psdparse/psdfx_parallel.cpp
    engine/source/plugins/psdfile/psdparse/psdfx_path.cpp
    engine/source/plugins/psdfile/psdparse/psdfx_simd_avx2.cpp
    engine/source/plugins/psdfile/psdparse/psdimage.cpp
    engine/source/plugins/psdfile/psdparse/psdlayer.cpp
    engine/source/plugins/psdfile/psdparse/psdlinked.cpp
    engine/source/plugins/psdfile/psdparse/psdmisc.cpp
    engine/source/plugins/psdfile/psdparse/psdparse.cpp
    engine/source/plugins/psdfile/psdparse/psdpath.cpp
    engine/source/plugins/psdfile/psdparse/psdpattern.cpp
    engine/source/plugins/psdfile/psdparse/psdresource.cpp
    engine/source/plugins/psdfile/psdparse/psdshape.cpp
    engine/source/plugins/psdfile/psdparse/psdwrite.cpp
    engine/source/plugins/extrans/extrans.cpp
    engine/source/plugins/extrans/mosaic.cpp
    engine/source/plugins/extrans/ripple.cpp
    engine/source/plugins/extrans/rotatebase.cpp
    engine/source/plugins/extrans/rotatetrans.cpp
    engine/source/plugins/extrans/turn.cpp
    engine/source/plugins/extrans/turntrans_table.cpp
    engine/source/plugins/extrans/wave.cpp
    engine/source/plugins/dirlist.cpp
    engine/source/plugins/fftgraph.cpp
    engine/source/plugins/getSample.cpp
    engine/source/plugins/layerExPerspective.cpp
    engine/source/plugins/ncbind/ncbind.cpp
    engine/source/plugins/saveStruct.cpp
    engine/source/plugins/psbfile.cpp
    engine/source/plugins/varfile.cpp
    engine/source/plugins/win32dialog.cpp
    engine/source/plugins/wutcwf.cpp
    engine/source/plugins/layerExDraw/LayerExDraw.cpp
    engine/source/plugins/layerExDraw/main.cpp
    engine/source/plugins/layerExDraw/plutovg/plutovg-blend.c
    engine/source/plugins/layerExDraw/plutovg/plutovg-canvas.c
    engine/source/plugins/layerExDraw/plutovg/plutovg-font.c
    engine/source/plugins/layerExDraw/plutovg/plutovg-ft-math.c
    engine/source/plugins/layerExDraw/plutovg/plutovg-ft-raster.c
    engine/source/plugins/layerExDraw/plutovg/plutovg-ft-stroker.c
    engine/source/plugins/layerExDraw/plutovg/plutovg-matrix.c
    engine/source/plugins/layerExDraw/plutovg/plutovg-paint.c
    engine/source/plugins/layerExDraw/plutovg/plutovg-path.c
    engine/source/plugins/layerExDraw/plutovg/plutovg-rasterize.c
    engine/source/plugins/layerExDraw/plutovg/plutovg-surface.c
)
find_package(PkgConfig REQUIRED)
set(KK4S_PKG_MODULES
    freetype2 libavcodec libavfilter libavformat libavutil libswscale
    libpng libjpeg libturbojpeg libwebp ogg vorbis vorbisfile opus opusfile
    oniguruma sdl2 zlib libcurl
)
pkg_check_modules(KK4S_DEPS REQUIRED ${KK4S_PKG_MODULES})
if(KK4S_PLATFORM STREQUAL "desktop")
    # PC can recover a CX scheme directly from the game's PE/TPM. Keep
    # Capstone's include directory out of the global include path: on Windows
    # its lowercase platform.h collides with the engine's Platform.h.
    pkg_check_modules(KK4S_CAPSTONE REQUIRED capstone)
    # Prefer static linking so the Linux viewer artifact does not need a
    # separately installed libcapstone at runtime. Fall back to pkg-config's
    # shared library when the platform package has no static archive.
    find_file(KK4S_CAPSTONE_STATIC_LIBRARY NAMES libcapstone.a
        PATHS ${KK4S_CAPSTONE_LIBRARY_DIRS} NO_DEFAULT_PATH)
endif()

add_executable(${PROJECT_NAME} ${SOURCES})

# --- ONScripter-NX built in (NScripter games, docs/engines.md) ---
# GPL-2+: with it, the kk4s binary is distributed under the GPL. AUTO = on when
# SDL2_image / SDL2_mixer / SDL2_ttf / smpeg2 are found; without them NScripter
# games use a separate onscripter program from <root>/engines/ as before.
set(KK4S_ONSCRIPTER "AUTO" CACHE STRING "Build ONScripter into kk4s: AUTO, ON or OFF")
set_property(CACHE KK4S_ONSCRIPTER PROPERTY STRINGS AUTO ON OFF)
if(NOT KK4S_ONSCRIPTER STREQUAL "OFF")
    if(KK4S_ONSCRIPTER STREQUAL "ON")
        set(KK4S_ONS_REQUIRED REQUIRED)
    endif()
    pkg_check_modules(KK4S_ONS ${KK4S_ONS_REQUIRED} SDL2_image SDL2_mixer SDL2_ttf)
    # smpeg2 (MP3 music) ships smpeg2-config, no .pc file
    find_path(KK4S_SMPEG2_INCLUDE_DIR smpeg2/smpeg.h)
    find_library(KK4S_SMPEG2_LIBRARY NAMES smpeg2)
    if(KK4S_ONS_FOUND AND NOT (KK4S_SMPEG2_INCLUDE_DIR AND KK4S_SMPEG2_LIBRARY))
        if(KK4S_ONSCRIPTER STREQUAL "ON")
            message(FATAL_ERROR "kk4s: smpeg2 not found (needed by ONScripter)")
        endif()
        set(KK4S_ONS_FOUND FALSE)
    endif()
endif()
if(KK4S_ONS_FOUND)
    message(STATUS "kk4s: ONScripter built in")
    file(GLOB KK4S_ONS_SOURCES ${CMAKE_SOURCE_DIR}/engines/onscripter-nx/source/*.cpp)
    if(NOT KK4S_PLATFORM STREQUAL "switch")
        list(FILTER KK4S_ONS_SOURCES EXCLUDE REGEX "switch_[a-z_]+\\.cpp$")
    endif()
    add_library(kk4s_onscripter STATIC ${KK4S_ONS_SOURCES})
    # old code (2006): register, narrowing, string literals as char*
    set_target_properties(kk4s_onscripter PROPERTIES CXX_STANDARD 11 CXX_EXTENSIONS ON)
    target_include_directories(kk4s_onscripter PUBLIC ${CMAKE_SOURCE_DIR}/engines/onscripter-nx/include)
    target_include_directories(kk4s_onscripter PRIVATE ${KK4S_SMPEG2_INCLUDE_DIR} ${KK4S_SMPEG2_INCLUDE_DIR}/smpeg2)
    target_compile_definitions(kk4s_onscripter PRIVATE
        KK4S_EMBEDDED SDL2 INSANI USE_OGG_VORBIS ENABLE_1BYTE_CHAR FORCE_1BYTE_CHAR)
    if(KK4S_PLATFORM STREQUAL "desktop" AND NOT WIN32)
        target_compile_definitions(kk4s_onscripter PRIVATE LINUX)
    endif()
    # ONScripter's class names (FontInfo, DirtyRect, ...) also exist in the
    # Kirikiri engine and plugins: same-named inline members would be merged
    # silently by the linker. Every ONScripter class gets an ons_ prefix.
    foreach(cls Alias AnimationInfo ArchiveInfo ArrayVariable BaseReader ButtonLink ButtonState
                DirectReader DirtyRect EffectLink FileInfo FontInfo GlyphCache LabelInfo LogInfo
                LogLink NestInfo NsaReader ONScripterLabel OVInfo RMenuLink RegisteredCompressionType
                RubyStruct SarReader SaveFileInfo ScriptHandler ScriptParser SelectLink TextBuffer
                UserFuncLUT VariableData VariableInfo WAVE_HEADER)
        target_compile_definitions(kk4s_onscripter PRIVATE ${cls}=ons_${cls})
    endforeach()
    target_compile_options(kk4s_onscripter PRIVATE -g -O2 -fpermissive -Wno-write-strings -Wno-narrowing
        -Wno-register -w)
    if(KK4S_PLATFORM STREQUAL "switch")
        target_include_directories(kk4s_onscripter PRIVATE ${KK4S_ONS_STATIC_INCLUDE_DIRS} ${KK4S_DEPS_STATIC_INCLUDE_DIRS})
        target_compile_options(kk4s_onscripter PRIVATE ${KK4S_ONS_STATIC_CFLAGS_OTHER})
        target_link_directories(${PROJECT_NAME} PRIVATE ${KK4S_ONS_STATIC_LIBRARY_DIRS})
        # SDL_mixer's Tremor would clash with libvorbis (kk4s, FFmpeg): renamed copies
        include(${CMAKE_SOURCE_DIR}/cmake/kk4s_tremor_rename.cmake)
        kk4s_tremor_rename("${DEVKITPRO}/portlibs/switch/lib" "${CMAKE_BINARY_DIR}/kk4s-tremor" KK4S_ONS_MIXER)
        set(KK4S_ONS_LIBS ${KK4S_ONS_STATIC_LIBRARIES})
        list(TRANSFORM KK4S_ONS_LIBS REPLACE "^SDL2_mixer$" "${KK4S_ONS_MIXER}")
        list(FILTER KK4S_ONS_LIBS EXCLUDE REGEX "^vorbisidec$")
        set(KK4S_ONS_LINK ${KK4S_SMPEG2_LIBRARY} ${KK4S_ONS_LIBS} bz2)
    else()
        target_include_directories(kk4s_onscripter PRIVATE ${KK4S_ONS_INCLUDE_DIRS} ${KK4S_DEPS_INCLUDE_DIRS})
        target_compile_options(kk4s_onscripter PRIVATE ${KK4S_ONS_CFLAGS_OTHER})
        target_link_directories(${PROJECT_NAME} PRIVATE ${KK4S_ONS_LIBRARY_DIRS})
        set(KK4S_ONS_LINK ${KK4S_SMPEG2_LIBRARY} ${KK4S_ONS_LIBRARIES} bz2)
    endif()
    target_compile_definitions(${PROJECT_NAME} PRIVATE KK4S_HAS_ONSCRIPTER=1)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/engines/onscripter-nx/include)
else()
    message(STATUS "kk4s: ONScripter not built in (separate program from engines/onscripter/)")
    set(KK4S_ONS_LINK "")
endif()

# --- rlvm built in (RealLive games, docs/engines.md) ---
# GPL-3+: with it the kk4s binary is distributed under the GPL. AUTO = on for
# the Switch when the libraries rlvm needs (Boost, libsndfile) are where
# tools/build-rlvm-deps.sh puts them and the SDL2 ports are installed; without
# them RealLive games use a separate rlvm.nro from <root>/engines/rlvm/.
set(KK4S_RLVM "AUTO" CACHE STRING "Build rlvm into kk4s: AUTO, ON or OFF")
set_property(CACHE KK4S_RLVM PROPERTY STRINGS AUTO ON OFF)
set(KK4S_RLVM_PREFIX "${CMAKE_SOURCE_DIR}/out-engines/rlvm-deps/prefix" CACHE PATH
    "Boost and libsndfile for rlvm (tools/build-rlvm-deps.sh)")
if(KK4S_PLATFORM STREQUAL "switch" AND NOT KK4S_RLVM STREQUAL "OFF")
    if(KK4S_RLVM STREQUAL "ON")
        set(KK4S_RLVM_REQUIRED REQUIRED)
    endif()
    pkg_check_modules(KK4S_RLVM_PC ${KK4S_RLVM_REQUIRED} SDL2_image SDL2_ttf)
    find_path(KK4S_RLVM_BOOST_INCLUDE_DIR boost/filesystem.hpp HINTS ${KK4S_RLVM_PREFIX}/include)
    find_library(KK4S_RLVM_BOOST_SERIALIZATION NAMES boost_serialization HINTS ${KK4S_RLVM_PREFIX}/lib)
    find_library(KK4S_RLVM_SNDFILE NAMES sndfile HINTS ${KK4S_RLVM_PREFIX}/lib)
    if(KK4S_RLVM_PC_FOUND AND KK4S_RLVM_BOOST_INCLUDE_DIR AND KK4S_RLVM_BOOST_SERIALIZATION
            AND KK4S_RLVM_SNDFILE AND EXISTS "${DEVKITPRO}/portlibs/switch/lib/libSDL2_mixer.a")
        set(KK4S_RLVM_FOUND TRUE)
    elseif(KK4S_RLVM STREQUAL "ON")
        message(FATAL_ERROR "kk4s: rlvm dependencies not found (run tools/build-rlvm-deps.sh; "
            "needs the switch-sdl2_image / switch-sdl2_ttf / switch-libvorbis ports)")
    endif()
endif()
if(KK4S_RLVM_FOUND)
    message(STATUS "kk4s: rlvm built in")
    set(KK4S_RLVM_DIR ${CMAKE_SOURCE_DIR}/engines/rlvm)
    # The same sources as engines/rlvm/Makefile: rlvm, its vendored libraries
    # (guichan, zita-resampler, xclannad, pygame's alphablit) and the Switch
    # front end, which with -DKK4S_EMBEDDED is rlvm_main() instead of main().
    file(GLOB_RECURSE KK4S_RLVM_SOURCES ${KK4S_RLVM_DIR}/src/*.cc)
    list(FILTER KK4S_RLVM_SOURCES EXCLUDE REGEX "/src/platforms/")
    list(APPEND KK4S_RLVM_SOURCES
        ${KK4S_RLVM_DIR}/src/platforms/gcn/gcn_platform.cc
        ${KK4S_RLVM_DIR}/src/platforms/switch/rlvm_switch.cc)
    file(GLOB KK4S_RLVM_VENDOR
        ${KK4S_RLVM_DIR}/vendor/xclannad/*.cc ${KK4S_RLVM_DIR}/vendor/xclannad/*.cpp
        ${KK4S_RLVM_DIR}/vendor/pygame/alphablit.cc
        ${KK4S_RLVM_DIR}/vendor/zita-resampler/src/*.cc
        ${KK4S_RLVM_DIR}/vendor/guichan/src/*.cpp
        ${KK4S_RLVM_DIR}/vendor/guichan/src/widgets/*.cpp
        ${KK4S_RLVM_DIR}/vendor/guichan/src/sdl/*.cpp
        ${KK4S_RLVM_DIR}/vendor/guichan/src/opengl/*.cpp)
    add_library(kk4s_rlvm STATIC ${KK4S_RLVM_SOURCES} ${KK4S_RLVM_VENDOR})
    set_target_properties(kk4s_rlvm PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS ON)
    target_include_directories(kk4s_rlvm PRIVATE
        ${KK4S_RLVM_DIR}/src ${KK4S_RLVM_DIR} ${KK4S_RLVM_DIR}/vendor
        ${KK4S_RLVM_DIR}/vendor/guichan/include ${KK4S_RLVM_DIR}/vendor/zita-resampler/include
        ${KK4S_RLVM_DIR}/vendor/xclannad ${KK4S_RLVM_DIR}/switch/include
        ${KK4S_RLVM_PREFIX}/include ${DEVKITPRO}/portlibs/switch/include
        ${KK4S_RLVM_PC_INCLUDE_DIRS})
    target_compile_definitions(kk4s_rlvm PRIVATE
        __SWITCH__ KK4S_EMBEDDED HAVE_CONFIG_H NO_SDL_GLEXT CASE_SENSITIVE_FILESYSTEM
        _THREAD_SAFE NDEBUG BOOST_DISABLE_ASSERTS BOOST_HAS_GETTIMEOFDAY
        # Class names that also exist in the Kirikiri engine (kk4s' x86 Memory,
        # ResampleImage.cpp's Resampler): same-named inline members would be
        # merged silently by the linker, so rlvm's get an rlvm_ prefix, like
        # ONScripter's ons_ prefix above.
        Memory=rlvm_Memory Resampler=rlvm_Resampler)
    target_compile_options(kk4s_rlvm PRIVATE
        -O2 -ffunction-sections -fdata-sections -funsigned-char -fexceptions -frtti
        -Wno-sign-compare -Wno-narrowing -Wno-write-strings -w)
    target_compile_definitions(${PROJECT_NAME} PRIVATE KK4S_HAS_RLVM=1)
    target_link_directories(${PROJECT_NAME} PRIVATE ${KK4S_RLVM_PREFIX}/lib)
    # SDL_mixer's Tremor with renamed symbols (see cmake/kk4s_tremor_rename.cmake):
    # reuse the copies ONScripter made, or make them for rlvm alone.
    if(KK4S_ONS_FOUND)
        list(GET KK4S_ONS_MIXER 0 KK4S_RLVM_MIXER)
        list(GET KK4S_ONS_MIXER 1 KK4S_RLVM_VORBISIDEC)
    else()
        include(${CMAKE_SOURCE_DIR}/cmake/kk4s_tremor_rename.cmake)
        kk4s_tremor_rename("${DEVKITPRO}/portlibs/switch/lib" "${CMAKE_BINARY_DIR}/kk4s-tremor" KK4S_RLVM_TREMOR)
        list(GET KK4S_RLVM_TREMOR 0 KK4S_RLVM_MIXER)
        list(GET KK4S_RLVM_TREMOR 1 KK4S_RLVM_VORBISIDEC)
    endif()
    set(KK4S_RLVM_LINK ${KK4S_RLVM_SNDFILE} ${KK4S_RLVM_BOOST_SERIALIZATION}
        ${KK4S_RLVM_MIXER} ${KK4S_RLVM_VORBISIDEC}
        boost_program_options boost_serialization boost_iostreams boost_filesystem boost_date_time
        SDL2_ttf SDL2_image vorbisfile vorbis ogg
        modplug mpg123 opusfile opus FLAC)
else()
    message(STATUS "kk4s: rlvm not built in (separate program from engines/rlvm/)")
    set(KK4S_RLVM_LINK "")
endif()

if(WIN32 AND KK4S_PLATFORM STREQUAL "desktop")
    target_sources(${PROJECT_NAME} PRIVATE platform/desktop/kk4s.rc)
endif()

if(KK4S_PLATFORM STREQUAL "switch")
    # Everything is a static portlib: use the --static flags and a link group
    # (freetype <-> harfbuzz and ffmpeg libraries depend on each other).
    target_include_directories(${PROJECT_NAME} PRIVATE ${KK4S_DEPS_STATIC_INCLUDE_DIRS})
    target_compile_options(${PROJECT_NAME} PRIVATE ${KK4S_DEPS_STATIC_CFLAGS_OTHER})
    target_link_directories(${PROJECT_NAME} PRIVATE ${KK4S_DEPS_STATIC_LIBRARY_DIRS})
    if(KK4S_ONS_FOUND)
        target_link_libraries(${PROJECT_NAME} kk4s_onscripter)
    endif()
    if(KK4S_RLVM_FOUND)
        target_link_libraries(${PROJECT_NAME} kk4s_rlvm)
    endif()
    target_link_libraries(${PROJECT_NAME}
        -Wl,--start-group ${KK4S_ONS_LINK} ${KK4S_RLVM_LINK} ${KK4S_DEPS_STATIC_LIBRARIES} -Wl,--end-group
        ${KK4S_DEPS_STATIC_LDFLAGS_OTHER}
    )
else()
    target_include_directories(${PROJECT_NAME} PRIVATE ${KK4S_DEPS_INCLUDE_DIRS})
    target_compile_options(${PROJECT_NAME} PRIVATE ${KK4S_DEPS_CFLAGS_OTHER})
    target_link_directories(${PROJECT_NAME} PRIVATE ${KK4S_DEPS_LIBRARY_DIRS})
    if(KK4S_ONS_FOUND)
        target_link_libraries(${PROJECT_NAME} kk4s_onscripter ${KK4S_ONS_LINK})
    endif()
    target_link_libraries(${PROJECT_NAME} ${KK4S_DEPS_LIBRARIES} ${KK4S_DEPS_LDFLAGS_OTHER})
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} Threads::Threads)
    if(KK4S_CAPSTONE_STATIC_LIBRARY)
        target_link_libraries(${PROJECT_NAME} "${KK4S_CAPSTONE_STATIC_LIBRARY}")
    else()
        target_link_directories(${PROJECT_NAME} PRIVATE ${KK4S_CAPSTONE_LIBRARY_DIRS})
        target_link_libraries(${PROJECT_NAME} ${KK4S_CAPSTONE_LIBRARIES} ${KK4S_CAPSTONE_LDFLAGS_OTHER})
    endif()
    target_compile_options(${PROJECT_NAME} PRIVATE ${KK4S_CAPSTONE_CFLAGS_OTHER})
    target_compile_definitions(${PROJECT_NAME} PRIVATE KK4S_DESKTOP=1)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ${INCLUDES})
# kk4s: layerExDraw.dll over plutovg (vendored, static, no exported symbols)
target_compile_definitions(${PROJECT_NAME} PRIVATE PLUTOVG_BUILD_STATIC PLUTOVG_BUILD)
target_compile_options(${PROJECT_NAME} PUBLIC $<$<COMPILE_LANGUAGE:C>:${C_FLAGS}>)
target_compile_options(${PROJECT_NAME} PUBLIC $<$<COMPILE_LANGUAGE:C>:${C_ONLY_FLAGS}>)
target_compile_definitions(${PROJECT_NAME} PRIVATE KK4S_VERSION="${KK4S_VERSION}" KK4S_GIT_HASH="${KK4S_GIT_HASH}")
target_compile_options(${PROJECT_NAME} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:${CXX_FLAGS}>)
target_compile_options(${PROJECT_NAME} PUBLIC $<$<CONFIG:RELEASE>:${OPTIMIZATION_FLAGS}>)
target_compile_options(${PROJECT_NAME} PUBLIC $<$<CONFIG:RELWITHDEBINFO>:${OPTIMIZATION_FLAGS}>)

# Pixel loops (layer blending, transitions, scaling) run on the CPU for every
# frame; -O3 lets GCC vectorize them (NEON on the Switch). Comes after -O2.
set_source_files_properties(
    engine/source/core/visual/tvpgl.cpp
    engine/source/core/visual/gl/blend_function.cpp
    engine/source/core/visual/gl/ResampleImage.cpp
    PROPERTIES COMPILE_OPTIONS "$<$<NOT:$<CONFIG:Debug>>:-O3>")

if(KK4S_PLATFORM STREQUAL "desktop")
    # PC viewer: plain executable, no packaging
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME kk4s)
    if(WIN32)
        target_link_options(${PROJECT_NAME} PRIVATE -static-libgcc -static-libstdc++)
    endif()
    return()
endif()

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
