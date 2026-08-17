/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 */

# -----------------------------------------------------------------------------
# gmf_fft HP butterflies: same dataflow as fft_radix2_fft_bf_s16 (this file's
# sibling), with per-stage block floating point.
#
# Shift rule: if max_abs >= 8192 then SAR=15 and add-shift sel4=1, else SAR=14
# and sel4=0. 8192 leaves headroom for |a-b| * W in 16-bit cmul (2*max). Extra
# bits stay in the mantissa;
# the return value is the number of shifted stages (for the exponent).
#
# Inner boxes use esp.lp.setup like the fixed-scale kernel. Last-3 tails keep
# the original gmf immediates except last-3 cmul sel4: upd4=0 does not apply
# an add-path >>1, so sel4 stays 0; SAR + r2bf sel4 carry the BFP shift.
#   last-3: r2bf.st.incp sel4 follows BFP; cmul.st.xp last imm always 0
#   last-2: r2bf.s16 sel2=0; add-shift is cmul.st.xp last imm
#   last-1: r2bf.s16 sel2=1; add-shift is cmul.st.xp last imm
# -----------------------------------------------------------------------------

    .text
    .option norvc

/* max(|x[i]|) for a 16-byte-aligned int16 buffer; count is a multiple of 8.
 * Probe the first 8 vectors (64 samples) and return as soon as max_abs
 * is known to be >= 8192. Skip-shift stages still scan the rest with
 * lp.setup. Horizontal max/min handles 0x8000. */
.macro gmf_hp_max_abs data_ptr count result tmp
    srli            \count, \count, 3
    esp.zero.q      q0
    esp.zero.q      q1
    li              \tmp, 8
    blt             \count, \tmp, .Lmax_rest\@
.Lmax_probe\@:
    esp.vld.128.ip  q2, \data_ptr, 16
    esp.vmax.s16    q0, q0, q2
    esp.vmin.s16    q1, q1, q2
    addi            \count, \count, -1
    addi            \tmp, \tmp, -1
    bnez            \tmp, .Lmax_probe\@
    esp.max.s16.a   q0, \result
    srli            \tmp, \result, 13
    bnez            \tmp, .Lmax_ok\@
    esp.min.s16.a   q1, \tmp
    neg             \tmp, \tmp
    srli            \tmp, \tmp, 13
    bnez            \tmp, .Lmax_ge\@
.Lmax_rest\@:
    beqz            \count, .Lmax_reduce\@
    esp.lp.setup    1, \count, (.Lmax_lp_end\@ - 4)
        esp.vld.128.ip  q2, \data_ptr, 16
        esp.vmax.s16    q0, q0, q2
        esp.vmin.s16    q1, q1, q2
.Lmax_lp_end\@:
.Lmax_reduce\@:
    esp.max.s16.a   q0, \result
    esp.min.s16.a   q1, \tmp
    neg             \tmp, \tmp
    blt             \result, \tmp, .Lmax_use_neg\@
    j               .Lmax_ok\@
.Lmax_ge\@:
    li              \result, 8192
    j               .Lmax_ok\@
.Lmax_use_neg\@:
    mv              \result, \tmp
.Lmax_ok\@:
.endm

/* Choose SAR from a carried |x| upper bound (x31). If the previous skip
 * left bound < 8192, this stage cannot overflow: skip the scan. After a
 * skip, bound *= 2 for the next stage; after a shift, bound is cleared. */
.macro gmf_hp_pick_sar
    li              x8, 8192
    beqz            x31, .Lps_scan\@
    bltu            x31, x8, .Lps_skip\@
.Lps_scan\@:
    mul             x15, x14, x26
    slli            x12, x15, 1
    mv              x15, x10
    gmf_hp_max_abs  x15, x12, x8, x9
    srli            x9, x8, 13
    bnez            x9, .Lps_shift\@
    mv              x31, x8
.Lps_skip\@:
    slli            x31, x31, 1
    li              x12, 14
    j               .Lps_done\@
.Lps_shift\@:
    li              x31, 0
    li              x12, 15
.Lps_done\@:
.endm

.macro gmf_hp_fft_lp sar4
    esp.lp.setup    1, x29, (.Lhp_fft_lp_end\@ - 4)
        esp.fft.r2bf.s16.st.incp q3, q0, q1, x24, \sar4
        esp.fft.cmul.s16.ld.xp   q4, x27, x25, q5, q3, q2, 0
        esp.fft.cmul.s16.ld.xp   q0, x9, x25, q5, q3, q2, 2
        esp.fft.cmul.s16.ld.xp   q1, x15, x25, q5, q3, q2, 4
        esp.fft.cmul.s16.st.xp   q3, q2, q5, x8, x25, 6, 0, \sar4
        esp.fft.r2bf.s16.st.incp q3, q0, q1, x24, \sar4
        esp.fft.cmul.s16.ld.xp   q2, x27, x25, q5, q3, q4, 0
        esp.fft.cmul.s16.ld.xp   q0, x9, x25, q5, q3, q4, 2
        esp.fft.cmul.s16.ld.xp   q1, x15, x25, q5, q3, q4, 4
        esp.fft.cmul.s16.st.xp   q3, q4, q5, x8, x25, 6, 0, \sar4
.Lhp_fft_lp_end\@:
.endm

