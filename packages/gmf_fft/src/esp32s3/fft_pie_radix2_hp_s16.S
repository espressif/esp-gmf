/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * ESP32-S3 HP butterflies: same dataflow as fft_pie_radix2_dit_s16.S, with
 * per-stage block floating point. Mirrors src/riscv_pie/fft_pie_radix2_hp_s16.S
 * (Xtensa ee.* / loopnez / ssr instead of RISC-V esp.* / lp.setup / movx.w.sar).
 *
 * Shift rule: if max_abs >= 8192 then SAR=15 and add-shift sel4=1, else SAR=14
 * and sel4=0. 8192 leaves headroom for |a-b| * W in 16-bit cmul. Extra bits stay
 * in the mantissa; the return value is the number of shifted stages.
 *
 * Last-3 tails keep the original gmf immediates except last-3 cmul sel4: upd4=0
 * does not apply an add-path >>1, so sel4 stays 0; SAR + r2bf sel4 carry BFP.
 *   last-3: r2bf.st.incp sel4 follows BFP; cmul.st.xp last imm always 0
 *   last-2: r2bf.s16 sel2=0; add-shift is cmul.st.xp last imm
 *   last-1: r2bf.s16 sel2=1; add-shift is cmul.st.xp last imm
 *
 * Hermitian pack/unpack is int32 (not AMS). AMS adds in 16 bits and wraps when
 * |sr|+|tr| exceeds int16; HP keeps extra BFP bits.
 */

/* max(|x[i]|) for a 16-byte-aligned int16 buffer; count is a multiple of 8.
 * Saturating negate maps 0x8000 to 0x7FFF. Horizontal tree via SAR_BYTE. */
.macro gmf_hp_max_abs data_ptr count result tmp
    srai            \count, \count, 3
    ee.zero.q       q2
    ee.vld.128.ip   q0, \data_ptr, 16
    ee.vsubs.s16    q1, q2, q0
    ee.vmax.s16     q0, q0, q1
    addi            \count, \count, -1
    loopgtz         \count, .Lmax_lp_end\@
        ee.vld.128.ip   q1, \data_ptr, 16
        ee.vsubs.s16    q3, q2, q1
        ee.vmax.s16     q1, q1, q3
        ee.vmax.s16     q0, q0, q1
.Lmax_lp_end\@:
    movi            \tmp, 8
    wur.sar_byte    \tmp
    ee.src.q        q1, q0, q0
    ee.vmax.s16     q0, q0, q1
    movi            \tmp, 4
    wur.sar_byte    \tmp
    ee.src.q        q1, q0, q0
    ee.vmax.s16     q0, q0, q1
    movi            \tmp, 2
    wur.sar_byte    \tmp
    ee.src.q        q1, q0, q0
    ee.vmax.s16     q0, q0, q1
    ee.movi.32.a    q0, \result, 0
    sext            \result, \result, 15
.endm

/* Choose SAR from max_abs of the current stage. Extra shift count lives in ACCX.
 * a2=data, a6=cpx in box, a12=nboxes. Sets a8 to 14 or 15 and restores a11=16.
 * Clobbers a7, a8, a9, q0-q3, SAR_BYTE. */
.macro gmf_hp_pick_sar
    mull            a7, a6, a12
    slli            a8, a7, 1
    mov             a7, a2
    gmf_hp_max_abs  a7, a8, a7, a9
    movi            a8, 15
    srli            a7, a7, 13
    bnez            a7, .Lps_done\@
    movi            a8, 14
.Lps_done\@:
    ssr             a8
    rur.accx_0      a7
    addi            a9, a8, -14
    add             a7, a7, a9
    wur.accx_0      a7
    movi            a11, 16
.endm

