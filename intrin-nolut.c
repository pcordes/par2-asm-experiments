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

//	prefetchT0      (%rdi)
	const __m128i *src = srcvoid;
	__m128i *dst = dstvoid;

	__m128i generator = broadcast_word(0x1234);  // FIXME
	uint16_t factor = LH[5];
	__m128i factor_vec = broadcast_word(factor); // broadcast(factor);	// movd / pshufd $0  to broadcast without a pshufb control mask reg

	__m128i lowbit_mask = broadcast_word(0x0001);
	__m128i highbit_mask = broadcast_word(0x8000);

//	_mm256_zeroupper();
	for (size_t i = 0; i < size/sizeof(factor_vec) ; i+=1) {
		__m128i s = _mm_loadu_si128(src + i);

		// peasant's algorithm: https://en.wikipedia.org/wiki/Finite_field_arithmetic#Multiplication
		// maintain the invariant a * b + prod = product

		__m128i prod = _mm_setzero_si128 ();
		__m128i a = factor_vec;
		__m128i b = s;
		for (int bit = 0; bit < 16; bit++) {
			// **** 1. if LSB of b is set, prod ^= a
			// There is no variable-mask pblendvw, only byte-wise pblendvb
			// (which would also require extending the mask), so just use PAND
			__m128i lsb_b = _mm_and_si128(b, lowbit_mask);	// words with low bit set: 0x0001.  others: 0x0000
			lsb_b = _mm_cmpeq_epi16(lsb_b, lowbit_mask);	// 0x1 words -> 0xffff.  0x0000 stays 0x0000.
			__m128i masked_a = _mm_and_si128(lsb_b, a);
			prod = _mm_xor_si128(prod, masked_a);		// prod ^= a  only for words where LSB(b) = 1.  prod ^= 0 for other words

			// ***** 2. b >>= 1
			b = _mm_srli_epi16(b, 1);
			// 3. carry = MSB(a)
			__m128i carry = _mm_and_si128(a, highbit_mask);
			carry = _mm_cmpeq_epi16(carry, highbit_mask);	// same as lsb_b sequence to generate a mask of 0x0000 or 0xffff

			// 4. a <<= 1
			a = _mm_slli_epi16(a, 1);
			// 5. if (carry) a ^= generator polynomial
			carry = _mm_and_si128(carry, generator);
			a = _mm_xor_si128(a, carry);	// a ^= generator  for words that had a carry.  a ^= 0 (nop) others

			// Early termination when a or b = 0 every few iterations?
			// unlikely to be profitable for a vector implementation, but only takes 3 instructions:
			// tmp = PMINUW(a,b) # 1 uop, p1/p5
			// PTEST (tmp, tmp)  # 2 uops, latency = 1 (SnB), 2 (Haswell)
			// jz DONE...	     # 1 uop.  Branch mispredict is fine with hyperthreading, otherwise bad.
		}
		// on Haswell, with 4 exec units to match 4 dispatch / cycle:
		// 10 AVX / AVX2 / AVX512 insns per GF bit -> 2.5 cycles per bit of the GF16
		// 16 / 32 / 64B at a time.  (avx1 lacks _mm256_srli_epi16, which is a showstopper.  no equiv like andps %ymm)
		// 2.5 / 1.25 / 0.625 cycles per source byte

		// SnB, 4 dispatch but only 3 execution units:
		// 10 AVX insns per GF bit -> 3.33 cycles per bit
		// in practice, runs ~1/4 the speed of SSE2 pinsrw128 on SnB (AVX).  3.64 vs. 0.970

		// two dep chains of 4 cycles (and/cmpeq/and/xor) means we need to
		// interleave operations on two source chunks at once, because OOExec queue depth is limited
		// (esp. with hyperthreading)

		// d = _mm_loadl_epi128(dst + i);
		__m128i d = _mm_xor_si128(prod, dst[i]);
		_mm_store_si128(&dst[i], d);
	}

//	_mm256_zeroupper();
}