.macro gmf_hp_ifft_lp sar4
    esp.lp.setup    1, x29, (.Lhp_ifft_lp_end\@ - 4)
        esp.fft.r2bf.s16.st.incp q3, q0, q1, x24, \sar4
        esp.fft.cmul.s16.ld.xp   q4, x27, x25, q5, q3, q2, 1
        esp.fft.cmul.s16.ld.xp   q0, x9, x25, q5, q3, q2, 3
        esp.fft.cmul.s16.ld.xp   q1, x15, x25, q5, q3, q2, 5
        esp.fft.cmul.s16.st.xp   q3, q2, q5, x8, x25, 7, 0, \sar4
        esp.fft.r2bf.s16.st.incp q3, q0, q1, x24, \sar4
        esp.fft.cmul.s16.ld.xp   q2, x27, x25, q5, q3, q4, 1
        esp.fft.cmul.s16.ld.xp   q0, x9, x25, q5, q3, q4, 3
        esp.fft.cmul.s16.ld.xp   q1, x15, x25, q5, q3, q4, 5
        esp.fft.cmul.s16.st.xp   q3, q4, q5, x8, x25, 7, 0, \sar4
.Lhp_ifft_lp_end\@:
.endm

    .section .text
    .global gmf_fft_pie_cfg_round
    .type   gmf_fft_pie_cfg_round, @function
gmf_fft_pie_cfg_round:
    .balign 4
    .option norvc
    esp.movx.r.cfg  x12
    li              x13, 0xffffff0f
    and             x12, x12, x13
    li              x13, 96
    or              x12, x12, x13
    esp.movx.w.cfg  x12
    ret

    .section .text
    .global fft_radix2_fft_bf_s16_hp
    .type   fft_radix2_fft_bf_s16_hp, @function
fft_radix2_fft_bf_s16_hp:
    .balign 4
    .option norvc
    addi            sp, sp, -32
    sw              x8, 0(sp)
    sw              x9, 4(sp)
    sw              x24, 8(sp)
    sw              x25, 12(sp)
    sw              x26, 16(sp)
    sw              x27, 20(sp)
    sw              x30, 24(sp)
    sw              x31, 28(sp)

    mv              x14, x13
    mv              x13, x12
    mv              x27, x11
    addi            x13, x13, -3
    li              x26, 1
    mv              x28, x26
    srli            x29, x14, 4
    li              x25, 16
    li              x30, 0
    li              x31, 0

    li              x5, 1
    blt             x13, x5, .Lgmf_hp_fft_last3

.Lgmf_hp_fft_stage:
    gmf_hp_pick_sar
    esp.movx.w.sar  x12
    addi            x9, x12, -14
    add             x30, x30, x9
    li              x25, 16
    andi            x8, x12, 1
    beqz            x8, .Lgmf_hp_fft_boxes0

    mv              x12, x10
    mv              x11, x27
.Lgmf_hp_fft_box1:
    mv              x9, x12
    mv              x24, x9
    slli            x15, x14, 1
    add             x15, x15, x12
    mv              x8, x15
    mv              x27, x11
    esp.vld.128.ip  q0, x9, 16
    esp.vld.128.ip  q1, x15, 16
    esp.vld.128.ip  q2, x27, 16
    gmf_hp_fft_lp   1
    slli            x5, x14, 2
    add             x12, x5, x12
    addi            x28, x28, -1
    sub             x27, x27, x25
    bnez            x28, .Lgmf_hp_fft_box1
    j               .Lgmf_hp_fft_stage_next

.Lgmf_hp_fft_boxes0:
    mv              x12, x10
    mv              x11, x27
.Lgmf_hp_fft_box0:
    mv              x9, x12
    mv              x24, x9
    slli            x15, x14, 1
    add             x15, x15, x12
    mv              x8, x15
    mv              x27, x11
    esp.vld.128.ip  q0, x9, 16
    esp.vld.128.ip  q1, x15, 16
    esp.vld.128.ip  q2, x27, 16
    gmf_hp_fft_lp   0
    slli            x5, x14, 2
    add             x12, x5, x12
    addi            x28, x28, -1
    sub             x27, x27, x25
    bnez            x28, .Lgmf_hp_fft_box0

.Lgmf_hp_fft_stage_next:
    srli            x29, x29, 1
    srli            x14, x14, 1
    slli            x26, x26, 1
    mv              x28, x26
    addi            x13, x13, -1
    bnez            x13, .Lgmf_hp_fft_stage

.Lgmf_hp_fft_last3:
    gmf_hp_pick_sar
    esp.movx.w.sar  x12
    addi            x9, x12, -14
    add             x30, x30, x9
    li              x25, 16
    li              x28, 0
    mv              x9, x10
    mv              x24, x9
    esp.vld.128.ip  q0, x9, 16
    esp.vld.128.ip  q1, x9, 16
    esp.vld.128.ip  q2, x27, 16
    andi            x8, x12, 1
    beqz            x8, .Lgmf_hp_fft_l3_0
    esp.lp.setup    1, x26, (.Lgmf_hp_fft_l3_1e - 4)
        esp.fft.r2bf.s16.st.incp q3, q0, q1, x24, 1
        esp.fft.cmul.s16.ld.xp   q4, x27, x28, q5, q3, q2, 0
        esp.fft.cmul.s16.ld.xp   q0, x9, x25, q5, q3, q2, 2
        esp.fft.cmul.s16.ld.xp   q1, x9, x25, q5, q3, q2, 4
        esp.fft.cmul.s16.st.xp   q3, q2, q5, x24, x25, 6, 0, 0
.Lgmf_hp_fft_l3_1e:
    j               .Lgmf_hp_fft_l2
