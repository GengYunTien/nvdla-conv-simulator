// =============================================================================
// tb_cmac.cpp — Unit testbench for the Cmac SystemC module
//
// nv_small: ATOMIC_C = 8, ATOMIC_K = 8, CMAC_DEPTH = 3
//
// Tests:
//   1. Reset            — all outputs LOW/zero while rst_n is deasserted
//   2. 3-cycle latency  — cacc_valid fires exactly 3 posedges after csc_valid
//   3. Dot product      — feat=[1..8], wt-row-0=all-ones → cacc_psum[0] = 36
//                         wt-rows-1..7=zero               → cacc_psum[1..7] = 0
//   4. chan_last         — propagates through the 3-stage pipeline unchanged
//   5. Back-to-back     — 3 consecutive valid slices → 3 consecutive outputs
//
// Timing model (SC_METHOD on clk.pos(), async reset):
//   sc_signal::read()  returns the value from before this posedge (pre-delta).
//   sc_signal::write() schedules the update for after this method (post-delta).
//
//   From the testbench SC_THREAD perspective:
//     tick() = wait(clk.posedge_event()) + wait(SC_ZERO_TIME)
//              ↑ advance one posedge      ↑ let SC_METHOD settle
//     After tick() returns, all DUT outputs reflect the just-completed posedge.
//
//   Latency trace (input driven before tick P0):
//     After P0: pipe[0] = {valid, psum}  — cacc_valid = false
//     After P1: pipe[1] = old pipe[0]   — cacc_early_valid = true (EARLY_TAP=0)
//     After P2: pipe[2] = old pipe[1]   — cacc_valid = false
//     After P3: outputs ← old pipe[2]   — cacc_valid = true ✓  (3-cycle latency)
// =============================================================================

#include <systemc.h>
#include "../src/cmac.h"
#include <iostream>

// ── Pass / fail counters ──────────────────────────────────────────────────────
static int g_pass = 0, g_fail = 0;

static void check(const char *tag, bool cond)
{
    std::cout << (cond ? "  PASS  " : "  FAIL  ") << tag << "\n";
    cond ? ++g_pass : ++g_fail;
}

