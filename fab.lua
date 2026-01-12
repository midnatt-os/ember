local opt_logging = fab.option("logging", { "serial", "fb", "all" }) or "serial"

local c = require("lang_c")
local asm = require("lang_nasm")
local ld = require("ld")

local kernel_sources = sources(fab.glob("kernel/**/*.{c,asm}"))
local include_dirs = { c.include_dir("kernel") }

local kernel_c_flags = {
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

local module_c_flags = { "-ffreestanding", "-fPIC", "-fno-plt", "-mno-red-zone", "-mgeneral-regs-only",
    "-fno-stack-protector", "-fno-stack-check", "-fno-strict-aliasing", "-fno-lto", "-g", "-std=gnu23", "-nostdinc",
    "-fno-omit-frame-pointer" }

if opt_logging == "fb" then
    table.insert(kernel_c_flags, "-DLOGGING_FB")
elseif opt_logging == "serial" then
    table.insert(kernel_c_flags, "-DLOGGING_SERIAL")
elseif opt_logging == "all" then
    table.insert(kernel_c_flags, "-DLOGGING_FB")
    table.insert(kernel_c_flags, "-DLOGGING_SERIAL")
end

local linker_flags = {
    "-static",
}

local cc = c.get_clang()
assert(cc ~= nil, "Clang not found")

local linker = ld.get_linker()
assert(linker ~= nil, "No viable linker found")

local nasm = asm.get_nasm()
assert(nasm ~= nil, "Nasm not found")

local cc_runtime = fab.git(
    "cc-runtime",
    "https://codeberg.org/osdev/cc-runtime.git",
    "dae79833b57a01b9fd3e359ee31def69f5ae899b"
)

local freestanding_c_headers = fab.git(
    "freestanding-c-headers",
    "https://codeberg.org/osdev/freestnd-c-hdrs.git",
    "4039f438fb1dc1064d8e98f70e1cf122f91b763b"
)

local limine = fab.git(
    "limine-proto",
    "https://codeberg.org/Limine/Limine-protocol.git",
    "v10.x"
)

local nanoprintf = fab.git(
    "nanoprintf",
    "https://github.com/charlesnicholson/nanoprintf.git",
    "main"
)

local flanterm = fab.git(
    "flanterm",
    "https://codeberg.org/mintsuki/flanterm.git",
    "trunk"
)

local uacpi = fab.git(
    "uacpi",
    "https://github.com/uACPI/uACPI.git",
    "3.1.0"
)

table.extend(kernel_sources, sources(
    path(cc_runtime.path, "src/cc-runtime.c"),
    fab.glob("**/*.c", { relative_to = flanterm.path }),
    fab.glob("source/*.c", { relative_to = uacpi.path })
))

table.extend(include_dirs, {
    c.include_dir(path(freestanding_c_headers.path, "x86_64/include")),
    c.include_dir(path(limine.path, "include")),
    c.include_dir(nanoprintf.path),
    c.include_dir(path(flanterm.path, "src")),
    c.include_dir(path(uacpi.path, "include")),
})

-- Modules
local modules = {}
for _, mod in ipairs(sources(fab.glob("modules/**"))) do
    local path = mod.path
    local components = path:split("/")
    local last_component = components[#components]
    modules[last_component:sub(0, -3)] = mod
end

local objs = generate(
    kernel_sources,
    {
        c = function(sources)
            return cc:generate(sources, kernel_c_flags, include_dirs)
        end,
        asm = function(sources)
            return nasm:generate(sources, { "-g", "-f elf64", "-Werror" })
        end
    }
)

local linker_script = fab.def_source("support/link_ember.ld")
local kernel = linker:link("ember", objs, linker_flags, linker_script)

-- Build Modules
local module_installs = {}
for name, source in pairs(modules) do
    local obj = cc:compile_object(name .. ".o", source, include_dirs, module_c_flags)
    local mod = linker:link(name, { obj }, { "-shared", "-nostdlib", "-z,now", "-z,relro", "-z,nocopyreloc" })
    module_installs["modules/" .. name .. ".mod"] = mod
end

return {
    install = {
        ["bin/ember"] = kernel,
        table.unpack(module_installs)
    }
}
