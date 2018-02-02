-- define target
target("openal-test")

    -- set kind
    set_kind("binary")

    -- add linker flags
    add_ldflags("-lcurl -lmpg123 -lopenal");

    -- set output directory
    set_targetdir('./')

    -- add files
    add_files("openal-test.c")

