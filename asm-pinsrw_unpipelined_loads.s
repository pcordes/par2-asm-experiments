	.align 16
	.globl rs_process_pinsrw_unpipelined
	.text
rs_process_pinsrw_unpipelined:
#	rs_process_pinsrw_unpipelined(void* dst (%rdi), const void* src (%rsi), size_t size (%rdx), const u16* LH (%rcx));
#	# 32b padded LH

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

#	mov			%rcx, %rbp						# combined multiplication table
	mov			%rdx, %r11						# number of bytes to process (multiple of 4)

#	sub			$16, %r11						# last8 is a loop iter without loading more src
#	jle			last8	# can only skip fixing up src/dest ptr if count is now exactly 0, not just under 16 on entry

#	prefetchT0       64(%rdi)
#	prefetchT0       64(%rsi)
#	prefetch0       128(%rsi)					# is it worth prefetching a lot, to trigger HW prefetch?  nvm, on Core, HW and SW prefetch aren't linked
	add			%r11, %rsi						# point to last set of 8-bytes of input
	add			%r11, %rdi						# point to last set of 8-bytes of output
	neg			%r11							# convert byte size to count-up

# %rdi		# destination (function arg)
# %rsi		# source  (function arg)
# rcx: lookup table

# eax: scratch (holds %dl)
# ebx: scratch (holds %dh)

# r11: -count, counts upward to 0.
# rdx: src.

# mm5: previous value of dest

	.align	32
loop:
	# do 16 bytes of data per iter, with two 8B loads of src data per 16B load/store of dest data
		movq		(%rsi, %r11), %rdx			# read-ahead next 8 source bytes
	movzx		%dl, %eax
	movzx		%dh, %ebx
	shr			$16, %rdx
	movd		0x0000(%rcx, %rax, 4), %xmm0		# There is no movw to vector reg.  upper 16 has garbage.  (and can cacheline-split)
	movd		0x0400(%rcx, %rbx, 4), %xmm1		# use movd over pinsrw anyway, to break the dependency chain.  (and one less uop)
	movzx		%dl, %eax
	movzx		%dh, %ebx
	shr			$16, %rdx
	pinsrw		$1, 0x0000(%rcx, %rax, 4), %xmm0
	pinsrw		$1, 0x0400(%rcx, %rbx, 4), %xmm1
	movzx		%dl, %eax
	movzx		%dh, %ebx
	shr			$16, %rdx
	pinsrw		$2, 0x0000(%rcx, %rax, 4), %xmm0
	pinsrw		$2, 0x0400(%rcx, %rbx, 4), %xmm1
	movzx		%dl, %eax
	movzx		%dh, %ebx
#		movq		8(%rsi, %r11), %rdx			# 16B  # next 8 source bytes
	pinsrw		$3, 0x0000(%rcx, %rax, 4), %xmm0
	pinsrw		$3, 0x0400(%rcx, %rbx, 4), %xmm1

	## movzx		%dl, %eax
	## movzx		%dh, %ebx
	## shr			$16, %rdx
	## pinsrw		$4, 0x0000(%rcx, %rax, 4), %xmm0
	## pinsrw		$4, 0x0400(%rcx, %rbx, 4), %xmm1
	## movzx		%dl, %eax
	## movzx		%dh, %ebx
	## shr			$16, %rdx
	## pinsrw		$5, 0x0000(%rcx, %rax, 4), %xmm0
	## pinsrw		$5, 0x0400(%rcx, %rbx, 4), %xmm1
	## movzx		%dl, %eax
	## movzx		%dh, %ebx
	## shr			$16, %rdx
	## pinsrw		$6, 0x0000(%rcx, %rax, 4), %xmm0
	## pinsrw		$6, 0x0400(%rcx, %rbx, 4), %xmm1
	## movzx		%dl, %eax
	## movzx		%dh, %ebx
	## pinsrw		$7, 0x0000(%rcx, %rax, 4), %xmm0
	## pinsrw		$7, 0x0400(%rcx, %rbx, 4), %xmm1

	pxor		%xmm0, %xmm1
	movq		0(%rdi, %r11, 1), %xmm5		# 8B
	pxor		%xmm5, %xmm1			# 8B
#	pxor		(%rdi, %r11), %xmm1		# 16B
#	movdqu		%xmm1, 0(%rdi, %r11, 1)		# 16B
	movq		%xmm1, 0(%rdi, %r11, 1)		# 8B

	add			$8, %r11
	jnz			loop

	#
	# handle final iteration separately (so that a read beyond the end of the input/output buffer is avoided)
	#
## last8:
## 	movzx		%dl, %eax
## 	movzx		%dh, %ebx
## 	movd		0x0000(%rcx, %rax, 4), %xmm0		# There is no movw to vector reg.  upper 16 has garbage.  (and can cacheline-split)
## 	movd		0x0400(%rcx, %rbx, 4), %xmm1		# use movd over pinsrw anyway, to break the dependency chain.  (and one less uop)
## 	shr			$16, %rdx
## 	movzx		%dl, %eax
## 	movzx		%dh, %ebx
## 	pinsrw		$1, 0x0000(%rcx, %rax, 4), %xmm0
## 	pinsrw		$1, 0x0400(%rcx, %rbx, 4), %xmm1
## 	shr			$16, %rdx
## 	movzx		%dl, %eax
## 	movzx		%dh, %ebx
## 	pinsrw		$2, 0x0000(%rcx, %rax, 4), %xmm0
## 	pinsrw		$2, 0x0400(%rcx, %rbx, 4), %xmm1
## #	movzx		0x0000(%rcx, %rax, 4), %r8
## #	movzx		0x0400(%rcx, %rbx, 4), %r9
## 	shr			$16, %rdx
## 	movzx		%dl, %eax
## 	movzx		%dh, %ebx
## #	movq		8(%rsi, %r11, 1), %rdx			# read-ahead next 8 source bytes
## 	pinsrw		$3, 0x0000(%rcx, %rax, 4), %xmm0
## 	pinsrw		$3, 0x0400(%rcx, %rbx, 4), %xmm1
## 	pxor		%xmm0, %xmm1
## 	movq		0(%rdi, %r11, 1), %xmm5
## 	pxor		%xmm5, %xmm1
## 	movq		%xmm1, 0(%rdi, %r11, 1)

#	pop			%r15
#	pop			%r14
#	pop			%r13
#	pop			%r12
	pop			%rbx
#	pop			%rdi
#	pop			%rsi
	pop			%rbp
	ret
