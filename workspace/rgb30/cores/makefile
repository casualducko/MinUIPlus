CORES = fceumm gambatte gpsp pcsx_rearmed picodrive snes9x2005_plus 
CORES+= pokemini fake-08 mednafen_pce_fast mednafen_vb mednafen_supafaust mgba race

###############################

# optional core vars
# *_REPO=
# *_HASH=
# *_CORE=
# *_FLAGS=
# *_MAKEFILE=
# *_BUILD_PATH=

mednafen_pce_fast_REPO = https://github.com/libretro/beetle-pce-fast-libretro
mednafen_vb_REPO = https://github.com/libretro/beetle-vb-libretro

fake-08_REPO = https://github.com/jtothebell/fake-08
fake-08_CORE = fake08_libretro.so
fake-08_BUILD_PATH = fake-08/platform/libretro

fceumm_REPO = https://github.com/libretro/libretro-fceumm

gambatte_REPO = https://github.com/libretro/gambatte-libretro

mednafen_supafaust_REPO = https://github.com/libretro/supafaust

pcsx_rearmed_MAKEFILE = Makefile.libretro

picodrive_REPO = https://github.com/irixxxx/picodrive
picodrive_MAKEFILE = Makefile.libretro

pokemini_REPO = https://github.com/libretro/PokeMini
pokemini_MAKEFILE = Makefile.libretro

snes9x2005_plus_REPO = https://github.com/libretro/snes9x2005
snes9x2005_plus_FLAGS = USE_BLARGG_APU=1

###############################

# pinned upstream commits
# each is the commit the matching core in MinUI-20251127-1 was built from
# (see that release's .system/commits.txt, ten of them are also embedded in the .so)
# the patches in ./patches apply to these, not to upstream HEAD

fceumm_HASH = 449db5d
gambatte_HASH = 9d92381
gpsp_HASH = 36061ca
pcsx_rearmed_HASH = 92bb759
picodrive_HASH = b0be121
snes9x2005_plus_HASH = 74d871d
mednafen_pce_fast_HASH = 931586f
mednafen_vb_HASH = 8f837eb
mednafen_supafaust_HASH = e25f667
mgba_HASH = 517e518
race_HASH = 171950e
pokemini_HASH = 78656d4
fake-08_HASH = 0d26fd5

###############################

include ../../all/cores/makefile