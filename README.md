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
| CDMA   | DMA engine — fetches weight/feature data into CBUF |
| CBUF   | 512 KB ping-pong buffer (32 banks × 16 KB) |
| CSC    | Stripe scheduler — sequences MAC operations |
| CMAC   | 8×8 MAC array (64 MACs/cycle), 7-cycle pipeline |
| CACC   | Accumulation — 11-cycle assembly SRAM circle |

## Project Structure

```
nvdla-conv-simulator/
├── 3rdparty/
│ └── hw/ ← nvdla/hw RTL submodule (master branch)
│ ├── tree.make ← create manually (see RTL build section)
│ ├── outdir/nv_small/verilator/
│ │ └── VNV_nvdla ← Verilator simulation binary
│ └── verif/tests/trace_tests/nv_small/
│   └── <test>/ ← .cfg, _dt.dat, _wt.dat ground-truth files
├── src/
│   ├── types.h
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

### 4. Run SystemC unit tests

```bash
make test
```

## Environment

| Tool             | Version |
|------------------|---------|
| SystemC          | 2.3.4 (C++17) |
| Verilator        | 4.228 (v5.x breaks NVDLA RTL — stalls at sanity test) |
| NVDLA RTL config | nv_small |
| Base OS          | Ubuntu 22.04 |
| Java             | OpenJDK 11 (tmake codegen) |
| Python           | 2 (tmake dependency) |

## nv_small Config Reference

| Parameter      | Value |
|----------------|-------|
| ATOMIC_C       | 8 |
| ATOMIC_K       | 8 |
| MAC array      | 8 × 8 = 64 MACs/cycle |
| Data type      | INT8 only |
| CBUF           | 512 KB (32 banks × 16 KB each) |
| Memory width   | 64-bit primary |
| Memory latency | 50 cycles |
| Weight compression | Disabled |
| Winograd       | Disabled |

## Building the Verilator RTL Ground Truth
See [docs/verilator-build.md](docs/verilator-build.md) for the full step-by-step build guide, including tmake setup, trace test data format, and the `dc_1x1x8_1x1x8x1_int8_0` ground-truth example.

## Project Status

- [x] Docker dev environment
- [x] Verilator RTL build (nv_small config)
- [x] Trace test data format documented
- [ ] Python ground-truth verification script
- [ ] `types.h` — ATOMIC_C=8, ATOMIC_K=8 constants
- [ ] CMAC module (`cmac.h` / `cmac.cpp`)
- [ ] CBUF module
- [ ] CDMA module
- [ ] CSC + CACC modules
- [ ] End-to-end conv layer validation

## References

- [NVDLA hw repo](https://github.com/nvdla/hw) — RTL source
- [NVDLA documentation](http://nvdla.org/) — architecture and register reference
- [Verilator](https://www.veripool.org/verilator/) — RTL simulation
- [SystemC 2.3.4](https://github.com/accellera-official/systemc) — cycle-accurate modelling