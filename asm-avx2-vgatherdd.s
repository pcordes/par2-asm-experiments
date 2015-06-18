testloop_align8:
	vzeroupper
#	push  %xmm7
#	push  %xmm6
	vpxor	%xmm7, %xmm7, %xmm7  # set to 0  # vzeroall is slow, and 64bit windows ABI has callee-saved xmm6-15
	vpcmpeqw %ymm6, %ymm6, %ymm6  # set to -1
	# %xmm8: vpand mask with only the low byte in each dword set

	# LH table in %rbp
###################### vpgatherdd 128bit, 64 src bits at a time
	.align	32
Lgather128:
	# generate a mask from count vs. alignment?
#	vmovdqa	0(%rsi), %ymm5
#	vmovdqu 0(%rsi), %xmm5
#	VBROADCASTI128 0(%rsi), %ymm5

	# or could do 16byte loads, and vpunpckhwd the 2nd round
	#vpunpcklwd 0(%rsi), %xmm7, %xmm5	# 4 src words, zero-padded into dwords.  (64b of src data)
	vpmovzxwd 0(%rsi), %xmm5		# 1 uop (fused), same port as punpck

	vpand	%xmm8, %xmm5, %xmm2	# L byte of 4 src words unpacked in %xmm2
	vpsrld	$8,    %xmm5, %xmm3	# H byte of 4 src words unpacked in %xmm3

	# could concat the index regs and do single gather, and unpack that?
	# Only if I add 0x400 / 4 to each index for the upper half
	vmovdqa	%xmm6, %xmm10
	VPGATHERDD %xmm10, 0x000(%rbp, %xmm2, 4), %xmm0

	vmovdqa	%xmm6, %xmm10
	VPGATHERDD %xmm10, 0x400(%rbp, %xmm3, 4), %xmm1

	vpxor	%xmm1, %xmm0, %xmm0
	vpackusdw %xmm0, %xmm0, %xmm0	# upper 64b is a repeat of lower 64, since same reg is both sources

	movq	0(%rdi), %xmm4


	# xmm1: lookup values from high byte of each src word
	# xmm0: lookup values from low  byte of each src word
#	pxor	0(%rdi), %xmm0
	vpxor	%xmm0, %xmm4, %xmm4
	movq	%xmm4, 0(%rdi)

	ja	Lgather128


#	pop  %xmm6
#	pop  %xmm7
	vzeroupper
	ret

################### 256bit vgather (128 src bits at a time)
testloop_align16:
	vzeroupper
	vpxor	%xmm7, %xmm7, %xmm7  # set to 0  # vzeroall is slow, and 64bit windows ABI has callee-saved xmm6-15
	vpcmpeqw %ymm6, %ymm6, %ymm6  # set to -1
	vpsrld	$24, %ymm6, %ymm8    # %ymm8: AND mask of 1s in the low byte of each dword
	# ymm13: vpshufb control mask that packs the LH lookup results into the right parts of the ymm

	# LH table in %rbp
	.align	32
Lgather256:

#	# 8xlow  bytes to look up, each in its own 32bit dword
#	vmovdqa	%ymm6, %ymm10
#	VPGATHERDD %ymm10, 0x000(%rbp, %ymm2, 4), %ymm0

	# 8xhigh bytes to look up
#	vmovdqa	%ymm6, %ymm10
#	VPGATHERDD %ymm10, 0x400(%rbp, %ymm3, 4), %ymm1


#	vmovdqa	0(%rsi), %ymm5
#	VBROADCASTI128 0(%rsi), %ymm5

#	vmovdqu 0(%rsi), %xmm5			# 16byte loads of src data
	# all 3 of these use p5
