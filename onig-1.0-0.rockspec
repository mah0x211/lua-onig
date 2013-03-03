package = "onig"
version = "1.0-0"
source = {
    url = "https://github.com/mah0x211/lua-oniguruma.git"
}
description = {
    summary = "bindings for oniguruma",
    detailed = [[]],
    homepage = "https://github.com/mah0x211/lua-onig", 
    license = "MIT/X11",
    maintainer = "Masatoshi Teruya"
}
dependencies = {
    "lua >= 5.1"
}
external_dependencies = {
    ONIG = {
        header = "oniguruma.h",
        library = "onig"
    },
    BUF = {
        header = "libbuf.h",
        library = "buf"
    }
}
build = {
    type = "builtin",
    modules = {
        onig = {
            sources = { "regexp.c" },
            libraries = { "onig", "buf" },
            incdirs = { 
                "$(ONIG_INCDIR)", 
                "$(BUF_INCDIR)"
            },
            libdirs = { 
                "$(ONIG_LIBDIR)",
                "$(BUF_LIBDIR)"
            }
        }
    }
}

