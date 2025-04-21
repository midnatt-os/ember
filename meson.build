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

deps += dependency(
    'cc-runtime',
    required: true,
    fallback: ['cc-runtime', 'cc_runtime_deps']
)

deps += dependency(
    'freestnd-c-hdrs-' + host_machine.cpu_family(),
    required: true,
    fallback: ['freestnd-c-hdrs', 'freestnd_c_hdrs_dep']
)

deps += dependency('limine', required: true)
deps += dependency('nanoprintf', required: true)

#limine_sp = subproject('limine')
#deps += limine_sp.get_variable('limine_dep')

subdir('kernel')

aloe = executable(
    'aloe',
    sources: sources,
    include_directories: includes,
    c_args: cflags,
    link_args: ldflags,
    dependencies: deps,
)