// ── Testbench SC_MODULE ───────────────────────────────────────────────────────
SC_MODULE(TbCmac) {

    // ── Signals ───────────────────────────────────────────────────────────────
    sc_clock                         clk{"clk", 10, SC_NS};
    sc_signal<bool>                  rst_n;
    sc_signal<bool>                  csc_valid;
    sc_signal<bool>                  csc_chan_last;
    sc_vector<sc_signal<elem_t>>     csc_feat{"csc_feat",  NVDLA_ATOMIC_C};
    sc_vector<sc_signal<elem_t>>     csc_wt  {"csc_wt",    NVDLA_ATOMIC_CK};
    sc_signal<bool>                  cacc_early_valid;
    sc_signal<bool>                  cacc_valid;
    sc_signal<bool>                  cacc_chan_last;
    sc_vector<sc_signal<mac2accu_t>> cacc_psum{"cacc_psum", NVDLA_ATOMIC_K};

    // ── DUT ───────────────────────────────────────────────────────────────────
    Cmac dut{"dut"};

    SC_CTOR(TbCmac) {
        dut.clk(clk);
        dut.rst_n(rst_n);
        dut.csc_valid(csc_valid);
        dut.csc_chan_last(csc_chan_last);
        for (int i = 0; i < NVDLA_ATOMIC_C;  i++) dut.csc_feat[i](csc_feat[i]);
        for (int i = 0; i < NVDLA_ATOMIC_CK; i++) dut.csc_wt[i](csc_wt[i]);
        dut.cacc_early_valid(cacc_early_valid);
        dut.cacc_valid(cacc_valid);
        dut.cacc_chan_last(cacc_chan_last);
        for (int i = 0; i < NVDLA_ATOMIC_K; i++) dut.cacc_psum[i](cacc_psum[i]);

        SC_THREAD(run);
    }

private:

    // ── Timing helpers ────────────────────────────────────────────────────────

    /// Advance one clock posedge; outputs are readable after return.
    void tick() {
        wait(clk.posedge_event());
        wait(SC_ZERO_TIME);  // let SC_METHOD settle (delta-cycle resolution)
    }

    /// Drive CSC inputs.  Values take effect at the NEXT tick().
    void set_inputs(bool valid, bool chan_last,
                    const int feat[], const int wt[]) {
        csc_valid.write(valid);
        csc_chan_last.write(chan_last);
        for (int i = 0; i < NVDLA_ATOMIC_C;  i++) csc_feat[i].write(elem_t(feat[i]));
        for (int i = 0; i < NVDLA_ATOMIC_CK; i++) csc_wt[i].write(elem_t(wt[i]));
    }

    /// Drive a bubble (csc_valid = 0) and advance one cycle.
    void idle() {
        static const int zf[NVDLA_ATOMIC_C]  = {};
        static const int zw[NVDLA_ATOMIC_CK] = {};
        set_inputs(false, false, zf, zw);
        tick();
    }

    // ── Test runner ───────────────────────────────────────────────────────────
    void run() {
        int zf[NVDLA_ATOMIC_C]  = {};
        int zw[NVDLA_ATOMIC_CK] = {};

        // ── TEST 1: Reset ─────────────────────────────────────────────────────
        std::cout << "\n=== TEST 1: Reset ===\n";
        rst_n.write(false);
        set_inputs(false, false, zf, zw);
        tick(); tick();

        check("cacc_valid LOW during reset",         !cacc_valid.read());
        check("cacc_early_valid LOW during reset",   !cacc_early_valid.read());
        check("cacc_chan_last LOW during reset",      !cacc_chan_last.read());
        check("cacc_psum[0] == 0 during reset",       cacc_psum[0].read() == mac2accu_t(0));

        rst_n.write(true);
        idle();  // settle after reset release

        // ── TEST 2+3: 3-cycle latency + dot product ────────────────────────────
        //
        // Input:
        //   feat[c] = c + 1  →  feat = [1, 2, 3, 4, 5, 6, 7, 8]
        //   wt[0*8 + c] = 1  →  row k=0: all ones
        //   wt[k*8 + c] = 0 for k>0
        //
        // Expected:
        //   cacc_psum[0] = Σ_{c=0}^{7} (c+1)*1 = 1+2+3+4+5+6+7+8 = 36
        //   cacc_psum[1..7] = 0
        //
        // Latency:
        //   P0 (input captured) → P3 (cacc_valid HIGH) = 3 clock cycles
        //   P1: cacc_early_valid HIGH (EARLY_TAP=0, leads by 2 cycles)
        //
        std::cout << "\n=== TEST 2+3: Latency and dot product ===\n";
        int feat[NVDLA_ATOMIC_C];
        int wt[NVDLA_ATOMIC_CK] = {};
        for (int c = 0; c < NVDLA_ATOMIC_C; c++) feat[c] = c + 1;
        for (int c = 0; c < NVDLA_ATOMIC_C; c++) wt[0 * NVDLA_ATOMIC_C + c] = 1;

        // P0: DUT captures input
        set_inputs(true, false, feat, wt);
        tick();
        check("cacc_valid LOW at P0+1",       !cacc_valid.read());
        check("cacc_early_valid LOW at P0+1", !cacc_early_valid.read());

        // P1: pipe[0] propagates; early_valid fires (EARLY_TAP=0 → reads pipe[0] old)
        idle();
        check("cacc_early_valid HIGH at P1+1", cacc_early_valid.read());
        check("cacc_valid LOW at P1+1",        !cacc_valid.read());

        // P2: data still in-flight
        idle();
        check("cacc_valid LOW at P2+1", !cacc_valid.read());

        // P3: data exits pipeline
        idle();
        check("cacc_valid HIGH at P3+1",   cacc_valid.read());
        check("cacc_psum[0] == 36",         cacc_psum[0].read() == mac2accu_t(36));

        bool rest_zero = true;
        for (int k = 1; k < NVDLA_ATOMIC_K; k++)
            if (cacc_psum[k].read() != mac2accu_t(0)) { rest_zero = false; break; }
        check("cacc_psum[1..7] == 0", rest_zero);

        idle();  // drain

        // ── TEST 4: chan_last propagation ──────────────────────────────────────
        //
        // Drive one valid slice with chan_last=1; wt all-zero (psum irrelevant).
        // Expect cacc_chan_last HIGH at P3 (same 3-cycle latency as valid/psum).
        //
        std::cout << "\n=== TEST 4: chan_last propagation ===\n";
        set_inputs(true, /*chan_last=*/true, feat, zw);
        tick();         // P0: captured
        idle();         // P1
        idle();         // P2
        idle();         // P3: chan_last exits pipeline
        check("cacc_chan_last HIGH after 3 cycles", cacc_chan_last.read());
        idle();         // drain

        // ── TEST 5: back-to-back slices ───────────────────────────────────────
        //
        // 3 consecutive valid slices (A, B, C) → 3 consecutive cacc_valid pulses.
        // Each slice produces psum[0] = 36.
        // After the last slice exits, cacc_valid must return LOW.
        //
        std::cout << "\n=== TEST 5: Back-to-back slices ===\n";
        set_inputs(true, false, feat, wt); tick();  // slice A at P0
        set_inputs(true, false, feat, wt); tick();  // slice B at P1
        set_inputs(true, false, feat, wt); tick();  // slice C at P2

        idle();  // P3: slice A exits
        check("cacc_valid slice A",            cacc_valid.read());
        check("cacc_psum[0] slice A == 36",    cacc_psum[0].read() == mac2accu_t(36));

        idle();  // P4: slice B exits
        check("cacc_valid slice B",            cacc_valid.read());

        idle();  // P5: slice C exits
        check("cacc_valid slice C",            cacc_valid.read());

        idle();  // P6: pipeline empty
        check("cacc_valid LOW after all slices", !cacc_valid.read());

        // ── Summary ───────────────────────────────────────────────────────────
        std::cout << "\n==============================\n";
        std::cout << "  PASS: " << g_pass
                  << "  FAIL: " << g_fail << "\n";
        std::cout << (g_fail == 0 ? "  ALL TESTS PASSED\n"
                                  : "  SOME TESTS FAILED\n");
        std::cout << "==============================\n";
        sc_stop();
    }
};

// ── sc_main ───────────────────────────────────────────────────────────────────
int sc_main(int /*argc*/, char ** /*argv*/)
{
    TbCmac tb{"tb"};
    sc_start();
    return g_fail > 0 ? 1 : 0;
}