#	vpunpcklwd %xmm7, %xmm5, %xmm11
#	vpunpckhwd %xmm7, %xmm5, %xmm12
#	vinserti128 $1, %xmm12, %ymm11, %ymm5	# src words zero-padded to dwords in %ymm5
	vpmovzxwd 0(%rsi), %ymm5	# replaces unpck l/h + vinsert.  like unpckl, but data crosses lanes to stay in order


	vpand	%ymm8, %ymm5, %ymm2	# L byte of 8 src words unpacked in %ymm2
	# shifts use p0, pack/shuffle uses p5
	vpsrld	$8,    %ymm5, %ymm3	# H byte of 8 src words unpacked in %ymm3

	vmovdqa	%ymm6, %ymm10
	VPGATHERDD %ymm10, 0x000(%rbp, %ymm2, 4), %ymm0  # 34 uops, 12 cycles.  vs. 8 * 2uops for pinsrw??

	vmovdqa	%ymm6, %ymm10
	VPGATHERDD %ymm10, 0x400(%rbp, %ymm3, 4), %ymm1
	#VPGATHERDD %ymm10, 0x200(%rbp, %ymm3, 2), %ymm1  # non-padded lookup table, upper 16 has garbage

	# ymm0: lookup values from low  byte of each src word
	# ymm1: lookup values from high byte of each src word
	vpxor	%ymm1, %ymm0, %ymm0
	vpshufb	%ymm13, %ymm0, %ymm0
	# upper 128: results to low64.  lower128: results to low64.  rest: zeroed
	# vpackusdw %ymm0, %ymm0, %ymm0	# would do the same, except if LH lookups aren't 0-padded.  (p5, same as vpshufb)
	vpermq	$0x58, %ymm0, %ymm0  # result in %xmm0 (upper128 zeroed)
	# shuffle control: 0x58 = 0 + 2<<2 + 1<<4 + 1<<6

	vmovdqu	0(%rdi), %xmm4
#	vpxor	0(%rdi), %xmm0

	vpxor	%xmm0, %xmm4, %xmm0
	vmovdqu	%xmm0, 0(%rdi)

	ja Lgather256
	ret

########### 256b vpgatherdd, with 256b src/dest loads/stores, and 256b vpunpck
	.data
	.align 16
Lrs_shuf_masks:
	# load with broadcast to get upper 128 holding the same shuffle control mask
	# %ymm13: vpshufb control mask that packs the LH lookup results into the low half of the 128bit lane
	# low 16b of each 32b is valuable
	.byte 0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0c, 0x0d, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	# %ymm14: different shuffle control mask to put results in the high half of each lane
	# generate it from ymm13 by re-ordering.
	# .byte 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0c, 0x0d
.text

	.align 16
.globl rs_process_vgather_align32
rs_process_vgather_align32:
	# args: src=%rsi dst=%rdi LH=%rcx size(bytes)=%rdx

	push		%rbp
#	push		%rsi
#	push		%rdi
	push		%rbx

#	# LH table in %rbp
#	mov			%rcx, %rbp						# combined multiplication table
#	mov			%rdx, %rcx						# number of bytes to process (multiple of 32)

	vzeroupper  # vzeroall is slow, and 64bit windows ABI has callee-saved xmm6-15

	# Ways to use fewer regs for ia32:
	# reorder the unpack / gather / xor to reuse temporaries
	# more generally, reuse regs at the expense of limiting insn scheduling options
	# use 32-bit padded lookup tables so a packusdw (no mask reg) can replace a vpshufb of vgather results.
	#     or generate one mask from the other with a vpshufd (no lane crossing)
	#     or use shuffle control masks directly from mem
	# all-1s: generate on the fly?  Costs a uop (but no exec unit or latency) to copy it every time anyway.
	#   SnB has zero-latency pcmeqw x,same.  dunno about haswell

	# ymm1-4: vpgather inputs
	# ymm0-3: vpgather outputs
	# ymm4: dst
	# ymm5: source
	# ymm6: scratch (mask input to vpgather).
	# ymm7-8: ones/low-bytes-AND-mask
	# ymm9: unused
	# ymm10: unused (or all-zeroes)
	# ymm11-12: scratch: punpckl / h of src + zeroes
	# ymm13-14: vpshufb control masks
	# ymm15: unused
	vpcmpeqw %ymm7, %ymm7, %ymm7  # set to -1 (all-ones)
	# vpxor	%xmm10, %xmm10, %xmm10  # zeroed for punpck.  not needed
	vpsrld	$24, %ymm7, %ymm8    # %ymm8: AND mask of 1s in the low byte of each dword

	vbroadcasti128	Lrs_shuf_masks(%rip),	%ymm13
	#vbroadcasti128	Lrs_shuf_masks+0x10(%rip), %ymm14
	vpshufd		 $0x4e, %ymm13, %ymm14	# rotate the shuffle mask to pack results to the other end
	# 01 00 11 10 -> $0x4e: swap the upper/lower 64b

