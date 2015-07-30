//#define _GNU_SOURCE
#include <emmintrin.h>
#include <immintrin.h> // vzeroupper
#include <stdint.h>
#include <stddef.h> // ptrdiff_t
#include "asm-test.h"

#ifdef IACA_MARKS_OFF
  #define IACA_START
  #define IACA_END
#else
  #include <iacaMarks.h>
#endif

// #define FORCE_ALIGN16(x) (void*)(  ((ptrdiff_t)x) & ~(ptrdiff_t)0x0f  )
#if defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__) >= 408 // GCC 4.8 or later.
#define FORCE_ALIGN16(x) __builtin_assume_aligned (x, 16)
#else
// maybe CLANG will have assume_aligned when they bump their version up to GCC 4.8?
#define FORCE_ALIGN16(x) (x)
#endif

#if 0
__m256 glob_val;
__m256i glob_vali;
float * fptr;

void bar(__m256i vec)
{
	__m256 tmp = _mm256_load_ps(fptr);
	__m256i tmpi = _mm256_cvtps_epi32(tmp);
	tmpi = _mm256_add_epi32(vec, tmpi);
	glob_vali = tmpi;
}

void baz(__m256i vec);
uint32_t foo(uint32_t factor)
{
	__m256i fac256_vec = _mm256_set1_epi16(factor);
	baz(fac256_vec);
	return factor * 2;
}
#endif


static __m128i broadcast_word(uint16_t w)
{
	union floatint { uint32_t u; float f; } u;
	u.u = ((uint32_t)w | w<<16);
	return _mm_castps_si128(_mm_broadcast_ss(&u.f));	// AVX without AVX2
}
//#define broadcast_word(x) _mm_broadcast_ss( (union floatint _f = ((uint32_t)x | x<<16);  )
// #define broadcast_word(x) _mm_set1_epi16(x) // expands to 128b constants and uses vmovdqa when AVX2 isn't available

