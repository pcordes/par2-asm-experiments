	.align 32
	.globl rs_process_x86_64_mmx
	.text
rs_process_x86_64_mmx:
	# LUT loads offset by 2 bytes to save a shift:
	# Much slower than orig with -a 0  (1.4 vs 1.1), because many loads cross into the previous cache line
	# (useless) Much better than orig mmx with -a 62 LH alignment.  2.8 vs 1.4.    (vs. 1.08 for pinsrw128)
	#  because half our loads are from 60, not 62, so they don't cross the cache line
	# (useless) Slightly faster with -a 63:  27 vs 25.
	# On uniform distribution of src bytes, it's maybe a SLIGHT speedup on SnB
#
	push		%rbp
#	push		%rsi
#	push		%rdi
	push		%rbx
	#r8-11 can be modified

	mov			%rcx, %rbp						# combined multiplication table
	mov			%rdx, %r8						# number of bytes to process (multiple of 16)

	movq			(%rsi), %rdx					# load 1st 8 source bytes
	movq			8(%rsi), %rcx					# load 2nd 8 source bytes

	sub			$16, %r8						# reduce # of loop iterations by 1
	jz			last8
	add			%r8, %rsi						# point to last set of 8-bytes of input
	add			%r8, %rdi						# point to last set of 8-bytes of output
	neg			%r8							# convert byte size to count-up


# %rdi		# destination (function arg)
# %rsi		# source  (function arg)
# rbp: lookup table

# eax: scratch (holds %dl)
# ebx: scratch (holds %dh)

# r8: -count, counts upward to 0.
# rdx / rcx: src data words [0..3] and [4..7]
# two parallel dependency chains.  very small speedup over by-8 punpck version, if any.

# mm0-3: src 0..3
# mm4-7: src 4..7
	.align	32
loop:
#IACA
#mov ebx, 111
#db 0x64, 0x67, 0x90
mov $111, %ebx
.byte 0x64, 0x67, 0x90
	movzx		%dl, %eax
	movzx		%dh, %ebx
	movd		0x0000(%rbp, %rax, 4), %mm0
	 movzx		%cl, %eax
	shr			$16, %rdx
	movd		0x0400(%rbp, %rbx, 4), %mm1

	 movzx		%ch, %ebx
	 movd		0x0000(%rbp, %rax, 4), %mm4
	movzx		%dl, %eax
	 shr			$16, %rcx
	 movd		0x0400(%rbp, %rbx, 4), %mm5

	movzx		%dh, %ebx
	movd		0x0000-2(%rbp, %rax, 4), %mm2
	 movzx		%cl, %eax
	shr			$16, %rdx
	movd		0x0400-2(%rbp, %rbx, 4), %mm3

	 movzx		%ch, %ebx
	 movd		0x0000-2(%rbp, %rax, 4), %mm6
	movzx		%dl, %eax
	 shr			$16, %rcx
	 movd		0x0400-2(%rbp, %rbx, 4), %mm7

	movzx		%dh, %ebx
	punpckldq	0x0000(%rbp, %rax, 4), %mm0
	 movzx		%cl, %eax
	shr			$16, %rdx
	punpckldq	0x0400(%rbp, %rbx, 4), %mm1

	 movzx		%ch, %ebx
	 punpckldq	0x0000(%rbp, %rax, 4), %mm4
	movzx		%dl, %eax
	 shr			$16, %rcx
	 punpckldq	0x0400(%rbp, %rbx, 4), %mm5

	movzx		%dh, %ebx
	punpckldq	0x0000-2(%rbp, %rax, 4), %mm2
	punpckldq	0x0400-2(%rbp, %rbx, 4), %mm3

	 movzx		%cl, %eax
	 movzx		%ch, %ebx
	 punpckldq	0x0000-2(%rbp, %rax, 4), %mm6
	 punpckldq	0x0400-2(%rbp, %rbx, 4), %mm7
		movq		16(%rsi, %r8), %rdx			# read for next iteration
		movq		24(%rsi, %r8), %rcx			# read for next iteration

	pxor		%mm1, %mm0
	pxor		%mm3, %mm2
#	psllq		$16, %mm2		# unneeded: LUT loads offset by -2 to get pre-shifted
	 pxor		%mm5, %mm4
	pxor		%mm2, %mm0

	 pxor		%mm7, %mm6
#	 psllq		$16, %mm6		# unneeded: LUT loads offset by -2 to get pre-shifted
	pxor		(%rdi, %r8), %mm0
	 pxor		%mm6, %mm4
	 pxor		8(%rdi, %r8), %mm4	# could play around with scheduling the xors, since it's transitive and associative
	movq		%mm0, (%rdi, %r8)
	 movq		%mm4, 8(%rdi, %r8)


	add			$16, %r8
	jnz			loop
