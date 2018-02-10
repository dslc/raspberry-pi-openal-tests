-- define target
target("openal-test")

on_load(
    function(t)
        import("lib.detect.find_package")
        t:add(find_package("curl"))
        t:add(find_package("mpg123"))
        t:add(find_package("openal"))
    end
)

-- set kind
set_kind("binary")

-- add linker flags
add_ldflags("-lcurl -lmpg123 -lopenal");

-- set output directory
set_targetdir('./')

-- add files
add_files("*.cpp")

