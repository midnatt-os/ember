local opt_logging = fab.option("logging", { "serial", "fb", "all" }) or "serial"
local kernel_sources = sources(fab.glob("kernel/**/*.{c,asm}", "modules/**"))

local include_dirs = { builtins.c.include_dir("kernel") }

local c_flags = {
    "-std=gnu23",
    "-ffreestanding",
    "-nostdinc",

    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wno-implicit-fallthrough",

    "-fno-stack-protector",
    "-fno-stack-check",
    "-fno-strict-aliasing",

    "-fsanitize=undefined",

    "-O0",
    "-g",
    "-fno-lto",
    "-fno-omit-frame-pointer",

    "--target=x86_64-none-elf",
    "-mcmodel=kernel",
    "-mno-red-zone",
    "-mgeneral-regs-only",
    "-mabi=sysv",

    "-DUACPI_FORMATTED_LOGGING",
    "-DUACPI_SIZED_FREES",
    "-DLIMINE_API_REVISION=4",
}

if opt_logging == "fb" then
    table.insert(c_flags, "-DLOGGING_FB")
elseif opt_logging == "serial" then
    table.insert(c_flags, "-DLOGGING_SERIAL")
elseif opt_logging == "all" then
    table.insert(c_flags, "-DLOGGING_FB")
    table.insert(c_flags, "-DLOGGING_SERIAL")
end

local linker_flags = {
    "-static",
    "-T" .. fab.path_rel("support/link_ember.ld")
}

local cc = builtins.c.get_compiler("clang")
if cc == nil then
    error("Clang not found")
end

local linker = builtins.get_linker()
if linker == nil then
    error("No viable linker found")
end

local nasm = builtins.nasm.get_assembler()
if nasm == nil then
    error("Nasm not found")
end


local cc_runtime = fab.dependency(
    "cc_runtime",
    "https://codeberg.org/osdev/cc-runtime.git",
    "d5425655388977fa12ff9b903e554a20b20c426e"
)

local freestnd_c_hdrs = fab.dependency(
    "freestnd_c_hdrs",
    "https://codeberg.org/osdev/freestnd-c-hdrs.git",
    "trunk"
)

local limine = fab.dependency(
    "limine",
    "https://codeberg.org/Limine/Limine.git",
    "v10.2.1-binary"
)

local nanoprintf = fab.dependency(
    "nanoprintf",
    "https://github.com/charlesnicholson/nanoprintf.git",
    "main"
)

local flanterm = fab.dependency(
    "flanterm",
    "https://codeberg.org/mintsuki/flanterm.git",
    "trunk"
)

local uacpi = fab.dependency(
    "uacpi",
    "https://github.com/uACPI/uACPI.git",
    "2.1.1"
)

table.extend(kernel_sources, sources(path(cc_runtime.path, "cc-runtime.c")))
table.extend(kernel_sources, sources(flanterm:glob("**/*.c")))
table.extend(kernel_sources, sources(uacpi:glob("source/*.c")))

table.extend(include_dirs, {
    builtins.c.include_dir(path(freestnd_c_hdrs.path, "x86_64/include")),
    builtins.c.include_dir(limine.path),
    builtins.c.include_dir(nanoprintf.path),
    builtins.c.include_dir(path(flanterm.path, "src")),
    builtins.c.include_dir(path(uacpi.path, "include")),
})

-- Modules
local modules = {}
for _, src in ipairs(sources(fab.glob("modules/**"))) do
    modules[src.name:sub(0, -3)] = src
end

local objs = builtins.generate(
    kernel_sources,
    {
        c = function(sources)
            return cc:generate(sources, c_flags, include_dirs)
        end,
        asm = function(sources)
            return nasm:generate(sources, { "-g", "-f elf64", "-Werror" })
        end
    }
)

local kernel = linker:link("ember", objs, linker_flags)
kernel:install("bin/ember")

-- Build Modules
local cflags_module = {
    "-ffreestanding",
    "-fPIC",
    "-fno-plt",
    "-mno-red-zone",
    "-mgeneral-regs-only",
    "-fno-stack-protector",
    "-fno-stack-check",
    "-fno-strict-aliasing",
    "-fno-lto",
    "-g",
    "-std=gnu23",
    "-nostdinc",
    "-fno-omit-frame-pointer",
    "-mcmodel=kernel",
}

for name, source in pairs(modules) do
    local obj = cc:compile_object(name .. ".o", source, include_dirs, cflags_module)
    linker:link(name, { obj }, { "-shared", "-nostdlib", "-z,now", "-z,relro", "-z,nocopyreloc" })
        :install("modules/" .. name .. ".mod")
end
