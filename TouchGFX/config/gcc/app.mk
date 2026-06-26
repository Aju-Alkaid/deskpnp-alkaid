
# Relative location of the TouchGFX framework from root of application
touchgfx_path := ../Middlewares/ST/touchgfx

# Location of the TouchGFX Environment
touchgfx_env := E:/TouchGFX/4.22.1/env
# Optional additional compiler flags
user_cflags := -DUSE_BPP=1

# Simulator-only OS shim for cmsis_os2 queue API
ifdef SIMULATOR
ADDITIONAL_INCLUDE_PATHS += simulator/include tools/os_shim
ADDITIONAL_SOURCES += tools/os_shim/touchgfx_sim_os_shim.cpp
endif




