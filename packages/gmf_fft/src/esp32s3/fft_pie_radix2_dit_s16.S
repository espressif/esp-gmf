/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * ESP32-S3: radix-2 DIT Q15 FFT helpers (Xtensa PIE, ee.* vector instructions).
 *
 * Data layout (matches fft_power2_q15.c):
 *   - Interleaved int16 [re0,im0, re1,im1, ...], length 2*N int16s; buffer alignment
 *     must satisfy the ISA / DMA expectations of the load/store widens used here.
 *   - Typical C plan order: fft_bf / bit_reverse, then fftr; inverse: ffti then ifft_bf.
 * Registers follow the Xtensa call convention; each routine documents a2-a6 (etc.) below.
 */

/* void fft_radix2_bit_reverse_s16(int16_t *data, int32_t cpx_points, int32_t log2N); */
/* Bit-reverse the spectrum by log2N bits; inner pattern touches 16 complex samples. */
    .text
    .align 4
    .global fft_radix2_bit_reverse_s16
    .type   fft_radix2_bit_reverse_s16, @function
fft_radix2_bit_reverse_s16:
    /* a2=data, a3=N complex points, a4=log2(N) */
     .align 4
    entry sp, 16
    srli a3, a3, 3
    wur.fft_bit_width a4
    movi a14, 0
    mov a7, a2          // index for store
    mov a6, a2          // index for load
    loopnez a3, loop_end_radix2_bit_reverse   // outer count; inner handles 16 cpx per step
        ee.vld.128.ip q0, a7, 16
        ee.bitrev q2, a14
        ee.ldxq.32 q1, q2, a2, 0, 0
        ee.ldxq.32 q1, q2, a2, 1, 1
        ee.ldxq.32 q1, q2, a2, 2, 2
        ee.ldxq.32 q1, q2, a2, 3, 3
        ee.stxq.32 q0, q2, a2, 0, 0
        ee.stxq.32 q0, q2, a2, 1, 1
        ee.stxq.32 q0, q2, a2, 2, 2
        ee.stxq.32 q0, q2, a2, 3, 3
        ee.vst.128.ip q1, a6, 16
        ee.vld.128.ip q0, a7, 16
        ee.ldxq.32 q1, q2, a2, 0, 4
        ee.ldxq.32 q1, q2, a2, 1, 5
        ee.ldxq.32 q1, q2, a2, 2, 6
        ee.ldxq.32 q1, q2, a2, 3, 7
        ee.stxq.32 q0, q2, a2, 0, 4
        ee.stxq.32 q0, q2, a2, 1, 5
        ee.stxq.32 q0, q2, a2, 2, 6
        ee.stxq.32 q0, q2, a2, 3, 7
        ee.vst.128.ip q1, a6, 16
    loop_end_radix2_bit_reverse:
    retw

/*
 * void fft_radix2_fft_bf_s16(int16_t *data, int16_t *twiddle_win, int32_t shift,
 *                            int32_t num_stages, int32_t cpx_points);
 * Multi-stage radix-2 butterflies (forward FFT). Twiddles match twiddle_win from
 * esp_gmf_fft_init. Main loop runs (num_stages - 3) stages, then three tail blocks.
 * a11=16 twiddle stride (bytes); a15 inner loop count per box (see srli a6,4).
 */
    .text
    .align 4
    .global fft_radix2_fft_bf_s16
    .type   fft_radix2_fft_bf_s16, @function
