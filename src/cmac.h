// =============================================================================
// cmac.h — Convolution MAC array (CMAC)
//
// nv_small configuration: ATOMIC_C = 8, ATOMIC_K = 8
//
// Responsibilities:
//   1. Accept one atomic slice per clock cycle from CSC:
//        csc_feat[8]  — 8 INT8 input feature elements (one channel-group)
//        csc_wt[64]   — 64 INT8 weight elements (8 output channels × 8 input channels)
//   2. Compute 8 dot products in parallel — one per output channel k:
//        psum[k] = Σ_{c=0}^{7}  feat[c] × wt[k * ATOMIC_C + c]
//      Each dot product is one row of the 8×8 weight tile dotted with the
//      feature vector.  The result is a signed 19-bit partial sum.
//   3. Pipeline the result through 3 stages; emit partial sums to CACC.
//   4. Assert cacc_early_valid (NVDLA_EARLY_OFFSET cycles before cacc_valid)
//      so CACC can pre-read the accumulation SRAM and avoid read-after-write.
//      NVDLA_EARLY_OFFSET is a placeholder — to be confirmed from CACC RTL.
//   5. Forward chan_last through the same 3-stage pipeline so CACC knows
//      when to flush the accumulation result to the output buffer.
//
// Timing diagram (single slice, no stall):
//
//   T0  csc_valid HIGH — feat/wt sampled
//   T1  S0 register: inputs latched, dot product computed
//   T2  S1 register: shift
//   T3  S2 register: shift → cacc_valid HIGH + cacc_psum[0..7] valid
//       → total latency: 3 cycles (confirmed from nv_small RTL)
//
// Pipeline stages (NVDLA_CMAC_DEPTH = 3, confirmed from nv_small outdir RTL):
//   S0 [captures at T1]: register csc_valid/chan_last; compute psum = feat·wt
//                        (models CMAC_core input stage, MAC_PD_LATENCY=2 start)
//   S1 [at T2]          : shift  (models CMAC_core MAC_PD_LATENCY=2 complete)
//   S2 [drives at T3]   : output register → cacc_valid / cacc_psum
//                        (models CMAC_CORE_rt_out 1-stage flop)
//
// early_valid tap (placeholder — confirmed when implementing CACC):
//   EARLY_TAP = NVDLA_EARLY_OFFSET - 1 = 0  →  fires at T1, leads cacc_valid by 2 cycles.
//   Real EARLY_OFFSET to be measured from Verilator waveform.
//
// Weight memory layout (K-major / C-minor — matches NVDLA RTL):
//   wt[k * ATOMIC_C + c]  where k ∈ [0, ATOMIC_K), c ∈ [0, ATOMIC_C)
//   Bytes in memory:  [wt(k=0,c=0), wt(k=0,c=1), ..., wt(k=0,c=7),
//                      wt(k=1,c=0), ..., wt(k=7,c=7)]
// =============================================================================
#pragma once

#include "types.h"

SC_MODULE(Cmac) {
    // -------------------------------------------------------------------------
    // Clock / reset
    // -------------------------------------------------------------------------
    sc_in<bool>  clk;
    sc_in<bool>  rst_n;   ///< active-low asynchronous reset

    // -------------------------------------------------------------------------
    // Input from CSC — one atomic slice per cycle
    // -------------------------------------------------------------------------
    sc_in<bool>                csc_valid;      ///< this cycle carries a valid slice
    sc_in<bool>                csc_chan_last;   ///< last channel-reduction step in this conv

    /// Feature vector: 8 INT8 elements for channels c=0..7
    sc_vector<sc_in<elem_t>>   csc_feat;       ///< [NVDLA_ATOMIC_C]

    /// Weight tile: 64 INT8 elements, layout wt[k * ATOMIC_C + c]
    sc_vector<sc_in<elem_t>>   csc_wt;         ///< [NVDLA_ATOMIC_CK]

    // -------------------------------------------------------------------------
    // Output to CACC
    // -------------------------------------------------------------------------

    /// Asserted NVDLA_EARLY_OFFSET cycles before cacc_valid.
    /// CACC uses this to pre-read the accumulation SRAM (avoids read-after-write).
    /// Current EARLY_OFFSET = 1 (placeholder); leads cacc_valid by 2 cycles.
    sc_out<bool>               cacc_early_valid;

    /// Partial-sum valid — asserted 3 cycles after csc_valid
    sc_out<bool>               cacc_valid;

    /// chan_last pipelined through the same 3-stage shift register as data
    sc_out<bool>               cacc_chan_last;

    /// Per-step dot products: mac2accu[k] = Σ_c feat[c] × wt[k*8+c],  k = 0..7
    /// 19-bit signed — matches RTL mac2accu_data0..data3 (CMAC_a + CMAC_b combined).
    /// CACC accumulates these into its 34-bit assembly-buffer accumulators.
    sc_vector<sc_out<mac2accu_t>>  cacc_psum;  ///< [NVDLA_ATOMIC_K]

    // -------------------------------------------------------------------------
    // Constructor
    // -------------------------------------------------------------------------
    SC_HAS_PROCESS(Cmac);

    explicit Cmac(sc_module_name name)
        : sc_module(name)
        , csc_feat  ("csc_feat",   NVDLA_ATOMIC_C)
        , csc_wt    ("csc_wt",     NVDLA_ATOMIC_CK)
        , cacc_psum ("cacc_psum",  NVDLA_ATOMIC_K)
    {
        SC_METHOD(pipe_stage);
        sensitive << clk.pos();
        async_reset_signal_is(rst_n, false);
    }

private:
    // -------------------------------------------------------------------------
    // 3-stage pipeline shift registers
    // -------------------------------------------------------------------------
    static constexpr int STAGES    = NVDLA_CMAC_DEPTH;           // 3 (confirmed)
    static constexpr int EARLY_TAP = NVDLA_EARLY_OFFSET - 1;     // must be < STAGES
    static_assert(EARLY_TAP >= 0 && EARLY_TAP < STAGES,
        "NVDLA_EARLY_OFFSET must satisfy 1 <= EARLY_OFFSET < CMAC_DEPTH");

    sc_signal<bool>       pipe_valid    [STAGES];
    sc_signal<bool>       pipe_chan_last[STAGES];

    // Linearised 2-D array: pipe_psum[stage * NVDLA_ATOMIC_K + k]
    // 19-bit matches the RTL mac2accu_data wire width.
    sc_signal<mac2accu_t> pipe_psum[STAGES * NVDLA_ATOMIC_K];

    // -------------------------------------------------------------------------
    // Clocked process — shifts all pipeline registers; computes dot products
    // at stage S0 (input register)
    // -------------------------------------------------------------------------
    void pipe_stage();
};