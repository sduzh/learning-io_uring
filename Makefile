PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

.PHONY: all clean format debug release

all: release

#### VCPKG config
VCPKG_TOOLCHAIN_PATH?=
ifneq ("${VCPKG_TOOLCHAIN_PATH}", "")
	TOOLCHAIN_FLAGS:=${TOOLCHAIN_FLAGS} -DVCPKG_MANIFEST_DIR='${PROJ_DIR}' -DCMAKE_TOOLCHAIN_FILE='${VCPKG_TOOLCHAIN_PATH}'
endif
ifneq ("${VCPKG_TARGET_TRIPLET}", "")
	TOOLCHAIN_FLAGS:=${TOOLCHAIN_FLAGS} -DVCPKG_TARGET_TRIPLET='${VCPKG_TARGET_TRIPLET}'
endif

#### Enable Ninja as generator
ifeq ($(GEN),ninja)
	TOOLCHAIN_FLAGS:=${TOOLCHAIN_FLAGS} -G "Ninja"
endif

debug:
	mkdir -p  build/debug
	cmake $(TOOLCHAIN_FLAGS) -DCMAKE_BUILD_TYPE=Debug -S . -B build/debug
	cmake --build build/debug --config Debug

release:
	mkdir -p build/release
	cmake $(TOOLCHAIN_FLAGS) -DCMAKE_BUILD_TYPE=Release -S . -B build/release
	cmake --build build/release --config Release

clean:
	rm -rf build

format:
	find src/ -iname *.h -o -iname *.cpp | xargs clang-format --sort-includes=0 -style=file -i
	cmake-format -i CMakeLists.txt
