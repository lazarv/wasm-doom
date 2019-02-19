#!/usr/bin/env bash

CC=emcc

if [[ $# -eq 0 ]]; then
  echo "Usage: $0 {libsamplerate|sdl_mixer|opl|chocolate|doom}"
fi

build_libsamplerate() {
    echo "Building libsamplerate.bc..."
    (cd "$(dirname "$0")/libsamplerate" && $CC \
    src_sinc.c \
    src_zoh.c \
    src_linear.c \
    samplerate.c \
    -Oz \
    -Wall \
    -o libsamplerate.bc)
}

build_sdl_mixer() {
    echo "Building sdl_mixer.bc..."
    (cd "$(dirname "$0")/sdl_mixer" && $CC \
    effect_position.c \
    effect_stereoreverse.c \
    effects_internal.c \
    mixer.c \
    music.c \
    -Oz \
    -Wall \
    -s USE_SDL=2 \
    -o sdl_mixer.bc)
}

build_opl() {
    echo "Building opl.bc..."
    (cd "$(dirname "$0")/opl" && $CC \
    opl_queue.c \
    opl_sdl.c \
    opl.c \
    opl3.c \
    -I../src \
    -Oz \
    -Wall \
    -o opl.bc)
}

build_system() {
    echo "Building system.bc..."
    (cd "$(dirname "$0")/src" && $CC \
    ../opl/opl.bc \
    ../sdl_mixer/sdl_mixer.bc \
    i_endoom.c \
    i_gif.c \
    i_input.c \
    i_joystick.c \
    i_main.c \
    i_oplmusic.c \
    i_sdlsound.c \
    i_sound.c \
    i_system.c \
    i_timer.c \
    i_txt.c \
    i_video.c \
    i_webmusic.c \
    sha1.c \
    tables.c \
    memio.c \
    midifile.c \
    mus2mid.c \
    d_event.c \
    d_iwad.c \
    d_loop.c \
    d_mode.c \
    m_argv.c \
    m_bbox.c \
    m_cheat.c \
    m_config.c \
    m_controls.c \
    m_fixed.c \
    m_misc.c \
    z_zone.c \
    v_diskicon.c \
    v_video.c \
    v_trans.c \
    w_checksum.c \
    w_file.c \
    w_file_stdc.c \
    w_main.c \
    w_merge.c \
    w_wad.c \
    deh_io.c \
    deh_main.c \
    deh_mapping.c \
    deh_str.c \
    deh_text.c \
    ../gifenc/gifenc.c \
    -Oz \
    -Wall \
    -I. \
    -I../sdl_mixer \
    -I../libsamplerate \
    -I../opl \
    -I../gifenc \
    -s USE_SDL=2 \
    -s USE_LIBPNG=1 \
    -o system.bc)
}

build_doom() {
    echo "Building doom.{js,wasm}..."
    (cd "$(dirname "$0")/src/doom" && $CC \
    ../system.bc \
    doomdef.c \
    doomstat.c \
    dstrings.c \
    info.c \
    sounds.c \
    statdump.c \
    am_map.c \
    d_items.c \
    d_main.c \
    deh_ammo.c \
    deh_bexincl.c \
    deh_bexpars.c \
    deh_bexptr.c \
    deh_bexstr.c \
    deh_cheat.c \
    deh_doom.c \
    deh_frame.c \
    deh_misc.c \
    deh_ptr.c \
    deh_sound.c \
    deh_thing.c \
    deh_weapon.c \
    f_finale.c \
    f_wipe.c \
    g_game.c \
    hu_lib.c \
    hu_stuff.c \
    m_menu.c \
    m_random.c \
    p_bexptr.c \
    p_blockmap.c \
    p_ceilng.c \
    p_doors.c \
    p_enemy.c \
    p_extnodes.c \
    p_floor.c \
    p_inter.c \
    p_lights.c \
    p_map.c \
    p_maputl.c \
    p_mobj.c \
    p_plats.c \
    p_pspr.c \
    p_saveg.c \
    p_setup.c \
    p_sight.c \
    p_spec.c \
    p_switch.c \
    p_telept.c \
    p_tick.c \
    p_user.c \
    r_bmaps.c \
    r_bsp.c \
    r_data.c \
    r_draw.c \
    r_main.c \
    r_plane.c \
    r_segs.c \
    r_sky.c \
    r_things.c \
    s_sound.c \
    st_lib.c \
    st_stuff.c \
    wi_stuff.c \
    -Oz \
    -Wall \
    -I. \
    -I.. \
    -I../gifenc \
    -s WASM=1 \
    -s USE_SDL=2 \
    -s USE_LIBPNG=1 \
    -s MODULARIZE=1 \
    -s ASSERTIONS=0 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s NO_EXIT_RUNTIME=0 \
    -s EXTRA_EXPORTED_RUNTIME_METHODS=['FS'] \
    --no-heap-copy \
    -o ../../doom.js)
}

for arg in $@; do
case "$arg" in
  -libsamplerate)
    build_libsamplerate
    ;;

  -sdl_mixer)
    build_sdl_mixer
    ;;

  -opl)
    build_opl
    ;;

  -system)
    build_system
    ;;

  -doom)
    build_doom
    ;;

  -all)
    build_libsamplerate
    build_sdl_mixer
    build_opl
    build_system
    build_doom
    ;;

  -system_doom)
    build_system
    build_doom
    ;;

  *)
    echo "Usage: $0 {libsamplerate|sdl_mixer|opl|chocolate|doom}"
    ;;
esac
done
