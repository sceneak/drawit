TARGET = drawit
CC = gcc
SRC_DIR = src
VENDOR_DIR = vendor
BUILD_DIR = build

BUILD ?= DEBUG
CFLAGS = -std=c11 -Wall -Wextra -I$(VENDOR_DIR) -fno-strict-aliasing
LDFLAGS =

RES_OBJ = 

ifeq ($(BUILD), RELEASE)
	CFLAGS += -O2 -flto -fdata-sections -ffunction-sections
	LDFLAGS += -s -flto -Wl,--gc-sections
else
	CFLAGS += -g3 -O0
endif

TARGET_BIN = $(BUILD_DIR)/$(TARGET)

ifeq ($(OS), Windows_NT)
	LIBS += -lopengl32 -lgdi32 -lwinmm
	TARGET_BIN := $(TARGET_BIN).exe
	RM = rmdir /s /q
	MD = if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))"
	RES_OBJ = ./build/resources.res.o

	PREFIX ?= $(LOCALAPPDATA)/Programs/$(TARGET)
	BINDIR ?= $(PREFIX)
	INSTALL_BIN = $(BINDIR)/$(TARGET).exe
	WIN_BINDIR = $(subst /,\,$(BINDIR))
	WIN_SRC = $(subst /,\,$(TARGET_BIN))
	WIN_DST = $(subst /,\,$(INSTALL_BIN))

	DO_INSTALL = if not exist "$(WIN_BINDIR)" mkdir "$(WIN_BINDIR)" && copy /y "$(WIN_SRC)" "$(WIN_DST)"
	DO_UNINSTALL = if exist "$(WIN_DST)" del /q "$(WIN_DST)"

	ifeq ($(BUILD), RELEASE)
		LDFLAGS += -mwindows
	endif
else
	LIBS += -lGL -lm -lpthread -ldl -lrt -lX11 -lXi -lXcursor
	RM = rm -rf
	CFLAGS += -D_POSIX_C_SOURCE=199309L # for clock_gettime that's posix
	ifeq ($(BUILD), DEBUG)
		CFLAGS += -fsanitize=address,undefined
		LDFLAGS += -fsanitize=address,undefined -ftrack-macro-expansion
	endif
	MD = mkdir -p $(dir $@)

	PREFIX ?= /usr/local
	BINDIR ?= $(PREFIX)/bin
	APPDIR ?= $(PREFIX)/share/applications
	ICONDIR ?= $(PREFIX)/share/icons/hicolor/32x32/apps

	DESKTOP_SRC ?= assets/drawit.desktop
	ICON_SRC ?= assets/app32x32.png

	INSTALL_BIN = $(BINDIR)/$(TARGET)
	DESKTOP_FILE = $(APPDIR)/drawit.desktop
	ICON_FILE = $(ICONDIR)/drawit.png

	DO_INSTALL = mkdir -p "$(BINDIR)" "$(APPDIR)" $(ICONDIR) && \
		install -m 755 "$(TARGET_BIN)" "$(INSTALL_BIN)" && \
		install -m 644 "$(DESKTOP_SRC)" "$(DESKTOP_FILE)" && \
		install -m 644 "$(ICON_SRC)" "$(ICON_FILE)"
	DO_UNINSTALL = rm -f "$(INSTALL_BIN)" "$(DESKTOP_FILE)" "$(ICON_FILE)"
endif

SRCS = $(wildcard $(SRC_DIR)/*.c) \
       $(wildcard $(VENDOR_DIR)/glad/*.c) \
       $(wildcard $(VENDOR_DIR)/nanovg/*.c) \
       $(wildcard $(VENDOR_DIR)/sokol/*.c)
       
OBJS := $(SRCS:%.c=$(BUILD_DIR)/%.o) $(RES_OBJ)
.PHONY: all install uninstall clean

all: $(TARGET_BIN)

$(TARGET_BIN): $(OBJS) 
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LIBS)

$(RES_OBJ): ./resources.rc
	windres ./resources.rc -O coff -o $(RES_OBJ) 

$(BUILD_DIR)/%.o: %.c 
	$(MD)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(TARGET_BIN)
	$(DO_INSTALL)

uninstall:
	$(DO_UNINSTALL)

clean:
	$(RM) $(BUILD_DIR)