fft_radix2_fft_bf_s16:
    /* Args: a2=data, a3=twiddle_win, a4=shift (ssr), a5=num_stages, a6=cpx_points */
    /* Inner temps: a2 stays spectrum base; a4 running box base; a7=ptr1; a8=store; a9=load */
    .align 4
    entry sp, 16
    ssr a4
    mov a13, a3
    addi a5, a5, -3 // main loop: (num_stages - 3)
    movi a12, 1
    mov a14, a12        // number of box in current stage
    srli a15, a6, 4 // inner iterations per box (~ N_box/16; each pass does 16 cpx)
    movi a11, 16     // twiddle table stride (bytes per group)
    start_fft_radix2_stage:  // outer stage loop
        mov a4, a2
        mov a3, a13         // start of current stage for twiddle factor
        start_fft_radix2_stage_box:
            mov a9, a4      // load
            mov a10, a9        // store
            addx2 a7, a6, a4    //*ptr1 = data + n        load
            mov a8, a7          // store ptr
            mov a13, a3     // start for twiddle factor
            ee.vld.128.ip q0, a9, 16
            ee.vld.128.ip q1, a7, 16
            ee.vld.128.ip q2, a13, 16
            loopnez a15, loop_end_fft_radix2_butterfly
                ee.fft.r2bf.s16.st.incp q3, q0, q1, a10, 1                  // [a1 a2 a3 a4]
                ee.fft.cmul.s16.ld.xp q4, a13, a11, q5, q3, q2, 0            // [b1]
                ee.fft.cmul.s16.ld.xp q0, a9, a11, q5, q3, q2, 2            // [b2]
                ee.fft.cmul.s16.ld.xp q1, a7, a11, q5, q3, q2, 4            // [b3]
                ee.fft.cmul.s16.st.xp q3, q2, q5, a8, a11, 6, 0, 1             // [b1 b2 b3 b4]
                ee.fft.r2bf.s16.st.incp q3, q0, q1, a10, 1
                ee.fft.cmul.s16.ld.xp q2, a13, a11, q5, q3, q4, 0
                ee.fft.cmul.s16.ld.xp q0, a9, a11, q5, q3, q4, 2
                ee.fft.cmul.s16.ld.xp q1, a7, a11, q5, q3, q4, 4
                ee.fft.cmul.s16.st.xp q3, q4, q5, a8, a11, 6, 0, 1
            loop_end_fft_radix2_butterfly:
            addx4 a4, a6, a4  // start index of box
            addi a14, a14, -1
            sub a13, a13, a11
        bnez a14, start_fft_radix2_stage_box
        srli a15, a15, 1                // iteration for bf /2
        srli a6, a6, 1                    // number of cpx_points in box
        slli a12, a12, 1                // number of box
        mov a14, a12
        addi a5, a5, -1
    bnez a5, start_fft_radix2_stage
    /* ---------- Last three stages: specialized butterflies (twiddle / immediates differ) ---------- */
    mov a9, a2      // load
    mov a10, a9        // store
    ee.vld.128.ip q0, a9, 16
    ee.vld.128.ip q1, a9, 16
    ee.vld.128.ip q2, a13, 16 // shared twiddle for this tail block
    movi a14, 0
    loopnez a12, loop_end_radix2_last_third_stage
        ee.fft.r2bf.s16.st.incp q3, q0, q1, a10, 1                      // [a1 a2 a3 a4]
        ee.fft.cmul.s16.ld.xp q4, a13, a14, q5, q3, q2, 0                // [b1] q4- fake load
        ee.fft.cmul.s16.ld.xp q0, a9, a11, q5, q3, q2, 2                // [b2]
        ee.fft.cmul.s16.ld.xp q1, a9, a11, q5, q3, q2, 4                // [b3]
        ee.fft.cmul.s16.st.xp q3, q2, q5, a10, a11, 6, 0, 1                 // [b1 b2 b3 b4]
    loop_end_radix2_last_third_stage:
    mov a9, a2      // load
    mov a10, a9        // store
    mov a14, a2 // dummy for address / pipeline
    mov a3, a13
    ee.vld.128.ip q0, a9, 16
    ee.vld.128.ip q1, a9, 16
    ee.vld.h.64.ip q2, a13, 0
    loopnez a12, loop_end_radix2_last_second_stage
        ee.fft.r2bf.s16 q3, q4, q0, q1, 0          // [a1 a2 a3 a4]   q5-fake load
        ee.fft.cmul.s16.ld.xp q0, a9, a11, q5, q3, q2, 4                   // [b1]
        ee.fft.cmul.s16.st.xp q3, q2, q5, a10, a11, 6, 1, 1                 // [a1 a2 b1 b2]
        ee.fft.cmul.s16.ld.xp q1, a9, a11, q5, q4, q2, 4                // [b3]
        ee.fft.cmul.s16.st.xp q4, q2, q5, a10, a11, 6, 1, 1                 // [a3 a4 b3 b4]
    loop_end_radix2_last_second_stage:
    mov a9, a2      // load
    mov a10, a9        // store
    ee.vld.128.ip q0, a9, 16
    ee.vld.128.ip q1, a9, 16
    ee.vldbc.32 q2, a3
    loopnez a12, loop_end_radix2_last_stage
        ee.fft.r2bf.s16 q3, q4, q0, q1, 1          // [a1 a2 a3 a4]
        ee.fft.cmul.s16.ld.xp q0, a9, a11, q5, q3, q2, 4                   // [b1]
        ee.fft.cmul.s16.st.xp q3, q2, q5, a10, a11, 6, 2, 1                 // [a1 a2 b1 b2]
        ee.fft.cmul.s16.ld.xp q1, a9, a11, q5, q4, q2, 4                // [b3]
        ee.fft.cmul.s16.st.xp q4, q2, q5, a10, a11, 6, 2, 1                 // [a3 a4 b3 b4]
    loop_end_radix2_last_stage:
    retw

