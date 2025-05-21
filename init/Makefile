CC := clang
CFLAGS := -static -ffreestanding -nostdlib -nodefaultlibs -Wall -Wextra

TARGET := init.elf
SRC := init.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	@echo "→ Compiling $< with $(CC)"
	$(CC) $(CFLAGS) -o $@ $<

clean:
	@echo "→ Removing $(TARGET)"
	@rm -f $(TARGET)