IMAGE     := nvdla-systemc-dev:latest
CONTAINER := nvdla-dev
BUILD_DIR := build

.PHONY: docker-build dev clean test

docker-build:
	docker build -t $(IMAGE) .

dev:
	docker run -it --rm \
	  --name $(CONTAINER) \
	  -v $(PWD):/workspace/nvdla-conv-simulator \
	  $(IMAGE)

$(BUILD_DIR):
	cmake -S . -B $(BUILD_DIR)

test: $(BUILD_DIR)
	cmake --build $(BUILD_DIR) --target test

clean:
	docker rmi $(IMAGE)
	rm -rf $(BUILD_DIR)