# Building the Verilator RTL Ground Truth

The `nvdla/hw` submodule compiles with Verilator into a cycle-accurate RTL simulation used as ground truth for validating the SystemC model.

## Prerequisites (all satisfied inside the dev container)

| Requirement     | Notes |
|-----------------|-------|
| Verilator 4.228 | Exactly 4.228 |
| OpenJDK 11      | `java` on PATH |
| Python 2        | `python` symlink |
| Perl modules    | `libyaml-perl`, `libxml-simple-perl`, `IO::Tee` |

## Step 1 — Create `tree.make`

```bash
cat > 3rdparty/hw/tree.make << 'EOF'
DESIGNWARE_NOEXIST := 1
PROJECTS := nv_small
EOF
```

> `DESIGNWARE_NOEXIST := 1` disables DesignWare IP (requires EDA license).  
> `PROJECTS := nv_small` selects the nv_small hardware configuration.  
> This file is not tracked by git.

## Step 2 — Run tmake

```bash
cd 3rdparty/hw
./tools/bin/tmake -build verilator 2>&1 | tee /tmp/verilator_build.log
```

Look for `BUILD PASS` at the end. The build takes ~15–30 minutes.

**Key outputs:**

```
outdir/nv_small/verilator/
├── VNV_nvdla        ← simulation binary
├── nvdla.cpp        ← testbench C++ (CSBMaster driver)
├── VNV_nvdla__ALL.a ← verilated RTL library
└── VNV_nvdla.h      ← verilated RTL header
```

## Step 3 — Inspect trace test data

```bash
make run TEST=dc_1x1x8_1x1x8x1_int8_0
```

> **Note:** The binary conversion step (`new_trace_to_verilator.py`) is NVIDIA-internal
> closed-source tooling not distributed with the hw repo. The trace parser runs and
> produces `.log` files, but `VNV_nvdla` cannot be invoked via the official flow.  
> Use the `.cfg` / `_dt.dat` / `_wt.dat` files in
> `verif/tests/trace_tests/nv_small/<TEST>/` directly as ground truth.

## Trace Test Data Format

Ground-truth data lives in `3rdparty/hw/verif/tests/trace_tests/nv_small/<test_name>/`.

### `.cfg` — Register sequence

```
reg_write(0x00b004, 0x00000001);
mem_init(sync_id_0, 0x80000400, 0x2100, "…_dt.dat");
mem_load(sync_id_0, 0x90000000, 0x80,   "…_wt.dat");
poll_reg_equal(sync_id_0, 0x00b01c, 0x1, 0x1);
check_crc(sync_id_0, 1, 0xa0000000, 0x8, 0x8f68a2ae);
```

| Directive | Meaning |
|-----------|---------|
| `reg_write(addr, val)` | Write CSB register |
| `mem_init` / `mem_load` | Load sparse data file into simulated memory |
| `poll_reg_equal` | Spin until register matches |
| `check_crc(id, n, addr, size, crc)` | Verify output against CRC32 |

### `_dt.dat` / `_wt.dat` — Sparse memory patches

```
{offset:0x0, size:4, payload:0x00 0x00 0x00 0x6f}
{offset:0x4, size:4, payload:0x00 0x10 0x00 0x00}
```

Each patch writes `payload` bytes (little-endian) to `base_addr + offset`.

### Example: `dc_1x1x8_1x1x8x1_int8_0`

1×1 convolution, C=8 input channels, K=1 output channel, INT8.

| Item         | Value |
|--------------|-------|
| Feature base | `0x80000400` |
| Weight base  | `0x90000000` (128 bytes) |
| Output base  | `0xa0000000` (8 bytes) |
| CACC clip    | right-shift 6 (`D_CLIP_CFG_0 = 0x6`) |
| Golden CRC   | `0x8f68a2ae` |

Decoded feature `[c0..c7]` (signed INT8): `[0, 0, 0, 111, 0, 16, 0, 0]`

Decoded weight first 8 bytes (signed INT8): `[117, -108, 97, 0, -86, -114, -3, 63]`