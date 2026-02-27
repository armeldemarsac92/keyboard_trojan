CC := x86_64-w64-mingw32-gcc
PYTHON ?= python3

CFLAGS ?= -O2 -Wall -Wextra -I. -Iinclude -Ivendor/hidapi
LDFLAGS ?=
LDLIBS ?= -lsetupapi

BUILD_DIR := output/build
ARTIFACT_DIR := output/artifacts
TARGET := $(BUILD_DIR)/time_tracker.exe

APP_SOURCES := \
	time_tracker.c \
	src/time_tracker/order_queue.c \
	src/time_tracker/worker.c \
	src/time_tracker/runtime.c \
	src/time_tracker/hid_channel.c

HID_SOURCES := \
	vendor/hidapi/hid.c

SOURCES := $(APP_SOURCES) $(HID_SOURCES)
OBJECTS := $(patsubst %.c,$(BUILD_DIR)/obj/%.o,$(SOURCES))

.PHONY: all package clean

all: $(TARGET)

$(BUILD_DIR) $(ARTIFACT_DIR):
	mkdir -p $@

$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(BUILD_DIR)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(ARTIFACT_DIR)/time_tracker.exe: $(TARGET) | $(ARTIFACT_DIR)
	cp $(TARGET) $@

$(ARTIFACT_DIR)/nice_text.txt: $(TARGET) tagger.py | $(ARTIFACT_DIR)
	$(PYTHON) tagger.py $(TARGET) $@

$(ARTIFACT_DIR)/windows_logo.png: windows.png $(ARTIFACT_DIR)/nice_text.txt | $(ARTIFACT_DIR)
	(cat windows.png; echo ""; cat $(ARTIFACT_DIR)/nice_text.txt) > $@

package: $(ARTIFACT_DIR)/time_tracker.exe $(ARTIFACT_DIR)/nice_text.txt $(ARTIFACT_DIR)/windows_logo.png

clean:
	rm -rf output
