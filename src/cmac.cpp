// =============================================================================
// cmac.cpp — Convolution MAC array implementation
//
// nv_small: ATOMIC_C = 8, ATOMIC_K = 8, INT8 only
//
// MAC operation (per cycle, per output channel k):
//   psum[k] = Σ_{c=0}^{ATOMIC_C-1}  feat[c] × wt[k * ATOMIC_C + c]
//
// Weight tile layout on csc_wt[0..63]:
//   index = k * ATOMIC_C + c   (K-major, C-minor)
//   i.e., wt[0..7]  = weights for k=0 (all input channels)
//         wt[8..15] = weights for k=1, etc.
//
// Behavioral simplification vs RTL:
//   Real NVDLA CMAC uses Booth encoding + Wallace tree spread across multiple
//   pipeline stages.  Here the full dot product is computed at stage S0 and
//   propagated as a shift-register.  The 3-cycle latency is identical.
// =============================================================================

#include "cmac.h"

void Cmac::pipe_stage()
{
    // =========================================================================
    // Async reset: clear all pipeline registers and outputs
    // =========================================================================
    if (!rst_n.read()) {
        for (int s = 0; s < STAGES; s++) {
            pipe_valid[s].write(false);
            pipe_chan_last[s].write(false);
            for (int k = 0; k < NVDLA_ATOMIC_K; k++)
                pipe_psum[s * NVDLA_ATOMIC_K + k].write(mac2accu_t(0));
        }
        cacc_early_valid.write(false);
        cacc_valid.write(false);
        cacc_chan_last.write(false);
        for (int k = 0; k < NVDLA_ATOMIC_K; k++)
            cacc_psum[k].write(mac2accu_t(0));
        return;
    }

    // =========================================================================
    // Step 1 — Shift: pipe[s] ← pipe[s-1]  (high to low, avoids read-after-write)
    // =========================================================================
    for (int s = STAGES - 1; s >= 1; s--) {
        pipe_valid[s].write(pipe_valid[s - 1].read());
        pipe_chan_last[s].write(pipe_chan_last[s - 1].read());
        for (int k = 0; k < NVDLA_ATOMIC_K; k++)
            pipe_psum[s * NVDLA_ATOMIC_K + k].write(
                pipe_psum[(s - 1) * NVDLA_ATOMIC_K + k].read());
    }

    // =========================================================================
    // Step 2 — Feed stage S0 from CSC inputs
    // =========================================================================
    pipe_valid[0].write(csc_valid.read());
    pipe_chan_last[0].write(csc_chan_last.read());

    // =========================================================================
    // Step 3 — Compute 8 dot products at S0
    //
    // Use sc_int<32> as intermediate accumulator to prevent overflow during the
    // 8-term sum (max: 8 × 127 × 127 = 129,032 — fits in 19-bit signed ✓),
    // then narrow-cast to mac2accu_t (19-bit) for the pipeline register.
    //
    // Bubbles (csc_valid == false) write zero to avoid stale values propagating.
    // =========================================================================
    {
        const bool valid = csc_valid.read();
        for (int k = 0; k < NVDLA_ATOMIC_K; k++) {
            sc_int<32> dot = 0;
            if (valid) {
                for (int c = 0; c < NVDLA_ATOMIC_C; c++)
                    dot += sc_int<32>(csc_feat[c].read())
                         * sc_int<32>(csc_wt[k * NVDLA_ATOMIC_C + c].read());
            }
            pipe_psum[k].write(mac2accu_t(dot));   // S0, lane k
        }
    }

    // =========================================================================
    // Step 4 — Drive outputs
    //
    // early_valid: tapped at EARLY_TAP (= NVDLA_EARLY_OFFSET - 1).
    //   Leads cacc_valid by (STAGES - 1 - EARLY_TAP) cycles.
    //   Current placeholder: EARLY_TAP = 0 → leads by 2 cycles.
    //
    // cacc_valid / chan_last / psum: driven from final stage (STAGES - 1).
    // =========================================================================
    cacc_early_valid.write(pipe_valid[EARLY_TAP].read());

    cacc_valid.write(pipe_valid[STAGES - 1].read());
    cacc_chan_last.write(pipe_chan_last[STAGES - 1].read());
    for (int k = 0; k < NVDLA_ATOMIC_K; k++)
        cacc_psum[k].write(pipe_psum[(STAGES - 1) * NVDLA_ATOMIC_K + k].read());
}