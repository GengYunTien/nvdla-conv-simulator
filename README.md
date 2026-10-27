# nvdla-conv-simulator

Cycle-accurate SystemC simulator for the NVDLA Conv pipeline, validated against Verilator RTL simulation.

## Motivation

The official [nvdla/vp](https://github.com/nvdla/vp) virtual platform is **register-accurate** (TLM2), not clock-driven. This project fills that gap by modeling the full Conv pipeline — CDMA → CBUF → CSC → CMAC → CACC — at cycle granularity, enabling latency analysis and micro-architecture exploration without EDA tools.

## What this models
- MAC array utilization (CMAC)
- SRAM buffer stall timing (CBUF)
- Memory fetch latency (CDMA)
- Back-pressure propagation (CACC → CSC)

## Pipeline
CDMA → CBUF → CSC → CMAC → CACC

| Module | Role |
|--------|------|
| CDMA | DMA engine — fetches weight/feature data into CBUF |
| CBUF | 512 KB ping-pong buffer (16 banks × 32 KB) |
| CSC | Stripe scheduler — sequences MAC operations |
| CMAC | 16 MAC cells, 7-cycle pipeline |
| CACC | Accumulation — 11-cycle assembly SRAM circle |

## Project Structure

```
nvdla-conv-simulator/
├── 3rdparty/
│   └── hw/                  ← nvdla/hw RTL submodule (nvdlav1 branch)
├── src/
│   ├── cmac.h / cmac.cpp
│   ├── cacc.h / cacc.cpp
│   ├── cbuf.h / cbuf.cpp
│   ├── cdma.h / cdma.cpp
│   ├── csc.h  / csc.cpp
│   └── top.cpp
├── tests/
│   └── conv_layer_test.cpp
├── Dockerfile
├── Makefile
└── README.md
```

## Getting Started

### 1. Clone with submodules
```bash
git clone --recurse-submodules https://github.com/GengYunTien/nvdla-conv-simulator.git
cd nvdla-conv-simulator
```

### 2. Build the dev image
```bash
make docker-build
```

### 3. Enter the dev container
```bash
make dev
```

## Environment

| Tool | Version |
|------|---------|
| SystemC | 2.3.4 (C++17) |
| Verilator | 4.228 |
| NVDLA RTL config | nv_small |
| Base OS | Ubuntu 22.04 |

## Project Status

- [x] Docker dev environment
- [ ] CMAC module
- [ ] CBUF module
- [ ] CDMA module
- [ ] CSC + CACC modules
- [ ] End-to-end conv layer validation
