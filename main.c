/*
 * test framework for par2 rs_process GF16 multiply-by-constant-factor function
 * Peter Cordes <peter@cordes.ca>
 * various asm version based on Vincent Tan's par2tbb version
 *
 * compile with:
 *  x86_64-w64-mingw32-gcc to make a.exe
 * gcc -g -Wall -march=native -funroll-loops -O3 -std=gnu11 main.c process-purec.c reedsolomon-x86_64-mmx.s reedsolomon-x86_64-mmx-orig.s asm-avx2-vgatherdd.s intrin-pinsrw.c asm-pinsrw*.s
 *
 * run with:
 * ./a.out -a 60
 * or (pin to CPU core #3)
 * taskset 0x04 ./a.out -a 60 && grep MHz /proc/cpuinfo
 *
 */


#include <unistd.h>	// getopt
#include <stdlib.h>	// atoi
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdint.h>
#include <stddef.h>	// ptrdiff_t

// #include <tbb/tbb.h>
//#include <intrin.h>

#include "asm-test.h"

#define ALIGN16 __attribute__ ((aligned (16)))
#define ALIGN64 __attribute__ ((aligned (64)))
#define ALIGN(x) __attribute__ ((aligned (x)))

#if defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__) >= 408 // GCC 4.8 or later.
// CLANG defines a lower version number, and doesn't have __builtin_cpu_supports
#define HAVE_AVX2 __builtin_cpu_supports("avx2")
#else
#define HAVE_AVX2 0
#endif

static __inline__ uint64_t rdtsc() {
    uint32_t low, high;
/*    __asm__ __volatile__ (
        "xorl %%eax,%%eax \n    cpuid"
        ::: "%rax", "%rbx", "%rcx", "%rdx" ); */
    __asm__ __volatile__ (
                          "rdtsc" : "=a" (low), "=d" (high));
    return (uint64_t)high << 32 | low;
}

/*extern "C" */
void SYSV_ABI rs_process_x86_64_vgather(void* dst, const void* src, size_t size, const uint32_t* LH);
void SYSV_ABI rs_process_testloop_align32(void* dst, const void* src, size_t size, const uint32_t* LH);
void SYSV_ABI rs_process_x86_64_mmx(void* dst, const void* src, size_t size, const uint32_t* LH);
void SYSV_ABI rs_process_x86_64_mmx_orig(void* dst, const void* src, size_t size, const uint32_t* LH);
void SYSV_ABI rs_process_pinsrw_mmx(void* dst, const void* src, size_t size, const uint32_t* LH);
void SYSV_ABI rs_process_pinsrw_unpipelined(void* dst, const void* src, size_t size, const uint32_t* LH);
void SYSV_ABI rs_process_pinsrw64(void* dst, const void* src, size_t size, const uint32_t* LH);
void SYSV_ABI rs_process_pinsrw128(void* dst, const void* src, size_t size, const uint32_t* LH);
// rs_process_pinsrw_intrin
void SYSV_ABI rs_dummy(void* dst, const void* src, size_t size, const uint32_t* LH) { }


#define ITERS 100
#define BUFSIZE (1024*1024)
//#define BUFSIZE (1024*64*16*64)
//#define BUFSIZE (1024*8)
//#define ITERS 100*128
char dstbuf[BUFSIZE] ALIGN(4096);
char srcbuf[BUFSIZE] ALIGN(4096);

typedef void SYSV_ABI (rs_procfunc_t) (void* , const void*, size_t , const uint32_t*);

// typedef (*rs_procfunc_t) rs_procfunc;

static uint64_t time_rs(rs_procfunc_t *fn, void* dst, const void* src, size_t size, const uint32_t* LH)
{
	uint64_t starttime, stoptime;
	starttime = rdtsc();
	const int maxiter = ITERS;
	for (int c=0 ; c<maxiter ; c++) {
		fn(dst, src, size, LH);
	}

	stoptime = rdtsc();
	return stoptime - starttime;
}

static uint64_t time_rs_print(const char* name, rs_procfunc_t *fn, void* dst, const void* src, size_t size, const uint32_t* LH)
{
	uint64_t tdelta = time_rs (fn, dst, src, size, LH);
	printf("%-16s: %" PRIu64 " rdtsc counts for %d iters\n", name, tdelta, ITERS);
	return tdelta;
}

