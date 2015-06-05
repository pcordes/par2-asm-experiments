#define SYSV_ABI __attribute__((sysv_abi))

// stand-alone ASM functions are written for the Linux calling convention,
// not win64 convention with args in different regs.
void SYSV_ABI rs_process_pinsrw_intrin(void* dstvoid, const void* srcvoid, size_t size, const uint32_t* LH);
void SYSV_ABI rs_process_purec(void* dstvoid, const void* srcvoid, size_t size, const uint32_t* LH);
void SYSV_ABI rs_process_nolut_intrin(void* dstvoid, const void* srcvoid, size_t size, const uint32_t* LH);
