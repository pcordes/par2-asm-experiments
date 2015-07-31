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

union wordbytes { uint16_t w; uint8_t b[2]; };

void SYSV_ABI rs_process_pinsrw_intrin(void* dstvoid, const void* srcvoid, size_t size, const uint32_t* LH)
{
//	prefetchT0      (%rdi)
//	srcvoid &= ~ (ptrdiff_t) 0x0f;
//	srcvoid = FORCE_ALIGN16(srcvoid);
//	dstvoid = FORCE_ALIGN16(dstvoid);	// compiler generates an AND $-16, %rax before using dst, for some reason, but this doesn't help
//	const uint64_t *src = FORCE_ALIGN16(srcvoid);
	const uint64_t *src = srcvoid;
	__m128i *dst = dstvoid;

	const typeof(LH) L = LH;
	const typeof(LH) H = LH + 256;

//	_mm256_zeroupper();
	for (size_t i = 0; i < size/sizeof(*dst) ; i+=1) {

#define LO(x) ((uint8_t)x)
//#define HI(x) ((uint8_t)((x >> 8) & 0xff))
//#define HI(x) ( (uint8_t) (((uint16_t)x) >> 8) )  // This gets gcc to emit  movz %bh, %eax, unlike the & 0xff version
// u8(((u16) s) >>  8)
#define HI(x) (( (union wordbytes) ((uint16_t)x) ).b[1])  // fastest code, but still horrible; WAY too much useless mov

		uint64_t s0 = src[i*2 + 0];
		uint64_t s1 = src[i*2 + 1];

		__m128i l0 = _mm_cvtsi32_si128( L[ LO(s0)] );		// movd the lookup for the l/h byte of the first word
		__m128i h0 = _mm_cvtsi32_si128( H[ HI(s0)] );
		s0 >>= 16;
		l0 = _mm_insert_epi16(l0, L[ LO(s0)], 1);
		h0 = _mm_insert_epi16(h0, H[ HI(s0)], 1);
		s0 >>= 16;

		__m128i l1 = _mm_cvtsi32_si128( L[ LO(s1)] );
		__m128i h1 = _mm_cvtsi32_si128( H[ HI(s1)] );
		s1 >>= 16;

		l0 = _mm_insert_epi16(l0, L[ LO(s0)], 2);
		h0 = _mm_insert_epi16(h0, H[ HI(s0)], 2);
		s0 >>= 16;

		l1 = _mm_insert_epi16(l1, L[ LO(s1)], 1);
		h1 = _mm_insert_epi16(h1, H[ HI(s1)], 1);
		s1 >>= 16;

		l0 = _mm_insert_epi16(l0, L[ LO(s0)], 3);
		h0 = _mm_insert_epi16(h0, H[ HI(s0)], 3);

		l1 = _mm_insert_epi16(l1, L[ LO(s1)], 2);
		h1 = _mm_insert_epi16(h1, H[ HI(s1)], 2);
		s1 >>= 16;

		l1 = _mm_insert_epi16(l1, L[ LO(s1)], 3);
		h1 = _mm_insert_epi16(h1, H[ HI(s1)], 3);
#undef LO
#undef HI
		__m128i d0 = _mm_xor_si128(l0, h0);
		__m128i d1 = _mm_xor_si128(l1, h1);

		__m128i d = _mm_unpacklo_epi64(d0, d1);

		__m128i result = _mm_xor_si128(d, dst[i]);
		_mm_store_si128(&dst[i], result);
	}

//	_mm256_zeroupper();
}
