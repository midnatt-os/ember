project('aloe', ['c', 'nasm'])

sources = []
includes = []
deps = []

includes += include_directories('include')

cflags = [
    '-std=gnu23',
    '-ffreestanding',
    '-nostdinc',

    '-Wall',
    '-Wextra',
    '-Werror',
    '-Wimplicit-fallthrough',

    '-fno-stack-protector',
    '-fno-stack-check',
    '-fno-strict-aliasing',

    '-O0',
    '-g',
    '-fno-lto',
    '-fno-omit-frame-pointer',

    '--target=x86_64-none-elf',
    '-mcmodel=kernel',
    '-mno-red-zone',
    '-mgeneral-regs-only',
    '-mabi=sysv'
]

ldflags = [
    '-nostdlib',
    '-static',
    '-Wl,-T' + meson.project_source_root() / 'support/link_aloe.ld',
]

deps += dependency('cc-runtime', required: true)
deps += dependency('freestnd-c-hdrs', required: true)
deps += dependency('limine', required: true)
deps += dependency('nanoprintf', required: true)

subdir('kernel')

aloe = executable(
    'aloe',
    sources: sources,
    include_directories: includes,
    c_args: cflags,
    nasm_args: ['-f elf64', '-Werror'],
    link_args: ldflags,
    dependencies: deps,
)