.macro gmf_hp_fft_lp sar4
    loopnez         a15, .Lhp_fft_lp_end\@
        ee.fft.r2bf.s16.st.incp q3, q0, q1, a10, \sar4
        ee.fft.cmul.s16.ld.xp   q4, a13, a11, q5, q3, q2, 0
        ee.fft.cmul.s16.ld.xp   q0, a9, a11, q5, q3, q2, 2
        ee.fft.cmul.s16.ld.xp   q1, a7, a11, q5, q3, q2, 4
        ee.fft.cmul.s16.st.xp   q3, q2, q5, a8, a11, 6, 0, \sar4
        ee.fft.r2bf.s16.st.incp q3, q0, q1, a10, \sar4
        ee.fft.cmul.s16.ld.xp   q2, a13, a11, q5, q3, q4, 0
        ee.fft.cmul.s16.ld.xp   q0, a9, a11, q5, q3, q4, 2
        ee.fft.cmul.s16.ld.xp   q1, a7, a11, q5, q3, q4, 4
        ee.fft.cmul.s16.st.xp   q3, q4, q5, a8, a11, 6, 0, \sar4
.Lhp_fft_lp_end\@:
.endm

.macro gmf_hp_ifft_lp sar4
    loopnez         a15, .Lhp_ifft_lp_end\@
        ee.fft.r2bf.s16.st.incp q3, q0, q1, a10, \sar4
        ee.fft.cmul.s16.ld.xp   q4, a13, a11, q5, q3, q2, 1
        ee.fft.cmul.s16.ld.xp   q0, a9, a11, q5, q3, q2, 3
        ee.fft.cmul.s16.ld.xp   q1, a7, a11, q5, q3, q2, 5
        ee.fft.cmul.s16.st.xp   q3, q2, q5, a8, a11, 7, 0, \sar4
        ee.fft.r2bf.s16.st.incp q3, q0, q1, a10, \sar4
        ee.fft.cmul.s16.ld.xp   q2, a13, a11, q5, q3, q4, 1
        ee.fft.cmul.s16.ld.xp   q0, a9, a11, q5, q3, q4, 3
        ee.fft.cmul.s16.ld.xp   q1, a7, a11, q5, q3, q4, 5
        ee.fft.cmul.s16.st.xp   q3, q4, q5, a8, a11, 7, 0, \sar4
.Lhp_ifft_lp_end\@:
.endm

/*
 * int32_t fft_radix2_fft_bf_s16_hp(int16_t *data, int16_t *win,
 *                                  int32_t log2n, int32_t cpx_points);
 */
    .text
    .align 4
    .global fft_radix2_fft_bf_s16_hp
    .type   fft_radix2_fft_bf_s16_hp, @function
fft_radix2_fft_bf_s16_hp:
    /* a2=data, a3=win, a4=log2n, a5=cpx_points */
    entry           sp, 16
    movi            a7, 0
    wur.accx_0      a7
    mov             a6, a5
    mov             a13, a3
    addi            a5, a4, -3
    movi            a12, 1
    mov             a14, a12
    srli            a15, a6, 4
    movi            a11, 16
    blti            a5, 1, .Lgmf_hp_fft_last3

.Lgmf_hp_fft_stage:
    gmf_hp_pick_sar
    bbsi            a8, 0, .Lgmf_hp_fft_boxes1
    mov             a4, a2
    mov             a3, a13
.Lgmf_hp_fft_box0:
    mov             a9, a4
    mov             a10, a9
    addx2           a7, a6, a4
    mov             a8, a7
    mov             a13, a3
    ee.vld.128.ip   q0, a9, 16
    ee.vld.128.ip   q1, a7, 16
    ee.vld.128.ip   q2, a13, 16
    gmf_hp_fft_lp   0
    addx4           a4, a6, a4
    addi            a14, a14, -1
    sub             a13, a13, a11
    bnez            a14, .Lgmf_hp_fft_box0
    j               .Lgmf_hp_fft_stage_next

.Lgmf_hp_fft_boxes1:
    mov             a4, a2
    mov             a3, a13
