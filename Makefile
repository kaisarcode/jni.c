## Makefile
## Summary: Cross-compilation builder for kcjni (Android-only JNI bridge).
##
## Author:  KaisarCode
## Website: https://kaisarcode.com
## License: https://www.gnu.org/licenses/gpl-3.0.html

ANDROID_HOME  ?= $(HOME)/.local/share/android-sdk
NDK_VERSION   ?= 27.2.12479018
NDK_DIR       := $(ANDROID_HOME)/ndk/$(NDK_VERSION)
NDK_TOOLCHAIN := $(NDK_DIR)/build/cmake/android.toolchain.cmake

BUILD_DIR := .build
BIN_DIR   := bin

define cmake_build
	@prelog=$$(mktemp); \
	if ! $(3) cmake --build $(1) -- -n > "$$prelog" 2>&1; then \
		cat "$$prelog"; \
		rm -f "$$prelog"; \
		exit 1; \
	fi; \
	if grep -q "ninja: no work to do." "$$prelog"; then \
		rm -f "$$prelog"; \
		out=$$(mktemp); \
		$(3) cmake --build $(1) 2>"$$out"; \
		r=$$?; \
		if [ -s "$$out" ]; then grep -v 'skipping incompatible' < "$$out"; fi; \
		rm -f "$$out"; \
		exit $$r; \
	fi; \
	rm -f "$$prelog"; \
	out=$$(mktemp); \
	$(3) cmake --build $(1) 2>"$$out"; \
	r=$$?; \
	if [ -s "$$out" ]; then grep -v 'skipping incompatible' < "$$out"; fi; \
	rm -f "$$out"; \
	if [ $$r -ne 0 ]; then \
		exit 1; \
	fi; \
	if [ -n "$(2)" ]; then \
		ver=$$(date +%s); \
		$(2); \
		log=$$(mktemp); \
		if ! $(3) cmake --build $(1) > "$$log" 2>&1; then \
			cat "$$log"; \
			rm -f "$$log"; \
			exit 1; \
		fi; \
		rm -f "$$log"; \
	fi; \
	:
endef

.DEFAULT_GOAL := android

.PHONY: all android aarch64/android armv7/android clean

all: android

## Android

define android_target
	@mkdir -p $(BIN_DIR)/$(1)/android
	@cache=$(BUILD_DIR)/$(1)-android/CMakeCache.txt && \
	if [ -f "$$cache" ] && { ! grep -q '^CMAKE_TOOLCHAIN_FILE:.*=$(NDK_TOOLCHAIN)$$' "$$cache" || ! grep -q '^ANDROID_ABI:.*=$(2)$$' "$$cache"; }; then \
		rm -f "$$cache" $(BUILD_DIR)/$(1)-android/build.ninja && rm -rf $(BUILD_DIR)/$(1)-android/CMakeFiles; \
	fi
	@if [ ! -f $(BUILD_DIR)/$(1)-android/CMakeCache.txt ]; then \
		cmake -S . -B $(BUILD_DIR)/$(1)-android \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_TOOLCHAIN_FILE=$(NDK_TOOLCHAIN) \
			-DANDROID_ABI=$(2) \
			-DANDROID_PLATFORM=android-21 \
			-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/android \
			-G Ninja -Wno-dev > /dev/null; \
	fi
	$(call cmake_build,$(BUILD_DIR)/$(1)-android,cmake -S . -B $(BUILD_DIR)/$(1)-android -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$(NDK_TOOLCHAIN) -DANDROID_ABI=$(2) -DANDROID_PLATFORM=android-21 -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(CURDIR)/$(BIN_DIR)/$(1)/android -G Ninja -Wno-dev > /dev/null)
	@echo "OK $(1)/android"
endef

android: aarch64/android armv7/android

aarch64/android:
	$(call android_target,aarch64,arm64-v8a)

armv7/android:
	$(call android_target,armv7,armeabi-v7a)

## Utility

clean:
	@rm -rf $(BUILD_DIR)
	@echo "OK clean"
