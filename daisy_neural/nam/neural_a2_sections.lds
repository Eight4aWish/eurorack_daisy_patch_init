/*
 * neural_a2_sections.lds — our version of the NAM A2 supplemental linker
 * fragment. NOT lifted code: nam_a2_sections.lds sits alongside it, unmodified,
 * as the upstream reference.
 *
 * Why a separate file rather than using theirs directly:
 *
 * Theirs must be passed to ld BEFORE libDaisy's main script — correct, and
 * confirmed here the hard way, because ld takes the LAST -T as the main linker
 * script, so putting the fragment second makes ld treat IT as the main script
 * and the INSERT anchor vanishes ("`.heap` not found for insert").
 *
 * But passed first with ld 12.3.1, its `> RAM_D2` cannot bind: the region is
 * declared in libDaisy's script, which ld has not read yet.
 *
 *     warning: memory region `RAM_D2' not declared
 *
 * That is a SILENT failure, not a loud one. The link succeeds, the section
 * falls back into DTCMRAM, and you get a working but crowded build: measured
 * 105,344 B of 128 KB DTCMRAM used with RAM_D2 at zero. It boots and it runs.
 * It is just slower and nearly out of tightly-coupled memory.
 *
 * Upstream's header says "Do NOT add a MEMORY command here", which holds for
 * the ld version they built against. Here it is the only thing that makes the
 * region resolvable at the point the fragment is parsed. libDaisy declares the
 * same region immediately afterwards, so ld emits
 *
 *     warning: redeclaration of memory region `RAM_D2'
 *
 * which is expected and harmless — the two declarations are identical. The
 * values below are copied from STM32H750IB_sram.lds and must be kept in step
 * with it.
 *
 * ALWAYS CHECK THE REGION TABLE after touching this. RAM_D2 should carry the
 * ~76 KB history and DTCMRAM should drop to roughly a quarter full. If RAM_D2
 * reads 0 GB, the placement has failed again and nothing will tell you but
 * this table.
 */

MEMORY
{
    RAM_D2 (RWX) : ORIGIN = 0x30008000, LENGTH = 256K
}

SECTIONS
{
    .sram_d2_bss (NOLOAD) :
    {
        . = ALIGN(32);
        *(.sram_d2_bss)
        *(.sram_d2_bss*)
        . = ALIGN(32);
    } > RAM_D2
} INSERT BEFORE .heap
