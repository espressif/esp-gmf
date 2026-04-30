/*
 * SPDX-FileCopyrightText: 2026 Contributors to gmf_fft
 *
 * SPDX-License-Identifier: Apache-2.0
 */

# -----------------------------------------------------------------------------
# ESP32-P4: radix-2 DIT Q15 FFT helpers (PIE vector instructions)
#
# Layout (matches fft_power2_q15.c):
#   - data: interleaved int16 [re0,im0, re1,im1, ...], length 2*N int16s; buffer must meet PIE alignment.
#   - C calls in plan order: fft_bf / bit_reverse, then fftr; inverse: ffti then ifft_bf, etc.
# Registers: RISC-V calling convention; each function documents args below. Some routines pair
#   entry save with tail `mv x10, x30` to restore a0 (data pointer).
# -----------------------------------------------------------------------------
# void fft_radix2_bit_reverse_s16(int16_t *data, int32_t cpx_points, int32_t log2N);
# Bit-reverse permute the spectrum by log2N bits; inner block touches 16 complex samples per pattern.
    .section .tcm.text
    .global fft_radix2_bit_reverse_s16
    .type   fft_radix2_bit_reverse_s16, @function
fft_radix2_bit_reverse_s16:
    .balign 4                                                                 # IALIGN: better for vector fetch
    .option norvc                                                             # no compressed insns (loop / PIE need fixed widths)
    # args: x10=data, x11=N complex points, x12=log2(N)
    srli  x11, x11, 3
    esp.movx.w.fft.bit.width  x12
    li  x28, 0
    mv  x15, x10          # index for store
    mv  x14, x10          # index for load
    esp.lp.setup  1, x11, (1f - 4)    # each loop switch 8 times(16 cpx)
        esp.fft.bitrev  q2, x28
        esp.vld.128.ip  q0, x15, 16
        esp.ldxq.32  q1, q2, x10, 0, 0
        esp.ldxq.32  q1, q2, x10, 1, 1
        esp.ldxq.32  q1, q2, x10, 2, 2
        esp.ldxq.32  q1, q2, x10, 3, 3
        esp.stxq.32  q0, q2, x10, 0, 0
        esp.stxq.32  q0, q2, x10, 1, 1
        esp.stxq.32  q0, q2, x10, 2, 2
        esp.stxq.32  q0, q2, x10, 3, 3
        esp.vst.128.ip  q1, x14, 16
        esp.vld.128.ip  q0, x15, 16
        esp.ldxq.32  q1, q2, x10, 0, 4
        esp.ldxq.32  q1, q2, x10, 1, 5
        esp.ldxq.32  q1, q2, x10, 2, 6
        esp.ldxq.32  q1, q2, x10, 3, 7
        esp.stxq.32  q0, q2, x10, 0, 4
        esp.stxq.32  q0, q2, x10, 1, 5
        esp.stxq.32  q0, q2, x10, 2, 6
        esp.stxq.32  q0, q2, x10, 3, 7
        esp.vst.128.ip  q1, x14, 16
        1:
    ret

# -----------------------------------------------------------------------------
# void fft_radix2_fft_bf_s16(int16_t *data, int16_t *twiddle_win, int32_t shift,
#                          int32_t num_stages, int32_t cpx_points);
# Multi-stage radix-2 butterflies (forward FFT). Twiddle layout matches twiddle_win from esp_gmf_fft_init.
# x12=arithmetic right-shift count (ssr), x13=total stages; subtract 3 then loop main + three tail butterfly blocks.
# x25=16 byte twiddle stride; x29=inner PIE iterations per box (related to complex count per box).
    .section .tcm.text
    .global fft_radix2_fft_bf_s16
    .type   fft_radix2_fft_bf_s16, @function