void SYSV_ABI rs_process_nolut_intrin(void* dstvoid, const void* srcvoid, size_t size, const uint32_t* LH)
{
	// peasant's algorithm: https://en.wikipedia.org/wiki/Finite_field_arithmetic#Multiplication
	// maintain the invariant a * b + prod = product

//	_mm256_zeroupper();
//	prefetchT0      (%rdi)
	const __m128i *src = srcvoid;
	__m128i *dst = dstvoid;

	/* par2 uses GF16 with a generator of 0x1100B.  This is wider than 16b:
	 * The leading 1 bit in the generator is there to XOR the carry back to zero when
	 * a <<= 1; is done with wider-than-16bit temporaries.
	 * We test the high bit before shifting, so we can use 16b temporaries and generator,
	 * with truncation.
	 * The wikipedia example for GF8 uses a truncated generator.  (0x1b instead of 0x11b)
	 */
	__m128i generator = broadcast_word((uint16_t)0x1100BU);
	uint16_t factor = (uint16_t) ((ptrdiff_t)LH);	// FIXME: take a factor arg, rather than using low16 of LH pointer
	__m128i factor_vec = broadcast_word(factor); // broadcast(factor);	// movd / pshufd $0  to broadcast without a pshufb control mask reg

	__m128i lowbit_mask = broadcast_word(0x0001);
	__m128i highbit_mask = broadcast_word(0x8000);

/** see the git version before this for the simple one vector width at a time non-interleaved version **/
// TODO: use a macro to reduce code duplication from interleaving?
#define INTERLEAVE 2  // how many src vectors to do in parallel, to hide the latency of the dependency chains in the per-bit loop
// #define USE_PMUL      // use VPMULLW instead of VPCMPEQ + VPAND
#if INTERLEAVE > 4
	#define INTERLEAVE 4
#endif

	for (ptrdiff_t i = 0; i < size/sizeof(factor_vec) ; i+=INTERLEAVE) {
		__m128i s0 = _mm_loadu_si128(src + i);
		__m128i prod0 = _mm_setzero_si128 ();
		__m128i a0 = factor_vec;
		__m128i b0 = s0;

#if INTERLEAVE > 1
		__m128i s1 = _mm_loadu_si128(src + i + 1);
		__m128i prod1 = _mm_setzero_si128 ();
		__m128i a1 = factor_vec;
		__m128i b1 = s1;
#if INTERLEAVE > 2
		__m128i s2 = _mm_loadu_si128(src + i + 2);
		__m128i prod2 = _mm_setzero_si128 ();
		__m128i a2 = factor_vec;
		__m128i b2 = s2;
#if INTERLEAVE > 3
		__m128i s3 = _mm_loadu_si128(src + i + 3);
		__m128i prod3 = _mm_setzero_si128 ();
		__m128i a3 = factor_vec;
		__m128i b3 = s3;
#endif
#endif
#endif

		// TODO: use b = factor, and find the position of its high bit to loop fewer than 16 times.
		for (int bit = 0; bit < 16; bit++) {
	IACA_START

#define BIT_ITER(prod, a, b, ID) \
		do {						\
			/* **** 1. if LSB of b is set, prod ^= a */	\
			/* There is no variable-mask pblendvw, only byte-wise pblendvb */ \
			/* (which would also require extending the mask), so just use PAND */ \
			__m128i msk##ID = _mm_and_si128(b, lowbit_mask);	/* words with low bit set: 0x0001.  others: 0x0000 */ \
			msk##ID = _mm_cmpeq_epi16(msk##ID, lowbit_mask); /* 0x1 words -> 0xffff.  0x0000 stays 0x0000. */ \
			msk##ID = _mm_and_si128(msk##ID, a);		/* masked a */ \
			/* or pmul instead */				\
			prod = _mm_xor_si128(prod, msk##ID);   /* prod ^= a  only for words where LSB(b) = 1.  prod ^= 0 for other words */ \
									\
			/* ***** 2. b >>= 1 */				\
			b = _mm_srli_epi16(b, 1);			\
			/* ***** 3. carry = MSB(a) */			\
			__m128i carry##ID = _mm_and_si128(a, highbit_mask); /* 0x8000 or 0x0000 */ \
			carry##ID = _mm_cmpeq_epi16(carry##ID, highbit_mask);	/* same as lsb_b sequence to generate a mask of 0x0000 or 0xffff */ \
									\
			/* ***** 4. a <<= 1 */				\
			a = _mm_slli_epi16(a, 1);			\
			/* ***** 5. if (carry) a ^= generator_polynomial */ \
			carry##ID = _mm_and_si128(carry##ID, generator);	/* masked generator */ \
			a = _mm_xor_si128(a, carry##ID);	/* a ^= generator  for words that had a carry.  a ^= 0 (nop) others */ \
		} while(0)
			/* AVX512: generated masked vectors for conditional XOR:
			 * replace and/cmpeq/and/xor with:
			 * __mmask32 mask = _mm512_test_epi16_mask (b, lowbit_mask);
			 * __mm512i masked_a = _mm512_maskz_mov_epi16(msk, a);
			 * prod = _mm512_xor_si512(prod, masked_a);
			 * This saves 2 of 10 uops in the inner loop (total), and shortens dep chains from 4 to 3 cycles.
			 *  // There is no prod = _mm512_mask_xor_epi16(prod, a, mask); // only vpxord/q (epi32/64)
			 */


			BIT_ITER(prod0, a0, b0, 0);
#if INTERLEAVE > 1	/*** 2nd dep chain ***/
			BIT_ITER(prod1, a1, b1, 1);
#if INTERLEAVE > 2	/*** 3rd dep chain ***/
			BIT_ITER(prod2, a2, b2, 2);
#if INTERLEAVE > 3	/*** 4th dep chain ***/
			BIT_ITER(prod3, a3, b3, 3);
#endif	// INTERLEAVE > 3
#endif	// INTERLEAVE > 2
#endif	// INTERLEAVE > 1

			// Early termination when a == 0 || b == 0 every 4 or 8 iterations?  or just after 8 and 12?
			// unlikely to be profitable for a vector implementation, but only takes 3 instructions:
			// tmp = PMINUW(a,b) # 1 uop, p1/p5
			// PTEST (tmp, tmp)  # 2 uops, latency = 1 (SnB), 2 (Haswell)
			// jz DONE...	     # 1 uop.  Branch mispredict is fine with hyperthreading, otherwise bad.
			// even less convenient when interleaving multiple dependency chains...
		}
	IACA_END

		// d = _mm_loadl_epi128(dst + i);
		__m128i d0 = _mm_xor_si128(prod0, dst[i]);
#if INTERLEAVE > 1
		__m128i d1 = _mm_xor_si128(prod1, dst[i+1]);
#if INTERLEAVE > 2
		__m128i d2 = _mm_xor_si128(prod2, dst[i+2]);
#if INTERLEAVE > 3
		__m128i d3 = _mm_xor_si128(prod3, dst[i+3]);
#endif
#endif
#endif
		_mm_store_si128(&dst[i+0], d0);
#if INTERLEAVE > 1
		_mm_store_si128(&dst[i+1], d1);
#if INTERLEAVE > 2
		_mm_store_si128(&dst[i+2], d2);
#if INTERLEAVE > 3
		_mm_store_si128(&dst[i+3], d3);
#endif
#endif
#endif
	}

//	_mm256_zeroupper();
}

/*
 * on Haswell, 4 dispatch, still only exec units that can do vector ALU uops.  (p015, not p6):
 * 10 AVX / 10 AVX2 / 8 AVX512 insns per GF bit
 * 16 / 32 / 64B at a time.  (avx1 lacks _mm256_srli_epi16, which is a showstopper.  no equiv like andps %ymm)
 * 3.33 / 1.66 / 0.666 cycles per source byte.  (when factor requires all 16 iterations: high bit set.)
 * (AVX512BW saves 2 uops with test / maskz_mov.)
 */

/*
 * SnB, 4 dispatch but only 3 execution units: (timed on i5-2500k: no hyperthreading so full OoOE resources available)
 * 10 AVX insns per GF bit -> 3.33 cycles per bit
 * in practice, runs ~1/4 the speed of SSE2 pinsrw128 on SnB (AVX).  3.64 vs. 0.970.  with pmul: 3.91
 * interleaved by 2 version: 3.35c/byte.  interleaved with pmul: 3.61
 * interleaved by 3: 3.55c/B  pmul: 3.52c/B.  (3.50 once?)
 * interleaved by 4: 3.61c/B  pmul: 3.49c/B.  (spills product to the stack.  big slowdown with insn-by-insn interleave, but not with iteration-by-iteration.  AVX512 has 32 vector regs, so this should be fine)
 * might need to pipeline src loads?
 * interleave by 2 still fits in the loop buffer, guaranteeing 4 uops / cycle.
 * More than that and we're dependent on the uop cache delivering 4 uops/c, which depends on insn alignment into 32B chunks containing multiples of 4 uops.
 */

/*
 * two dep chains of 4 cycles (and/cmpeq/and/xor) means we need to
 * interleave operations on two source chunks at once, because
 * OOExec queue depth is limited (esp. with hyperthreading)
 */

/*
 * optimizations:
 * AVX512BW has a test instruction to set a mask reg from an AND, so this isn't needed.
 * see comments in the loop, since this works for the lowbit too, and is much better than PMUL
 * nolut is only faster than than the pinsrw loop with AVX512BW, except when factor has few significant bits,
 * so these aren't very useful.

 * low-bit set -> a, else 0: (step 1):
 *   use PMULLW instead of PCMPEQ+AND: masked_a = _mm_mullo_epi16(lsb_b, a);  # 1uop, 5 cycle latency
 *   since 0*a = 0, and 1*a = a, this works because we're testing the low bit.
 *   In practice, such a high latency requires interleave by more than 2,
 *   And then insn alignment becomes an issue for getting 3 uops / cycle from the uop cache, I think.
 *   (Haswell only has vector execution units on 3 of its 4 ALU ports, so still only 3/cycle)
 *
 * high-bit set -> generator, else 0: (step 3+5)
 *  (not viable) Use a variable shift to get a masked_generator without a PCMPEQ,
 *   directly from the highbits (0x8000 or 0x0000):
 *   shift by more than 16bits will zero the reg, shift by zero does nothing.
 *   PSLLW takes the same shift count for all vector components (src[63:0])
 *   VPSLLVW doesn't exist until AVX512BW.  AVX2 only has D and Q sizes.
 *    On Haswell, those take 3 uops anyway (lat=2, recip tput=2).  useless without fast vshift
 */