/*
 * void fft_radix2_ifft_bf_s16(same 5 args as fft_bf);
 * Inverse butterflies: same box/stage structure; ee.fft.cmul.s16.* immediates differ
 * (often 1/3/5/7 vs forward 0/2/4/6). Same three tail blocks as fft_bf.
 */
    .text
    .align 4
    .global fft_radix2_ifft_bf_s16
    .type   fft_radix2_ifft_bf_s16, @function
fft_radix2_ifft_bf_s16:
    /* Args: a2=data, a3=twiddle_win, a4=shift, a5=num_stages, a6=cpx_points */
    /* Inner temps: a4 box base; a7=ptr1; a8=store; a9=load */
    .align 4
    entry sp, 16
    ssr a4
    mov a13, a3
    addi a5, a5, -3 // main loop: (num_stages - 3)
    movi a12, 1
    mov a14, a12        // number of box in current stage
    srli a15, a6, 4 // inner iterations per box (~ N_box/16; each pass does 16 cpx)
    movi a11, 16     // twiddle table stride (bytes per group)
    start_ifft_radix2_stage:  // outer stage loop
        mov a4, a2
        mov a3, a13         // start of current stage for twiddle factor
        start_ifft_radix2_stage_box:
            mov a9, a4      // load
            mov a10, a9        // store
            addx2 a7, a6, a4    //*ptr1 = data + n        load
            mov a8, a7          // store ptr
            mov a13, a3     // start for twiddle factor
            ee.vld.128.ip q0, a9, 16
            ee.vld.128.ip q1, a7, 16
            ee.vld.128.ip q2, a13, 16
            loopnez a15, loop_end_ifft_radix2_butterfly
                ee.fft.r2bf.s16.st.incp q3, q0, q1, a10, 1                  // [a1 a2 a3 a4]
                ee.fft.cmul.s16.ld.xp q4, a13, a11, q5, q3, q2, 1            // [b1]
                ee.fft.cmul.s16.ld.xp q0, a9, a11, q5, q3, q2, 3            // [b2]
                ee.fft.cmul.s16.ld.xp q1, a7, a11, q5, q3, q2, 5            // [b3]
                ee.fft.cmul.s16.st.xp q3, q2, q5, a8, a11, 7, 0, 1
                ee.fft.r2bf.s16.st.incp q3, q0, q1, a10, 1
                ee.fft.cmul.s16.ld.xp q2, a13, a11, q5, q3, q4, 1
                ee.fft.cmul.s16.ld.xp q0, a9, a11, q5, q3, q4, 3
                ee.fft.cmul.s16.ld.xp q1, a7, a11, q5, q3, q4, 5
                ee.fft.cmul.s16.st.xp q3, q4, q5, a8, a11, 7, 0, 1
            loop_end_ifft_radix2_butterfly:
            addx4 a4, a6, a4  // start index of box
            addi a14, a14, -1
            sub a13, a13, a11
        bnez a14, start_ifft_radix2_stage_box
        srli a15, a15, 1                // iteration for bf /2
        srli a6, a6, 1                    // number of cpx_points in box
        slli a12, a12, 1                // number of box
        mov a14, a12
        addi a5, a5, -1
    bnez a5, start_ifft_radix2_stage
    /* ---------- Last three stages (ifft): same shape as forward tail ---------- */
    mov a9, a2      // load
    mov a10, a9        // store
    ee.vld.128.ip q0, a9, 16
    ee.vld.128.ip q1, a9, 16
    ee.vld.128.ip q2, a13, 16 // shared twiddle
    movi a14, 0
    loopnez a12, loop_end_radix2_ifft_last_third_stage
        ee.fft.r2bf.s16.st.incp q3, q0, q1, a10, 1                      // [a1 a2 a3 a4]
        ee.fft.cmul.s16.ld.xp q4, a13, a14, q5, q3, q2, 1                // [b1] q4- fake load
        ee.fft.cmul.s16.ld.xp q0, a9, a11, q5, q3, q2, 3                // [b2]
        ee.fft.cmul.s16.ld.xp q1, a9, a11, q5, q3, q2, 5                // [b3]
        ee.fft.cmul.s16.st.xp q3, q2, q5, a10, a11, 7, 0, 1                 // [b1 b2 b3 b4]
    loop_end_radix2_ifft_last_third_stage:
    mov a9, a2      // load
    mov a10, a9        // store
    mov a14, a2 // dummy for address / pipeline
    mov a3, a13
    ee.vld.128.ip q0, a9, 16
    ee.vld.128.ip q1, a9, 16
    ee.vld.h.64.ip q2, a13, 0
    loopnez a12, loop_end_radix2_ifft_last_second_stage
        ee.fft.r2bf.s16 q3, q4, q0, q1, 0          // [a1 a2 a3 a4]   q5-fake load
        ee.fft.cmul.s16.ld.xp q0, a9, a11, q5, q3, q2, 5                   // [b1]
        ee.fft.cmul.s16.st.xp q3, q2, q5, a10, a11, 7, 1, 1                 // [a1 a2 b1 b2]
        ee.fft.cmul.s16.ld.xp q1, a9, a11, q5, q4, q2, 5                // [b3]
        ee.fft.cmul.s16.st.xp q4, q2, q5, a10, a11, 7, 1, 1                 // [a3 a4 b3 b4]
    loop_end_radix2_ifft_last_second_stage:
    mov a9, a2      // load
    mov a10, a9        // store
    ee.vld.128.ip q0, a9, 16
    ee.vld.128.ip q1, a9, 16
    ee.vldbc.32 q2, a3
    loopnez a12, loop_end_radix2_ifft_last_stage
        ee.fft.r2bf.s16 q3, q4, q0, q1, 1          // [a1 a2 a3 a4]   q5-fake load
        ee.fft.cmul.s16.ld.xp q0, a9, a11, q5, q3, q2, 5                   // [b1]
        ee.fft.cmul.s16.st.xp q3, q2, q5, a10, a11, 7, 2, 1                 // [a1 a2 b1 b2]
        ee.fft.cmul.s16.ld.xp q1, a9, a11, q5, q4, q2, 5                // [b3]
        ee.fft.cmul.s16.st.xp q4, q2, q5, a10, a11, 7, 2, 1                 // [a3 a4 b3 b4]
    loop_end_radix2_ifft_last_stage:
    retw