.Lgmf_hp_fft_box1:
    mov             a9, a4
    mov             a10, a9
    addx2           a7, a6, a4
    mov             a8, a7
    mov             a13, a3
    ee.vld.128.ip   q0, a9, 16
    ee.vld.128.ip   q1, a7, 16
    ee.vld.128.ip   q2, a13, 16
    gmf_hp_fft_lp   1
    addx4           a4, a6, a4
    addi            a14, a14, -1
    sub             a13, a13, a11
    bnez            a14, .Lgmf_hp_fft_box1

.Lgmf_hp_fft_stage_next:
    srli            a15, a15, 1
    srli            a6, a6, 1
    slli            a12, a12, 1
    mov             a14, a12
    addi            a5, a5, -1
    bnez            a5, .Lgmf_hp_fft_stage

.Lgmf_hp_fft_last3:
    gmf_hp_pick_sar
    movi            a14, 0
    mov             a9, a2
    mov             a10, a9
    ee.vld.128.ip   q0, a9, 16
    ee.vld.128.ip   q1, a9, 16
    ee.vld.128.ip   q2, a13, 16
    bbsi            a8, 0, .Lgmf_hp_fft_l3_1
    loopnez         a12, .Lgmf_hp_fft_l3_0e
        ee.fft.r2bf.s16.st.incp q3, q0, q1, a10, 0
        ee.fft.cmul.s16.ld.xp   q4, a13, a14, q5, q3, q2, 0
        ee.fft.cmul.s16.ld.xp   q0, a9, a11, q5, q3, q2, 2
        ee.fft.cmul.s16.ld.xp   q1, a9, a11, q5, q3, q2, 4
        ee.fft.cmul.s16.st.xp   q3, q2, q5, a10, a11, 6, 0, 0
.Lgmf_hp_fft_l3_0e:
    j               .Lgmf_hp_fft_l2
.Lgmf_hp_fft_l3_1:
    loopnez         a12, .Lgmf_hp_fft_l3_1e
        ee.fft.r2bf.s16.st.incp q3, q0, q1, a10, 1
        ee.fft.cmul.s16.ld.xp   q4, a13, a14, q5, q3, q2, 0
        ee.fft.cmul.s16.ld.xp   q0, a9, a11, q5, q3, q2, 2
        ee.fft.cmul.s16.ld.xp   q1, a9, a11, q5, q3, q2, 4
        ee.fft.cmul.s16.st.xp   q3, q2, q5, a10, a11, 6, 0, 0
.Lgmf_hp_fft_l3_1e:

.Lgmf_hp_fft_l2:
    gmf_hp_pick_sar
    mov             a9, a2
    mov             a10, a9
    mov             a3, a13
    ee.vld.128.ip   q0, a9, 16
    ee.vld.128.ip   q1, a9, 16
    ee.vld.h.64.ip  q2, a13, 0
    bbsi            a8, 0, .Lgmf_hp_fft_l2_1
    loopnez         a12, .Lgmf_hp_fft_l2_0e
        ee.fft.r2bf.s16         q3, q4, q0, q1, 0
        ee.fft.cmul.s16.ld.xp   q0, a9, a11, q5, q3, q2, 4
        ee.fft.cmul.s16.st.xp   q3, q2, q5, a10, a11, 6, 1, 0
        ee.fft.cmul.s16.ld.xp   q1, a9, a11, q5, q4, q2, 4
        ee.fft.cmul.s16.st.xp   q4, q2, q5, a10, a11, 6, 1, 0
.Lgmf_hp_fft_l2_0e:
    j               .Lgmf_hp_fft_l1
