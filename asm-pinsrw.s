	# in amd64, encoding (%rbp, %rax, 4) takes an extra byte vs. using other regs.  It has to get encoded with a 0-byte displacement, rather than no disp
	# r13 suffers the same problem as rbp.  http://www.x86-64.org/documentation/assembly.html
	# pinsrw		$2, 0x0000(%rbp, %rax, 4), %xmm0  # 7B
	# pinsrw		$2, 0x0000(%r10, %rax, 4), %xmm0  # 7B
	# pinsrw		$2, 0x0000(%rcx, %rax, 4), %xmm0  # 6B
	# pinsrw		$1, (%rsi,%rax,4), %xmm2	  # 6B
	# vpinsrw		$0x4,(%rcx,%rbp,4),%xmm0,%xmm1	  # 6B
	# vpinsrw		$0x7,(%rcx,%rbx,4),%xmm12,%xmm14  # 6B
	# vpinsrw		$0x3,(%rcx,%r11,4),%xmm10,%xmm12  # 7B
	# freeing up more low regs would seem to require more prologue to shuffle rsi and rdi
	# so don't bother unless optimizing for something without a uop cache
	# or could use rax or rbx, but then we'd lose the nice readability, and get mov %ch, %rbp or something


	.text
	.align 16
	.globl rs_process_pinsrw128
rs_process_pinsrw128:
#	rs_process_pinsrw128(void* dst (%rdi), const void* src (%rsi), size_t size (%rdx), const u16* LH (%rcx));
# ~1 byte per cycle on SandyBridge
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

	mov			%rsi, %rbp	# src buffer
	mov			%rdi, %r10	# dst buffer
	mov			%rcx, %rsi						# combined multiplication table.
	lea			0x400(%rcx), %rdi					# LH: high
	mov			%rdx, %r11						# number of bytes to process (multiple of 16)
	movq		(%rbp),  %rdx			# load first 8 source bytes
#	movq		8(%rbp), %rcx

	sub			$16, %r11						# last8 is a loop iter without loading more src
	jle			.last8	# can only skip fixing up src/dest ptr if count is now exactly 0, not just under 16 on entry

#	prefetchT0       64(%rsi)
#	prefetchT0       64(%rbp)
#	prefetch0       128(%rbp)					# is it worth prefetching a lot, to trigger HW prefetch?  nvm, on Core, HW and SW prefetch aren't linked
	add			%r11, %rbp						# point to last set of 8-bytes of input
#	add			%r11, %r10						# point to last set of 8-bytes of output
	neg			%r11							# convert byte size to count-up

# %r10		# destination (function arg comes in in %rdi)
# %rbp		# source  (function arg comes in in %rsi)
# %rsi: lookup table (one less byte to encode than %rbp, when displacement is zero).  swapping thse regs seems to be a very small slowdown on SnB.
# %rdi: high half of LH.  (saves 4B in every other pinsrw)

# eax: scratch (holds %dl)
# ebx: scratch (holds %dh)

# r11: -count, counts upward to 0.
# rdx, rcx: src.  src words [0..3] and [4..7]

# mm5: previous value of dest


#mov ebx, 111		; IACA start
#db 0x64, 0x67, 0x90
#mov $111, %ebx          # IACA start
#.byte 0x64, 0x67, 0x90

	.align	32
.loop:
	# do 16 bytes of data per iter, with two 8B loads of src data per 16B load/store of dest data
	movzx		%dl, %eax
	movzx		%dh, %ebx
	shr			$16, %rdx
		movq		8(%rbp, %r11), %rcx			# next 8 source bytes
	movd		0x0000(%rsi, %rax, 4), %xmm0		# There is no movw to vector reg.  upper 16 has garbage.  (and can cacheline-split)
	movd		0x0000(%rdi, %rbx, 4), %xmm1		# use movd over pinsrw anyway, to break the dependency chain.  (and one less uop)

	# it seems to be a slowdown to rearrange things to alternate rdx and rcx blocks.  Maybe having a movd block after a pinsrw block is good?
	# scheduling for 2-1-1 decoding is a tiny slowdown on Sandybridge (with uop cache).  Haven't tested on Nehalem
	movzx		%dl, %eax
	movzx		%dh, %ebx
	shr			$16, %rdx
	pinsrw		$1, 0x0000(%rsi, %rax, 4), %xmm0
	 movzx		%cl, %eax
	pinsrw		$1, 0x0000(%rdi, %rbx, 4), %xmm1

	 movzx		%ch, %ebx
	 movd		0x0000(%rsi, %rax, 4), %xmm2		# separate dep chain for the other 8 src bytes costs 2 uops (pxor + punpck)
	 shr			$16, %rcx
	 movd		0x0000(%rdi, %rbx, 4), %xmm3		# but movd is cheaper than pinsrw.  No net uop diff.  (It is slightly faster)

	movzx		%dl, %eax
	movzx		%dh, %ebx
	pinsrw		$2, 0x0000(%rsi, %rax, 4), %xmm0
	 movzx		%cl, %eax
	shr			$16, %rdx
	pinsrw		$2, 0x0000(%rdi, %rbx, 4), %xmm1

	 movzx		%ch, %ebx
	 pinsrw		$1, 0x0000(%rsi, %rax, 4), %xmm2
	 shr			$16, %rcx
	movzx		%dl, %eax
	 pinsrw		$1, 0x0000(%rdi, %rbx, 4), %xmm3

	movzx		%dh, %ebx
	pinsrw		$3, 0x0000(%rsi, %rax, 4), %xmm0
	 movzx		%cl, %eax
	pinsrw		$3, 0x0000(%rdi, %rbx, 4), %xmm1

	 movzx		%ch, %ebx
	 shr			$16, %rcx
		movq		16(%rbp, %r11), %rdx			# read-ahead for next iter
	 pinsrw		$2, 0x0000(%rsi, %rax, 4), %xmm2
	 pinsrw		$2, 0x0000(%rdi, %rbx, 4), %xmm3
	 movzx		%cl, %eax
	 movzx		%ch, %ebx
	pxor		%xmm1, %xmm0
	 pinsrw		$3, 0x0000(%rsi, %rax, 4), %xmm2
	 pinsrw		$3, 0x0000(%rdi, %rbx, 4), %xmm3

	 pxor		%xmm3, %xmm2