.Lgmf_hp_fft_l3_0:
    esp.lp.setup    1, x26, (.Lgmf_hp_fft_l3_0e - 4)
        esp.fft.r2bf.s16.st.incp q3, q0, q1, x24, 0
        esp.fft.cmul.s16.ld.xp   q4, x27, x28, q5, q3, q2, 0
        esp.fft.cmul.s16.ld.xp   q0, x9, x25, q5, q3, q2, 2
        esp.fft.cmul.s16.ld.xp   q1, x9, x25, q5, q3, q2, 4
        esp.fft.cmul.s16.st.xp   q3, q2, q5, x24, x25, 6, 0, 0
.Lgmf_hp_fft_l3_0e:

.Lgmf_hp_fft_l2:
    gmf_hp_pick_sar
    esp.movx.w.sar  x12
    addi            x9, x12, -14
    add             x30, x30, x9
    li              x25, 16
    mv              x9, x10
    mv              x24, x9
    mv              x28, x10
    mv              x11, x27
    esp.vld.128.ip  q0, x9, 16
    esp.vld.128.ip  q1, x9, 16
    esp.vld.h.64.ip q2, x27, 0
    andi            x8, x12, 1
    beqz            x8, .Lgmf_hp_fft_l2_0
    esp.lp.setup    1, x26, (.Lgmf_hp_fft_l2_1e - 4)
        esp.fft.r2bf.s16         q3, q4, q0, q1, 0
        esp.fft.cmul.s16.ld.xp   q0, x9, x25, q5, q3, q2, 4
        esp.fft.cmul.s16.st.xp   q3, q2, q5, x24, x25, 6, 1, 1
        esp.fft.cmul.s16.ld.xp   q1, x9, x25, q5, q4, q2, 4
        esp.fft.cmul.s16.st.xp   q4, q2, q5, x24, x25, 6, 1, 1
.Lgmf_hp_fft_l2_1e:
    j               .Lgmf_hp_fft_l1
.Lgmf_hp_fft_l2_0:
    esp.lp.setup    1, x26, (.Lgmf_hp_fft_l2_0e - 4)
        esp.fft.r2bf.s16         q3, q4, q0, q1, 0
        esp.fft.cmul.s16.ld.xp   q0, x9, x25, q5, q3, q2, 4
        esp.fft.cmul.s16.st.xp   q3, q2, q5, x24, x25, 6, 1, 0
        esp.fft.cmul.s16.ld.xp   q1, x9, x25, q5, q4, q2, 4
        esp.fft.cmul.s16.st.xp   q4, q2, q5, x24, x25, 6, 1, 0
.Lgmf_hp_fft_l2_0e:

.Lgmf_hp_fft_l1:
    gmf_hp_pick_sar
    esp.movx.w.sar  x12
    addi            x9, x12, -14
    add             x30, x30, x9
    li              x25, 16
    mv              x9, x10
    mv              x24, x9
    esp.vld.128.ip  q0, x9, 16
    esp.vld.128.ip  q1, x9, 16
    esp.vldbc.32.ip q2, x11, 0
    andi            x8, x12, 1
    beqz            x8, .Lgmf_hp_fft_l1_0
    esp.lp.setup    1, x26, (.Lgmf_hp_fft_l1_1e - 4)
        esp.fft.r2bf.s16         q3, q4, q0, q1, 1
        esp.fft.cmul.s16.ld.xp   q0, x9, x25, q5, q3, q2, 4
        esp.fft.cmul.s16.st.xp   q3, q2, q5, x24, x25, 6, 2, 1
        esp.fft.cmul.s16.ld.xp   q1, x9, x25, q5, q4, q2, 4
        esp.fft.cmul.s16.st.xp   q4, q2, q5, x24, x25, 6, 2, 1
.Lgmf_hp_fft_l1_1e:
    j               .Lgmf_hp_fft_done
.Lgmf_hp_fft_l1_0:
    esp.lp.setup    1, x26, (.Lgmf_hp_fft_l1_0e - 4)
        esp.fft.r2bf.s16         q3, q4, q0, q1, 1
        esp.fft.cmul.s16.ld.xp   q0, x9, x25, q5, q3, q2, 4
        esp.fft.cmul.s16.st.xp   q3, q2, q5, x24, x25, 6, 2, 0
        esp.fft.cmul.s16.ld.xp   q1, x9, x25, q5, q4, q2, 4
        esp.fft.cmul.s16.st.xp   q4, q2, q5, x24, x25, 6, 2, 0
.Lgmf_hp_fft_l1_0e:

.Lgmf_hp_fft_done:
    mv              x10, x30
    lw              x8, 0(sp)
    lw              x9, 4(sp)
    lw              x24, 8(sp)
    lw              x25, 12(sp)
    lw              x26, 16(sp)
    lw              x27, 20(sp)
    lw              x30, 24(sp)
    lw              x31, 28(sp)
    addi            sp, sp, 32
    ret

    .section .iram1.text,"ax",@progbits
    .global fft_radix2_ifft_bf_s16_hp
    .type   fft_radix2_ifft_bf_s16_hp, @function
fft_radix2_ifft_bf_s16_hp:
    .balign 4
    .option norvc
    addi            sp, sp, -32
    sw              x8, 0(sp)
    sw              x9, 4(sp)
    sw              x24, 8(sp)
    sw              x25, 12(sp)
    sw              x26, 16(sp)
    sw              x27, 20(sp)
    sw              x30, 24(sp)
    sw              x31, 28(sp)

    mv              x14, x13
    mv              x13, x12
    mv              x27, x11
    addi            x13, x13, -3
    li              x26, 1
    mv              x28, x26
    srli            x29, x14, 4
    li              x25, 16
    li              x30, 0
    li              x31, 0

    li              x5, 1
    blt             x13, x5, .Lgmf_hp_ifft_last3