.Lgmf_hp_fft_l2_1:
    loopnez         a12, .Lgmf_hp_fft_l2_1e
        ee.fft.r2bf.s16         q3, q4, q0, q1, 0
        ee.fft.cmul.s16.ld.xp   q0, a9, a11, q5, q3, q2, 4
        ee.fft.cmul.s16.st.xp   q3, q2, q5, a10, a11, 6, 1, 1
        ee.fft.cmul.s16.ld.xp   q1, a9, a11, q5, q4, q2, 4
        ee.fft.cmul.s16.st.xp   q4, q2, q5, a10, a11, 6, 1, 1
.Lgmf_hp_fft_l2_1e:

.Lgmf_hp_fft_l1:
    gmf_hp_pick_sar
    mov             a9, a2
    mov             a10, a9
    ee.vld.128.ip   q0, a9, 16
    ee.vld.128.ip   q1, a9, 16
    ee.vldbc.32     q2, a3
    bbsi            a8, 0, .Lgmf_hp_fft_l1_1
    loopnez         a12, .Lgmf_hp_fft_l1_0e
        ee.fft.r2bf.s16         q3, q4, q0, q1, 1
        ee.fft.cmul.s16.ld.xp   q0, a9, a11, q5, q3, q2, 4
        ee.fft.cmul.s16.st.xp   q3, q2, q5, a10, a11, 6, 2, 0
        ee.fft.cmul.s16.ld.xp   q1, a9, a11, q5, q4, q2, 4
        ee.fft.cmul.s16.st.xp   q4, q2, q5, a10, a11, 6, 2, 0
.Lgmf_hp_fft_l1_0e:
    j               .Lgmf_hp_fft_done
.Lgmf_hp_fft_l1_1:
    loopnez         a12, .Lgmf_hp_fft_l1_1e
        ee.fft.r2bf.s16         q3, q4, q0, q1, 1
        ee.fft.cmul.s16.ld.xp   q0, a9, a11, q5, q3, q2, 4
        ee.fft.cmul.s16.st.xp   q3, q2, q5, a10, a11, 6, 2, 1
        ee.fft.cmul.s16.ld.xp   q1, a9, a11, q5, q4, q2, 4
        ee.fft.cmul.s16.st.xp   q4, q2, q5, a10, a11, 6, 2, 1
.Lgmf_hp_fft_l1_1e:

.Lgmf_hp_fft_done:
    rur.accx_0      a2
    retw

/*
 * int32_t fft_radix2_ifft_bf_s16_hp(int16_t *data, int16_t *win,
 *                                   int32_t log2n, int32_t cpx_points);
 */
    .text
    .align 4
    .global fft_radix2_ifft_bf_s16_hp
    .type   fft_radix2_ifft_bf_s16_hp, @function
fft_radix2_ifft_bf_s16_hp:
    /* a2=data, a3=win, a4=log2n, a5=cpx_points */
    entry           sp, 16
    movi            a7, 0
    wur.accx_0      a7
    mov             a6, a5
    mov             a13, a3
    addi            a5, a4, -3
    movi            a12, 1
    mov             a14, a12
    srli            a15, a6, 4
    movi            a11, 16
    blti            a5, 1, .Lgmf_hp_ifft_last3

.Lgmf_hp_ifft_stage:
    gmf_hp_pick_sar
    bbsi            a8, 0, .Lgmf_hp_ifft_boxes1
    mov             a4, a2
    mov             a3, a13
.Lgmf_hp_ifft_box0:
    mov             a9, a4
    mov             a10, a9
    addx2           a7, a6, a4
    mov             a8, a7
    mov             a13, a3
    ee.vld.128.ip   q0, a9, 16
    ee.vld.128.ip   q1, a7, 16
    ee.vld.128.ip   q2, a13, 16
    gmf_hp_ifft_lp  0
    addx4           a4, a6, a4
    addi            a14, a14, -1
    sub             a13, a13, a11
    bnez            a14, .Lgmf_hp_ifft_box0
    j               .Lgmf_hp_ifft_stage_next

.Lgmf_hp_ifft_boxes1:
    mov             a4, a2
    mov             a3, a13