fft_radix2_fft_bf_s16:
    .balign 4
    .option norvc
    addi sp, sp, -24
    sw x8, 0(sp)
    sw x9, 4(sp)
    sw x24, 8(sp)
    sw x25, 12(sp)
    sw x26, 16(sp)
    sw x27, 20(sp)
    mv  x30, x15
    esp.movx.w.sar x12                                                        # fixed-point scaling (Q format)
    mv x27, x11                                                               # twiddle base for this stage
    addi x13, x13, -3                                                         # main loop runs (num_stages - 3) stages
    li x26, 1
    mv x28, x26                                                               # box count this stage
    srli x29, x14, 4                                                          # esp.lp.setup count per box ~ N_box/16
    li x25, 16 
    # twiddle stride 16 bytes per group
    start_fft_radix2_stage:                                                   # -------- outer stage loop --------
        mv  x12, x10
        mv  x11, x27           # start of current stage for twiddle factor
        start_fft_radix2_stage_box:
            mv  x9, x12 # load
            mv  x24, x9 # store
            slli  x15, x14, 1
            add   x15, x15, x12  #*ptr1 = data + n
            mv  x8, x15         # store ptr
            mv  x27, x11        # start for twiddle factor
            esp.vld.128.ip q0, x9, 16
            esp.vld.128.ip q1, x15, 16
            esp.vld.128.ip q2, x27, 16
            esp.lp.setup  1, x29, (1f - 4)
                esp.fft.r2bf.s16.st.incp q3, q0, q1, x24, 1
                esp.fft.cmul.s16.ld.xp q4, x27, x25, q5, q3, q2, 0
                esp.fft.cmul.s16.ld.xp q0, x9, x25, q5, q3, q2, 2
                esp.fft.cmul.s16.ld.xp q1, x15, x25, q5, q3, q2, 4
                esp.fft.cmul.s16.st.xp q3, q2, q5, x8, x25, 6, 0, 1
                esp.fft.r2bf.s16.st.incp q3, q0, q1, x24, 1
                esp.fft.cmul.s16.ld.xp q2, x27, x25, q5, q3, q4, 0
                esp.fft.cmul.s16.ld.xp q0, x9, x25, q5, q3, q4, 2
                esp.fft.cmul.s16.ld.xp q1, x15, x25, q5, q3, q4, 4
                esp.fft.cmul.s16.st.xp q3, q4, q5, x8, x25, 6, 0, 1
                1:
            slli  x5, x14, 2
            add   x12, x5, x12   # start index of box
            addi  x28, x28, -1
            sub x27, x27, x25
            bnez x28, start_fft_radix2_stage_box
        srli  x29, x29, 1    # iteration for bf /2
        srli  x14, x14, 1    # number of cpx_points in box
        slli  x26, x26, 1    # number of box
        mv  x28, x26
        addi  x13, x13, -1
        bnez x13, start_fft_radix2_stage
    # ---------- last three stages: specialized butterflies (twiddle/immediates differ from main loop) ----------
    mv x9, x10
    mv x24, x9
    esp.vld.128.ip q0, x9, 16
    esp.vld.128.ip q1, x9, 16
    esp.vld.128.ip q2, x27, 16                                               # shared twiddle for final block
    li x28, 0
    esp.lp.setup  1, x26, (1f - 4)
        esp.fft.r2bf.s16.st.incp q3, q0, q1, x24, 1          # [a1 x10 x11 x12]
        esp.fft.cmul.s16.ld.xp q4, x27, x28, q5, q3, q2, 0   # [b1] q4- fake load
        esp.fft.cmul.s16.ld.xp q0, x9, x25, q5, q3, q2, 2    # [b2]
        esp.fft.cmul.s16.ld.xp q1, x9, x25, q5, q3, q2, 4    # [b3]
        esp.fft.cmul.s16.st.xp q3, q2, q5, x24, x25, 6, 0, 1 # [b1 b2 b3 b4]
        1:
    mv x9, x10
    mv x24, x9
    mv x28, x10                                                               # dummy op for load/store address pipeline
    mv x11, x27
    esp.vld.128.ip q0, x9, 16
    esp.vld.128.ip q1, x9, 16
    esp.vld.h.64.ip q2, x27, 0
    esp.lp.setup  1, x26, (1f - 4)
        esp.fft.r2bf.s16 q3, q4, q0, q1, 0                   # [a1 x10 x11 x12]   q5-fake load
        esp.fft.cmul.s16.ld.xp q0, x9, x25, q5, q3, q2, 4    # [b1]
        esp.fft.cmul.s16.st.xp q3, q2, q5, x24, x25, 6, 1, 1 # [a1 x10 b1 b2]
        esp.fft.cmul.s16.ld.xp q1, x9, x25, q5, q4, q2, 4    # [b3]
        esp.fft.cmul.s16.st.xp q4, q2, q5, x24, x25, 6, 1, 1 # [x11 x12 b3 b4]
        1:
    mv x9, x10     # load
    mv x24, x9     # store
    esp.vld.128.ip q0, x9, 16
    esp.vld.128.ip q1, x9, 16
    esp.vldbc.32.ip q2, x11, 0
    esp.lp.setup  1, x26, (1f - 4)
        esp.fft.r2bf.s16 q3, q4, q0, q1, 1                   # [a1 x10 x11 x12]
        esp.fft.cmul.s16.ld.xp q0, x9, x25, q5, q3, q2, 4    # [b1]
        esp.fft.cmul.s16.st.xp q3, q2, q5, x24, x25, 6, 2, 1 # [a1 x10 b1 b2]
        esp.fft.cmul.s16.ld.xp q1, x9, x25, q5, q4, q2, 4    # [b3]
        esp.fft.cmul.s16.st.xp q4, q2, q5, x24, x25, 6, 2, 1 # [x11 x12 b3 b4]
        1:
    mv  x10, x30   
    lw x8, 0(sp)
    lw x9, 4(sp)
    lw x24, 8(sp)
    lw x25, 12(sp)
    lw x26, 16(sp)
    lw x27, 20(sp)
    addi sp, sp, 24
    ret