#IACA ;END_MARKER
mov $222, %ebx
.byte 0x64, 0x67, 0x90

	#
	# handle final iteration separately (so that a read beyond the end of the input/output buffer is avoided)
	#
last8:
	# FIXME: only does 8, not 16
	movzx		%dl, %eax
	movzx		%dh, %ebx
	movd		0x0000(%rbp, %rax, 4), %mm0
	shr			$16, %rdx
	movd		0x0400(%rbp, %rbx, 4), %mm1
	movzx		%dl, %eax
#	movq		0(%rdi, %r8, 1), %mm5
	movzx		%dh, %ebx
	movd		0x0000(%rbp, %rax, 4), %mm2
	shr			$16, %rdx
#	movd		%mm4, %edx
#	movq		8(%rsi, %r8, 1), %mm4			# read-ahead next 8 source bytes
	movzx		%dl, %eax
	movd		0x0400(%rbp, %rbx, 4), %mm3
	movzx		%dh, %ebx
	shr			$16, %rdx
	punpckldq	0x0000(%rbp, %rax, 4), %mm0
	movzx		%dl, %eax
	punpckldq	0x0400(%rbp, %rbx, 4), %mm1
	movzx		%dh, %ebx
	punpckldq	0x0000(%rbp, %rax, 4), %mm2
	pxor		%mm0, %mm1
	punpckldq	0x0400(%rbp, %rbx, 4), %mm3
#	movd		%mm4, %edx						# prepare src bytes 3-0 for next loop
#	pxor		%mm5, %mm1
	pxor		%mm2, %mm3
	psllq		$16, %mm3
#	psrlq		$32, %mm4						# align src bytes 7-4 for next loop
	pxor		%mm3, %mm1
	pxor		0(%rdi, %r8, 1), %mm1
	movq		%mm1, 0(%rdi, %r8, 1)

	#
	# done: exit MMX mode, restore regs/stack, exit
	#
	emms
	pop			%rbx
#	pop			%rdi
#	pop			%rsi
	pop			%rbp
	ret

#IACA
#mov ebx, 111
#db 0x64, 0x67, 0x90
mov $111, %ebx
.byte 0x64, 0x67, 0x90
	# IACA says punpck can micro-fuse into one uop with a memory address,
	# but only with a single-register addressing mode.
	# Unless we can scale the source bytes by 2 or 4 (to be LUT indices) without using an extra instruction
	# beyond the shifts and movzx, then there's no advantage to punpck over pinsrw.

	# movd is a single uop, even with a complex addressing mode, so it's still useful to do more movd
	# and less pinsrw, as long as we don't add more uops to merge the registers
	punpckldq	0x0400(%rbp, %rax), %mm0	# 2 uops
	punpckldq	(%rax, %rbx), %mm0		# 2 uops
#	punpckldq	loop(%rax,4), %mm0		# nope, doesn't work
	punpckldq	0x0(%rbp), %mm0			# 1 uop (micro-fused)
	punpckldq	(%rax), %mm0			# 1 uop (micro-fused)
	punpckldq	0x0400(%rax), %mm0		# 1 uop (micro-fused)
	pinsrw		$2, 0x0000(%rsi, %rax, 4), %xmm2 # 2 uops
	pinsrw		$2, 0x0000(%rdi, %rbx, 4), %xmm3 # 2 uops
	pinsrw		$2, 0x0000(%rax), %xmm2		# 2 uops
	pinsrw		$2, 0x0400(%rbx), %xmm3		# 2 uops
	pinsrw		$2, loop, %xmm3			# 2 uops

	movq		%mm1, 0(%rdi, %r8, 1)		# store: 2 uops
	movq		%mm1, 0(%rdi)			# store: 1 uop (micro-fused)
	movq		%mm1, 0+loop			# store: 1 uop (micro-fused)
	movq		%xmm1, 0(%rdi, %r8, 1)		# store: 2 uops
	movq		%xmm1, 0(%rdi)			# store: 1 uop (micro-fused)
	movq		%xmm1, 0+loop			# store: 1 uop (micro-fused)

	movq		16(%rsi, %r8), %rdx		# 1 uop (no fusion)
	movd		0x0400(%rbp, %rbx, 4), %mm1	# 1 uop (no fusion)


#IACA ;END_MARKER
mov $222, %ebx
.byte 0x64, 0x67, 0x90