.Lgmf_hp_ifft_box1:
    mov             a9, a4
    mov             a10, a9
    addx2           a7, a6, a4
    mov             a8, a7
    mov             a13, a3
    ee.vld.128.ip   q0, a9, 16
    ee.vld.128.ip   q1, a7, 16
    ee.vld.128.ip   q2, a13, 16
    gmf_hp_ifft_lp  1
    addx4           a4, a6, a4
    addi            a14, a14, -1
    sub             a13, a13, a11
    bnez            a14, .Lgmf_hp_ifft_box1

.Lgmf_hp_ifft_stage_next:
    srli            a15, a15, 1
    srli            a6, a6, 1
    slli            a12, a12, 1
    mov             a14, a12
    addi            a5, a5, -1
    bnez            a5, .Lgmf_hp_ifft_stage

.Lgmf_hp_ifft_last3:
    gmf_hp_pick_sar
    movi            a14, 0
    mov             a9, a2
    mov             a10, a9
    ee.vld.128.ip   q0, a9, 16
    ee.vld.128.ip   q1, a9, 16
    ee.vld.128.ip   q2, a13, 16
    bbsi            a8, 0, .Lgmf_hp_ifft_l3_1
    loopnez         a12, .Lgmf_hp_ifft_l3_0e
        ee.fft.r2bf.s16.st.incp q3, q0, q1, a10, 0
        ee.fft.cmul.s16.ld.xp   q4, a13, a14, q5, q3, q2, 1
        ee.fft.cmul.s16.ld.xp   q0, a9, a11, q5, q3, q2, 3
        ee.fft.cmul.s16.ld.xp   q1, a9, a11, q5, q3, q2, 5
        ee.fft.cmul.s16.st.xp   q3, q2, q5, a10, a11, 7, 0, 0
.Lgmf_hp_ifft_l3_0e:
    j               .Lgmf_hp_ifft_l2
.Lgmf_hp_ifft_l3_1:
    loopnez         a12, .Lgmf_hp_ifft_l3_1e
        ee.fft.r2bf.s16.st.incp q3, q0, q1, a10, 1
        ee.fft.cmul.s16.ld.xp   q4, a13, a14, q5, q3, q2, 1
        ee.fft.cmul.s16.ld.xp   q0, a9, a11, q5, q3, q2, 3
        ee.fft.cmul.s16.ld.xp   q1, a9, a11, q5, q3, q2, 5
        ee.fft.cmul.s16.st.xp   q3, q2, q5, a10, a11, 7, 0, 0
.Lgmf_hp_ifft_l3_1e:

.Lgmf_hp_ifft_l2:
    gmf_hp_pick_sar
    mov             a9, a2
    mov             a10, a9
    mov             a3, a13
    ee.vld.128.ip   q0, a9, 16
    ee.vld.128.ip   q1, a9, 16
    ee.vld.h.64.ip  q2, a13, 0
    bbsi            a8, 0, .Lgmf_hp_ifft_l2_1
    loopnez         a12, .Lgmf_hp_ifft_l2_0e
        ee.fft.r2bf.s16         q3, q4, q0, q1, 0
        ee.fft.cmul.s16.ld.xp   q0, a9, a11, q5, q3, q2, 5
        ee.fft.cmul.s16.st.xp   q3, q2, q5, a10, a11, 7, 1, 0
        ee.fft.cmul.s16.ld.xp   q1, a9, a11, q5, q4, q2, 5
        ee.fft.cmul.s16.st.xp   q4, q2, q5, a10, a11, 7, 1, 0
.Lgmf_hp_ifft_l2_0e:
    j               .Lgmf_hp_ifft_l1