# -----------------------------------------------------------------------------
# void fft_radix2_ifft_bf_s16(same 5 args as fft_bf);
# Inverse butterflies: symmetric dataflow; esp.fft.cmul.s16.* immediates differ (often 1/3/5/7 vs forward 0/2/4/6).
# Main loop / box layout matches fft_bf; same three tail blocks.
    .section .tcm.text
    .global fft_radix2_ifft_bf_s16
    .type   fft_radix2_ifft_bf_s16, @function
fft_radix2_ifft_bf_s16:
    .balign 4
    .option norvc
    addi sp, sp, -24
    sw x8, 0(sp)
    sw x9, 4(sp)
    sw x24, 8(sp)
    sw x25, 12(sp)
    sw x26, 16(sp)
    sw x27, 20(sp)
    esp.movx.w.sar x12
    mv x27, x11
    addi x13, x13, -3           # number of stage - 3
    li x26, 1
    mv x28, x26                # number of box in current stage
    srli x29, x14, 4            # iteration times: number of bf in each box/8(each iter calculate 16 cpx_points = 8 bf)
    li x25, 16                # offset of bf
    start_ifft_radix2_stage:    # stage loop
        mv x12, x10
        mv x11, x27            # start of current stage for twiddle factor
        start_ifft_radix2_stage_box:
            mv x9, x12         # load
            mv x24, x9         # store
            slli  x15, x14, 1
            add   x15, x15, x12  #*ptr1 = data + n
            mv x8, x15         # store ptr
            mv x27, x11        # start for twiddle factor
            esp.vld.128.ip q0, x9, 16
            esp.vld.128.ip q1, x15, 16
            esp.vld.128.ip q2, x27, 16
            esp.lp.setup  1, x29, (1f - 4)
                esp.fft.r2bf.s16.st.incp q3, q0, q1, x24, 1          # [a1 x10 x11 x12]
                esp.fft.cmul.s16.ld.xp q4, x27, x25, q5, q3, q2, 1   # [b1]
                esp.fft.cmul.s16.ld.xp q0, x9, x25, q5, q3, q2, 3    # [b2]
                esp.fft.cmul.s16.ld.xp q1, x15, x25, q5, q3, q2, 5   # [b3]
                esp.fft.cmul.s16.st.xp q3, q2, q5, x8, x25, 7, 0, 1
                esp.fft.r2bf.s16.st.incp q3, q0, q1, x24, 1
                esp.fft.cmul.s16.ld.xp q2, x27, x25, q5, q3, q4, 1
                esp.fft.cmul.s16.ld.xp q0, x9, x25, q5, q3, q4, 3
                esp.fft.cmul.s16.ld.xp q1, x15, x25, q5, q3, q4, 5
                esp.fft.cmul.s16.st.xp q3, q4, q5, x8, x25, 7, 0, 1
                1:
            slli  x5, x14, 2
            add   x12, x5, x12   # start index of box
            addi x28, x28, -1
            sub x27, x27, x25
        bnez x28, start_ifft_radix2_stage_box
        srli x29, x29, 1    # iteration for bf /2
        srli x14, x14, 1    # number of cpx_points in box
        slli x26, x26, 1    # number of box
        mv x28, x26
        addi x13, x13, -1
    bnez x13, start_ifft_radix2_stage   #loop for stage
    mv x9, x10 # load
    mv x24, x9 # store
    esp.vld.128.ip q0, x9, 16
    esp.vld.128.ip q1, x9, 16
    esp.vld.128.ip q2, x27, 16   #fixed twiddle factor
    li x28, 0
    esp.lp.setup  1, x26, (1f - 4)
        esp.fft.r2bf.s16.st.incp q3, q0, q1, x24, 1          # [a1 x10 x11 x12]
        esp.fft.cmul.s16.ld.xp q4, x27, x28, q5, q3, q2, 1   # [b1] q4- fake load
        esp.fft.cmul.s16.ld.xp q0, x9, x25, q5, q3, q2, 3    # [b2]
        esp.fft.cmul.s16.ld.xp q1, x9, x25, q5, q3, q2, 5    # [b3]
        esp.fft.cmul.s16.st.xp q3, q2, q5, x24, x25, 7, 0, 1 # [b1 b2 b3 b4]
        1:
    mv x9, x10 # load
    mv x24, x9 # store
    mv x28, x10 #fake load
    mv x11, x27
    esp.vld.128.ip q0, x9, 16
    esp.vld.128.ip q1, x9, 16
    esp.vld.h.64.ip q2, x27, 0
    esp.lp.setup  1, x26, (1f - 4)
        esp.fft.r2bf.s16 q3, q4, q0, q1, 0                   # [a1 x10 x11 x12]   q5-fake load
        esp.fft.cmul.s16.ld.xp q0, x9, x25, q5, q3, q2, 5    # [b1]
        esp.fft.cmul.s16.st.xp q3, q2, q5, x24, x25, 7, 1, 1 # [a1 x10 b1 b2]
        esp.fft.cmul.s16.ld.xp q1, x9, x25, q5, q4, q2, 5    # [b3]
        esp.fft.cmul.s16.st.xp q4, q2, q5, x24, x25, 7, 1, 1 # [x11 x12 b3 b4]
        1:
    mv x9, x10 # load
    mv x24, x9 # store
    esp.vld.128.ip q0, x9, 16
    esp.vld.128.ip q1, x9, 16
    esp.vldbc.32.ip q2, x11, 0
    esp.lp.setup  1, x26, (1f - 4)
        esp.fft.r2bf.s16 q3, q4, q0, q1, 1                   # [a1 x10 x11 x12]   q5-fake load
        esp.fft.cmul.s16.ld.xp q0, x9, x25, q5, q3, q2, 5    # [b1]
        esp.fft.cmul.s16.st.xp q3, q2, q5, x24, x25, 7, 2, 1 # [a1 x10 b1 b2]
        esp.fft.cmul.s16.ld.xp q1, x9, x25, q5, q4, q2, 5    # [b3]
        esp.fft.cmul.s16.st.xp q4, q2, q5, x24, x25, 7, 2, 1 # [x11 x12 b3 b4]
        1:
    lw x8, 0(sp)
    lw x9, 4(sp)
    lw x24, 8(sp)
    lw x25, 12(sp)
    lw x26, 16(sp)
    lw x27, 20(sp)
    addi sp, sp, 24
    ret