.Lgmf_hp_ifft_stage:
    gmf_hp_pick_sar
    esp.movx.w.sar  x12
    addi            x9, x12, -14
    add             x30, x30, x9
    li              x25, 16
    andi            x8, x12, 1
    beqz            x8, .Lgmf_hp_ifft_boxes0

    mv              x12, x10
    mv              x11, x27
.Lgmf_hp_ifft_box1:
    mv              x9, x12
    mv              x24, x9
    slli            x15, x14, 1
    add             x15, x15, x12
    mv              x8, x15
    mv              x27, x11
    esp.vld.128.ip  q0, x9, 16
    esp.vld.128.ip  q1, x15, 16
    esp.vld.128.ip  q2, x27, 16
    gmf_hp_ifft_lp  1
    slli            x5, x14, 2
    add             x12, x5, x12
    addi            x28, x28, -1
    sub             x27, x27, x25
    bnez            x28, .Lgmf_hp_ifft_box1
    j               .Lgmf_hp_ifft_stage_next

.Lgmf_hp_ifft_boxes0:
    mv              x12, x10
    mv              x11, x27
.Lgmf_hp_ifft_box0:
    mv              x9, x12
    mv              x24, x9
    slli            x15, x14, 1
    add             x15, x15, x12
    mv              x8, x15
    mv              x27, x11
    esp.vld.128.ip  q0, x9, 16
    esp.vld.128.ip  q1, x15, 16
    esp.vld.128.ip  q2, x27, 16
    gmf_hp_ifft_lp  0
    slli            x5, x14, 2
    add             x12, x5, x12
    addi            x28, x28, -1
    sub             x27, x27, x25
    bnez            x28, .Lgmf_hp_ifft_box0

.Lgmf_hp_ifft_stage_next:
    srli            x29, x29, 1
    srli            x14, x14, 1
    slli            x26, x26, 1
    mv              x28, x26
    addi            x13, x13, -1
    bnez            x13, .Lgmf_hp_ifft_stage

.Lgmf_hp_ifft_last3:
    gmf_hp_pick_sar
    esp.movx.w.sar  x12
    addi            x9, x12, -14
    add             x30, x30, x9
    li              x25, 16
    li              x28, 0
    mv              x9, x10
    mv              x24, x9
    esp.vld.128.ip  q0, x9, 16
    esp.vld.128.ip  q1, x9, 16
    esp.vld.128.ip  q2, x27, 16
    andi            x8, x12, 1
    beqz            x8, .Lgmf_hp_ifft_l3_0
    esp.lp.setup    1, x26, (.Lgmf_hp_ifft_l3_1e - 4)
        esp.fft.r2bf.s16.st.incp q3, q0, q1, x24, 1
        esp.fft.cmul.s16.ld.xp   q4, x27, x28, q5, q3, q2, 1
        esp.fft.cmul.s16.ld.xp   q0, x9, x25, q5, q3, q2, 3
        esp.fft.cmul.s16.ld.xp   q1, x9, x25, q5, q3, q2, 5
        esp.fft.cmul.s16.st.xp   q3, q2, q5, x24, x25, 7, 0, 0
.Lgmf_hp_ifft_l3_1e:
    j               .Lgmf_hp_ifft_l2
.Lgmf_hp_ifft_l3_0:
    esp.lp.setup    1, x26, (.Lgmf_hp_ifft_l3_0e - 4)
        esp.fft.r2bf.s16.st.incp q3, q0, q1, x24, 0
        esp.fft.cmul.s16.ld.xp   q4, x27, x28, q5, q3, q2, 1
        esp.fft.cmul.s16.ld.xp   q0, x9, x25, q5, q3, q2, 3
        esp.fft.cmul.s16.ld.xp   q1, x9, x25, q5, q3, q2, 5
        esp.fft.cmul.s16.st.xp   q3, q2, q5, x24, x25, 7, 0, 0
.Lgmf_hp_ifft_l3_0e:

.Lgmf_hp_ifft_l2:
    gmf_hp_pick_sar
    esp.movx.w.sar  x12
    addi            x9, x12, -14
    add             x30, x30, x9
    li              x25, 16
    mv              x9, x10
    mv              x24, x9
    mv              x28, x10
    mv              x11, x27
    esp.vld.128.ip  q0, x9, 16
    esp.vld.128.ip  q1, x9, 16
    esp.vld.h.64.ip q2, x27, 0
    andi            x8, x12, 1
    beqz            x8, .Lgmf_hp_ifft_l2_0
    esp.lp.setup    1, x26, (.Lgmf_hp_ifft_l2_1e - 4)
        esp.fft.r2bf.s16         q3, q4, q0, q1, 0
        esp.fft.cmul.s16.ld.xp   q0, x9, x25, q5, q3, q2, 5
        esp.fft.cmul.s16.st.xp   q3, q2, q5, x24, x25, 7, 1, 1
        esp.fft.cmul.s16.ld.xp   q1, x9, x25, q5, q4, q2, 5
        esp.fft.cmul.s16.st.xp   q4, q2, q5, x24, x25, 7, 1, 1