.Lgmf_hp_ifft_l2_1:
    loopnez         a12, .Lgmf_hp_ifft_l2_1e
        ee.fft.r2bf.s16         q3, q4, q0, q1, 0
        ee.fft.cmul.s16.ld.xp   q0, a9, a11, q5, q3, q2, 5
        ee.fft.cmul.s16.st.xp   q3, q2, q5, a10, a11, 7, 1, 1
        ee.fft.cmul.s16.ld.xp   q1, a9, a11, q5, q4, q2, 5
        ee.fft.cmul.s16.st.xp   q4, q2, q5, a10, a11, 7, 1, 1
.Lgmf_hp_ifft_l2_1e:

.Lgmf_hp_ifft_l1:
    gmf_hp_pick_sar
    mov             a9, a2
    mov             a10, a9
    ee.vld.128.ip   q0, a9, 16
    ee.vld.128.ip   q1, a9, 16
    ee.vldbc.32     q2, a3
    bbsi            a8, 0, .Lgmf_hp_ifft_l1_1
    loopnez         a12, .Lgmf_hp_ifft_l1_0e
        ee.fft.r2bf.s16         q3, q4, q0, q1, 1
        ee.fft.cmul.s16.ld.xp   q0, a9, a11, q5, q3, q2, 5
        ee.fft.cmul.s16.st.xp   q3, q2, q5, a10, a11, 7, 2, 0
        ee.fft.cmul.s16.ld.xp   q1, a9, a11, q5, q4, q2, 5
        ee.fft.cmul.s16.st.xp   q4, q2, q5, a10, a11, 7, 2, 0
.Lgmf_hp_ifft_l1_0e:
    j               .Lgmf_hp_ifft_done
.Lgmf_hp_ifft_l1_1:
    loopnez         a12, .Lgmf_hp_ifft_l1_1e
        ee.fft.r2bf.s16         q3, q4, q0, q1, 1
        ee.fft.cmul.s16.ld.xp   q0, a9, a11, q5, q3, q2, 5
        ee.fft.cmul.s16.st.xp   q3, q2, q5, a10, a11, 7, 2, 1
        ee.fft.cmul.s16.ld.xp   q1, a9, a11, q5, q4, q2, 5
        ee.fft.cmul.s16.st.xp   q4, q2, q5, a10, a11, 7, 2, 1
.Lgmf_hp_ifft_l1_1e:

.Lgmf_hp_ifft_done:
    rur.accx_0      a2
    retw

/*
 * void fft_hp_fftr(int16_t *data, int16_t *win, int32_t cpx_points, int32_t shift);
 * void fft_hp_ffti(int16_t *data, int16_t *win, int32_t cpx_points, int32_t shift);
 *
 * Stack: 0=round, 4=sr, 8=si. a2=data, a3=win, a4=m, a5=shift, a7=p_w, a8=p_lo, a9=p_hi.
 */
.macro gmf_hp_fftr_once
    l16si           a10, a8, 0
    l16si           a11, a8, 2
    l16si           a12, a9, 0
    l16si           a13, a9, 2
    add             a14, a10, a12
    sub             a10, a10, a12
    sub             a12, a11, a13
    add             a11, a11, a13
    s32i            a14, sp, 4
    s32i            a12, sp, 8
    l16si           a12, a7, 0
    l16si           a13, a7, 2
    l32i            a14, sp, 0
    mull            a15, a10, a12
    mull            a6, a11, a13
    sub             a15, a15, a6
    add             a15, a15, a14
    sra             a15, a15
    mull            a6, a10, a13
    mull            a10, a11, a12
    add             a6, a6, a10
    add             a6, a6, a14
    sra             a6, a6
    l32i            a14, sp, 4
    l32i            a12, sp, 8
    add             a10, a14, a15
    addi            a10, a10, 1
    srai            a10, a10, 1
    clamps          a10, a10, 15
    s16i            a10, a8, 0
    add             a10, a12, a6
    addi            a10, a10, 1
    srai            a10, a10, 1
    clamps          a10, a10, 15
    s16i            a10, a8, 2
    sub             a10, a14, a15
    addi            a10, a10, 1
    srai            a10, a10, 1
    clamps          a10, a10, 15
    s16i            a10, a9, 0
    sub             a10, a6, a12
    addi            a10, a10, 1
    srai            a10, a10, 1
    clamps          a10, a10, 15
    s16i            a10, a9, 2
    addi            a8, a8, 4
    addi            a9, a9, -4
    addi            a7, a7, 4
