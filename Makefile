# 1.44MB Roguelike - cross-platform build
# Mac (dev) and Windows (submission) targets, optimized for binary size.

BIN_NAME    := disk
SRC_GAME    := $(wildcard src/*.c)
SRC_TIGR    := third_party/tigr.c
SRC         := $(SRC_GAME) $(SRC_TIGR)

INCLUDE     := -Ithird_party -Isrc
BUDGET      := 1474560

# ---- size-focused compiler flags -------------------------------------------
COMMON_CFLAGS := -std=c99 -Os -flto \
                 -ffunction-sections -fdata-sections \
                 -fno-unwind-tables -fno-asynchronous-unwind-tables \
                 -fno-stack-protector \
                 -fomit-frame-pointer \
                 -Wall -Wno-unused-function -Wno-unused-variable \
                 -DNDEBUG $(INCLUDE)

# ---- Mac (native, development) ---------------------------------------------
MAC_CC      := clang
MAC_CFLAGS  := $(COMMON_CFLAGS) -Wno-deprecated-declarations
MAC_LDFLAGS := -framework Cocoa -framework OpenGL -framework Foundation \
               -Wl,-dead_strip -flto
MAC_OUT     := build/$(BIN_NAME)

# ---- Windows (mingw-w64 cross, submission) ---------------------------------
WIN_CC      := x86_64-w64-mingw32-gcc
WIN_CFLAGS  := $(COMMON_CFLAGS) -DWINVER=0x0501
WIN_LDFLAGS := -static -mwindows -s \
               -Wl,--gc-sections -Wl,--strip-all \
               -lopengl32 -lgdi32 -lshell32 -luser32 -ladvapi32 -lmsvcrt
WIN_OUT     := build/$(BIN_NAME).exe

# ============================================================================
.PHONY: all mac win clean size submit run check-size screenshots assets

all: mac

# Regenerate src/assets_gen.c from the (gitignored) DCSS tileset folder.
# Requires python3 + ImageMagick (`magick`). Normal builds just compile the
# already-committed src/assets_gen.c, so this is a dev-only step.
assets:
	python3 tools/gen_assets.py

run: mac
	./$(MAC_OUT)

mac: $(MAC_OUT)
	@printf "MAC  %s : " "$(MAC_OUT)"; wc -c < $(MAC_OUT) | awk '{printf "%d bytes (%.1f KB)\n", $$1, $$1/1024}'

$(MAC_OUT): $(SRC) | build
	$(MAC_CC) $(MAC_CFLAGS) $(SRC) $(MAC_LDFLAGS) -o $@
	@strip -x $@ 2>/dev/null || true

win: $(WIN_OUT)
	@printf "WIN  %s : " "$(WIN_OUT)"; wc -c < $(WIN_OUT) | awk '{printf "%d bytes (%.1f KB)\n", $$1, $$1/1024}'

$(WIN_OUT): $(SRC) | build
	$(WIN_CC) $(WIN_CFLAGS) $(SRC) $(WIN_LDFLAGS) -o $@

# UPX-compressed Windows build (final submission artifact)
submit: $(WIN_OUT)
	@cp $(WIN_OUT) build/$(BIN_NAME).upx.exe
	@upx --best --lzma --no-progress build/$(BIN_NAME).upx.exe >/dev/null || true
	@mv build/$(BIN_NAME).upx.exe $(WIN_OUT)
	@$(MAKE) --no-print-directory check-size

check-size:
	@size=$$(wc -c < $(WIN_OUT)); \
	pct=$$(awk "BEGIN { printf \"%.1f\", $$size * 100 / $(BUDGET) }"); \
	printf "SUBMIT  %s : %d / %d bytes  (%.1f%% of floppy)\n" $(WIN_OUT) $$size $(BUDGET) $$pct; \
	if [ $$size -gt $(BUDGET) ]; then \
	  printf "  >>> OVER BUDGET by %d bytes <<<\n" $$(($$size - $(BUDGET))); \
	  exit 1; \
	fi

size: mac
	@printf "\nSize report:\n"; \
	for f in $(MAC_OUT) $(WIN_OUT); do \
	  if [ -f $$f ]; then \
	    s=$$(wc -c < $$f); \
	    p=$$(awk "BEGIN { printf \"%.1f\", $$s * 100 / $(BUDGET) }"); \
	    printf "  %-24s %8d bytes  %5s%% of 1.44MB\n" $$f $$s $$p; \
	  fi; \
	done

build:
	@mkdir -p build

clean:
	rm -rf build

# Build a separate Mac binary that can capture screenshots.
screenshots: | build
	@mkdir -p screenshots
	$(MAC_CC) $(MAC_CFLAGS) -DDISK_SCREENSHOT $(SRC) $(MAC_LDFLAGS) -o build/disk-shots
	./build/disk-shots s
	@ls -la screenshots/