.Lgmf_hp_ifft_l2_1e:
    j               .Lgmf_hp_ifft_l1
.Lgmf_hp_ifft_l2_0:
    esp.lp.setup    1, x26, (.Lgmf_hp_ifft_l2_0e - 4)
        esp.fft.r2bf.s16         q3, q4, q0, q1, 0
        esp.fft.cmul.s16.ld.xp   q0, x9, x25, q5, q3, q2, 5
        esp.fft.cmul.s16.st.xp   q3, q2, q5, x24, x25, 7, 1, 0
        esp.fft.cmul.s16.ld.xp   q1, x9, x25, q5, q4, q2, 5
        esp.fft.cmul.s16.st.xp   q4, q2, q5, x24, x25, 7, 1, 0
.Lgmf_hp_ifft_l2_0e:

.Lgmf_hp_ifft_l1:
    gmf_hp_pick_sar
    esp.movx.w.sar  x12
    addi            x9, x12, -14
    add             x30, x30, x9
    li              x25, 16
    mv              x9, x10
    mv              x24, x9
    esp.vld.128.ip  q0, x9, 16
    esp.vld.128.ip  q1, x9, 16
    esp.vldbc.32.ip q2, x11, 0
    andi            x8, x12, 1
    beqz            x8, .Lgmf_hp_ifft_l1_0
    esp.lp.setup    1, x26, (.Lgmf_hp_ifft_l1_1e - 4)
        esp.fft.r2bf.s16         q3, q4, q0, q1, 1
        esp.fft.cmul.s16.ld.xp   q0, x9, x25, q5, q3, q2, 5
        esp.fft.cmul.s16.st.xp   q3, q2, q5, x24, x25, 7, 2, 1
        esp.fft.cmul.s16.ld.xp   q1, x9, x25, q5, q4, q2, 5
        esp.fft.cmul.s16.st.xp   q4, q2, q5, x24, x25, 7, 2, 1
.Lgmf_hp_ifft_l1_1e:
    j               .Lgmf_hp_ifft_done
.Lgmf_hp_ifft_l1_0:
    esp.lp.setup    1, x26, (.Lgmf_hp_ifft_l1_0e - 4)
        esp.fft.r2bf.s16         q3, q4, q0, q1, 1
        esp.fft.cmul.s16.ld.xp   q0, x9, x25, q5, q3, q2, 5
        esp.fft.cmul.s16.st.xp   q3, q2, q5, x24, x25, 7, 2, 0
        esp.fft.cmul.s16.ld.xp   q1, x9, x25, q5, q4, q2, 5
        esp.fft.cmul.s16.st.xp   q4, q2, q5, x24, x25, 7, 2, 0
.Lgmf_hp_ifft_l1_0e:

.Lgmf_hp_ifft_done:
    mv              x10, x30
    lw              x8, 0(sp)
    lw              x9, 4(sp)
    lw              x24, 8(sp)
    lw              x25, 12(sp)
    lw              x26, 16(sp)
    lw              x27, 20(sp)
    lw              x30, 24(sp)
    lw              x31, 28(sp)
    addi            sp, sp, 32
    ret

# int32_t gmf_fft_pie_prescale_if_full(int16_t *data, int32_t n_i16)
# Arithmetic >>1 when max_abs >= 8192 so PIE AMS fftr/ffti cannot wrap.
# n_i16 is a multiple of 8. Returns 1 if a shift was applied.
    .section .text
    .global gmf_fft_pie_prescale_if_full
    .type   gmf_fft_pie_prescale_if_full, @function
gmf_fft_pie_prescale_if_full:
    .balign 4
    .option norvc
    addi            sp, sp, -16
    sw              x8, 0(sp)
    sw              x9, 4(sp)
    sw              x24, 8(sp)
    mv              x24, x10
    mv              x15, x10
    mv              x12, x11
    gmf_hp_max_abs  x15, x12, x8, x9
    srli            x8, x8, 13
    beqz            x8, .Lhp_pre_none
    li              x9, 15
    esp.movx.w.sar  x9
    li              x9, 16384
    esp.zero.q      q7
    esp.movi.16.q   q7, x9, 0
    esp.movi.16.q   q7, x9, 1
    esp.movi.16.q   q7, x9, 2
    esp.movi.16.q   q7, x9, 3
    esp.movi.16.q   q7, x9, 4
    esp.movi.16.q   q7, x9, 5
    esp.movi.16.q   q7, x9, 6
    esp.movi.16.q   q7, x9, 7
    srli            x12, x11, 3
    mv              x14, x24
    mv              x8, x24
    esp.lp.setup    1, x12, (.Lhp_pre_sh_end - 4)
        esp.vld.128.ip  q0, x14, 16
        esp.vmul.s16    q1, q0, q7
        esp.vst.128.ip  q1, x8, 16
.Lhp_pre_sh_end:
    li              x10, 1
    j               .Lhp_pre_done
.Lhp_pre_none:
    li              x10, 0
.Lhp_pre_done:
    lw              x8, 0(sp)
    lw              x9, 4(sp)
    lw              x24, 8(sp)
    addi            sp, sp, 16
    ret

