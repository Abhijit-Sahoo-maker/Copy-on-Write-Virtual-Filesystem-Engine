.PHONY: all build shell test bench clean

BUILD_DIR ?= build

all: build

build:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build . -j$$(nproc 2>/dev/null || echo 4)

shell: build
	@./$(BUILD_DIR)/cowfs-shell

test: build
	@echo "=========================================="
	@echo "    RUNNING COWFS VERIFICATION SUITE      "
	@echo "=========================================="
	@./$(BUILD_DIR)/test_allocator
	@./$(BUILD_DIR)/test_cow
	@./$(BUILD_DIR)/test_snapshot
	@./$(BUILD_DIR)/test_crash_consistency
	@./$(BUILD_DIR)/test_concurrency
	@./$(BUILD_DIR)/test_vfs

bench: build
	@./$(BUILD_DIR)/benchmark_suite

clean:
	@rm -rf $(BUILD_DIR)
