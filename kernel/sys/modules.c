#include "sys/modules.h"

#include "lib/elf.h"


bool module_load(const void* elf, [[maybe_unused]] size_t size, module_t* out_mod) {
    *out_mod = (module_t) { 0 };

    const elf64_ehdr_t* ehdr = elf;
    if (!elf_validate(ehdr))
        return false;

    elf_image_t img;
    elf_map_segments(elf, ehdr, &global_as, &img);

    dyn_info_t di;
    if (!elf_dyn_parse(elf, ehdr, &img, &di)) {
        vm_unmap(&global_as, img.base, img.size);
        return false;
    }

    if (!elf_apply_relocations(&di, &img)) {
        vm_unmap(&global_as, img.base, img.size);
        return false;
    }

    elf_finalize_protections(elf, ehdr, &img, &global_as);

    uintptr_t init_addr = elf_find_func(&di, &img, "init");
    uintptr_t deinit_addr = elf_find_func(&di, &img, "deinit");

    if (!(init_addr && deinit_addr)) {
        vm_unmap(&global_as, img.base, img.size);
        return false;
    }

    *out_mod = (module_t) {
        .base = img.base,
        .size = img.size,
        .init = (void (*)(void)) init_addr,
        .deinit = (void (*)(void)) deinit_addr,
    };

    return true;
}
