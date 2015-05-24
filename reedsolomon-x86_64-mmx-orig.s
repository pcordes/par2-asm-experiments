	.globl rs_process_x86_64_mmx_orig
	.text
rs_process_x86_64_mmx_orig:

# void rs_process_x86_64_mmx(void* dst, const void* src, size_t size, unsigned* LH);
#
	push		%rbp
#	push		%rsi
#	push		%rdi
	push		%rbx

	mov			%rcx, %rbp						# combined multiplication table
	mov			%rdx, %rcx						# number of bytes to process (multiple of 8)

	mov			(%rsi), %edx					# load 1st 8 source bytes
	movd		4(%rsi), %mm4

	sub			$8, %rcx						# reduce # of loop iterations by 1
	jz			last8
	add			%rcx, %rsi						# point to last set of 8-bytes of input
	add			%rcx, %rdi						# point to last set of 8-bytes of output
	neg			%rcx							# convert byte size to count-up


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
# edx / mm4: src. (mm4 loads 64B.  edx gets 32B at a time from mm4, and is shifted by 16B for the low/high GF16)

# mm5: previous value of dest

	.align	16
loop:
	movzx		%dl, %eax
	movzx		%dh, %ebx
	movd		0x0000(%rbp, %rax, 4), %mm0
	shr			$16, %edx
	movd		0x0400(%rbp, %rbx, 4), %mm1
	movzx		%dl, %eax
	movq		0(%rdi, %rcx, 1), %mm5
	movzx		%dh, %ebx
	movd		0x0000(%rbp, %rax, 4), %mm2
	movd		%mm4, %edx
	movq		8(%rsi, %rcx, 1), %mm4			# read-ahead next 8 source bytes
	movzx		%dl, %eax
	movd		0x0400(%rbp, %rbx, 4), %mm3
	movzx		%dh, %ebx
	shr			$16, %edx
	punpckldq	0x0000(%rbp, %rax, 4), %mm0
	movzx		%dl, %eax
	punpckldq	0x0400(%rbp, %rbx, 4), %mm1
	movzx		%dh, %ebx
	punpckldq	0x0000(%rbp, %rax, 4), %mm2
	pxor		%mm0, %mm1
	punpckldq	0x0400(%rbp, %rbx, 4), %mm3
	movd		%mm4, %edx						# prepare src bytes 3-0 for next loop
	pxor		%mm5, %mm1
	pxor		%mm2, %mm3
	psllq		$16, %mm3
	psrlq		$32, %mm4						# align src bytes 7-4 for next loop
	pxor		%mm3, %mm1
	movq		%mm1, 0(%rdi, %rcx, 1)

	add			$8, %rcx
	jnz			loop

	#
	# handle final iteration separately (so that a read beyond the end of the input/output buffer is avoided)
	#
last8:
	movzx		%dl, %eax
	movzx		%dh, %ebx
	movd		0x0000(%rbp, %rax, 4), %mm0
	shr			$16, %edx
	movd		0x0400(%rbp, %rbx, 4), %mm1
	movzx		%dl, %eax
	movq		0(%rdi, %rcx, 1), %mm5
	movzx		%dh, %ebx
	movd		0x0000(%rbp, %rax, 4), %mm2
	movd		%mm4, %edx
#	movq		8(%rsi, %rcx, 1), %mm4			# read-ahead next 8 source bytes
	movzx		%dl, %eax
	movd		0x0400(%rbp, %rbx, 4), %mm3
	movzx		%dh, %ebx
	shr			$16, %edx
	punpckldq	0x0000(%rbp, %rax, 4), %mm0
	movzx		%dl, %eax
	punpckldq	0x0400(%rbp, %rbx, 4), %mm1
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
	movq		%mm1, 0(%rdi, %rcx, 1)

	#
	# done: exit MMX mode, restore regs/stack, exit
	#
	emms
	pop			%rbx
#	pop			%rdi
#	pop			%rsi
	pop			%rbp
	ret
