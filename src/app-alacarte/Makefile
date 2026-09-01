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
# make does not expand "~", and a literal tilde silently becomes a path that
# does not exist, so expand it here rather than shipping an empty bundle.
RESOURCES_ROOT = $(subst ~,$(HOME),$(RESOURCES_FROM))

.PHONY: all run bundle verify-bundle clean

all: $(TARGET)

bundle: $(TARGET)
	@set -e; \
	if [ -n "$(RESOURCES_ROOT)" ] && [ ! -d "$(RESOURCES_ROOT)" ]; then \
	    echo "ERROR: RESOURCES_FROM does not exist:"; \
	    echo "       $(RESOURCES_ROOT)"; \
	    echo "       Point it at an assembled formuls app's Resources folder."; \
	    exit 1; \
	fi; \
	rm -rf "$(BUNDLE)"; \
	mkdir -p "$(BUNDLE)/Contents/MacOS" "$(BUNDLE)/Contents/Frameworks" \
	         "$(BUNDLE)/Contents/Resources"; \
	cp $(TARGET) "$(BUNDLE)/Contents/MacOS/"; \
	cp Info.plist "$(BUNDLE)/Contents/"; \
	cp $(LIBPD_DIR)/libs/libpd.dylib "$(BUNDLE)/Contents/Frameworks/"; \
	if [ -n "$(RESOURCES_ROOT)" ]; then \
	    cp -R "$(RESOURCES_ROOT)/pd" "$(BUNDLE)/Contents/Resources/pd"; \
	    cp -R "$(RESOURCES_ROOT)/gui" "$(BUNDLE)/Contents/Resources/gui"; \
	    if [ -f "$(RESOURCES_ROOT)/Icon.icns" ]; then \
	        cp "$(RESOURCES_ROOT)/Icon.icns" "$(BUNDLE)/Contents/Resources/"; \
	    fi; \
	else \
	    cp -R ../pd "$(BUNDLE)/Contents/Resources/pd"; \
	    echo "NOTE: no RESOURCES_FROM given; bundled src/pd only"; \
	    echo "      (no faust externals, no gui/node -> no control GUI)"; \
	fi; \
	codesign --force --deep -s - "$(BUNDLE)"; \
	$(MAKE) --no-print-directory verify-bundle
	@echo "Done: $(BUNDLE)"

# Checks the assembled bundle really contains what it needs to run, so a
# failed copy can never masquerade as a working app again.
verify-bundle:
	@set -e; \
	missing=""; \
	for f in Contents/MacOS/formuls-alacarte Contents/Info.plist \
	         Contents/Frameworks/libpd.dylib Contents/Resources/pd/_main.pd; do \
	    [ -e "$(BUNDLE)/$$f" ] || missing="$$missing $$f"; \
	done; \
	if [ -n "$(RESOURCES_ROOT)" ]; then \
	    for f in Contents/Resources/gui/node Contents/Resources/gui/_main.json \
	             Contents/Resources/gui/open-stage-control; do \
	        [ -e "$(BUNDLE)/$$f" ] || missing="$$missing $$f"; \
	    done; \
	fi; \
	if [ -n "$$missing" ]; then \
	    echo "ERROR: bundle is incomplete, missing:"; \
	    for f in $$missing; do echo "       $$f"; done; \
	    exit 1; \
	fi; \
	echo "bundle verified: $$(ls $(BUNDLE)/Contents/Resources/pd/externals 2>/dev/null | wc -l | tr -d ' ') externals, gui $$([ -e "$(BUNDLE)/Contents/Resources/gui/node" ] && echo present || echo absent)"

$(TARGET): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)
	codesign --force -s - $@

$(BUILD_DIR)/main.o: src/main.mm src/Engine.h src/OpenStageControl.h src/ResourceLocator.h ../shared/PerformanceMeter.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -ObjC++ -fobjc-arc -c $< -o $@

$(BUILD_DIR)/Engine.o: src/Engine.cpp src/Engine.h ../shared/PerformanceMeter.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/OpenStageControl.o: src/OpenStageControl.cpp src/OpenStageControl.h ../shared/ChildProcess.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/ChildProcess.o: ../shared/ChildProcess.cpp ../shared/ChildProcess.h | $(BUILD_DIR)
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
