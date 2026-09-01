# Makefile for the à la carte formuls prototype (macOS).
#
# Build libpd first (it is linked as a dylib, same as the JUCE app):
#   cd ../libs/libpd && make UTIL=true EXTRA=true
#   install_name_tool -id @rpath/libpd.dylib libs/libpd.dylib
#   codesign --force -s - libs/libpd.dylib
# then:
#   make            -> build/formuls-alacarte
#   make run        -> build and launch it
#   make bundle     -> build/formuls-alacarte.app, a double-clickable bundle
#   make clean
#
# "make bundle" needs the runtime resources (the pd patch + externals, and
# gui/node + gui/open-stage-control, which are downloaded by the top-level
# build script rather than kept in the repo). Point RESOURCES_FROM at any
# assembled formuls app's Resources folder to copy them from there:
#   make bundle RESOURCES_FROM=/path/to/formuls-0.3.0-beta.app/Contents/Resources
# Without RESOURCES_FROM only src/pd is bundled (engine runs; no GUI server).
#
# No cmake, no framework, no code generation: clang++, this file, done.

LIBPD_DIR = ../libs/libpd
BUILD_DIR = build
TARGET = $(BUILD_DIR)/formuls-alacarte

CXX = clang++
CXXFLAGS = -std=c++17 -O2 -Wall \
           -D__MACOSX_CORE__ \
           -Isrc -Ivendor -I../shared \
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

BUNDLE = $(BUILD_DIR)/formuls-alacarte.app
RESOURCES_FROM ?=

.PHONY: all run bundle clean

all: $(TARGET)

bundle: $(TARGET)
	rm -rf "$(BUNDLE)"
	mkdir -p "$(BUNDLE)/Contents/MacOS" "$(BUNDLE)/Contents/Frameworks" \
	         "$(BUNDLE)/Contents/Resources"
	cp $(TARGET) "$(BUNDLE)/Contents/MacOS/"
	cp Info.plist "$(BUNDLE)/Contents/"
	cp $(LIBPD_DIR)/libs/libpd.dylib "$(BUNDLE)/Contents/Frameworks/"
	@if [ -n "$(RESOURCES_FROM)" ]; then \
	    cp -R "$(RESOURCES_FROM)/pd" "$(BUNDLE)/Contents/Resources/pd"; \
	    cp -R "$(RESOURCES_FROM)/gui" "$(BUNDLE)/Contents/Resources/gui"; \
	    if [ -f "$(RESOURCES_FROM)/Icon.icns" ]; then \
	        cp "$(RESOURCES_FROM)/Icon.icns" "$(BUNDLE)/Contents/Resources/"; fi; \
	else \
	    cp -R ../pd "$(BUNDLE)/Contents/Resources/pd"; \
	    echo "NOTE: no RESOURCES_FROM given; bundled src/pd only" \
	         "(no faust externals, no gui/node -> no control GUI)"; \
	fi
	codesign --force --deep -s - "$(BUNDLE)"
	@echo "Done: $(BUNDLE)"

$(TARGET): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)
	codesign --force -s - $@

$(BUILD_DIR)/main.o: src/main.mm src/Engine.h src/OpenStageControl.h src/ResourceLocator.h ../shared/PerformanceMeter.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -ObjC++ -fobjc-arc -c $< -o $@

$(BUILD_DIR)/Engine.o: src/Engine.cpp src/Engine.h ../shared/PerformanceMeter.h | $(BUILD_DIR)
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
