#pragma once

#include <limine.h>

volatile extern struct limine_framebuffer_request framebuffer_request;
volatile extern struct limine_hhdm_request hhdm_request;
volatile extern struct limine_memmap_request memmap_request;
volatile extern struct limine_module_request module_request;
volatile extern struct limine_executable_address_request executable_address_request;
volatile extern struct limine_rsdp_request rsdp_request;
volatile extern struct limine_mp_request mp_request;
volatile extern struct limine_executable_file_request executable_file_request;

struct limine_file* find_limine_module(const char* name);
