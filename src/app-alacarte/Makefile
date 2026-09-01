# Makefile for the à la carte formuls prototype (macOS).
#
# Build libpd first (it is linked as a dylib, same as the JUCE app):
#   cd ../libs/libpd && make UTIL=true EXTRA=true
#   install_name_tool -id @rpath/libpd.dylib libs/libpd.dylib
#   codesign --force -s - libs/libpd.dylib
# then:
#   make            -> build/formuls-alacarte
#   make run        -> build and launch it
#   make clean
#
# No cmake, no framework, no code generation: clang++, this file, done.

LIBPD_DIR = ../libs/libpd
BUILD_DIR = build
TARGET = $(BUILD_DIR)/formuls-alacarte

CXX = clang++
CXXFLAGS = -std=c++17 -O2 -Wall \
           -D__MACOSX_CORE__ \
           -Isrc -Ivendor \
           -I$(LIBPD_DIR)/cpp \
           -I$(LIBPD_DIR)/libpd_wrapper \
           -I$(LIBPD_DIR)/libpd_wrapper/util \
           -I$(LIBPD_DIR)/pure-data/src

# RtAudio 5.1 is not warning-clean under current clang; keep our code at
# -Wall but compile the vendored file quietly.
VENDOR_CXXFLAGS = $(CXXFLAGS) -w

# rpaths: libpd.dylib next to the binary in a libs/ folder (a packaged
# layout), in Contents/Frameworks (an .app bundle), or straight from the
# libpd source tree (development, this Makefile's default).
LDFLAGS = $(LIBPD_DIR)/libs/libpd.dylib \
          -framework Cocoa -framework CoreAudio -framework CoreFoundation \
          -lpthread \
          -Wl,-rpath,@executable_path/libs \
          -Wl,-rpath,@executable_path/../Frameworks \
          -Wl,-rpath,@executable_path/../../libs/libpd/libs

OBJS = $(BUILD_DIR)/main.o \
       $(BUILD_DIR)/Engine.o \
       $(BUILD_DIR)/OpenStageControl.o \
       $(BUILD_DIR)/ChildProcess.o \
       $(BUILD_DIR)/ResourceLocator.o \
       $(BUILD_DIR)/RtAudio.o

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)
	codesign --force -s - $@

$(BUILD_DIR)/main.o: src/main.mm src/Engine.h src/OpenStageControl.h src/ResourceLocator.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -ObjC++ -fobjc-arc -c $< -o $@

$(BUILD_DIR)/Engine.o: src/Engine.cpp src/Engine.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/OpenStageControl.o: src/OpenStageControl.cpp src/OpenStageControl.h src/ChildProcess.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/ChildProcess.o: src/ChildProcess.cpp src/ChildProcess.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/ResourceLocator.o: src/ResourceLocator.cpp src/ResourceLocator.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/RtAudio.o: vendor/RtAudio.cpp vendor/RtAudio.h | $(BUILD_DIR)
	$(CXX) $(VENDOR_CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