#	movlhps		%xmm2, %xmm0	# runs on p5 only
	punpcklqdq	%xmm2, %xmm0	# runs on p1 / p5, same as pinsrw (SnB)

#	movq		(%r10, %r11), %xmm5
#	pxor		%xmm5, %xmm0
	pxor		(%r10), %xmm0  ## (%r10, %r11)   # counting-up version can't micro-fuse on SnB and later
	movdqu		%xmm0, (%r10)  ## (%r10, %r11)
#	movq		%xmm0, (%r10, %r11)

	add			$16, %r10   # with this insn commented out, the micro-fusion version is faster.  Else slower.  Maybe dest cache effects
	add			$16, %r11
	jnz			.loop

	#
	# handle final iteration separately (so that a read beyond the end of the input/output buffer is avoided)
	#
.last8:

#mov $222, %ebx  #IACA ;END_MARKER
#.byte 0x64, 0x67, 0x90

	# do 16 bytes of data per iter, with two 8B loads of src data per 16B load/store of dest data
	# still using the longer-dep-chain PINSRW all the way, instead of 2 chains and punpck Q->DQ
	# TODO: update last iter code to whatever proves fastest in the loop, with loads for next iter commented out
	movzx		%dl, %eax
	movzx		%dh, %ebx
	shr			$16, %rdx
	movd		0x0000(%rsi, %rax, 4), %xmm0		# There is no movw to vector reg.  upper 16 has garbage.  (and can cacheline-split)
	movd		0x0000(%rdi, %rbx, 4), %xmm1		# use movd over pinsrw anyway, to break the dependency chain.  (and one less uop)
#		movq		8(%rbp, %r11), %rcx			# next 8 source bytes
	movzx		%dl, %eax
	movzx		%dh, %ebx
	shr			$16, %rdx
	pinsrw		$1, 0x0000(%rsi, %rax, 4), %xmm0
	pinsrw		$1, 0x0000(%rdi, %rbx, 4), %xmm1

	movzx		%cl, %eax
	movzx		%ch, %ebx
	shr			$16, %rcx
	pinsrw		$4, 0x0000(%rsi, %rax, 4), %xmm0
	pinsrw		$4, 0x0000(%rdi, %rbx, 4), %xmm1

	movzx		%dl, %eax
	movzx		%dh, %ebx
	shr			$16, %rdx
	pinsrw		$2, 0x0000(%rsi, %rax, 4), %xmm0
	pinsrw		$2, 0x0000(%rdi, %rbx, 4), %xmm1

	movzx		%cl, %eax
	movzx		%ch, %ebx
	shr			$16, %rcx
	pinsrw		$5, 0x0000(%rsi, %rax, 4), %xmm0
	pinsrw		$5, 0x0000(%rdi, %rbx, 4), %xmm1

	movzx		%dl, %eax
	movzx		%dh, %ebx
	pinsrw		$3, 0x0000(%rsi, %rax, 4), %xmm0
	pinsrw		$3, 0x0000(%rdi, %rbx, 4), %xmm1

#		movq		16(%rbp, %r11), %rdx			# read-ahead for next iter
	movzx		%cl, %eax
	movzx		%ch, %ebx
	shr			$16, %rcx
	pinsrw		$6, 0x0000(%rsi, %rax, 4), %xmm0
	pinsrw		$6, 0x0000(%rdi, %rbx, 4), %xmm1
	movzx		%cl, %eax
	movzx		%ch, %ebx
	pinsrw		$7, 0x0000(%rsi, %rax, 4), %xmm0
	pinsrw		$7, 0x0000(%rdi, %rbx, 4), %xmm1

	pxor		%xmm0, %xmm1

#	movq		0(%r10, %r11, 1), %xmm5
#	pxor		%xmm5, %xmm1
	pxor		(%r10), %xmm0  ## (%r10, %r11)   # counting-up version can't micro-fuse
	movdqu		%xmm0, (%r10)  ## (%r10, %r11)
#	movq		%xmm1, 0(%r10, %r11, 1)

#	pop			%r15
#	pop			%r14
#	pop			%r13
#	pop			%r12
	pop			%rbx
#	pop			%rdi
#	pop			%rsi
	pop			%rbp
	ret
