	.align 16
	.globl rs_process_pinsrw_mmx
	.text
rs_process_pinsrw_mmx:
#	rs_process_pinsrw_mmx(void* dst (%rdi), const void* src (%rsi), size_t size (%rdx), const u16* LH (%rcx));
	# expects 32bit padded LH, but could use 16bit packed easily.
#
	# TODO: use 8 or 16-byte aligned SIMD loads when src is aligned
#	movdqa		(%rsi), %xmm4
#	movd		4(%rsi), %mm4

	prefetchT0      (%rdi)

	push		%rbp
#	push		%rsi
#	push		%rdi
	push		%rbx
	#r8-11 can be modified
#	push		%r12
#	push		%r13
#	push		%r14
#	push		%r15

	mov			%rcx, %rbp						# combined multiplication table
	mov			%rdx, %r11						# number of bytes to process (multiple of 4)
	movq		(%rsi), %rdx		# load 1st 8 source bytes

	sub			$8, %r11						# reduce # of loop iterations by 1
	jz			last8

#	prefetchT0       64(%rdi)
#	prefetchT0       64(%rsi)
#	prefetch0       128(%rsi)					# is it worth prefetching a lot, to trigger HW prefetch?
	add			%r11, %rsi						# point to last set of 8-bytes of input
	add			%r11, %rdi						# point to last set of 8-bytes of output
	neg			%r11							# convert byte size to count-up

# This is faster than the scalar code mainly because wider load/stores
# for the source and dest data leave the load unit(s) free
# for 32b loads from the LH lookup table.
# punpckldq just loads 32b from memory into the high half of the MMX reg

# %rdi		# destination (function arg)
# %rsi		# source  (function arg)
# rbp: lookup table

# eax: scratch (holds %dl)
# ebx: scratch (holds %dh)

# ecx: -count, counts upward to 0.
# rdx: src.

# mm5: previous value of dest

	.align	32
loop:
	movzx		%dl, %eax
	movzx		%dh, %ebx
	movd		0x0000(%rbp, %rax, 4), %mm0		# FIXME: there is no movw to mmx reg.  We need to mask off the bits we don't need
	movd		0x0400(%rbp, %rbx, 4), %mm1
	shr			$16, %rdx
	movzx		%dl, %eax
	movzx		%dh, %ebx
	movd		0x0000(%rbp, %rax, 4), %mm2
	movd		0x0400(%rbp, %rbx, 4), %mm3
#	movd		%mm4, %edx
#	movq		8(%rsi, %r11, 1), %mm4			# read-ahead next 8 source bytes
	shr			$16, %rdx
	movzx		%dl, %eax
	movzx		%dh, %ebx
#	punpckldq	0x0000(%rbp, %rax, 4), %mm0
#	punpckldq	0x0400(%rbp, %rbx, 4), %mm1
	pinsrw		$2, 0x0000(%rbp, %rax, 4), %mm0
	pinsrw		$2, 0x0400(%rbp, %rbx, 4), %mm1
#	movzx		0x0000(%rbp, %rax, 4), %r8
#	movzx		0x0400(%rbp, %rbx, 4), %r9
	shr			$16, %rdx
	movzx		%dl, %eax
	movzx		%dh, %ebx
	movq		8(%rsi, %r11, 1), %rdx			# read-ahead next 8 source bytes
#	punpckldq	0x0000(%rbp, %rax, 4), %mm2
#	punpckldq	0x0400(%rbp, %rbx, 4), %mm3
	pinsrw		$2, 0x0000(%rbp, %rax, 4), %mm2
	pinsrw		$2, 0x0400(%rbp, %rbx, 4), %mm3
	pxor		%mm0, %mm1
#	movd		%mm4, %edx						# prepare src bytes 3-0 for next loop
#	movq		0(%rdi, %r11, 1), %mm5
#	pxor		%mm5, %mm1
	pxor		0(%rdi, %r11, 1), %mm1
	pxor		%mm2, %mm3
	psllq		$16, %mm3
#	psrlq		$32, %mm4						# align src bytes 7-4 for next loop
	pxor		%mm3, %mm1	# or POR, merge odd/even results
	movq		%mm1, 0(%rdi, %r11, 1)

	add			$8, %r11
	jnz			loop

	#
	# handle final iteration separately (so that a read beyond the end of the input/output buffer is avoided)
	#
last8:
	movzx		%dl, %eax
	movzx		%dh, %ebx
	movd		0x0000(%rbp, %rax, 4), %mm0
	shr			$16, %rdx
	movd		0x0400(%rbp, %rbx, 4), %mm1
	movzx		%dl, %eax
		movq		0(%rdi, %r11, 1), %mm5		# dest data
	movzx		%dh, %ebx
	movd		0x0000(%rbp, %rax, 4), %mm2
#	movd		%mm4, %edx
#	movq		8(%rsi, %r11, 1), %mm4			# read-ahead next 8 source bytes
	shr			$16, %rdx
	movzx		%dl, %eax
	movd		0x0400(%rbp, %rbx, 4), %mm3
	movzx		%dh, %ebx
	shr			$16, %rdx
#	punpckldq	0x0000(%rbp, %rax, 4), %mm0
	pinsrw		$2, 0x0000(%rbp, %rax, 4), %mm0
	movzx		%dl, %eax
#	punpckldq	0x0400(%rbp, %rbx, 4), %mm1
	pinsrw		$2, 0x0400(%rbp, %rbx, 4), %mm1
	movzx		%dh, %ebx
	punpckldq	0x0000(%rbp, %rax, 4), %mm2
	pxor		%mm0, %mm1
	punpckldq	0x0400(%rbp, %rbx, 4), %mm3
#	movd		%mm4, %edx						# prepare src bytes 3-0 for next loop
	pxor		%mm5, %mm1
	pxor		%mm2, %mm3
	psllq		$16, %mm3
#	psrlq		$32, %mm4						# align src bytes 7-4 for next loop
	pxor		%mm3, %mm1
	movq		%mm1, 0(%rdi, %r11, 1)

	#
	# done: exit MMX mode, restore regs/stack, exit
	#
	emms
#	pop			%r15
#	pop			%r14
#	pop			%r13
#	pop			%r12
	pop			%rbx
#	pop			%rdi
#	pop			%rsi
	pop			%rbp
	ret
