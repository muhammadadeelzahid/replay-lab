# Strict warnings for our own targets. Qt and GStreamer come in as imported targets, so their
# headers are treated as system headers and do not trigger these.
function(abrviz_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(ABR_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        return()
    endif()

    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wimplicit-fallthrough)
    # GCC warns about aggregate initialisers that leave out members even when those members
    # have default initialisers, which is how the model structs are meant to be filled.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE -Wno-missing-field-initializers)
    endif()
    if(ABR_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()
