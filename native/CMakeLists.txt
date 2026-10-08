cmake_minimum_required(VERSION 3.16)
project(th10_headless C CXX)

# 32-bit build on purpose: the reconstructed game and its platform layer pass
# pointers through u32 (wasm32 / original x86 layout), and the real targets
# (PSP, Android armeabi-v7a/x86) are 32-bit too. SSE math keeps float/double
# at IEEE precision (no x87 excess precision), matching wasm, ARM and PSP.
set(TH10_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(WEB ${TH10_ROOT}/th10_web/cpp)
set(COMMON_FLAGS -m32 -msse2 -mfpmath=sse -fno-strict-aliasing -ffp-contract=off)
set(LINK_FLAGS_32 -m32)

# Without the g++-multilib package, use a user-local sysroot made from the
# Ubuntu 32-bit packages (apt-get download + dpkg -x, no root):
#   lib32gcc-13-dev lib32stdc++-13-dev libc6-dev-i386 libc6-i386 lib32gcc-s1 lib32stdc++6
# The binary is linked statically, so no 32-bit loader is needed at run time.
set(TH10_I386_SYSROOT "$ENV{HOME}/opt/i386-sysroot" CACHE PATH "User-local 32-bit sysroot")
option(TH10_UBSAN_ALIGN "UBSan misaligned-access check" OFF)
if(NOT EXISTS /usr/lib32/crt1.o AND EXISTS ${TH10_I386_SYSROOT}/usr/lib32/crt1.o)
    set(S ${TH10_I386_SYSROOT})
    # SHELL: keeps each "-isystem <dir>" pair intact (CMake de-duplicates
    # repeated option tokens otherwise).
    list(APPEND COMMON_FLAGS
        "SHELL:-isystem ${S}/usr/include/x86_64-linux-gnu/c++/13/32"
        "SHELL:-isystem ${S}/usr/include/x86_64-linux-gnu"
        "SHELL:-isystem /usr/include/x86_64-linux-gnu")
    list(APPEND LINK_FLAGS_32
        -B${S}/usr/lib/gcc/x86_64-linux-gnu/13/32 -B${S}/usr/lib32
        -L${S}/usr/lib/gcc/x86_64-linux-gnu/13/32 -L${S}/usr/lib32)
    if(TH10_UBSAN_ALIGN)
        # UBSan's runtime needs dynamic TLS: link against the sysroot's shared
        # libc and run with its loader:
        #   $S/usr/lib32/ld-linux.so.2 --library-path $S/usr/lib32:$S/usr/lib/gcc/x86_64-linux-gnu/13/32 ./th10_headless
        list(APPEND LINK_FLAGS_32 -Wl,--sysroot=${S})
    else()
        list(APPEND LINK_FLAGS_32 -static)
    endif()
    # glibc 2.39's static i386 libm has no plain fmod symbol (only
    # __ieee754_fmod); the fmod wrapper in main.cpp calls that instead.
    if(NOT TH10_UBSAN_ALIGN)
        set(TH10_STATIC_I386 1)
    endif()
    message(STATUS "TH10: using local i386 sysroot ${S} (static link)")
endif()

file(GLOB GAME_SOURCES ${WEB}/game/*.cpp)
list(FILTER GAME_SOURCES EXCLUDE REGEX "/(LegacyBridge|LegacyCallbacks|Exports|Freestanding)\\.cpp$")
file(GLOB PLATFORM_SOURCES ${WEB}/platform/*.cpp)
set(HOST_SOURCES
    ${WEB}/sdl/GraphicsHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HeadlessRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HeadlessFiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HeadlessFonts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HeadlessAudio.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

add_library(th10_softfloat STATIC ${WEB}/rebuild/third_party/softfloat.c)
set_target_properties(th10_softfloat PROPERTIES C_STANDARD 11)
target_compile_definitions(th10_softfloat PRIVATE SOFTFLOAT_FAST_INT64 INLINE_LEVEL=5)
# The v86 amalgamation declares a few f128 helpers it never defines; they are
# only reached from functions TH10 never calls, so let the linker drop those.
target_compile_options(th10_softfloat PRIVATE ${COMMON_FLAGS} -O2 -w -ffunction-sections -fdata-sections)

add_executable(th10_headless ${GAME_SOURCES} ${PLATFORM_SOURCES} ${HOST_SOURCES})
set_target_properties(th10_headless PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_definitions(th10_headless PRIVATE TH_SDL3=1 TH_NATIVE_PLATFORM=1 TH10_AUDIO_OUT=1)
if(TH10_STATIC_I386)
    target_compile_definitions(th10_headless PRIVATE TH10_STATIC_I386_FMOD=1)
endif()
# Memory-limited profile (PSP/Android): drop the web runtime's two caches,
# the decoded archive-entry LRU (32 MiB) and the transition ANM preload.
# Call-site profiling build: no inlining/sibling calls so backtraces map to
# the real caller (counts are unchanged; only speed differs).
option(TH10_PROFILE_BUILD "Build for call-site attribution" OFF)
if(TH10_PROFILE_BUILD)
    target_compile_options(th10_headless PRIVATE -fno-inline -fno-optimize-sibling-calls -fno-omit-frame-pointer)
endif()
# Alignment check (PC): real Allegrex raises an address error on misaligned
# loads/stores where x86 and PPSSPP silently continue.
if(TH10_UBSAN_ALIGN)
    target_compile_options(th10_headless PRIVATE -fsanitize=alignment -fno-omit-frame-pointer)
    target_link_options(th10_headless PRIVATE -fsanitize=alignment)  # whole binary is -static already
endif()
option(TH10_SAMPLE_BUILD "Per-callback timing and the 20 kHz sampling profiler (TH10_SAMPLE=1)" OFF)
if(TH10_SAMPLE_BUILD)
    target_compile_options(th10_headless PRIVATE -fno-omit-frame-pointer)
    target_compile_definitions(th10_headless PRIVATE TH10_CHAIN_TIMING=1 TH10_SAMPLER=1)
endif()
option(TH10_LOWMEM "Disable upstream decode/preload caches" OFF)
if(TH10_LOWMEM)
    target_compile_definitions(th10_headless PRIVATE TH10_FILE_CACHE_LIMIT=0 TH10_PRELOAD_TRANSITIONS=0 TH10_ANM_COMPACT=1)
endif()
target_include_directories(th10_headless BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(th10_headless PRIVATE ${TH10_ROOT}/portable/sdl)
target_compile_options(th10_headless PRIVATE ${COMMON_FLAGS} -O2 -g -fno-exceptions -fno-rtti -Wno-attributes)
set(TH10_WRAPPED f32_to_extF80M f64_to_extF80M extF80M_to_f32 extF80M_to_f64 extF80M_to_i64
    extF80M_add extF80M_sub extF80M_mul extF80M_div extF80M_roundToInt extF80M_sqrt extF80M_lt_quiet extF80M_eq
    sin cos tan acos atan2 fmod
    malloc free calloc realloc posix_memalign aligned_alloc memalign)
set(TH10_WRAP_FLAGS)
foreach(symbol ${TH10_WRAPPED})
    list(APPEND TH10_WRAP_FLAGS "-Wl,--wrap=${symbol}")
endforeach()
target_link_options(th10_headless PRIVATE ${LINK_FLAGS_32} ${TH10_WRAP_FLAGS} -Wl,--gc-sections)
target_link_libraries(th10_headless PRIVATE th10_softfloat m)

# Extended (game/Arithmetic.cpp) against SoftFloat in every mode:
#   cmake --build <dir> --target th10_extended_check && <dir>/th10_extended_check [iterations]
add_executable(th10_extended_check EXCLUDE_FROM_ALL tools/extended_check.cpp ${WEB}/game/Arithmetic.cpp)
target_compile_options(th10_extended_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_extended_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_extended_check PRIVATE th10_softfloat m)
# UV-scroll skip proof (game/AnmInterpreter.cpp TH10_ANM_SCROLL_SKIP): tools/scroll_check.cpp
# TH10_RENDER_FAST_BULLETS floor_inline against std::floor for every float bit pattern
add_executable(th10_floor_inline_check EXCLUDE_FROM_ALL tools/floor_inline_check.cpp)
target_compile_options(th10_floor_inline_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_floor_inline_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_floor_inline_check PRIVATE m)
# TH10_FAST_RNG_UNIT: Rng::signed_unit's Extended expression against the fast float form for every u32
add_executable(th10_rng_unit_check EXCLUDE_FROM_ALL tools/rng_unit_check.cpp ${WEB}/game/Arithmetic.cpp ${WEB}/game/Rng.cpp)
target_compile_options(th10_rng_unit_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_rng_unit_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_rng_unit_check PRIVATE th10_softfloat m)
# UV-scroll skip proof (game/AnmInterpreter.cpp TH10_ANM_SCROLL_SKIP): tools/scroll_check.cpp
add_executable(th10_scroll_check EXCLUDE_FROM_ALL tools/scroll_check.cpp ${WEB}/game/Arithmetic.cpp)
target_compile_options(th10_scroll_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_scroll_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_scroll_check PRIVATE th10_softfloat m)
# TH10_HOT_MOVE check (game/BulletFrame.cpp move): tools/move_check.cpp
add_executable(th10_move_check EXCLUDE_FROM_ALL tools/move_check.cpp ${WEB}/game/Arithmetic.cpp)
target_compile_options(th10_move_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_move_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_move_check PRIVATE th10_softfloat m)
# TH10_FAST_ANGLE check (game/GameMath.cpp add_angle/normalize_angle): tools/angle_check.cpp
add_executable(th10_angle_check EXCLUDE_FROM_ALL tools/angle_check.cpp ${WEB}/game/Arithmetic.cpp)
target_compile_options(th10_angle_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_angle_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_angle_check PRIVATE th10_softfloat m)
# TH10_FAST_ALIGN / TH10_FAST_SHOT check (AnmRenderer.cpp submit, PlayerDamage.cpp): tools/shot_align_check.cpp
add_executable(th10_shot_align_check EXCLUDE_FROM_ALL tools/shot_align_check.cpp ${WEB}/game/Arithmetic.cpp)
target_compile_options(th10_shot_align_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_shot_align_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_shot_align_check PRIVATE th10_softfloat m)
# TH10_FAST_TIMER check (game/Timer.cpp Timer::advance): tools/timer_check.cpp
add_executable(th10_timer_check EXCLUDE_FROM_ALL tools/timer_check.cpp ${WEB}/game/Arithmetic.cpp)
target_compile_options(th10_timer_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_timer_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_timer_check PRIVATE th10_softfloat m)
# TH10_FAST_BILLBOARD check (game/AnmProjection.cpp billboard_geometry stages): tools/billboard_check.cpp
add_executable(th10_billboard_check EXCLUDE_FROM_ALL tools/billboard_check.cpp ${WEB}/game/Arithmetic.cpp)
target_compile_options(th10_billboard_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_billboard_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_billboard_check PRIVATE th10_softfloat m)
# TH10_FAST_FLOOR check (game/Movement.cpp snap, AnmRenderer.cpp pixel floor): tools/floor_check.cpp
add_executable(th10_floor_check EXCLUDE_FROM_ALL tools/floor_check.cpp ${WEB}/game/Arithmetic.cpp)
target_compile_options(th10_floor_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_floor_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_floor_check PRIVATE th10_softfloat m)
# TH10_FAST_ITEM / TH10_FAST_BIND check (ItemFrame.cpp, ItemDraw.cpp, AnmFile.cpp bind_sprite): tools/item_check.cpp
add_executable(th10_item_check EXCLUDE_FROM_ALL tools/item_check.cpp ${WEB}/game/Arithmetic.cpp ${WEB}/game/GameMath.cpp ${WEB}/game/Timer.cpp)
target_compile_options(th10_item_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_item_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_item_check PRIVATE th10_softfloat m)
# TH10_FAST_CANCEL / TH10_FAST_PLAYFIELD check (BulletCancellation.cpp cancel_bullet_circle, PlayerShooting.cpp outside_playfield,
# LaserFrame.cpp laser_outside_playfield; FastFloat.hpp circle_outside/outside_playfield): tools/cancel_check.cpp
add_executable(th10_cancel_check EXCLUDE_FROM_ALL tools/cancel_check.cpp ${WEB}/game/Arithmetic.cpp)
target_compile_definitions(th10_cancel_check PRIVATE TH10_FAST_CANCEL=1 TH10_FAST_PLAYFIELD=1 TH10_EXTENDED_INLINE=0)
target_compile_options(th10_cancel_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_cancel_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_cancel_check PRIVATE th10_softfloat m)
# TH10_FAST_FEATURES / TH10_FAST_OUTSIDE check (BulletFeatures.cpp, BulletFrame.cpp): tools/feature_check.cpp
add_executable(th10_feature_check EXCLUDE_FROM_ALL tools/feature_check.cpp ${WEB}/game/Arithmetic.cpp)
target_compile_options(th10_feature_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_feature_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_feature_check PRIVATE th10_softfloat m)
# TH10_FAST_RING check (game/AnmFile.cpp update_ring_geometry): tools/ring_check.cpp, with the PSP's DF flags
add_executable(th10_ring_check EXCLUDE_FROM_ALL tools/ring_check.cpp ${WEB}/game/GameMath.cpp ${WEB}/game/Arithmetic.cpp)
target_compile_definitions(th10_ring_check PRIVATE TH10_TRIG_MEMO=1 TH10_DF_LEAN=1)
if(TH10_STATIC_I386)
    target_compile_definitions(th10_ring_check PRIVATE TH10_STATIC_I386_FMOD=1)
endif()
target_compile_options(th10_ring_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_ring_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_ring_check PRIVATE th10_softfloat m)
# TH10_FAST_EASING_INT / _INLINE check (game/Interpolation.cpp Rgb/Alpha sample float path): tools/int_easing_check.cpp
add_executable(th10_int_easing_check EXCLUDE_FROM_ALL tools/int_easing_check.cpp ${WEB}/game/Arithmetic.cpp ${WEB}/game/Interpolation.cpp ${WEB}/game/Timer.cpp)
target_compile_definitions(th10_int_easing_check PRIVATE TH10_FAST_EASING=1 TH10_FAST_EASING_INT=1 TH10_FAST_EASING_INLINE=1 TH10_INT_EASING_COUNT=1)
target_compile_options(th10_int_easing_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_int_easing_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_int_easing_check PRIVATE th10_softfloat m)
# TH10_FAST_LASER check (game/PlayerCollision.cpp collide_laser rotation): tools/laser_check.cpp, with the PSP's DF flags
add_executable(th10_laser_check EXCLUDE_FROM_ALL tools/laser_check.cpp ${WEB}/game/GameMath.cpp ${WEB}/game/Arithmetic.cpp)
target_compile_definitions(th10_laser_check PRIVATE TH10_TRIG_MEMO=1 TH10_DF_LEAN=1)
if(TH10_STATIC_I386)
    target_compile_definitions(th10_laser_check PRIVATE TH10_STATIC_I386_FMOD=1)
endif()
target_compile_options(th10_laser_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_laser_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_laser_check PRIVATE th10_softfloat m)
# TH10_GUARD_CLIP check (psp/GeGuardClip.hpp, world draws in GeRenderer.cpp): tools/guard_clip_check.cpp
add_executable(th10_guard_clip_check EXCLUDE_FROM_ALL tools/guard_clip_check.cpp)
target_compile_options(th10_guard_clip_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_guard_clip_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_guard_clip_check PRIVATE m)
# TH10_FAST_ANIMATE check (game/AnmInterpreter.cpp animate): tools/animate_check.cpp
add_executable(th10_animate_check EXCLUDE_FROM_ALL tools/animate_check.cpp ${WEB}/game/Arithmetic.cpp)
target_compile_options(th10_animate_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_animate_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_animate_check PRIVATE th10_softfloat m)
# TH10_FAST_EASING check (game/Interpolation.cpp easing and sample): tools/easing_check.cpp
add_executable(th10_easing_check EXCLUDE_FROM_ALL tools/easing_check.cpp ${WEB}/game/Arithmetic.cpp ${WEB}/game/Interpolation.cpp ${WEB}/game/Timer.cpp)
target_compile_options(th10_easing_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_easing_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_easing_check PRIVATE th10_softfloat m)
# TH10_FAST_PACK check (game/Argb4444.cpp pack quantizer): tools/pack_check.cpp
add_executable(th10_pack_check EXCLUDE_FROM_ALL tools/pack_check.cpp)
target_compile_options(th10_pack_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_pack_check PRIVATE ${LINK_FLAGS_32})
# TH10_FAST_TEXT check (game/AnmText.cpp draw_marked, TextureResample triangle_content,
# HeadlessFonts marking): tools/text_raster_check.cpp
add_executable(th10_text_raster_check EXCLUDE_FROM_ALL tools/text_raster_check.cpp ${WEB}/game/Arithmetic.cpp ${WEB}/game/AnmText.cpp
    ${WEB}/game/RasterImage.cpp ${WEB}/game/Argb4444.cpp ${WEB}/game/TriangleCoefficients.cpp ${WEB}/platform/TextureResample.cpp ${CMAKE_CURRENT_SOURCE_DIR}/HeadlessFonts.cpp)
set_target_properties(th10_text_raster_check PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(th10_text_raster_check PRIVATE TH10_FAST_TEXT=1 TH_SDL3=1 TH_NATIVE_PLATFORM=1)
target_compile_options(th10_text_raster_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_text_raster_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_text_raster_check PRIVATE th10_softfloat m)
# TH10_FAST_STAGE_CULL / TH10_FAST_MODEL check (game/StageRenderer.cpp culled distance, AnmProjection.cpp draw_model translation): tools/stage_draw_check.cpp
add_executable(th10_stage_draw_check EXCLUDE_FROM_ALL tools/stage_draw_check.cpp ${WEB}/game/Arithmetic.cpp)
target_compile_options(th10_stage_draw_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_stage_draw_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_stage_draw_check PRIVATE th10_softfloat m)
# the same check with TH10_GUARD_NEAR (near plane 2^-12 inside, behind-the-eye vertices clipped too)
add_executable(th10_guard_clip_check_near EXCLUDE_FROM_ALL tools/guard_clip_check.cpp)
target_compile_definitions(th10_guard_clip_check_near PRIVATE TH10_GUARD_NEAR=1)
target_compile_options(th10_guard_clip_check_near PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_guard_clip_check_near PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_guard_clip_check_near PRIVATE m)
# TH10_REC recorder (psp_ge EXTRA_CXXFLAGS -DTH10_REC=1): the portable core
# (psp/RecCore.hpp AVI writer/muxer/idx1 spill/split/repair, psp/RecJpeg.hpp)
# against the XMB-proven B0 layout, then the whole SC side (psp/Recorder.cpp
# and the ME kernel) driven by PC threads:
#   cmake --build <dir> --target th10_rec_check && <dir>/th10_rec_check <out> [frames_565.raw] && python3 tools/rec_check.py <out>
#   cmake --build <dir> --target th10_rec_sim && REC_SIM_ROOT=<out> <dir>/th10_rec_sim && python3 tools/rec_sim_check.py <out>
add_executable(th10_rec_check EXCLUDE_FROM_ALL tools/rec_check.cpp)
target_compile_options(th10_rec_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_rec_check PRIVATE ${LINK_FLAGS_32})
add_executable(th10_rec_sim EXCLUDE_FROM_ALL tools/rec_sim.cpp)
target_include_directories(th10_rec_sim PRIVATE tools/rec_sim)
target_compile_options(th10_rec_sim PRIVATE ${COMMON_FLAGS} -O2 -Wno-deprecated-declarations)
target_link_options(th10_rec_sim PRIVATE ${LINK_FLAGS_32} -pthread)
# TH10_TIMER_INLINE check (game/Timer.hpp tick_inline/advance_inline): tools/timer_inline_check.cpp
add_executable(th10_timer_inline_check EXCLUDE_FROM_ALL tools/timer_inline_check.cpp ${WEB}/game/Timer.cpp ${WEB}/game/Arithmetic.cpp)
target_compile_definitions(th10_timer_inline_check PRIVATE TH10_TIMER_INLINE=1 TH10_FAST_TIMER=1 TH10_EXTENDED_INLINE=0)
target_compile_options(th10_timer_inline_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_timer_inline_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_timer_inline_check PRIVATE th10_softfloat m)
# TH10_FAST_TURN check (game/BulletFeatures.cpp turn_speed): tools/turn_check.cpp
add_executable(th10_turn_check EXCLUDE_FROM_ALL tools/turn_check.cpp ${WEB}/game/Arithmetic.cpp)
target_compile_definitions(th10_turn_check PRIVATE TH10_EXTENDED_INLINE=0)
target_compile_options(th10_turn_check PRIVATE ${COMMON_FLAGS} -O2 -fno-exceptions -fno-rtti)
target_link_options(th10_turn_check PRIVATE ${LINK_FLAGS_32} -Wl,--gc-sections)
target_link_libraries(th10_turn_check PRIVATE th10_softfloat m)