# -----------------------------------------------------------------------------
# int32 Hermitian pack/unpack (not PIE AMS). AMS adds in 16 bits and wraps
# when |sr|+|tr| exceeds int16; HP keeps extra BFP bits, so the add stays in
# 32-bit GPRs. Same rounding as the C pack: cmul round-to-nearest, fftr
# (sr+tr+1)>>1, ffti no extra >>1.
#
# void fft_hp_fftr(int16_t *data, int16_t *win, int32_t cpx_points, int32_t shift);
# void fft_hp_ffti(int16_t *data, int16_t *win, int32_t cpx_points, int32_t shift);
# -----------------------------------------------------------------------------
.macro gmf_hp_sat16 val tmp
    srai            \tmp, \val, 15
    addi            \tmp, \tmp, 1
    srli            \tmp, \tmp, 1
    beqz            \tmp, .Lsat_ok\@
    srai            \val, \val, 31
    li              \tmp, 32767
    xor             \val, \val, \tmp
.Lsat_ok\@:
.endm

# One Hermitian fftr bin. Pointers x8=lo, x9=hi, x18=win; x19=shift, x21=round.
.macro gmf_hp_fftr_once
    lh              x5, 0(x8)
    lh              x6, 2(x8)
    lh              x7, 0(x9)
    lh              x28, 2(x9)
    lh              x14, 0(x18)
    lh              x15, 2(x18)
    add             x16, x5, x7
    sub             x17, x5, x7
    sub             x5, x6, x28
    add             x6, x6, x28
    mul             x7, x17, x14
    mul             x28, x6, x15
    mul             x29, x17, x15
    mul             x30, x6, x14
    sub             x7, x7, x28
    add             x29, x29, x30
    add             x7, x7, x21
    add             x29, x29, x21
    sra             x7, x7, x19
    sra             x29, x29, x19
    add             x28, x16, x7
    addi            x28, x28, 1
    srai            x28, x28, 1
    sh              x28, 0(x8)
    add             x28, x5, x29
    addi            x28, x28, 1
    srai            x28, x28, 1
    sh              x28, 2(x8)
    sub             x28, x16, x7
    addi            x28, x28, 1
    srai            x28, x28, 1
    sh              x28, 0(x9)
    sub             x28, x29, x5
    addi            x28, x28, 1
    srai            x28, x28, 1
    sh              x28, 2(x9)
    addi            x8, x8, 4
    addi            x9, x9, -4
    addi            x18, x18, 4
.endm

# Four Hermitian ffti bins. CFG.mis_ld/mis_st must be set: pointers are
# 4-byte aligned. Hi 16-byte block is at original x9-12; x31 holds that
# address through the store. x8=lo, x9=hi, x15=win.
#
# Hi reverse [a,b,c,d] -> [d,c,b,a]: load 64-bit halves swapped so the
# QR is [c,d,a,b], then unzip.32/zip.32. Same permute on store via
# SRC.Q 64-bit half-swap (SAR_BYTE=8).
.macro gmf_hp_ffti_vec4
    esp.vld.128.ip  q0, x8, 16
    esp.zero.q      q1
    esp.vunzip.16   q0, q1
    addi            x31, x9, -4
    esp.vld.l.64.ip q2, x31, 0
    addi            x31, x9, -12
    esp.vld.h.64.ip q2, x31, 0
    esp.zero.q      q3
    esp.vunzip.32   q2, q3
    esp.vzip.32     q3, q2
    esp.zero.q      q2
    esp.vadd.s16    q2, q3, q2
    esp.zero.q      q3
    esp.vunzip.16   q2, q3
    esp.vld.128.ip  q4, x15, 16
    esp.zero.q      q5
    esp.vunzip.16   q4, q5
    esp.zero.q      q6
    esp.vsub.s16    q5, q6, q5
    esp.vst.128.ip  q5, x30, 0
    esp.movx.w.sar  x25
    esp.vmul.s32.s16xs16 q6, q7, q0, q4
    esp.vmul.s32.s16xs16 q5, q7, q2, q4
    esp.vsub.s32    q6, q6, q5
    esp.vld.128.ip  q5, x30, 0
    esp.vmul.s32.s16xs16 q5, q7, q1, q5
    esp.vsub.s32    q6, q6, q5
    esp.vld.128.ip  q5, x30, 0
    esp.vmul.s32.s16xs16 q5, q7, q3, q5
    esp.vsub.s32    q6, q6, q5
    addi            x14, x30, 16
    esp.vst.128.ip  q6, x14, 0
    esp.vld.128.ip  q5, x30, 0
    esp.vmul.s32.s16xs16 q6, q7, q0, q5
    esp.vmul.s32.s16xs16 q5, q7, q2, q5
    esp.vsub.s32    q6, q6, q5
    esp.vmul.s32.s16xs16 q5, q7, q1, q4
    esp.vadd.s32    q6, q6, q5
    esp.vmul.s32.s16xs16 q5, q7, q3, q4
    esp.vadd.s32    q6, q6, q5
    addi            x14, sp, 44
    esp.vldbc.32.ip q7, x14, 0
    esp.vadd.s32    q6, q6, q7
    addi            x14, x30, 16
    esp.vld.128.ip  q5, x14, 0
    esp.vadd.s32    q5, q5, q7
    mv              x14, x19
    esp.movx.w.sar  x14
    esp.vsr.s32     q6, q6
    esp.vsr.s32     q5, q5
    esp.vext.s16    q4, q7, q0
    esp.vext.s16    q7, q0, q2
    esp.vadd.s32    q0, q4, q7
    esp.vext.s16    q4, q7, q1
    esp.vext.s16    q7, q1, q3
    esp.vsub.s32    q1, q4, q7
    esp.vadd.s32    q4, q0, q5
    esp.vadd.s32    q7, q1, q6
    esp.vsub.s32    q0, q0, q5
    esp.vsub.s32    q1, q6, q1
    esp.vsat.s32    q4, q4, x24, x25
    esp.vsat.s32    q7, q7, x24, x25
    esp.vsat.s32    q0, q0, x24, x25
    esp.vsat.s32    q1, q1, x24, x25
    esp.zero.q      q5
    esp.vunzip.16   q4, q5
    esp.zero.q      q5
    esp.vunzip.16   q7, q5
    esp.vzip.16     q4, q7
    addi            x14, x8, -16
    esp.vst.128.ip  q4, x14, 0
    esp.zero.q      q5
    esp.vunzip.16   q0, q5
    esp.zero.q      q5
    esp.vunzip.16   q1, q5
    esp.vzip.16     q0, q1
    li              x14, 8
    esp.movx.w.sar.bytes x14
    esp.src.q       q0, q0, q0
    li              x14, 0
    esp.movx.w.sar.bytes x14
    esp.zero.q      q5
    esp.vunzip.32   q0, q5
    esp.vzip.32     q5, q0
    esp.vst.128.ip  q5, x31, 0
    addi            x9, x9, -16
