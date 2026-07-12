CC      = g++
STD     = -std=c++20
MAKEFLAGS := --jobs=$(shell nproc)

BASE    = /home/louis/cheat/Axion-NeverloseUI
CSTRIKE = $(BASE)/cstrike
DEPS    = $(BASE)/dependencies
IMGUI   = $(DEPS)/imgui

BUILD ?= debug

CFLAGS_BASE = -shared -fPIC -fvisibility=hidden $(STD) \
	-D__linux__ \
	-DIMGUI_DEFINE_MATH_OPERATORS \
	-I$(BASE) \
	-I$(CSTRIKE) \
	-I$(DEPS) \
	-I$(IMGUI) \
	-I$(IMGUI)/imgui \
	-I$(IMGUI)/backends \
	-I/home/louis/cheat/CounterStrike2-Linux-InternalOLD/libs/funchook/include \
	-I/usr/include/freetype2 \
	-I/usr/include \
	-Wno-error -Wall -Wextra \
	-Wno-unused-parameter -Wno-missing-field-initializers \
	-Wno-ignored-qualifiers -Wno-attributes -Wno-cpp \
	-Wno-sign-compare -Wno-unused-variable -Wno-unused-function \
	-Wno-unused-but-set-variable -Wno-parentheses -Wno-nonnull-compare \
	-Wno-reorder -Wno-deprecated-enum-float-conversion \
	-Wno-unknown-pragmas

ifeq ($(BUILD),debug)
CFLAGS = $(CFLAGS_BASE) -g -D_DEBUG
else ifeq ($(BUILD),release)
CFLAGS = $(CFLAGS_BASE) -O2 -DNDEBUG
else
$(error Unknown build type '$(BUILD)'. Use BUILD=debug or BUILD=release.)
endif

LDFLAGS = -Wl,-z,defs -lSDL3 -lvulkan -ldl -lpthread -lfreetype \
	/home/louis/cheat/CounterStrike2-Linux-InternalOLD/libs/funchook/build/libfunchook.a \
	/home/louis/cheat/CounterStrike2-Linux-InternalOLD/libs/funchook/build/libdistorm.a

