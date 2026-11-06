# kk4s (Switch): devkitPro's SDL2_mixer decodes Ogg with Tremor (libvorbisidec),
# whose ov_* / vorbis_* functions have the same names as libvorbis' but other
# arguments (ov_read). kk4s and FFmpeg use libvorbis, so both cannot be linked
# as they are. This makes copies of libvorbisidec.a and libSDL2_mixer.a in the
# build folder with every global Tremor symbol renamed to kk4s_tremor_<name>;
# SDL_mixer then calls its own Tremor and everything else keeps libvorbis.
#
#   kk4s_tremor_rename(<portlibs lib dir> <out dir> <out var: list of the 2 copies>)
function(kk4s_tremor_rename libdir outdir outvar)
    set(tremor "${libdir}/libvorbisidec.a")
    set(mixer "${libdir}/libSDL2_mixer.a")
    if(NOT EXISTS "${tremor}" OR NOT EXISTS "${mixer}")
        message(FATAL_ERROR "kk4s: ${tremor} or ${mixer} missing")
    endif()
    file(MAKE_DIRECTORY "${outdir}")
    execute_process(COMMAND "${CMAKE_NM}" -g --defined-only "${tremor}"
        OUTPUT_VARIABLE nm_out RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "kk4s: ${CMAKE_NM} failed on ${tremor}")
    endif()
    string(REPLACE "\n" ";" lines "${nm_out}")
    set(map "")
    set(count 0)
    foreach(line IN LISTS lines)
        if(line MATCHES "^[0-9a-fA-F]+ [A-Z] ([A-Za-z_][A-Za-z0-9_]*)$")
            string(APPEND map "${CMAKE_MATCH_1} kk4s_tremor_${CMAKE_MATCH_1}\n")
            math(EXPR count "${count} + 1")
        endif()
    endforeach()
    if(count EQUAL 0)
        message(FATAL_ERROR "kk4s: no symbols found in ${tremor}")
    endif()
    file(WRITE "${outdir}/tremor.syms" "${map}")
    set(out_tremor "${outdir}/libkk4s_vorbisidec.a")
    set(out_mixer "${outdir}/libkk4s_SDL2_mixer.a")
    foreach(pair "${tremor}|${out_tremor}" "${mixer}|${out_mixer}")
        string(REPLACE "|" ";" p "${pair}")
        list(GET p 0 in)
        list(GET p 1 out)
        execute_process(COMMAND "${CMAKE_OBJCOPY}" "--redefine-syms=${outdir}/tremor.syms" "${in}" "${out}"
            RESULT_VARIABLE rc)
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "kk4s: objcopy failed on ${in}")
        endif()
    endforeach()
    message(STATUS "kk4s: SDL2_mixer with ${count} Tremor symbols renamed (${outdir})")
    set(${outvar} "${out_mixer};${out_tremor}" PARENT_SCOPE)
endfunction()
