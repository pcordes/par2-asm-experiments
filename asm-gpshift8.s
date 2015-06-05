
#define LUT_MOVD(r, x1, x2) \
	movzx	%r#l, %eax	\
	movzx	%r#h, %ebx	\
		movd	0x000(%rbp, %rax, 2), x1 \
		movd	0x200(%rbp, %rbx, 2), x2
	# VC++ intrinsics code used s0 = punpck(s0, movd(lut))
	# cacheline splits or MMX might be faster than the extra uop to load first.
	# PUNPCKLDQ is one fused uop, even from RAM.  ...Quad -> DoubleQuad (64->128) from RAM is 2 fused uops, but others are 1
#define LUT_UNPCK(r, x1, x2) \
	movzx	%r#l, %eax	\
	movzx	%r#h, %ebx	\
		punpckldq	0x000(%rbp, %rax, 2), x1 \
		punpckldq	0x200(%rbp, %rbx, 2), x2
#define LUT_UNPCK_AVX
	movzx	%r#l, %eax	\
	movzx	%r#h, %ebx	\
		vpunpckldq	0x000(%rbp, %rax, 2), x1 \
		vpunpcklwd	0x200(%rbp, %rbx, 2), x2

	.align	32
loop2xGP:
	movq	0(%rsi), %rcx	# src[0..3]  (4xu16)
	movq	8(%rsi), %rdx	# src[4..8]
	LUT_MOVD (c, %xmm0, %xmm1)
	LUT_MOVD (d, %xmm8, %xmm9)
	shr	$16, %rcx
	shr	$16, %rdx
	LUT_UNPCK (c, %xmm0, %xmm1)	# cacheline split 1 in 16  (assuming uniform distribution of src lower few bits in every byte)
	LUT_UNPCK (d, %xmm8, %xmm9)	# cacheline split 1 in 16
	# xmm0 has: dwords: [3]=0,  [2]=0,  [1]=LUT(src[1LO])  [0]=LUT(src[0LO])
	# xmm1 has: dwords: [3]=0,  [2]=0,  [1]=LUT(src[1HI])  [0]=LUT(src[0HI])

	shr	$16, %rcx
	shr	$16, %rdx
	LUT_MOVD (c, %xmm2, %xmm3)
	LUT_MOVD (d, %xmm8, %xmm9)


	.align	32
shr8loop:
### %rdx has 8 bytes data from (%rsi)
# %rsi points to source data
# %rdi points to the location in the output buffer to modify
	movq		(%rsi), %rdx			 # TODO: read-ahead next 8 source bytes to avoid potential false dep on store that's a multiple of 4k away
	movzx		%dl, %eax	# 0
	shr		$8, %rdx
	movzx		%dl, %eax	# 1
	shr		$8, %rdx
	movzx		%dl, %eax	# 2
	shr		$8, %rdx
	movzx		%dl, %eax	# 3
	shr		$8, %rdx
	movzx		%dl, %eax	# 4
	shr		$8, %rdx
	movzx		%dl, %eax	# 5
	shr		$8, %rdx
	movzx		%dl, %eax	# 6
	shr		$8, %rdx
	#movzx		%dl, %eax	# 7
	# %rdx is ready for use
	ja			shr8loop