OBJ_FILES  = linux/entry.cpp.o
OBJ_FILES += linux/vulkan_hook.cpp.o
OBJ_FILES += linux/stb_sprintf.cpp.o
OBJ_FILES += cstrike/core/memory/cmodule.cpp.o
OBJ_FILES += cstrike/core/memory/memadd.cpp.o
OBJ_FILES += cstrike/core/csig/sigscan.cpp.o
OBJ_FILES += cstrike/core/pointer/pointer.cpp.o
OBJ_FILES += cstrike/core/sdk.cpp.o
OBJ_FILES += cstrike/core/spoofcall/callstack.cpp.o
OBJ_FILES += cstrike/core/schema.cpp.o
OBJ_FILES += cstrike/core/schemav2.cpp.o
OBJ_FILES += cstrike/core/interfaces.cpp.o
OBJ_FILES += cstrike/core/variables.cpp.o
OBJ_FILES += cstrike/core/convars.cpp.o
OBJ_FILES += cstrike/core/config.cpp.o
OBJ_FILES += cstrike/core/gui.cpp.o
OBJ_FILES += cstrike/core/menu.cpp.o
OBJ_FILES += cstrike/core/hooks.cpp.o
OBJ_FILES += cstrike/core/silentvmt/ShadowVMT.cpp.o
OBJ_FILES += cstrike/core/silentvmt/InlineHook.cpp.o
OBJ_FILES += cstrike/features/misc.cpp.o
OBJ_FILES += cstrike/features/visuals.cpp.o
OBJ_FILES += cstrike/features/visuals/overlay.cpp.o
OBJ_FILES += cstrike/features/visuals/chams.cpp.o
OBJ_FILES += cstrike/features/antiaim/antiaim.cpp.o
OBJ_FILES += cstrike/features/rage/rage.cpp.o
OBJ_FILES += cstrike/features/enginepred/pred.cpp.o
OBJ_FILES += cstrike/features/legit/legit.cpp.o
OBJ_FILES += cstrike/features/penetration/penetration.cpp.o
OBJ_FILES += cstrike/features/skins/skin_changer.cpp.o
OBJ_FILES += cstrike/features/skins/ccsinventorymanager.cpp.o
OBJ_FILES += cstrike/features/skins/ccsplayerinventory.cpp.o
OBJ_FILES += cstrike/features/lagcomp/lagcomp.cpp.o
OBJ_FILES += cstrike/features/misc/movement.cpp.o
OBJ_FILES += cstrike/utilities/inputsystem.cpp.o
OBJ_FILES += cstrike/utilities/memory.cpp.o
OBJ_FILES += cstrike/utilities/math.cpp.o
OBJ_FILES += cstrike/utilities/notify.cpp.o
OBJ_FILES += cstrike/utilities/log.cpp.o
OBJ_FILES += cstrike/utilities/draw.cpp.o
OBJ_FILES += cstrike/sdk/datatypes/K3V.cpp.o
OBJ_FILES += cstrike/sdk/datatypes/buf/strtools.cpp.o
OBJ_FILES += cstrike/sdk/datatypes/buf/utlbuffer.cpp.o
OBJ_FILES += cstrike/sdk/datatypes/matrix.cpp.o
OBJ_FILES += cstrike/sdk/datatypes/qangle.cpp.o
OBJ_FILES += cstrike/sdk/datatypes/vector.cpp.o
OBJ_FILES += cstrike/sdk/entity.cpp.o
OBJ_FILES += cstrike/sdk/entity_handle.cpp.o
OBJ_FILES += cstrike/sdk/interfaces/ccsgoinput.cpp.o
OBJ_FILES += cstrike/sdk/interfaces/events.cpp.o
OBJ_FILES += cstrike/sdk/interfaces/iengineclient.cpp.o
OBJ_FILES += cstrike/sdk/interfaces/itrace.cpp.o
OBJ_FILES += cstrike/features.cpp.o
OBJ_FILES += cstrike/core.cpp.o
# ImGui
OBJ_FILES += $(IMGUI)/imgui.cpp.o
OBJ_FILES += $(IMGUI)/imgui_draw.cpp.o
OBJ_FILES += $(IMGUI)/imgui_tables.cpp.o
OBJ_FILES += $(IMGUI)/imgui_widgets.cpp.o
OBJ_FILES += $(IMGUI)/imgui_demo.cpp.o
OBJ_FILES += $(IMGUI)/imgui_edited.cpp.o
OBJ_FILES += $(IMGUI)/imgui_freetype.cpp.o
OBJ_FILES += $(IMGUI)/examples/example_win32_directx9/gui.cpp.o
OBJ_FILES += $(IMGUI)/examples/example_win32_directx9/blur.cpp.o
OBJ_FILES += $(IMGUI)/backends/imgui_impl_sdl3.cpp.o
OBJ_FILES += $(IMGUI)/backends/imgui_impl_vulkan.cpp.o

OBJS = $(addprefix obj/, $(OBJ_FILES))
BIN  = cs2_axion.so
LOADER = axion_loader
GTK_CFLAGS = $(shell pkg-config --cflags gtk4)
GTK_LIBS = $(shell pkg-config --libs gtk4)

.PHONY: all debug release loader clean

all: debug loader

debug: BUILD = debug
debug: $(BIN)

release: BUILD = release
release: $(BIN)

loader: $(LOADER)

clean:
	rm -rf obj $(BIN) $(LOADER)

$(LOADER): loader/axion_loader.cpp
	$(CC) $(STD) -O2 -Wall -Wextra $(GTK_CFLAGS) -o $@ $< $(GTK_LIBS)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

obj/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

obj/%.cpp.o: /%.cpp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