# -----------------------------------------------------------------------------
# void fft_radix2_fftr_s16(int16_t *data, int16_t *twiddle_win2, int32_t cpx_points, int32_t shift);
# Post-forward-FFT real-spectrum / conjugate-pack step; twiddle_win2 is Q14 cos/sin from fft_power2_q15.
# x10= spectrum base; x9~high-half / symmetry boundary; x8 scans backward; buffer needs +1 complex tail (C plan ensures this).
    .section .tcm.text
    .global fft_radix2_fftr_s16
    .type   fft_radix2_fftr_s16, @function
fft_radix2_fftr_s16:
    .balign 4
    .option norvc
    addi sp, sp, -24
    sw x8, 0(sp)
    sw x9, 4(sp)
    sw x24, 8(sp)
    sw x25, 12(sp)
    sw x26, 16(sp)
    sw x27, 20(sp)
    mv  x30, x10                                                             # save a0 data; x14 becomes store base; tail mv x10,x30 restores a0
    esp.movx.w.sar x13
    mv x14, x10
    addi x15, x10, 16                                                         # ptr1 = data + 16 (second complex group)
    slli  x9, x12, 2                                                          # ptr_high = data + 4*N bytes (tail region)
    add   x9, x9, x10
    addi  x8, x9, -16                                                         # ptr_sym: walk backward through symmetry block
    lh    x24, 0(x10)                                                          # DC and bin-1 real packing
    lh    x25, 2(x10)
    add   x27, x24, x25
    sub   x25, x24, x25
    sh    x25, 0(x9)
    li    x25, 0
    sh    x25, 2(x9)
    addi  x9, x9, -16
    esp.vld.128.ip q2, x11, 16
    esp.fft.ams.s16.ld.r32.decp  q1, x8, q6, q7, q3, q4, q5, 0
    esp.vld.128.ip q3, x10, 0
    li    x24, 4
    esp.movx.w.sar.bytes x24
    esp.vld.128.ip q4, x15, 16
    esp.src.q q0, q3, q4
    addi x24, x10, 16
    esp.fft.ams.s16.ld.incp.uaup q3, x24, q6, q7, q0, q1, q2, 0  # pipeline / alignment placeholder load
    srli x12, x12, 4
    esp.lp.setup  1, x12, (1f - 4)
        esp.fft.ams.s16.ld.incp.uaup q3, x15, q6, q7, q0, q1, q2, 0
        esp.fft.ams.s16.ld.incp q5, x11, q6, q7, q0, q1, q2, 0
        esp.fft.ams.s16.ld.r32.decp q4, x8, q6, q7, q0, q1, q2, 0
        esp.fft.ams.s16.st.incp q6, q7, x25, x14, q0, q1, q2, 0
        esp.fft.vst.r32.decp q7, x9, 1
        esp.fft.ams.s16.ld.incp.uaup q0, x15, q6, q7, q3, q4, q5, 0
        esp.fft.ams.s16.ld.incp q2, x11, q6, q7, q3, q4, q5, 0
        esp.fft.ams.s16.ld.r32.decp q1, x8, q6, q7, q3, q4, q5, 0
        esp.fft.ams.s16.st.incp q6, q7, x25, x14, q3, q4, q5, 0
        esp.fft.vst.r32.decp q7, x9, 1
        1:
    sw    x27, 0(x10)
    mv    x10, x30
    lw x8, 0(sp)
    lw x9, 4(sp)
    lw x24, 8(sp)
    lw x25, 12(sp)
    lw x26, 16(sp)
    lw x27, 20(sp)
    addi sp, sp, 24
    ret

