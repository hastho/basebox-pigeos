libgeos_sources = files(
    'geoshost.cpp',
)

libgeos = static_library(
    'geos',
    libgeos_sources,
    include_directories: incdir,
    dependencies: [
        sdl2_dep,
        sdl2_net_dep,
	sdl3_dep,
    ],
)

libgeos_dep = declare_dependency(link_with: libgeos)

internal_deps += libgeos_dep