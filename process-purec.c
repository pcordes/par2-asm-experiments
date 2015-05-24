#define _GNU_SOURCE
// #include <emmintrin.h>
#include <stdint.h>
#include <stddef.h> // ptrdiff_t
#include "asm-test.h"

#define FORCE_ALIGN16(x) (void*)(  ((ptrdiff_t)x) & ~(ptrdiff_t)0x0f  )

union wordbytes { uint16_t w; uint8_t b[2]; };

/* 32b version with gcc 4.9, on Intel SnB: 1MiB in/out buffers, 100iters
 * 163878678 rdtsc counts:  -O3
 * 143510736 rdtsc counts:  -O3  -funroll-loops
 * no effect: -funsafe-loop-optimizations -faggressive-loop-optimizations
 * no effect: -fmodulo-sched
 * no effect: -flive-range-shrinkage
 */

/* gcc 4.9: 64b version is slower than 32b
 * because the compiler won't s0 >>= 16, but instead does a lot of extra reg-reg moves
 * The 32bit version does load32; movzx %al/%ah; shr $16; movzx %al/%ah, like it should
 *
 * Clang 3.5 for 32b version: uses 4 insn (mov, shr, movzx;  shr) instead of 3 to unpack the high 16 of each 32b load
 * And doesn't unroll, so a lot of loop overhead
 */
#define rs_process_purec_32b rs_process_purec



void SYSV_ABI rs_process_purec_64b(void* dstvoid, const void* srcvoid, size_t size, const uint32_t* LH)
{
//	prefetchT0      (%rdi)
//	srcvoid &= ~ (ptrdiff_t) 0x0f;
//	srcvoid = FORCE_ALIGN16(srcvoid);
//	dstvoid = FORCE_ALIGN16(dstvoid);	// compiler generates an AND $-16, %rax before using dst, for some reason, but this doesn't help
//	long *dst = dstvoid;

	// GCC is silly and keeps L and H in separate regs, instead of using an addressing mode with a displacement
	// or even worse, generates add $256, %index_reg   and then uses a load with no displacement
	typeof(LH) L = LH;
	typeof(LH) H = LH+256;

	size &= ~0x07;	// multiple of 8
	const uint64_t *src64 = srcvoid;
	uint64_t *dst64 = dstvoid;
	for (size_t i = 0; i < size/sizeof(*dst64) ; i+=1) {

#define LO(x) ((uint8_t)x)
//#define HI(x) ((uint8_t)((x >> 8) & 0xff))
//#define HI(x) ( (uint8_t) (((uint16_t)x) >> 8) )  // This gets gcc to emit  movz %bh, %eax, unlike the & 0xff version
#define HI(x) (( (union wordbytes) ((uint16_t)x) ).b[1])

		uint64_t l0,h0, l1,h1, l2,h2, l3,h3;
		uint64_t s0 = src64[i];
		l0 = L[ LO(s0)];
		h0 = H[ HI(s0)];

		s0 >>= 16;
		l1 = L[ LO(s0)];
		h1 = H[ HI(s0)];
//		uint64_t d = (l0 | l1<<16) ^ (h0 | h1<<16); // FIXME: endian
		uint64_t d0 = (l0 ^ h0) | ((l1 ^ h1)<<16);  // FIXME: endian.  // Generates better code (gcc 4.9)

		s0 >>= 16;
		l2 = L[ LO(s0)];
		h2 = H[ HI(s0)];
		s0 >>= 16;
		l3 = L[ LO(s0)];
		h3 = H[ HI(s0)];

		uint64_t d1 = (l2 ^ h2) | ((l3 ^ h3)<<16);
		uint64_t d = d0 | (d1 << 32);
		dst64[i] ^= d;

#undef LO
#undef HI
	}

}


/**************** uint32_t version ****************/
void SYSV_ABI rs_process_purec_32b(void* dstvoid, const void* srcvoid, size_t size, const uint32_t* LH)
{
	typeof(LH) L = LH;
	typeof(LH) H = LH+256;

	size &= ~0x07;	// multiple of 8
	const uint32_t *src32 = srcvoid;
	uint32_t *dst32 = dstvoid;
	for (size_t i = 0; i < size/sizeof(uint32_t) ; i+=1) {

#define LO(x) ((uint8_t)x)
#define HI(x) (( (union wordbytes) ((uint16_t)x) ).b[1])  // Endian?

		uint32_t l0, h0, l1, h1;
		uint32_t s0 = src32[i];
		l0 = L[ LO(s0)];
		h0 = H[ HI(s0)];
		s0 >>= 16;

		l1 = L[ LO(s0)];
		h1 = H[ HI(s0)];
//		uint32_t d = (l0 | l1<<16) ^ (h0 | h1<<16); // FIXME: endian
		uint32_t d = (l0 ^ h0) | ((l1 ^ h1)<<16);  // FIXME: endian.  // Generates better code (gcc 4.9)
		dst32[i] ^= d;

		// TODO: save the <<16 by loading l1/h1 offset by -2 bytes,
		// to get the zero-padding from the previous entry
		// requires a padding entry at the front of the table
		// mmx punpck does this, but if src bytes hit the beginning of a cache line very often, it's a big slowdown
#undef LO
#undef HI
	}

}
