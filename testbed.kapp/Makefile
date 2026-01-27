# Testbed.Kapp makefile

ASSEMBLY_NAME=testbed.kapp

# All compiler flags/rules used across the board.
CFLAGS =-std=gnu11 -Wall -Wextra -Werror -Wno-error=deprecated-declarations -Wno-error=unused-function 
CFLAGS +=-Wvla -Werror=vla -Wgnu-folding-constant -Wno-missing-braces -Wstrict-prototypes -Wno-unused-parameter 
CFLAGS +=-Wno-missing-field-initializers -Wno-tautological-compare 
# NOTE: -fvisibility=hidden hides all symbols by default, and only those that explicitly say otherwise are exported (i.e. via KAPI).
CFLAGS +=-fvisibility=hidden

# Base linker flags
LDFLAGS = -Lbin -Llib -L../kohi.core/bin -L../kohi.runtime/bin -lkohi.core -lkohi.runtime
LDFLAGS += -L../kohi.plugin.utils/bin -L../kohi.plugin.ui.kui/bin -L../testbed.klib/bin -lkohi.plugin.ui.kui -lkohi.plugin.utils -ltestbed.klib

# Base include flags
INCLUDE_FLAGS = -Isrc -I../kohi.core/src -I../kohi.runtime/src
INCLUDE_FLAGS += -I../testbed.klib/src

DEFINES =-DKEXPORT

OUTPUT_NAME =


UNAME_S := 
ifneq ($(OS),Windows_NT)
	UNAME_S = $(shell uname -s)
endif
ifeq ($(OS),Windows_NT)
	PLATFORM := win32

# 	Make does not offer a recursive wildcard function, and Windows needs one, so here it is:
	rwildcard=$(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))
	DIR := $(subst /,\,${CURDIR})
	SRC_FILES := $(call rwildcard,src,*.c)
	DIRECTORIES := \src $(subst $(DIR),,$(shell dir src /S /AD /B | findstr /i src)) 
	OBJ_FILES := $(SRC_FILES:%=obj/%.o)
else ifeq ($(UNAME_S),Linux)
	PLATFORM := linux
	SRC_FILES := $(shell find src -name "*.c")
	DIRECTORIES := $(shell find src -type d)
	OBJ_FILES := $(SRC_FILES:%=obj/%.o)
else ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos
	SRC_FILES := $(shell find src -type f \( -name "*.c" \))
	DIRECTORIES := $(shell find src -type d)
	OBJ_FILES := $(SRC_FILES:%=obj/%.o)
else
    $(error Unsupported platform)
endif

# =========================================================
# BEGIN TARGET RECIPES
# =========================================================
.PHONY: all-release all-debug flags-debug flags-release 
.PHONY: genversion genversion-win32 genversion-linux genversion-macos 
.PHONY: scaffold scaffold-win32 scaffold-linux scaffold-macos 
.PHONY: link link-win32 link-linux link-macos

all-release: scaffold genversion gen_compile_flags flags-release compile link
all-debug: scaffold genversion gen_compile_flags flags-debug compile link

flags-debug: DEFINES += -D_DEBUG
flags-debug: CFLAGS += -g -MD -O0 -fno-omit-frame-pointer
flags-debug: LDFLAGS += -g
flags-debug: link compile

flags-release: DEFINES += -DKRELEASE
flags-release: CFLAGS += -MD -O2
flags-release: LDFLAGS += 
flags-release: link compile

scaffold: scaffold-$(PLATFORM)

.NOTPARALLEL: scaffold

scaffold-win32:
	@if not exist $(addprefix obj\, $(DIRECTORIES)) mkdir $(addprefix obj\, $(DIRECTORIES))
	@if not exist bin mkdir bin

scaffold-linux scaffold-macos:
	@mkdir -p bin
	@mkdir -p $(addprefix obj/,$(DIRECTORIES))
	

genversion: genversion-$(PLATFORM)

genversion-win32:
	@..\utils\bin\versiongen.exe version.txt -outfile=src\$(ASSEMBLY_NAME)_version.h

genversion-linux genversion-macos:
	@../utils/bin/versiongen version.txt -outfile=src/$(ASSEMBLY_NAME)_version.h


.PHONY: clean
clean:    clean-$(PLATFORM)

clean-win32:
	if exist bin del /s /q bin\*.*
	if exist obj del /s /q obj\*.*

clean-linux clean-macos:
	@rm -rf bin/*
	@rm -rf obj/*

link: link-$(PLATFORM)
 
link-win32: LDFLAGS += -fdeclspec -Wno-cast-function-type-mismatch -Xlinker /INCREMENTAL 
link-win32: DEFINES += -D_CRT_SECURE_NO_WARNINGS -DUNICODE
link-win32: OUTPUT_NAME = bin\$(ASSEMBLY_NAME).exe

link-linux: CFLAGS += -Wno-cast-function-type-mismatch -fPIC
link-linux: LDFLAGS += -Wl,--no-undefined,--no-allow-shlib-undefined,-rpath='$$ORIGIN'
link-linux: OUTPUT_NAME = bin/$(ASSEMBLY_NAME)

link-macos: LDFLAGS += 
link-macos: OUTPUT_NAME = bin/$(ASSEMBLY_NAME)

link-win32 link-linux link-macos: link-common

link-common: $(OBJ_FILES)
	@clang $(OBJ_FILES) -o $(OUTPUT_NAME) $(LDFLAGS)

.PHONY: compile
compile:
-include $(OBJ_FILES:.o=.d)

# compile .c to .o object for windows, linux and mac
obj/%.c.o: %.c 
	@echo   $<...
	@clang $< $(CFLAGS) -c -o $@ $(DEFINES) $(INCLUDE_FLAGS)

-include $(OBJ_FILES:.o=.d)

.PHONY: gen_compile_flags
gen_compile_flags:
ifeq ($(BUILD_PLATFORM),windows)
	@..\utils\bin\cfgen -outfile=compile_flags.txt $(INCLUDE_FLAGS) $(DEFINES) -ferror-limit=0
else
	@../utils/bin/cfgen -outfile=compile_flags.txt $(INCLUDE_FLAGS) $(DEFINES) -ferror-limit=0
endif
