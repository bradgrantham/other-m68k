/* Musashi configuration for the other-m68k emulator.
 * Selected via -DMUSASHI_CNF (see Makefile) so the vendored musashi/m68kconf.h
 * is never used.  68000 only.
 */
#ifndef M68KCONF__HEADER
#define M68KCONF__HEADER

#define M68K_OPT_OFF             0
#define M68K_OPT_ON              1
#define M68K_OPT_SPECIFY_HANDLER 2

#define M68K_COMPILE_FOR_MAME      M68K_OPT_OFF

/* CPU variants: 68000 only */
#define M68K_EMULATE_010            M68K_OPT_OFF
#define M68K_EMULATE_EC020          M68K_OPT_OFF
#define M68K_EMULATE_020            M68K_OPT_OFF
#define M68K_EMULATE_030            M68K_OPT_OFF
#define M68K_EMULATE_040            M68K_OPT_OFF

#define M68K_SEPARATE_READS         M68K_OPT_OFF
#define M68K_SIMULATE_PD_WRITES     M68K_OPT_OFF

/* IACK: callback (set with m68k_set_int_ack_callback) returns
 * M68K_INT_ACK_AUTOVECTOR; IPL stays level-triggered until the glue lowers it. */
#define M68K_EMULATE_INT_ACK        M68K_OPT_ON
#define M68K_INT_ACK_CALLBACK(A)    your_int_ack_handler_function(A)

#define M68K_EMULATE_BKPT_ACK       M68K_OPT_OFF
#define M68K_BKPT_ACK_CALLBACK()    your_bkpt_ack_handler_function()

#define M68K_EMULATE_TRACE          M68K_OPT_OFF

/* RESET instruction: callback does nothing (the glue is not reset, see
 * docs/architecture.md section 6). */
#define M68K_EMULATE_RESET          M68K_OPT_ON
#define M68K_RESET_CALLBACK()       your_reset_handler_function()

#define M68K_CMPILD_HAS_CALLBACK     M68K_OPT_OFF
#define M68K_CMPILD_CALLBACK(v,r)    your_cmpild_handler_function(v,r)
#define M68K_RTE_HAS_CALLBACK       M68K_OPT_OFF
#define M68K_RTE_CALLBACK()         your_rte_handler_function()
#define M68K_TAS_HAS_CALLBACK       M68K_OPT_OFF
#define M68K_TAS_CALLBACK()         your_tas_handler_function()
#define M68K_ILLG_HAS_CALLBACK      M68K_OPT_OFF
#define M68K_ILLG_CALLBACK(opcode)  your_op_illg_handler_function(opcode)
#define M68K_TRAP_HAS_CALLBACK      M68K_OPT_OFF
#define M68K_TRAP_CALLBACK(trap)    your_op_trap_handler_function(trap)

/* Function code: zero-cost macro that records the current FC so the bus layer
 * can fill the 68000 group-0 bus-error frame (access-info word). */
extern unsigned int emu_fc;
#define M68K_EMULATE_FC             M68K_OPT_SPECIFY_HANDLER
#define M68K_SET_FC_CALLBACK(A)     (emu_fc = (A))

#define M68K_MONITOR_PC             M68K_OPT_OFF
#define M68K_SET_PC_CALLBACK(A)     your_pc_changed_handler_function(A)

/* Instruction hook via m68k_set_instr_hook_callback(); only installed when
 * --trace / --break are requested. */
#define M68K_INSTRUCTION_HOOK       M68K_OPT_ON
#define M68K_INSTRUCTION_CALLBACK(pc) your_instruction_hook_function(pc)

#define M68K_EMULATE_PREFETCH       M68K_OPT_OFF
#define M68K_EMULATE_ADDRESS_ERROR  M68K_OPT_ON

#define M68K_LOG_ENABLE             M68K_OPT_OFF
#define M68K_LOG_1010_1111          M68K_OPT_OFF
#define M68K_LOG_TRAP               M68K_OPT_OFF
#define M68K_LOG_FILEHANDLE         some_file_handle

#define M68K_EMULATE_PMMU           M68K_OPT_OFF
#define M68K_USE_64_BIT             M68K_OPT_ON

#endif /* M68KCONF__HEADER */