/*
 * void fft_radix2_fftr_s16(int16_t *data, int16_t *twiddle_win2, int32_t cpx_points, int32_t shift);
 * After forward FFT: real-spectrum / conjugate-pack using Q14 cos/sin twiddle_win2 from fft_power2_q15.
 * a2=spectrum base; a9 high-half boundary; a8 walks backward through symmetric taps; plan allocates +1 cpx tail.
 */
    .text
    .align 4
    .global fft_radix2_fftr_s16
    .type   fft_radix2_fftr_s16, @function
fft_radix2_fftr_s16:
    /* Args: a2=data, a3=twiddle_win2, a4=cpx_points, a5=shift */
    /* Inner: a2 base; a6 store alias; a7=ptr1; a8=ptr_sym; a9=ptr_high; a4 halved for loop count */
    .align 4
    entry sp, 16
    ssr a5
    mov a6, a2
    addi a7, a2, 16  // ptr1 = data + 16 (second complex group)
    addx4 a9, a4, a2  // ptr_high = data + 4*cpx_points (bytes; 4 per complex)
    addi a8, a9, -16  // ptr_sym: walk backward inside symmetry block
    l16si a10, a2, 0  // DC / bin-1 real packing (matches reference PIE path)
    l16si a11, a2, 2
    add a13, a10, a11
    sub a11, a10, a11
    s16i a11, a9, 0
    movi a11, 0
    s16i a11, a9, 2
    addi a9, a9, -16
    ee.vld.128.ip q2, a3, 16
    ee.fft.ams.s16.ld.r32.decp q1, a8, q6, q7, q3, q4, q5, 0 // prefetch symmetry; some ops only need load side-effect
    ee.vld.128.ip q3, a2, 0
    movi a10, 4
    wur.sar_byte a10
    ee.vld.128.ip q4, a7, 16
    ee.src.q q0, q3, q4
    addi a10, a2, 16
    ee.fft.ams.s16.ld.incp.uaup q3, a10, q6, q7, q0, q1, q2, 0 // pipeline / alignment placeholder load
    srli a4, a4, 4
    loopnez a4, loop_end_fftr
        ee.fft.ams.s16.ld.incp.uaup q3, a7, q6, q7, q0, q1, q2, 0
        ee.fft.ams.s16.ld.incp q5, a3, q6, q7, q0, q1, q2, 0
        ee.fft.ams.s16.ld.r32.decp q4, a8, q6, q7, q0, q1, q2, 0
        ee.fft.ams.s16.st.incp q6, q7, a11, a6, q0, q1, q2, 0
        ee.fft.vst.r32.decp q7, a9, 1
        ee.fft.ams.s16.ld.incp.uaup q0, a7, q6, q7, q3, q4, q5, 0
        ee.fft.ams.s16.ld.incp q2, a3, q6, q7, q3, q4, q5, 0
        ee.fft.ams.s16.ld.r32.decp q1, a8, q6, q7, q3, q4, q5, 0
        ee.fft.ams.s16.st.incp q6, q7, a11, a6, q3, q4, q5, 0
        ee.fft.vst.r32.decp q7, a9, 1
    loop_end_fftr:
    s32i a13, a2, 0
    retw