# IACA start
mov $111, %ebx
.byte 0x64, 0x67, 0x90

	.align 32
Lgather256full:
	vmovdqu 0(%rsi), %ymm5			# 32byte loads of src data
	## vpmovzxwd %ymm5, %ymm11  # nope, punpck is easier to repack later, without having to move data between lanes

	# vpunpckhwd %ymm10, %ymm5, %ymm12
	# src words unpacked to zero-padded dwords.  src order is 1L, 2L, 1H, 2H
	# 0-63:   low128 of %ymm11
	# 64-127: low128 of %ymm12
	# 128-191: hi128 of %ymm11
	# 196-255: hi128 of %ymm12
	vpunpcklwd %ymm5, %ymm5, %ymm11  # unpack with self, shift by 24 instead of 8 to get high word
	vpunpckhwd %ymm5, %ymm5, %ymm12

	vpsrld	$24,    %ymm11, %ymm3
	vpsrld	$24,    %ymm12, %ymm4	# H bytes of the src words, each byte in a zero-padded dword
	# shifts use p0, pack/unpck/shuffle/pinsr uses p5
	# 6uops 2*(unpck/shift+and) could be replaced with 4*vpshufb from %ymm5, but it can only run on one port
	# and would take more control mask regs
	vpand	%ymm8, %ymm11, %ymm1
	vpand	%ymm8, %ymm12, %ymm2	# L bytes of the src words

	# TODO: vpcmp?? to zero part of the mask for zero-bytes of src data?
	vmovdqa	%ymm7, %ymm6
	VPGATHERDD %ymm6, 0x000(%rcx, %ymm1, 4), %ymm0
	vmovdqa	%ymm7, %ymm6
	VPGATHERDD %ymm6, 0x000(%rcx, %ymm2, 4), %ymm1

	vmovdqa	%ymm7, %ymm6
	VPGATHERDD %ymm6, 0x400(%rcx, %ymm3, 4), %ymm2
	vmovdqa	%ymm7, %ymm6
	VPGATHERDD %ymm6, 0x400(%rcx, %ymm4, 4), %ymm3
	# or non-padded 16b lookup table, upper 16 has garbage

	# ymm0,1: lookup values from low  byte of each src word
	# ymm2,3: lookup values from high byte of each src word
	vpxor	%ymm2, %ymm0, %ymm0
	vpxor	%ymm3, %ymm1, %ymm1    # dest order is 0L, 1L, 0H, 1H.
	# AMD XOP: VPPERM: only works on 128b xmm regs?  else use it to pick and order the useful 256b from the concatenation of %ymm0, %ymm1

	# vpand mask, %ymm0, %ymm0	# only for 16b lookup tables: clear high16 above each result
	# vpslld  $16,  %ymm1, %ymm1
	# We could vpslld, vpor, vpshufb:  both lanes would need the same shuffle to re-order this + punpck
	# Would take an extra uop (vpand of ymm0) with 16b packed lookup tables though
	# vpor %ymm1, %ymm0, %ymm0

	# vpackusdw %ymm0, %ymm0, %ymm0	# same, assuming 0-padded LH tables. (p5, same as vpshufb)
	vpshufb	%ymm13, %ymm0, %ymm0	# results from punpckl half.  %ymm13: each lane: results to low64.  rest: zeroed
	vpshufb	%ymm14, %ymm1, %ymm1	# results from punpckh half.  %ymm14: each lane: results to high64  rest: zeroed
	vpor	%ymm1, %ymm0, %ymm0	# correctly-ordered results in %ymm0

	vmovdqu	0(%rdi), %ymm4
#	vpxor	0(%rdi), %xmm0

	vpxor	%ymm0, %ymm4, %ymm0
	vmovdqu	%ymm0, 0(%rdi)

	add $32, %rsi
	add $32, %rdi
	sub $32, %rdx
	jg Lgather256full

#IACA ;END_MARKER
mov $222, %ebx
.byte 0x64, 0x67, 0x90

	pop %rbx
	pop %rbp
	vzeroupper  # vzeroall is slow, and 64bit windows ABI has callee-saved xmm6-15
	ret

	.globl rs_process_end
rs_process_end:
