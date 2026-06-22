CMAKE ?= cmake
CTEST ?= ctest
BUILD_DIR ?= build
CMAKE_BUILD_TYPE ?= Release

.PHONY: all configure build test clean

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

build: configure
	$(CMAKE) --build $(BUILD_DIR)

test: configure
	$(CMAKE) --build $(BUILD_DIR)
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

clean:
	rm -rf $(BUILD_DIR)