.endm

# One Hermitian ffti bin. x24=32767, x25=-32768 for ESP.SAT (PIE AR only).
.macro gmf_hp_ffti_once
    lh              x5, 0(x8)
    lh              x6, 2(x8)
    lh              x7, 0(x9)
    lh              x28, 2(x9)
    lh              x14, 0(x18)
    lh              x15, 2(x18)
    add             x16, x5, x7
    sub             x17, x5, x7
    sub             x5, x6, x28
    add             x6, x6, x28
    neg             x15, x15
    mul             x7, x17, x14
    mul             x28, x6, x15
    mul             x29, x17, x15
    mul             x30, x6, x14
    sub             x7, x7, x28
    add             x29, x29, x30
    add             x7, x7, x21
    add             x29, x29, x21
    sra             x7, x7, x19
    sra             x29, x29, x19
    add             x28, x16, x7
    esp.sat         x28, x24, x25
    sh              x28, 0(x8)
    add             x28, x5, x29
    esp.sat         x28, x24, x25
    sh              x28, 2(x8)
    sub             x28, x16, x7
    esp.sat         x28, x24, x25
    sh              x28, 0(x9)
    sub             x28, x5, x29
    neg             x28, x28
    esp.sat         x28, x24, x25
    sh              x28, 2(x9)
    addi            x8, x8, 4
    addi            x9, x9, -4
    addi            x18, x18, 4
.endm

    .section .text
    .global fft_hp_fftr
    .type   fft_hp_fftr, @function
fft_hp_fftr:
    .balign 4
    .option norvc
    addi            sp, sp, -32
    sw              x8, 0(sp)
    sw              x9, 4(sp)
    sw              x18, 8(sp)
    sw              x19, 12(sp)
    sw              x20, 16(sp)
    sw              x21, 20(sp)

    mv              x19, x13
    li              x21, 1
    sll             x21, x21, x19
    srai            x21, x21, 1

    lh              x5, 0(x10)
    lh              x6, 2(x10)
    add             x7, x5, x6
    sub             x28, x5, x6
    gmf_hp_sat16    x7, x29
    gmf_hp_sat16    x28, x29
    sh              x7, 0(x10)
    sh              x28, 2(x10)
    slli            x29, x12, 2
    add             x29, x10, x29
    sh              x28, 0(x29)
    sh              x0, 2(x29)

    srli            x20, x12, 1
    addi            x20, x20, -1
    blez            x20, .Lhp_fftr_mid

    addi            x8, x10, 4
    addi            x9, x12, -1
    slli            x9, x9, 2
    add             x9, x10, x9
    mv              x18, x11
    andi            x31, x20, 1
    srli            x20, x20, 1
    beqz            x20, .Lhp_fftr_odd
    esp.lp.setup    1, x20, (.Lhp_fftr_lp_e - 4)
        gmf_hp_fftr_once
        gmf_hp_fftr_once
.Lhp_fftr_lp_e:

.Lhp_fftr_odd:
    beqz            x31, .Lhp_fftr_mid
    gmf_hp_fftr_once

.Lhp_fftr_mid:
    slli            x8, x12, 1
    add             x8, x10, x8
    lh              x5, 0(x8)
    lh              x6, 2(x8)
    addi            x9, x12, -2
    slli            x9, x9, 1
    add             x9, x11, x9
    lh              x14, 0(x9)
    lh              x15, 2(x9)
    add             x6, x6, x6
    mul             x7, x6, x15
    mul             x29, x6, x14
    sub             x7, zero, x7
    add             x7, x7, x21
    add             x29, x29, x21
    sra             x7, x7, x19
    sra             x29, x29, x19
    add             x5, x5, x5
    add             x5, x5, x7
    addi            x5, x5, 1
    srai            x5, x5, 1
    addi            x29, x29, 1
    srai            x29, x29, 1
    sh              x5, 0(x8)
    sh              x29, 2(x8)

    lw              x8, 0(sp)
    lw              x9, 4(sp)
    lw              x18, 8(sp)
    lw              x19, 12(sp)
    lw              x20, 16(sp)
    lw              x21, 20(sp)
    addi            sp, sp, 32
    ret

    .section .iram1.text,"ax",@progbits
    .global fft_hp_ffti
    .type   fft_hp_ffti, @function
