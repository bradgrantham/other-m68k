/* PDM encoder table (64 KB, ROM). Generated into build/pdmtab.bin by tools/pdmtab.py; see
 * that script for the layout. Indexed as pdm_table[((acc << 8) | sample) >> 1]. */
	.section .rodata
	.balign 2
	.globl	pdm_table
pdm_table:
	.incbin	"build/pdmtab.bin"
	.globl	pdm_table_end
pdm_table_end:
