// =============================================================================
// types.h — Shared types, constants, and configuration for nvdla-conv-simulator
//
// Pipeline: CDMA → CBUF → CSC → CMAC → CACC
// Hardware target: NVDLA nv_small
//   ATOMIC_C = 8, ATOMIC_K = 8, INT8 only
//   CBUF: 128 KB = 32 banks × 4 KB
//         (CBUF_BANK_DEPTH_512 × CBUF_BANK_WIDTH_8 bytes, confirmed from nv_small.spec)
// =============================================================================
#pragma once

#include <systemc.h>
#include <cstdint>

// -----------------------------------------------------------------------------
// nv_small MAC array dimensions
// -----------------------------------------------------------------------------

/// Input channels consumed per atomic MAC operation (= feature vector width)
static constexpr int NVDLA_ATOMIC_C = 8;

/// Output channels produced per atomic MAC operation (= psum vector width)
static constexpr int NVDLA_ATOMIC_K = 8;

/// Total weight elements per atomic MAC operation (ATOMIC_C × ATOMIC_K)
static constexpr int NVDLA_ATOMIC_CK     = NVDLA_ATOMIC_C * NVDLA_ATOMIC_K; // 64

// -----------------------------------------------------------------------------
// nv_small pipeline timing constants
// -----------------------------------------------------------------------------

/// CMAC pipeline depth in clock cycles (input→psum latency).
/// Confirmed from nv_small outdir RTL:
///   RT_csc2cmac (0) + CMAC_core MAC_PD_LATENCY (2) + CMAC_rt_out (1) = 3
static constexpr int NVDLA_CMAC_DEPTH    = 3;

/// CACC SRAM read-store circle depth in clock cycles.
/// UNVERIFIED — to be confirmed from CACC RTL when implementing CACC module.
static constexpr int NVDLA_CACC_DEPTH    = 11;

/// CMAC asserts early-valid this many cycles before cacc_valid.
/// Must satisfy: NVDLA_EARLY_OFFSET < NVDLA_CMAC_DEPTH (so EARLY_TAP is in-bounds).
/// PLACEHOLDER = 1 (EARLY_TAP=0, fires 2 cycles early with CMAC_DEPTH=3).
///    Real value to be confirmed from CACC RTL when implementing CACC module.
static constexpr int NVDLA_EARLY_OFFSET  = 1;

// -----------------------------------------------------------------------------
// nv_small CBUF (Convolution Buffer)
// -----------------------------------------------------------------------------

/// Total CBUF size in KB
/// 32 banks × 512 entries × 8 bytes = 131,072 bytes = 128 KB
/// (confirmed from nv_small.spec: CBUF_BANK_NUMBER_32, CBUF_BANK_DEPTH_512, CBUF_BANK_WIDTH_8)
static constexpr int NVDLA_CBUF_KB       = 128;

/// Number of independent CBUF banks
/// (confirmed: CBUF_BANK_NUMBER_32)
static constexpr int NVDLA_CBUF_BANKS    = 32;

/// Size of each CBUF bank in KB
/// 512 entries × 8 bytes/entry = 4096 bytes = 4 KB
/// (RTL: two nv_ram_rws_256x64 per bank = 2 × 2KB = 4KB)
static constexpr int NVDLA_CBUF_BANK_KB  = 4;

// -----------------------------------------------------------------------------
// Port-width constants
// -----------------------------------------------------------------------------

/// Bit width of one INT8 data element (feature or weight)
static constexpr int DATA_W       = 8;

/// Bit width of one CMAC→CACC wire per K-lane (one channel-group dot product).
/// RTL: `output [19-1:0] mac2accu_data{0..3}` in generated NV_NVDLA_CMAC_core.v.
/// Max magnitude: 8 × 127 × 127 = 129,032 < 2^17 = 131,072.  19-bit signed
/// covers ±262,144 — one guard bit beyond the required 18-bit minimum.
static constexpr int MAC2ACCU_W   = 19;

/// Bit width of one CACC internal accumulator lane for INT8 mode.
/// RTL: `input [33:0] in_op` and `output [33:0] out_partial_data`
/// in generated NV_NVDLA_CACC_CALC_int8.v.
/// Accumulates up to ~1024 steps of 19-bit values → ~29 bits; 34-bit gives
/// ample headroom and matches the ABUF storage width (abuf_wr_data [34*8-1:0]).
static constexpr int PSUM_W       = 34;

/// Number of entries in the CACC assembly buffer (the "circle" SRAM).
/// RTL: abuf_rd_addr [3+1-1:0] = [3:0] → 4-bit address → 16 entries.
/// Actual circle depth (how many in-flight positions the scheduler uses)
/// must be ≤ 16; the typical nv_small value is 11 (unverified against scheduler).
static constexpr int CACC_ABUF_ENTRIES = 16;

/// CBUF bank select width: ceil(log2(32)) = 5
static constexpr int CBUF_BANK_W  = 5;

/// CBUF word-address width per bank.
/// 4 KB bank, accessed as 8-byte (64-bit) words:
///   CBUF_BANK_DEPTH_512 entries → 9 bits.
static constexpr int CBUF_ADDR_W  = 9;

// -----------------------------------------------------------------------------
// Convenience type aliases used in port declarations
// -----------------------------------------------------------------------------

/// One signed 8-bit data element (feature map or weight)
using elem_t  = sc_int<DATA_W>;

/// CMAC→CACC wire: one signed 19-bit per-step dot product per K-lane.
/// This is what the CMAC outputs each clock cycle on mac2accu_data{0..3}.
using mac2accu_t = sc_int<MAC2ACCU_W>;

/// CACC internal accumulator: one signed 34-bit running sum per K-lane.
/// CACC stores this in its assembly buffer (abuf_wr_data [34*8-1:0]).
using psum_t  = sc_int<PSUM_W>;

/// CBUF bank select
using bank_t  = sc_uint<CBUF_BANK_W>;

/// CBUF word address within a bank
using addr_t  = sc_uint<CBUF_ADDR_W>;

/// Free-slot counter shared between CDMA and CSC
using slot_t  = sc_uint<16>;

// -----------------------------------------------------------------------------
// Convolution layer configuration
// Filled in by the testbench / top-level before simulation starts.
// -----------------------------------------------------------------------------
struct NvdlaConvConfig {
    // Data precision
    int data_width;   ///< element width in bits (must be 8 for nv_small)

    // Input feature map
    int input_c;      ///< input channels (C)
    int input_h;      ///< input height   (H)
    int input_w;      ///< input width    (W)

    // Kernel / weight
    int kernel_h;     ///< kernel height  (R)
    int kernel_w;     ///< kernel width   (S)
    int output_c;     ///< number of output channels / kernels (K)

    // Output feature map
    int output_h;     ///< computed by caller: (H + 2*pad_h - R) / stride_h + 1
    int output_w;     ///< computed by caller: (W + 2*pad_w - S) / stride_w + 1

    // Convolution parameters
    int stride_h;
    int stride_w;
    int pad_h;
    int pad_w;

    // Derived helper (filled by caller)
    // Total atomic-MAC groups = ceil(input_c * kernel_h * kernel_w / ATOMIC_C)
    // × output_h × output_w × ceil(output_c / ATOMIC_K)
};