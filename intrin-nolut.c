//#define _GNU_SOURCE
#include <emmintrin.h>
#include <immintrin.h> // vzeroupper
#include <stdint.h>
// #include <stddef.h> // ptrdiff_t
#include "asm-test.h"

// #define FORCE_ALIGN16(x) (void*)(  ((ptrdiff_t)x) & ~(ptrdiff_t)0x0f  )
#if defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__) >= 408 // GCC 4.8 or later.
#define FORCE_ALIGN16(x) __builtin_assume_aligned (x, 16)
#else
// maybe CLANG will have assume_aligned when they bump their version up to GCC 4.8?
#define FORCE_ALIGN16(x) (x)
#endif

static __m128i broadcast_word(uint16_t w)
{
	union floatint { uint32_t u; float f; } u;
	u.u = ((uint32_t)w | w<<16);
	return _mm_castps_si128(_mm_broadcast_ss(&u.f));	// AVX, but not AVX2
}
//#define broadcast_word(x) _mm_broadcast_ss( (union floatint _f = ((uint32_t)x | x<<16);  )

void SYSV_ABI rs_process_nolut_intrin(void* dstvoid, const void* srcvoid, size_t size, const uint32_t* LH)
{
	// peasant's algorithm: https://en.wikipedia.org/wiki/Finite_field_arithmetic#Multiplication
	// maintain the invariant a * b + prod = product

	_mm256_zeroupper();
//	prefetchT0      (%rdi)
	const __m128i *src = srcvoid;
	__m128i *dst = dstvoid;

	__m128i generator = broadcast_word(0x1234);  // FIXME
	uint16_t factor = LH[5];
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

	for (size_t i = 0; i < size/sizeof(factor_vec) ; i+=INTERLEAVE) {
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
			// **** 1. if LSB of b is set, prod ^= a
			// There is no variable-mask pblendvw, only byte-wise pblendvb
			// (which would also require extending the mask), so just use PAND
			__m128i msk0 = _mm_and_si128(b0, lowbit_mask);	// words with low bit set: 0x0001.  others: 0x0000
	#ifndef USE_PMUL
			msk0 = _mm_cmpeq_epi16(msk0, lowbit_mask); // 0x1 words -> 0xffff.  0x0000 stays 0x0000.
			msk0 = _mm_and_si128(msk0, a0);
	#else
			msk0 = _mm_mullo_epi16(msk0, a0);
			// or use PMULLW instead of PCMPEQ+AND: masked_a = _mm_mullo_epi16(lsb_b, a);  # 1uop, 5 cycle latency
			// since 0*a = 0, and 1*a = a, this works because we're testing the low bit.
	#endif
			prod0 = _mm_xor_si128(prod0, msk0);  // prod ^= a  only for words where LSB(b) = 1.  prod ^= 0 for other words

			// ***** 2. b >>= 1
			b0 = _mm_srli_epi16(b0, 1);
			// 3. carry = MSB(a)
			__m128i carry0 = _mm_and_si128(a0, highbit_mask); // 0x8000 or 0x0000
			carry0 = _mm_cmpeq_epi16(carry0, highbit_mask);	// same as lsb_b sequence to generate a mask of 0x0000 or 0xffff

			// 4. a <<= 1
			a0 = _mm_slli_epi16(a0, 1);
			// 5. if (carry) a ^= generator polynomial
			carry0 = _mm_and_si128(carry0, generator);
			a0 = _mm_xor_si128(a0, carry0);	// a ^= generator  for words that had a carry.  a ^= 0 (nop) others

#if INTERLEAVE > 1	/*** 2nd dep chain ***/
			__m128i msk1 = _mm_and_si128(b1, lowbit_mask);	// words with low bit set: 0x0001.  others: 0x0000
	#ifndef USE_PMUL
			msk1 = _mm_cmpeq_epi16(msk1, lowbit_mask); // 0x1 words -> 0xffff.  0x0000 stays 0x0000.
			msk1 = _mm_and_si128(msk1, a1);
	#else
			msk1 = _mm_mullo_epi16(msk1, a1);
	#endif
			prod1 = _mm_xor_si128(prod1, msk1);  // prod ^= a  only for words where LSB(b) = 1.  prod ^= 0 for other words
			b1 = _mm_srli_epi16(b1, 1);
			__m128i carry1 = _mm_and_si128(a1, highbit_mask); // 0x8000 or 0x0000
			carry1 = _mm_cmpeq_epi16(carry1, highbit_mask);	// same as lsb_b sequence to generate a mask of 0x0000 or 0xffff
			a1 = _mm_slli_epi16(a1, 1);
			carry1 = _mm_and_si128(carry1, generator);
			a1 = _mm_xor_si128(a1, carry1);	// a ^= generator  for words that had a carry.  a ^= 0 (nop) others


#if INTERLEAVE > 2	/*** 3rd dep chain ***/
			__m128i msk2 = _mm_and_si128(b2, lowbit_mask);	// words with low bit set: 0x0001.  others: 0x0000
			msk2 = _mm_cmpeq_epi16(msk2, lowbit_mask); // 0x1 words -> 0xffff.  0x0000 stays 0x0000.
			msk2 = _mm_and_si128(msk2, a2);
	#ifndef USE_PMUL
			msk2 = _mm_cmpeq_epi16(msk2, lowbit_mask); // 0x1 words -> 0xffff.  0x0000 stays 0x0000.
			msk2 = _mm_and_si128(msk2, a1);
	#else
			msk2 = _mm_mullo_epi16(msk2, a2);
	#endif
			prod2 = _mm_xor_si128(prod2, msk2);  // prod ^= a  only for words where LSB(b) = 1.  prod ^= 0 for other words
			b2 = _mm_srli_epi16(b2, 1);
			__m128i carry2 = _mm_and_si128(a2, highbit_mask); // 0x8000 or 0x0000
			carry2 = _mm_cmpeq_epi16(carry2, highbit_mask);	// same as lsb_b sequence to generate a mask of 0x0000 or 0xffff
			a2 = _mm_slli_epi16(a2, 1);
			carry2 = _mm_and_si128(carry2, generator);
			a2 = _mm_xor_si128(a2, carry2);	// a ^= generator  for words that had a carry.  a ^= 0 (nop) others

#if INTERLEAVE > 3	/*** 4th dep chain ***/
			__m128i msk3 = _mm_and_si128(b3, lowbit_mask);	// words with low bit set: 0x0001.  others: 0x0000
			msk3 = _mm_cmpeq_epi16(msk3, lowbit_mask); // 0x1 words -> 0xffff.  0x0000 stays 0x0000.
			msk3 = _mm_and_si128(msk3, a3);
	#ifndef USE_PMUL
			msk3 = _mm_cmpeq_epi16(msk3, lowbit_mask); // 0x1 words -> 0xffff.  0x0000 stays 0x0000.
			msk3 = _mm_and_si128(msk3, a3);
	#else
			msk3 = _mm_mullo_epi16(msk3, a3);
	#endif
			prod3 = _mm_xor_si128(prod3, msk3);  // prod ^= a  only for words where LSB(b) = 1.  prod ^= 0 for other words
			b3 = _mm_srli_epi16(b3, 1);
			__m128i carry3 = _mm_and_si128(a3, highbit_mask); // 0x8000 or 0x0000
			carry3 = _mm_cmpeq_epi16(carry3, highbit_mask);	// same as lsb_b sequence to generate a mask of 0x0000 or 0xffff
			a3 = _mm_slli_epi16(a3, 1);
			carry3 = _mm_and_si128(carry3, generator);
			a3 = _mm_xor_si128(a3, carry3);	// a ^= generator  for words that had a carry.  a ^= 0 (nop) others
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
		// on Haswell, with 4 exec units to match 4 dispatch / cycle:
		// 10 AVX / AVX2 / AVX512 insns per GF bit -> 2.5 cycles per bit of the GF16
		// 16 / 32 / 64B at a time.  (avx1 lacks _mm256_srli_epi16, which is a showstopper.  no equiv like andps %ymm)
		// 2.5 / 1.25 / 0.625 cycles per source byte

		// SnB, 4 dispatch but only 3 execution units: (timed on i5-2500k: no hyperthreading so full OoOE resources available)
		// 10 AVX insns per GF bit -> 3.33 cycles per bit
		// in practice, runs ~1/4 the speed of SSE2 pinsrw128 on SnB (AVX).  3.64 vs. 0.970.  with pmul: 3.91
		// interleaved by 2 version: 3.35c/byte.  interleaved with pmul: 3.61
		// interleaved by 3: 3.55c/B  pmul: 3.52c/B.  (3.50 once?)
		// interleaved by 4: 3.61c/B  pmul: 3.49c/B.  (spills product to the stack.  big slowdown with insn-by-insn interleave, but not with iteration-by-iteration.  AVX512 has 32 vector regs, so this should be fine)
		// might need to pipeline src loads?

		// two dep chains of 4 cycles (and/cmpeq/and/xor) means we need to
		// interleave operations on two source chunks at once, because
		// OOExec queue depth is limited (esp. with hyperthreading)


		// optimizations:
		// high-bit set -> generator, else 0: (step 3+5)
		//   or use a variable shift get a masked_generator directly from the highbits (0x8000 or 0x0000):
		//  shift by more than 16bits will zero the reg, shift by zero does nothing.
		//   PSLLW takes the same shift count for all vector components (src[63:0])
		//  VPSLLVW doesn't exist until AVX512BW + AXV512VL.  AVX2 only has D and Q sizes.
		//    On Haswell, those take 3 uops anyway (lat=2, recip tput=2).  useless without fast vshift



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
