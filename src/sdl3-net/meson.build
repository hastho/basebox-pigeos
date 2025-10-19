libsdl3net_sources = files(
    'SDL_net.c',
)

libsdl3net = static_library(
    'sdl3net',
    libsdl3net_sources,
    include_directories: incdir,
    dependencies: [
        sdl3_dep,
    ],
)

libsdl3net_dep = declare_dependency(link_with: libsdl3net)

internal_deps += libsdl3net_dep