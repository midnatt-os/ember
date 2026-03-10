#include "cpu/msr.h"
#include "sys/init.h"

#include <stdint.h>

/* Courtesy of wux <3
  - PA0: WB  (Write Back)
  - PA1: WT  (Write Through)
  - PA2: UC- (Uncached)
  - PA3: UC  (Uncacheable)
  - PA4: WB  (Write Back)
  - PA5: WP  (Write Protected)
  - PA6: WC  (Write Combining)
  - PA7: UC  (Uncacheable)
*/

INIT_TARGET(pat, INIT_STAGE_EARLY, INIT_SCOPE_ALL, INIT_DEPS()) {
    uint64_t pat = msr_read(MSR_PAT);
    pat &= ~(((uint64_t) 0b111 << 48) | ((uint64_t) 0b111 << 40)); // clear PAT[5] and PAT[6]
    pat |= ((uint64_t) 0x1 << 48) | ((uint64_t) 0x5 << 40); // PAT[5] = WP; PAT[6] = WC
    msr_write(MSR_PAT, pat);
}