/*
 * void fft_radix2_ffti_s16(int16_t *data, int16_t *twiddle_win2, int32_t cpx_points, int32_t shift);
 * Inverse of fftr: run before inverse FFT. AMS immediates use 1 where fftr uses 0 on the mirrored ops.
 */
    .text
    .align 4
    .global fft_radix2_ffti_s16
    .type   fft_radix2_ffti_s16, @function
fft_radix2_ffti_s16:
    /* Args: a2=data, a3=twiddle_win2, a4=cpx_points, a5=shift */
    /* Inner layout matches fftr; restores DC / first bins at the end */
    .align 4
    entry sp, 16
    ssr a5
    mov a6, a2
    addi a7, a2, 16  // ptr1 = data + 16
    addx4 a9, a4, a2  // ptr_high = data + 4*cpx_points
    addi a8, a9, -16  // ptr_sym
    l16si a10, a2, 0
    l16si a11, a9, 0  // paired load for inverse DC / edge bins
    add a13, a10, a11
    sub a12, a10, a11
    addi a9, a9, -16
    ee.vld.128.ip q2, a3, 16
    ee.fft.ams.s16.ld.r32.decp q1, a8, q6, q7, q3, q4, q5, 0 // prefetch symmetry; some ops only need load side-effect
    ee.vld.128.ip q3, a2, 0
    movi a10, 4
    wur.sar_byte a10
    ee.vld.128.ip q4, a7, 16
    ee.src.q q0, q3, q4
    addi a10, a2, 16
    ee.fft.ams.s16.ld.incp.uaup q3, a10, q6, q7, q0, q1, q2, 0 // pipeline / alignment placeholder load
    srli a4, a4, 4
    loopnez a4, loop_end_ffti
        ee.fft.ams.s16.ld.incp.uaup q3, a7, q6, q7, q0, q1, q2, 1
        ee.fft.ams.s16.ld.incp q5, a3, q6, q7, q0, q1, q2, 1
        ee.fft.ams.s16.ld.r32.decp q4, a8, q6, q7, q0, q1, q2, 1
        ee.fft.ams.s16.st.incp q6, q7, a11, a6, q0, q1, q2, 1
        ee.fft.vst.r32.decp q7, a9, 0
        ee.fft.ams.s16.ld.incp.uaup q0, a7, q6, q7, q3, q4, q5, 1
        ee.fft.ams.s16.ld.incp q2, a3, q6, q7, q3, q4, q5, 1
        ee.fft.ams.s16.ld.r32.decp q1, a8, q6, q7, q3, q4, q5, 1
        ee.fft.ams.s16.st.incp q6, q7, a11, a6, q3, q4, q5, 1
        ee.fft.vst.r32.decp q7, a9, 0
    loop_end_ffti:
    s16i a13, a2, 0
    s16i a12, a2, 2
    retw
