IMAGE     := nvdla-systemc-dev:latest
CONTAINER := nvdla-dev
BUILD_DIR := build

.PHONY: docker-build dev clean test tb-cmac

docker-build:
	docker build -t $(IMAGE) .

dev:
	docker run -it --rm \
	  --name $(CONTAINER) \
	  -v $(PWD):/workspace/nvdla-conv-simulator \
	  $(IMAGE)

# Configure (only runs if build/Makefile is missing)
$(BUILD_DIR)/Makefile:
	cmake -S . -B $(BUILD_DIR)

# Build all targets, then run CTest
test: $(BUILD_DIR)/Makefile
	cmake --build $(BUILD_DIR)
	ctest --test-dir $(BUILD_DIR) --output-on-failure

# Build + run tb_cmac only
tb-cmac: $(BUILD_DIR)/Makefile
	cmake --build $(BUILD_DIR) --target tb_cmac
	./$(BUILD_DIR)/tb_cmac

clean:
	docker rmi $(IMAGE)
	rm -rf $(BUILD_DIR)