.endm

.macro gmf_hp_ffti_once
    l16si           a10, a8, 0
    l16si           a11, a8, 2
    l16si           a12, a9, 0
    l16si           a13, a9, 2
    add             a14, a10, a12
    sub             a10, a10, a12
    sub             a12, a11, a13
    add             a11, a11, a13
    s32i            a14, sp, 4
    s32i            a12, sp, 8
    l16si           a12, a7, 0
    l16si           a13, a7, 2
    neg             a13, a13
    l32i            a14, sp, 0
    mull            a15, a10, a12
    mull            a6, a11, a13
    sub             a15, a15, a6
    add             a15, a15, a14
    sra             a15, a15
    mull            a6, a10, a13
    mull            a10, a11, a12
    add             a6, a6, a10
    add             a6, a6, a14
    sra             a6, a6
    l32i            a14, sp, 4
    l32i            a12, sp, 8
    add             a10, a14, a15
    clamps          a10, a10, 15
    s16i            a10, a8, 0
    add             a10, a12, a6
    clamps          a10, a10, 15
    s16i            a10, a8, 2
    sub             a10, a14, a15
    clamps          a10, a10, 15
    s16i            a10, a9, 0
    sub             a10, a12, a6
    neg             a10, a10
    clamps          a10, a10, 15
    s16i            a10, a9, 2
    addi            a8, a8, 4
    addi            a9, a9, -4
    addi            a7, a7, 4
.endm

    .text
    .align 4
    .global fft_hp_fftr
    .type   fft_hp_fftr, @function
fft_hp_fftr:
    /* a2=data, a3=win, a4=cpx_points, a5=shift */
    entry           sp, 32
    movi            a6, 1
    addi            a7, a5, -1
    ssl             a7
    sll             a6, a6
    s32i            a6, sp, 0
    ssr             a5

    l16si           a10, a2, 0
    l16si           a11, a2, 2
    add             a12, a10, a11
    sub             a11, a10, a11
    clamps          a12, a12, 15
    clamps          a11, a11, 15
    s16i            a12, a2, 0
    s16i            a11, a2, 2
    addx4           a10, a4, a2
    s16i            a11, a10, 0
    movi            a11, 0
    s16i            a11, a10, 2

    srli            a6, a4, 1
    addi            a6, a6, -1
    beqz            a6, .Lgmf_hp_fftr_mid
    addi            a8, a2, 4
    addi            a9, a4, -1
    slli            a9, a9, 2
    add             a9, a2, a9
    mov             a7, a3
    loopnez         a6, .Lgmf_hp_fftr_lp_e
        gmf_hp_fftr_once
.Lgmf_hp_fftr_lp_e:

.Lgmf_hp_fftr_mid:
    slli            a8, a4, 1
    add             a8, a2, a8
    l16si           a10, a8, 0
    l16si           a11, a8, 2
    addi            a9, a4, -2
    slli            a9, a9, 1
    add             a9, a3, a9
    l16si           a12, a9, 0
    l16si           a13, a9, 2
    l32i            a14, sp, 0
    add             a11, a11, a11
    /* tr = (0*wr - 2ai*wi + rnd) >> shift */
    mull            a15, a11, a13
    neg             a15, a15
    add             a15, a15, a14
    sra             a15, a15
    mull            a6, a11, a12
    add             a6, a6, a14
    sra             a6, a6
    add             a10, a10, a10
    add             a10, a10, a15
    addi            a10, a10, 1
    srai            a10, a10, 1
    clamps          a10, a10, 15
    addi            a6, a6, 1
    srai            a6, a6, 1
    clamps          a6, a6, 15
    s16i            a10, a8, 0
    s16i            a6, a8, 2
    retw

    .text
    .align 4
    .global fft_hp_ffti
    .type   fft_hp_ffti, @function
