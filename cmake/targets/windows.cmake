# windows specific target definitions

# No visible console window; main.cpp calls AttachConsole(ATTACH_PARENT_PROCESS) so
# CLI output (--help, `sunshine creds`, etc.) still prints when launched from a
# terminal, same pattern Hunter's Moonlight.exe already uses.
set_target_properties(sunshine PROPERTIES WIN32_EXECUTABLE TRUE)

set_target_properties(sunshine PROPERTIES LINK_SEARCH_START_STATIC 1)
set(CMAKE_FIND_LIBRARY_SUFFIXES ".dll")
find_library(ZLIB ZLIB1)
list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
        $<TARGET_OBJECTS:sunshine_rc_object>
        Windowsapp.lib
        Wtsapi32.lib
        version.lib)