int main (int argc, char *argv[])
{
	unsigned int lhTable[256*2 *1 + 64] ALIGN(2048);  // include an 64*size to make room for shifting around
	for (int i=0; i<512; i++) {
		lhTable[i] = i;
	}

	size_t size = sizeof(srcbuf);
//	size = 128;

	int align = -1;		// negative: no tweaking of src bytes.  else: alignment of every LUT lookup within a 64B cache line

	int opt;
	while ((opt = getopt(argc, argv, "a:")) != -1) {
		switch (opt) {
		case 'a':
			align = atoi(optarg);
			break;
		default: /* '?' */
			fprintf(stderr, "Usage: %s [-a align(multiple of %d)]\n",
				argv[0], (int)sizeof(*lhTable));
			exit(EXIT_FAILURE);
		}
	}

	ptrdiff_t LH_adjusted = (ptrdiff_t)lhTable;
	if (align >= 0 && align & 0x3) {		// FIXME: not based on sizeof
		int align_lowbits = align & 0x3;
		LH_adjusted |= align_lowbits;
		// -a 63 confirms that cacheline splits are a MAJOR speed hit (factor of 2.7 slowdown)
//		printf ("shifting LH not implemented yet, align has to be a multiple of %d\n", (int)sizeof(*lhTable));
//		return 2;
	}
	typeof (*lhTable) *LH = (void*)LH_adjusted;

	uint16_t *srcwords = (uint16_t *) srcbuf;
	for (int i=0; i < size / 2; i++) {
		if (align < 0) {
			srcwords[i] = i;
		} else {
			// This only gets the low byte lookups
			srcwords[i] = i*(64/sizeof(*lhTable)) + align/sizeof(*lhTable);
		}
	}
	memset(dstbuf, 0, size);

	printf("LH = %p.  size=%" PRIu64 "\n", LH, size);

	time_rs_print ("pinsrw128     ", rs_process_pinsrw128, dstbuf, srcbuf, size, LH);
	time_rs_print ("orig MMX-unpck", rs_process_x86_64_mmx_orig, dstbuf, srcbuf, size, LH);
	time_rs_print ("dummy         ", rs_dummy, dstbuf, srcbuf, size, LH);
	time_rs_print ("MMX w/ 64b rdx", rs_process_x86_64_mmx, dstbuf, srcbuf, size, LH);
	time_rs_print ("pinsrw-intrin ", rs_process_pinsrw_intrin, dstbuf, srcbuf, size, LH);
	time_rs_print ("pinsrw-unpipe ", rs_process_pinsrw_unpipelined, dstbuf, srcbuf, size, LH);
	time_rs_print ("Pure C        ", rs_process_purec, dstbuf, srcbuf, size, LH);
	puts ("----------------");

	for (int i=0 ; i<3 ; i++) {
		time_rs_print ("orig MMX-unpck", rs_process_x86_64_mmx_orig, dstbuf, srcbuf, size, LH);
		time_rs_print ("MMX w/ 64b rdx", rs_process_x86_64_mmx, dstbuf, srcbuf, size, LH);
		time_rs_print ("pinsrw-mmx    ", rs_process_pinsrw_mmx, dstbuf, srcbuf, size, LH);
		time_rs_print ("pinsrw64      ", rs_process_pinsrw64, dstbuf, srcbuf, size, LH);
		time_rs_print ("pinsrw128     ", rs_process_pinsrw128, dstbuf, srcbuf, size, LH);
		// fflush(stdout);
		if (HAVE_AVX2) {
			time_rs_print ("AVX2 vgather  ", rs_process_testloop_align32, dstbuf, srcbuf, size, LH);
			time_rs_print ("AVX2 vgather  ", rs_process_testloop_align32, dstbuf, srcbuf, size, LH);
		}
	}

	puts ("----------------");
	time_rs_print ("Pure C        ", rs_process_purec, dstbuf, srcbuf, size, LH);
	time_rs_print ("pinsrw128     ", rs_process_pinsrw128, dstbuf, srcbuf, size, LH);
	time_rs_print ("orig MMX-unpck", rs_process_x86_64_mmx_orig, dstbuf, srcbuf, size, LH);
	time_rs_print ("MMX w/ 64b rdx", rs_process_x86_64_mmx, dstbuf, srcbuf, size, LH);
	time_rs_print ("pinsrw-intrin ", rs_process_pinsrw_intrin, dstbuf, srcbuf, size, LH);
	time_rs_print ("pinsrw-unpipe ", rs_process_pinsrw_unpipelined, dstbuf, srcbuf, size, LH);

	return 0;
}
