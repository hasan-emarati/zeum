# ساخت و تست هسته قابل‌حمل روی PC (بدون Zephyr)
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
GENOME  ?= $(abspath ../../genome/organs)
BUILD   := build

.PHONY: test host clean
test: $(BUILD)/test_organ
	$(BUILD)/test_organ

host: $(BUILD)/creature_host
	$(BUILD)/creature_host $(GENOME)

$(BUILD)/test_organ: test/test_organ.c src/organ.c src/organ.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -DGENOME_DIR='"$(GENOME)"' test/test_organ.c src/organ.c -o $@

$(BUILD)/creature_host: test/host_main.c src/organ.c src/organ.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc test/host_main.c src/organ.c -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
