//#define _GNU_SOURCE
#include <emmintrin.h>
#include <immintrin.h> // vzeroupper
#include <stdint.h>
#include <stddef.h> // ptrdiff_t
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
	srcvoid = FORCE_ALIGN16(srcvoid);
	dstvoid = FORCE_ALIGN16(dstvoid);	// compiler generates an AND $-16, %rax before using dst, for some reason, but this doesn't help
	__m128i d, l, h;
	__m128i *dst = dstvoid;

	_mm256_zeroupper();
	uint64_t s0, s1;
	for (size_t i = 0; i < size ; i+=16) {
		s0 = *((uint64_t*) ((char*)srcvoid + i));	// byte address math, 64bit load
		s1 = *((uint64_t*) ((char*)srcvoid+8 + i));	// byte address math, 64bit load

#define LO(x) ((uint8_t)x)
//#define HI(x) ((uint8_t)((x >> 8) & 0xff))
//#define HI(x) ( (uint8_t) (((uint16_t)x) >> 8) )  // This gets gcc to emit  movz %bh, %eax, unlike the & 0xff version
// u8(((u16) s) >>  8)
#define HI(x) (( (union wordbytes) ((uint16_t)x) ).b[1])  // fastest code, but still horrible; WAY too much useless mov

		l = _mm_cvtsi32_si128( LH[      LO(s0)] );		// movd the lookup for the l/h byte of the first word
		h = _mm_cvtsi32_si128( LH[256 + HI(s0)] );
		s0 >>= 16;
		l = _mm_insert_epi16(l, LH[      LO(s1)], 4);
		h = _mm_insert_epi16(h, LH[256 + HI(s1)], 4);
		s1 >>= 16;

		l = _mm_insert_epi16(l, LH[      LO(s0)], 1);
		h = _mm_insert_epi16(h, LH[256 + HI(s0)], 1);
		s0 >>= 16;
		l = _mm_insert_epi16(l, LH[      LO(s1)], 5);
		h = _mm_insert_epi16(h, LH[256 + HI(s1)], 5);
		s1 >>= 16;

		l = _mm_insert_epi16(l, LH[      LO(s0)], 2);
		h = _mm_insert_epi16(h, LH[256 + HI(s0)], 2);
		s0 >>= 16;
		l = _mm_insert_epi16(l, LH[      LO(s1)], 6);
		h = _mm_insert_epi16(h, LH[256 + HI(s1)], 6);
		s1 >>= 16;

		l = _mm_insert_epi16(l, LH[      LO(s0)], 3);
		h = _mm_insert_epi16(h, LH[256 + HI(s0)], 3);
		l = _mm_insert_epi16(l, LH[      LO(s1)], 7);
		h = _mm_insert_epi16(h, LH[256 + HI(s1)], 7);
#undef LO
#undef HI
		// d = _mm_loadl_epi128(dst + i);
		d = _mm_xor_si128(l, h);	// gcc 4.9 uses a 3rd xmm reg for no reason, even with l instead of d
		d = _mm_xor_si128(d, *(dst+i/16));
		_mm_store_si128(dst+i/16, d);
	}

	_mm256_zeroupper();
}