fft_hp_ffti:
    .balign 4
    .option norvc
    addi            sp, sp, -96
    sw              x8, 0(sp)
    sw              x9, 4(sp)
    sw              x18, 8(sp)
    sw              x19, 12(sp)
    sw              x20, 16(sp)
    sw              x21, 20(sp)
    sw              x24, 24(sp)
    sw              x25, 28(sp)

    mv              x19, x13
    li              x21, 1
    sll             x21, x21, x19
    srai            x21, x21, 1
    li              x24, 32767
    li              x25, -32768
    sw              x21, 44(sp)
    addi            x30, sp, 48
    sw              x26, 32(sp)
    sw              x27, 36(sp)
    esp.movx.r.cfg  x14
    sw              x14, 80(sp)
    li              x26, 0xffffff0c
    and             x14, x14, x26
    ori             x14, x14, 3
    esp.movx.w.cfg  x14

    lh              x5, 0(x10)
    lh              x6, 2(x10)
    add             x28, x5, x6
    sub             x29, x5, x6
    esp.sat         x28, x24, x25
    esp.sat         x29, x24, x25
    sh              x28, 0(x10)
    sh              x29, 2(x10)

    srli            x20, x12, 1
    addi            x20, x20, -1
    blez            x20, .Lhp_ffti_mid

    addi            x8, x10, 4
    addi            x9, x12, -1
    slli            x9, x9, 2
    add             x9, x10, x9
    mv              x18, x11
    mv              x15, x11
    andi            x31, x20, 3
    srli            x20, x20, 2
    sw              x31, 40(sp)
    beqz            x20, .Lhp_ffti_rem
    esp.lp.setup    1, x20, (.Lhp_ffti_lp4e - 4)
        gmf_hp_ffti_vec4
.Lhp_ffti_lp4e:
    mv              x18, x15

.Lhp_ffti_rem:
    lw              x20, 40(sp)
    beqz            x20, .Lhp_ffti_mid
.Lhp_ffti_rem_lp:
    gmf_hp_ffti_once
    addi            x20, x20, -1
    bnez            x20, .Lhp_ffti_rem_lp

.Lhp_ffti_mid:
    slli            x8, x12, 1
    add             x8, x10, x8
    lh              x5, 0(x8)
    lh              x6, 2(x8)
    addi            x9, x12, -2
    slli            x9, x9, 1
    add             x9, x11, x9
    lh              x14, 0(x9)
    lh              x15, 2(x9)
    neg             x15, x15
    add             x6, x6, x6
    mul             x7, x6, x15
    mul             x29, x6, x14
    sub             x7, zero, x7
    add             x7, x7, x21
    add             x29, x29, x21
    sra             x7, x7, x19
    sra             x29, x29, x19
    add             x28, x5, x5
    add             x28, x28, x7
    esp.sat         x28, x24, x25
    esp.sat         x29, x24, x25
    sh              x28, 0(x8)
    sh              x29, 2(x8)

    lw              x14, 80(sp)
    esp.movx.w.cfg  x14
    lw              x8, 0(sp)
    lw              x9, 4(sp)
    lw              x18, 8(sp)
    lw              x19, 12(sp)
    lw              x20, 16(sp)
    lw              x21, 20(sp)
    lw              x24, 24(sp)
    lw              x25, 28(sp)
    lw              x26, 32(sp)
    lw              x27, 36(sp)
    addi            sp, sp, 96
    ret

# Saturating int16 shift: shl>0 left (x * 2^shl, sat to s16), shl<0 right.
# void fft_hp_scale_s16(int16_t *data, int32_t n, int32_t shl);
    .section .iram1.text,"ax",@progbits
    .global fft_hp_scale_s16
    .type   fft_hp_scale_s16, @function
fft_hp_scale_s16:
    .balign 4
    .option norvc
    beqz            a2, .Lhp_scale_ret
    blez            a1, .Lhp_scale_ret
    mv              t3, a0
    srli            t4, a1, 3
    beqz            t4, .Lhp_scale_ret
    blt             a2, zero, .Lhp_scale_shr

    /* Left: QACC = x * (1<<shl), pack with shift 0. SAR must be 0 for vmulas. */
    li              t6, 0
    esp.movx.w.sar  t6
    li              a3, 1
    sll             a3, a3, a2
    esp.zero.q      q1
    esp.movi.16.q   q1, a3, 0
    esp.movi.16.q   q1, a3, 1
    esp.movi.16.q   q1, a3, 2
    esp.movi.16.q   q1, a3, 3
    esp.movi.16.q   q1, a3, 4
    esp.movi.16.q   q1, a3, 5
    esp.movi.16.q   q1, a3, 6
    esp.movi.16.q   q1, a3, 7
    li              a4, 0
.Lhp_scale_shl_lp:
    mv              t5, t3
    esp.zero.qacc
    esp.vld.128.ip  q0, t3, 16
    esp.vmulas.s16.qacc q0, q1
    esp.srcmb.s16.qacc q2, a4, 0
    esp.vst.128.ip  q2, t5, 16
    addi            t4, t4, -1
    bnez            t4, .Lhp_scale_shl_lp
    ret

.Lhp_scale_shr:
    /* Right: ldqa sign-extends to QACC; srcmb with negative shl arithmetic-shifts. */
    esp.lp.setup    1, t4, (.Lhp_scale_shr_e - 4)
        esp.ldqa.s16.128.ip t3, 0
        esp.srcmb.s16.qacc  q2, a2, 0
        esp.vst.128.ip      q2, t3, 16
.Lhp_scale_shr_e:
.Lhp_scale_ret:
    ret