# -----------------------------------------------------------------------------
# void fft_radix2_ffti_s16(int16_t *data, int16_t *twiddle_win2, int32_t cpx_points, int32_t shift);
# Inverse of fftr: call before inverse FFT; AMS path mirrors fftr, some immediates use 1 where fftr uses 0.
    .section .tcm.text
    .global fft_radix2_ffti_s16
    .type   fft_radix2_ffti_s16, @function
fft_radix2_ffti_s16:
    .balign 4
    .option norvc
    addi sp, sp, -24
    sw x8, 0(sp)
    sw x9, 4(sp)
    sw x24, 8(sp)
    sw x25, 12(sp)
    sw x26, 16(sp)
    sw x27, 20(sp)
    esp.movx.w.sar x13
    mv x14, x10
    addi x15, x10, 16                                                         # same pointer layout as fftr
    slli  x9, x12, 2
    add   x9, x9, x10
    addi  x8, x9, -16
    lh    x24, 0(x10)                                                          # restore DC / first bins
    lh    x25, 0(x9)
    add   x27, x24, x25
    sub   x26, x24, x25
    addi  x9, x9, -16
    esp.vld.128.ip q2, x11, 16
    esp.fft.ams.s16.ld.r32.decp q1, x8, q6, q7, q3, q4, q5, 0
    esp.vld.128.ip q3, x10, 0
    li    x24, 4
    esp.movx.w.sar.bytes  x24
    esp.vld.128.ip q4, x15, 16
    esp.src.q q0, q3, q4
    addi x24, x10, 16
    esp.fft.ams.s16.ld.incp.uaup q3, x24, q6, q7, q0, q1, q2, 0               # placeholder load paired with fftr
    srli x12, x12, 4
    esp.lp.setup  1, x12, (1f - 4)
        esp.fft.ams.s16.ld.incp.uaup  q3, x15, q6, q7, q0, q1, q2, 1
        esp.fft.ams.s16.ld.incp  q5, x11, q6, q7, q0, q1, q2, 1
        esp.fft.ams.s16.ld.r32.decp  q4, x8, q6, q7, q0, q1, q2, 1
        esp.fft.ams.s16.st.incp  q6, q7, x25, x14, q0, q1, q2, 1
        esp.fft.vst.r32.decp  q7, x9, 0
        esp.fft.ams.s16.ld.incp.uaup  q0, x15, q6, q7, q3, q4, q5, 1
        esp.fft.ams.s16.ld.incp  q2, x11, q6, q7, q3, q4, q5, 1
        esp.fft.ams.s16.ld.r32.decp  q1, x8, q6, q7, q3, q4, q5, 1
        esp.fft.ams.s16.st.incp  q6, q7, x25, x14, q3, q4, q5, 1
        esp.fft.vst.r32.decp  q7, x9, 0
        1:
    sh    x27, 0(x10)
    sh    x26, 2(x10)
    lw x8, 0(sp)
    lw x9, 4(sp)
    lw x24, 8(sp)
    lw x25, 12(sp)
    lw x26, 16(sp)
    lw x27, 20(sp)
    addi sp, sp, 24
    ret
