cmake_minimum_required(VERSION 3.20)
project(DSAemuEngine VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED   ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------------------------------
#  submodule パス確認
#  git submodule add https://github.com/digital-sound-antiques/emu2149  extern/emu2149
#  git submodule add https://github.com/digital-sound-antiques/emu2413  extern/emu2413
#  git submodule add https://github.com/digital-sound-antiques/emu8950  extern/emu8950
#  git submodule add https://github.com/digital-sound-antiques/emu2212  extern/emu2212
#  git submodule add https://github.com/digital-sound-antiques/emu76489 extern/emu76489
# -------------------------------------------------------
foreach(_core emu2149 emu2413 emu8950 emu2212 emu76489)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/extern/${_core}/CMakeLists.txt")
        message(FATAL_ERROR
            "Submodule extern/${_core} is not initialized.\n"
            "  Run: git submodule update --init --recursive")
    endif()
endforeach()

# =======================================================
#  ソースファイル
#  各 submodule の .c を直接コンパイル (submodule 側の
#  CMakeLists は使わず、ヘッダと実装ファイルのみ取得)
# =======================================================
set(CORE_SOURCES
    # emu2149  YM2149 / AY-3-8910 PSG → SSG
    extern/emu2149/emu2149.c

    # emu2413  YM2413 OPLL → OPLL / OPLLP / OPLLX / VRC7
    extern/emu2413/emu2413.c

    # emu8950  Y8950 / YM3526 / YM3812 OPL → Y8950 / OPL / OPL2
    extern/emu8950/emu8950.c
    extern/emu8950/emuadpcm.c

    # emu2212  Konami SCC → SCC
    extern/emu2212/emu2212.c

    # emu76489 SN76489 DCSG → DCSG
    extern/emu76489/emu76489.c

    # emu8950 / emu2413 / emu2149 のフォーク → OPL2EX / OPLLEX / SSGS (Y8960)
    src/y8960/Y8960Opl2exCore.c
    src/y8960/Y8960Opl2exAdpcm.c
    src/y8960/Y8960OpllCore.c
    src/y8960/Y8960SsgsCore.c
)

set(ENGINE_SOURCES
    src/DSAemuEngine.cpp
)

# =======================================================
#  共有ライブラリ
#  出力ファイル名: DSAemuEngine.dll / libDSAemuEngine.so / libDSAemuEngine.dylib
#  FMEngineTest から使う場合は -e オプションでパスを指定する。
# =======================================================
add_library(DSAemuEngine SHARED ${ENGINE_SOURCES} ${CORE_SOURCES})

target_include_directories(DSAemuEngine PRIVATE
    src                     # FmEngineApi.h
    extern/emu2149
    extern/emu2413
    extern/emu8950          # emuadpcm.h も含む
    extern/emu2212
    extern/emu76489
)

target_compile_definitions(DSAemuEngine PRIVATE FMENGINE_EXPORTS)

# -------------------------------------------------------
#  プラットフォーム別設定
# -------------------------------------------------------
if(MSVC)
    target_compile_options(DSAemuEngine PRIVATE /W3 /utf-8)
    set_target_properties(DSAemuEngine PROPERTIES
        OUTPUT_NAME              "DSAemuEngine"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        PDB_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/bin"
    )
else()
    target_compile_options(DSAemuEngine PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -fvisibility=hidden   # 非公開シンボルを隠す
    )
    find_package(Threads REQUIRED)
    target_link_libraries(DSAemuEngine PRIVATE Threads::Threads)
    set_target_properties(DSAemuEngine PROPERTIES
        OUTPUT_NAME              "DSAemuEngine"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

# -------------------------------------------------------
#  テスト (任意)
#  api_test は FMEngineTest と同じく DLL を実行時にロードして
#  エクスポートされた関数だけを叩く。
#  ctest -C Release で走らせる。
# -------------------------------------------------------
option(DSAEMU_BUILD_TESTS "Build the API test" ON)
if(DSAEMU_BUILD_TESTS)
    enable_testing()
    add_executable(api_test tests/api_test.cpp)
    target_include_directories(api_test PRIVATE src)
    target_link_libraries(api_test PRIVATE ${CMAKE_DL_LIBS})
    if(MSVC)
        target_compile_options(api_test PRIVATE /W3 /utf-8)
    endif()
    add_dependencies(api_test DSAemuEngine)
    add_test(NAME api_test COMMAND api_test $<TARGET_FILE:DSAemuEngine>)
endif()

# -------------------------------------------------------
#  インストール (任意)
# -------------------------------------------------------
install(TARGETS DSAemuEngine
    RUNTIME DESTINATION bin   # Windows DLL
    LIBRARY DESTINATION lib   # Linux/macOS .so/.dylib
)
install(FILES src/FmEngineApi.h DESTINATION include)
