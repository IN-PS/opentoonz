# OpenToonz Mobile native engine.
#
# Builds a subset of the ORIGINAL OpenToonz C++ sources (tnzcore, toonzlib,
# image/tzl) unmodified from ../../toonz/sources, plus the mobile engine
# (document model, tools, compositing, export) and the JNI bridge.
#
# Two targets are supported with the same source list:
#   * Android arm64-v8a (via the NDK toolchain file, Qt5Core cross-built for
#     Android)  -> libotmengine.so packaged into the APK
#   * Host Linux (system Qt5Core) -> engine tests (otm_tests) so that the
#     engine can be verified without a device.
cmake_minimum_required(VERSION 3.16)
project(otmengine CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC OFF)

get_filename_component(OT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
set(OT_SRC ${OT_ROOT}/toonz/sources)
set(OT_COMMON ${OT_SRC}/common)

option(OTM_BUILD_TESTS "Build host test executable" ON)
option(OTM_BUILD_JNI "Build the JNI bridge" ON)

# ---------------------------------------------------------------- Qt5 Core
# For Android pass -DQt5_DIR=<qt5-android>/lib/cmake/Qt5 and
# -DOTM_HOST_MOC=/usr/bin/moc (or the host moc from the Qt build).
# OTM_QT_INCLUDE_DIR: compile-only check against Qt headers without a Qt
# build for the target (static libraries only; nothing is linked).
if(OTM_QT_INCLUDE_DIR)
  add_library(Qt5::Core INTERFACE IMPORTED)
  set_target_properties(Qt5::Core PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
    "${OTM_QT_INCLUDE_DIR};${OTM_QT_INCLUDE_DIR}/QtCore;${OTM_QT_INCLUDE_DIR}/QtCore/${OTM_QT_VERSION}")
else()
  find_package(Qt5 COMPONENTS Core REQUIRED)
endif()
if(OTM_HOST_MOC)
  set(MOC_EXE ${OTM_HOST_MOC})
else()
  get_target_property(MOC_EXE Qt5::moc IMPORTED_LOCATION)
endif()

# Boost is used header-only by tnzcore.  Only the boost/ directory is exposed
# (a symlink in the build tree) so that cross builds never see host libc
# headers that may live next to it (e.g. /usr/include).
find_path(OTM_BOOST_INCLUDE boost/config.hpp PATHS /usr/include /usr/local/include
  NO_CMAKE_FIND_ROOT_PATH DOC "directory containing boost/")
if(NOT EXISTS ${OTM_BOOST_INCLUDE}/boost/config.hpp)
  message(FATAL_ERROR "Boost headers not found (set OTM_BOOST_INCLUDE)")
endif()
set(BOOST_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/boost-include)
file(MAKE_DIRECTORY ${BOOST_INCLUDE})
file(CREATE_LINK ${OTM_BOOST_INCLUDE}/boost ${BOOST_INCLUDE}/boost SYMBOLIC)

# LZ4 (used by tcodec for the TLV/image cache codec): compiled from the
# copy bundled in OpenToonz's thirdparty directory so it works on Android.
set(LZ4_DIR ${OT_ROOT}/thirdparty/Lz4/Lz4_131/lib)
add_library(otm_lz4 STATIC ${LZ4_DIR}/lz4.c ${LZ4_DIR}/lz4hc.c ${LZ4_DIR}/lz4frame.c ${LZ4_DIR}/xxhash.c)
target_include_directories(otm_lz4 PUBLIC ${LZ4_DIR})

# libpng 1.6.21 bundled in OpenToonz thirdparty (used by image/png/tiio_png).
set(PNG_DIR ${OT_ROOT}/thirdparty/libpng-1.6.21)
set(PNG_GEN ${CMAKE_CURRENT_BINARY_DIR}/pnggen)
configure_file(${PNG_DIR}/scripts/pnglibconf.h.prebuilt ${PNG_GEN}/pnglibconf.h COPYONLY)
add_library(otm_png STATIC
  ${PNG_DIR}/png.c ${PNG_DIR}/pngerror.c ${PNG_DIR}/pngget.c ${PNG_DIR}/pngmem.c
  ${PNG_DIR}/pngpread.c ${PNG_DIR}/pngread.c ${PNG_DIR}/pngrio.c
  ${PNG_DIR}/pngrtran.c ${PNG_DIR}/pngrutil.c ${PNG_DIR}/pngset.c
  ${PNG_DIR}/pngtrans.c ${PNG_DIR}/pngwio.c ${PNG_DIR}/pngwrite.c
  ${PNG_DIR}/pngwtran.c ${PNG_DIR}/pngwutil.c)
target_include_directories(otm_png PUBLIC ${PNG_DIR} ${PNG_GEN})
target_compile_definitions(otm_png PRIVATE PNG_ARM_NEON_OPT=0)
target_compile_options(otm_png PRIVATE -w)
find_package(ZLIB REQUIRED)
target_link_libraries(otm_png PUBLIC ZLIB::ZLIB)

# LZO 2.03 (thirdparty) + OpenToonz's lzocompress/lzodecompress drivers.
# TLV levels are LZO compressed; OpenToonz runs LZO in these separate helper
# processes (GPL isolation).  On Android they are packaged as
# lib/arm64-v8a/liblzocompress.so / liblzodecompress.so (PIE executables).
set(LZO_DIR ${OT_ROOT}/thirdparty/lzo/2.03)
add_library(otm_lzo STATIC
  ${LZO_DIR}/src/lzo1x_1.c ${LZO_DIR}/src/lzo1x_d1.c ${LZO_DIR}/src/lzo1x_d2.c ${LZO_DIR}/src/lzo_init.c
  ${LZO_DIR}/src/lzo_util.c ${LZO_DIR}/src/lzo_ptr.c ${LZO_DIR}/src/lzo_str.c
  ${LZO_DIR}/src/lzo_crc.c)
target_include_directories(otm_lzo PUBLIC ${LZO_DIR}/include ${LZO_DIR}/src)
target_compile_options(otm_lzo PRIVATE -w -O2 -fno-strict-aliasing)
# old LZO compile-time asserts are rejected by modern compilers
target_compile_options(otm_lzo PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/compat/lzo_compat.h)
foreach(t lzocompress lzodecompress)
  add_executable(${t} ${OT_ROOT}/thirdparty/lzo/driver/${t}.c)
  target_link_libraries(${t} PRIVATE otm_lzo)
  target_compile_options(${t} PRIVATE -w)
endforeach()

# ---------------------------------------------------------------- OpenToonz sources
set(TNZCORE_SOURCES
  ${OT_COMMON}/tcore/tdata.cpp
  ${OT_COMMON}/tcore/tdebugmessage.cpp
  ${OT_COMMON}/tcore/texception.cpp
  ${OT_COMMON}/tcore/tmathutil.cpp
  ${OT_COMMON}/tproperty.cpp
  ${OT_COMMON}/tcore/trandom.cpp
  ${OT_COMMON}/tcore/tsmartpointer.cpp
  ${OT_COMMON}/tcore/tstopwatch.cpp
  ${OT_COMMON}/tcore/tstring.cpp
  ${OT_COMMON}/tcore/tundo.cpp
  ${OT_COMMON}/tcolor/tcolorfunctions.cpp
  ${OT_COMMON}/tcolor/tcolorvalue.cpp
  ${OT_COMMON}/tcolor/tpixel.cpp
  ${OT_COMMON}/tcolor/tpixelutils.cpp
  ${OT_COMMON}/tcolor/tspectrum.cpp
  ${OT_COMMON}/tgeometry/tcurves.cpp
  ${OT_COMMON}/tgeometry/tcurveutil.cpp
  ${OT_COMMON}/tgeometry/tgeometry.cpp
  ${OT_COMMON}/traster/traster.cpp
  ${OT_COMMON}/timage/timage.cpp
  ${OT_COMMON}/timage/tlevel.cpp
  ${OT_COMMON}/tsystem/tbigmemorymanager.cpp
  ${OT_COMMON}/tcontenthistory.cpp
  ${OT_COMMON}/tsystem/tfilepath.cpp
  ${OT_COMMON}/tsystem/tfilepath_io.cpp
  ${OT_COMMON}/tsystem/tfiletype.cpp
  ${OT_COMMON}/tcache/timagecache.cpp
  ${OT_COMMON}/tsystem/tlogger.cpp
  ${OT_COMMON}/tsystem/tsystem.cpp
  ${OT_COMMON}/tsystem/tsystempd.cpp
  ${OT_COMMON}/tvectorimage/cornerdetector.cpp
  ${OT_COMMON}/tvectorimage/drawutil.cpp
  ${OT_COMMON}/tvectorimage/tcomputeregions.cpp
  ${OT_COMMON}/tvectorimage/tregion.cpp
  ${OT_COMMON}/tvectorimage/tstroke.cpp
  ${OT_COMMON}/tvectorimage/tstrokeoutline.cpp
  ${OT_COMMON}/tvectorimage/tsegmentadjuster.cpp
  ${OT_COMMON}/tvectorimage/tsweepboundary.cpp
  ${OT_COMMON}/tvectorimage/tvectorimage.cpp
  ${OT_COMMON}/tvectorimage/tl2lautocloser.cpp
  ${OT_COMMON}/tvectorimage/outlineApproximation.cpp
  ${OT_COMMON}/timage_io/timage_io.cpp
  ${OT_COMMON}/timage_io/tlevel_io.cpp
  ${OT_COMMON}/trasterimage/tcodec.cpp
  ${OT_COMMON}/trasterimage/trasterimage.cpp
  ${OT_COMMON}/tgl/tstencilcontrol.cpp
  ${OT_COMMON}/tgl/tgldisplaylistsmanager.cpp
  ${OT_COMMON}/tvrender/tcolorstyles.cpp
  ${OT_COMMON}/tvrender/tellipticbrush.cpp
  ${OT_COMMON}/tvrender/tglcurves.cpp
  ${OT_COMMON}/tvrender/tglregions.cpp
  ${OT_COMMON}/tvrender/tinbetween.cpp
  ${OT_COMMON}/tvrender/tpalette.cpp
  ${OT_COMMON}/tvrender/tpaletteutil.cpp
  ${OT_COMMON}/tvrender/tregionprop.cpp
  ${OT_COMMON}/tvrender/tsimplecolorstyles.cpp
  ${OT_COMMON}/tvrender/tstrokedeformations.cpp
  ${OT_COMMON}/tvrender/tstrokeprop.cpp
  ${OT_COMMON}/tvrender/tstrokeutil.cpp
  ${OT_COMMON}/tvrender/ttessellator.cpp
  ${OT_COMMON}/tvrender/tvectorbrush.cpp
  ${OT_COMMON}/tvrender/tvectorbrushstyle.cpp
  ${OT_COMMON}/trop/bbox.cpp
  ${OT_COMMON}/trop/brush.cpp
  ${OT_COMMON}/trop/quickput.cpp
  ${OT_COMMON}/trop/runsmap.cpp
  ${OT_COMMON}/trop/tantialias.cpp
  ${OT_COMMON}/trop/tblur.cpp
  ${OT_COMMON}/trop/tcheckboard.cpp
  ${OT_COMMON}/trop/tconvert.cpp
  ${OT_COMMON}/trop/tconvolve.cpp
  ${OT_COMMON}/trop/tdespeckle.cpp
  ${OT_COMMON}/trop/tdistancetransform.cpp
  ${OT_COMMON}/trop/terodilate.cpp
  ${OT_COMMON}/trop/tinvert.cpp
  ${OT_COMMON}/trop/toperators.cpp
  ${OT_COMMON}/trop/tover.cpp
  ${OT_COMMON}/trop/traylit.cpp
  ${OT_COMMON}/trop/tresample.cpp
  ${OT_COMMON}/trop/trgbmscale.cpp
  ${OT_COMMON}/trop/trop.cpp
  ${OT_COMMON}/trop/tropcm.cpp
  ${OT_COMMON}/trop/trop_borders.cpp
  ${OT_COMMON}/trop/tfracmove.cpp
  ${OT_COMMON}/tstream/tstream.cpp
  ${OT_COMMON}/tstream/tstreamexception.cpp
  ${OT_COMMON}/tstream/tpersistset.cpp
  ${OT_COMMON}/tiio/tiio.cpp
  ${OT_COMMON}/ttoonzimage/ttonzimage.cpp
  ${OT_COMMON}/tapptools/tenv.cpp
  ${OT_COMMON}/tmeshimage/tmeshimage.cpp
  ${OT_COMMON}/tstringid.cpp
  ${OT_COMMON}/tvariant.cpp
)

set(TOONZLIB_SOURCES
  ${OT_SRC}/toonzlib/rasterstrokegenerator.cpp
  ${OT_SRC}/toonzlib/rasterbrush.cpp
  ${OT_SRC}/toonzlib/fill.cpp
  ${OT_SRC}/toonzlib/ttilesaver.cpp
  ${OT_SRC}/toonzlib/ttileset.cpp
)

set(IMAGE_SOURCES
  ${OT_SRC}/image/tzl/tiio_tzl.cpp
  ${OT_SRC}/image/png/tiio_png.cpp
  ${OT_SRC}/image/pli/tiio_pli.cpp
  ${OT_SRC}/image/pli/pli_io.cpp
  ${OT_SRC}/image/pli/tags.cpp
  ${OT_SRC}/image/compatibility/tfile_io.c
)

set(MOC_HEADERS ${OT_SRC}/include/tundo.h)

set(MOC_OUT)
foreach(h ${MOC_HEADERS})
  get_filename_component(n ${h} NAME_WE)
  set(o ${CMAKE_CURRENT_BINARY_DIR}/moc_${n}.cpp)
  add_custom_command(OUTPUT ${o} COMMAND ${MOC_EXE} ${h} -o ${o} DEPENDS ${h})
  list(APPEND MOC_OUT ${o})
endforeach()

add_library(otm_opentoonz STATIC
  ${TNZCORE_SOURCES} ${TOONZLIB_SOURCES} ${IMAGE_SOURCES} ${MOC_OUT}
  compat/glstub.cpp
  compat/tgl_stub.cpp)
target_include_directories(otm_opentoonz PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/compat
  ${OT_SRC}/include
  ${OT_SRC}/include/toonz
  ${OT_SRC}/image/tzl
  ${OT_SRC}/image/png
  ${OT_SRC}/image/pli
  ${OT_COMMON}/tvectorimage
  ${OT_COMMON}/tvrender
  ${OT_COMMON}/trop
  ${OT_COMMON}/tcore
  ${BOOST_INCLUDE})
target_compile_definitions(otm_opentoonz PUBLIC
  LINUX OTM_GL_STUB OTM_NO_QTGUI OTM_NO_PREFERENCES OTM_LZO_ENV OTM_NO_JPEG TNZ_LITTLE_ENDIAN=1 x64 QT_CORE_LIB
  TNZCORE_EXPORTS TCOLOR_EXPORTS TGEOMETRY_EXPORTS TRASTER_EXPORTS
  TIMAGE_EXPORTS TSYSTEM_EXPORTS TVECTORIMAGE_EXPORTS TGL_EXPORTS
  TSOUND_EXPORTS TIMAGE_IO_EXPORTS TRASTERIMAGE_EXPORTS TMETAIMAGE_EXPORTS
  TVRENDER_EXPORTS TFLASH_EXPORTS TROP_EXPORTS TSTREAM_EXPORTS
  TSTRINGID_EXPORTS TVARIANT_EXPORTS TOONZLIB_EXPORTS TNZBASE_EXPORTS
  TAPPTOOLS_EXPORTS)
target_compile_options(otm_opentoonz PRIVATE -w -fno-strict-aliasing)
target_link_libraries(otm_opentoonz PUBLIC Qt5::Core otm_lz4 otm_png)
if(UNIX AND NOT ANDROID)
  target_link_libraries(otm_opentoonz PUBLIC pthread)
endif()

# ---------------------------------------------------------------- MyPaint
# libmypaint 1.6 + json-c 0.17 (sources: tools/fetch-mypaint.sh) and the
# OpenToonz MyPaint adapter (tnztools/mypainttoonzbrush.cpp), as used by the
# desktop Toonz Raster / Raster brush tools.
set(OTM_MYPAINT_DIR "" CACHE PATH "libmypaint-1.6.0 source dir (empty = no MyPaint)")
set(OTM_JSONC_DIR "" CACHE PATH "json-c 0.17 source dir")
if(OTM_MYPAINT_DIR AND OTM_JSONC_DIR)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(BUILD_APPS OFF CACHE BOOL "" FORCE)
  set(DISABLE_EXTRA_LIBS ON CACHE BOOL "" FORCE)
  set(DISABLE_WERROR ON CACHE BOOL "" FORCE)
  set(DISABLE_BSYMBOLIC ON CACHE BOOL "" FORCE)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  add_subdirectory(${OTM_JSONC_DIR} ${CMAKE_CURRENT_BINARY_DIR}/json-c EXCLUDE_FROM_ALL)
  # the tarball's config.h is the one of a Linux autotools build (gettext,
  # glib); sources are copied without it and get a minimal one instead
  set(MP ${CMAKE_CURRENT_BINARY_DIR}/mypaint-src)
  file(GLOB _mp_files ${OTM_MYPAINT_DIR}/*.c ${OTM_MYPAINT_DIR}/*.h)
  list(FILTER _mp_files EXCLUDE REGEX "/config\\.h$")
  file(COPY ${_mp_files} DESTINATION ${MP})
  file(COPY ${OTM_MYPAINT_DIR}/fastapprox ${OTM_MYPAINT_DIR}/glib DESTINATION ${MP})
  file(WRITE ${MP}/config.h
    "/* OpenToonz Mobile: no gettext, no glib, no OpenMP */\n#define GETTEXT_PACKAGE \"libmypaint\"\n")
  add_library(otm_mypaint STATIC
    ${MP}/brushmodes.c ${MP}/fifo.c ${MP}/helpers.c ${MP}/mypaint-mapping.c
    ${MP}/mypaint.c ${MP}/mypaint-brush.c ${MP}/mypaint-brush-settings.c
    ${MP}/mypaint-fixed-tiled-surface.c ${MP}/mypaint-matrix.c
    ${MP}/mypaint-symmetry.c ${MP}/mypaint-rectangle.c ${MP}/mypaint-surface.c
    ${MP}/mypaint-tiled-surface.c ${MP}/operationqueue.c ${MP}/rng-double.c
    ${MP}/tilemap.c)
  target_include_directories(otm_mypaint PUBLIC ${MP})
  target_compile_definitions(otm_mypaint PRIVATE MYPAINT_CONFIG_USE_GLIB=0)
  target_compile_options(otm_mypaint PRIVATE -w -std=gnu99)
  target_link_libraries(otm_mypaint PUBLIC json-c m)
  set(OTM_HAVE_MYPAINT ON)
  message(STATUS "MyPaint brushes: ON (${OTM_MYPAINT_DIR})")
else()
  message(STATUS "MyPaint brushes: OFF (set OTM_MYPAINT_DIR / OTM_JSONC_DIR)")
endif()

# ---------------------------------------------------------------- mobile engine
add_library(otm_core STATIC
  src/document.cpp
  src/compositor.cpp
  src/tools.cpp
  src/projectio.cpp
  src/gifencoder.cpp
  src/vector.cpp
  src/tnzimport.cpp
  src/engine.cpp)
target_include_directories(otm_core PUBLIC src)
target_link_libraries(otm_core PUBLIC otm_opentoonz)
if(OTM_HAVE_MYPAINT)
  target_sources(otm_core PRIVATE ${OT_SRC}/tnztools/mypainttoonzbrush.cpp)
  target_include_directories(otm_core PRIVATE ${OT_SRC}/tnztools)
  target_compile_definitions(otm_core PUBLIC OTM_HAVE_MYPAINT)
  target_link_libraries(otm_core PUBLIC otm_mypaint)
endif()
target_compile_options(otm_core PRIVATE -Wall -Wno-unused-parameter)

if(OTM_BUILD_JNI)
  if(NOT ANDROID)
    find_package(JNI REQUIRED)
  endif()
  add_library(otmengine SHARED src/jni_bridge.cpp)
  target_link_libraries(otmengine PRIVATE otm_core)
  if(ANDROID)
    target_link_libraries(otmengine PRIVATE jnigraphics log android)
    # QtAndroidPrivate::initJNI (qjnihelpers_p.h)
    target_include_directories(otmengine PRIVATE ${Qt5Core_PRIVATE_INCLUDE_DIRS})
    # Qt5AndroidSupport.cmake renames outputs (lib<name>_<abi>.so in
    # android-build/); the app loads plain libotmengine.so
    set_target_properties(otmengine PROPERTIES SUFFIX ".so"
      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    target_link_options(otmengine PRIVATE -Wl,--gc-sections -Wl,-z,max-page-size=16384)
  else()
    target_include_directories(otmengine PRIVATE ${JNI_INCLUDE_DIRS})
  endif()
endif()

if(OTM_BUILD_TESTS AND NOT ANDROID)
  add_executable(otm_tests tests/otm_tests.cpp)
  target_link_libraries(otm_tests PRIVATE otm_core)
  add_dependencies(otm_tests lzocompress lzodecompress)
  target_compile_definitions(otm_tests PRIVATE
    OTM_TEST_LZO_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    OTM_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
    OTM_TEST_MYPAINT_DIR="${OT_ROOT}/stuff/library/mypaint brushes")
endif()