fft_hp_ffti:
    /* a2=data, a3=win, a4=cpx_points, a5=shift */
    entry           sp, 32
    movi            a6, 1
    addi            a7, a5, -1
    ssl             a7
    sll             a6, a6
    s32i            a6, sp, 0
    ssr             a5

    l16si           a10, a2, 0
    l16si           a11, a2, 2
    add             a12, a10, a11
    sub             a13, a10, a11
    clamps          a12, a12, 15
    clamps          a13, a13, 15
    s16i            a12, a2, 0
    s16i            a13, a2, 2

    srli            a6, a4, 1
    addi            a6, a6, -1
    beqz            a6, .Lgmf_hp_ffti_mid
    addi            a8, a2, 4
    addi            a9, a4, -1
    slli            a9, a9, 2
    add             a9, a2, a9
    mov             a7, a3
    loopnez         a6, .Lgmf_hp_ffti_lp_e
        gmf_hp_ffti_once
.Lgmf_hp_ffti_lp_e:

.Lgmf_hp_ffti_mid:
    slli            a8, a4, 1
    add             a8, a2, a8
    l16si           a10, a8, 0
    l16si           a11, a8, 2
    addi            a9, a4, -2
    slli            a9, a9, 1
    add             a9, a3, a9
    l16si           a12, a9, 0
    l16si           a13, a9, 2
    neg             a13, a13
    l32i            a14, sp, 0
    add             a11, a11, a11
    /* dr = (0*wr - 2xi*(-wi) + rnd) >> shift */
    mull            a15, a11, a13
    neg             a15, a15
    add             a15, a15, a14
    sra             a15, a15
    mull            a6, a11, a12
    add             a6, a6, a14
    sra             a6, a6
    add             a10, a10, a10
    add             a10, a10, a15
    clamps          a10, a10, 15
    clamps          a6, a6, 15
    s16i            a10, a8, 0
    s16i            a6, a8, 2
    retw

/* void fft_hp_scale_s16(int16_t *data, int32_t n, int32_t shl); */
    .section .iram1.text,"ax"
    .global fft_hp_scale_s16
    .type   fft_hp_scale_s16, @function
    .align  4
fft_hp_scale_s16:
    entry           sp, 32
    beqz            a4, .Lhp_scale_ret
    blti            a3, 1, .Lhp_scale_ret
    srli            a5, a3, 3
    beqz            a5, .Lhp_scale_ret
    bltz            a4, .Lhp_scale_shr

    /* Left: QACC = x * (1<<shl). Compute scale first (ssl clobbers SAR), then SAR=0. */
    movi            a7, 1
    ssl             a4
    sll             a7, a7
    s16i            a7, sp, 0
    addi            a8, sp, 0
    ee.vldbc.16.ip  q1, a8, 0
    movi            a8, 0
    wsr.sar         a8
    movi            a6, 0
.Lhp_scale_shl_lp:
    ee.zero.qacc
    ee.vld.128.ip   q0, a2, 16
    ee.vmulas.s16.qacc q0, q1
    addi            a8, a2, -16
    ee.srcmb.s16.qacc q2, a6, 0
    ee.vst.128.ip   q2, a8, 16
    addi            a5, a5, -1
    bnez            a5, .Lhp_scale_shl_lp
    retw

.Lhp_scale_shr:
    loopnez         a5, .Lhp_scale_shr_e
        ee.ldqa.s16.128.ip  a2, 0
        ee.srcmb.s16.qacc   q2, a4, 0
        ee.vst.128.ip       q2, a2, 16
.Lhp_scale_shr_e:
.Lhp_scale_ret:
    retw
