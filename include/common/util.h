#pragma once

#define ALIGN_DOWN(x, align) ((x) & ~((typeof(x))(align) - 1))

#define ALIGN_UP(x, align) (((x) + ((typeof(x))(align) - 1)) & ~((typeof(x))(align) - 1))