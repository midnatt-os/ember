CC := clang
CFLAGS := -static -ffreestanding -nostdlib -nodefaultlibs -Wall -Wextra

TARGET := init.elf
SRC := init.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	@rm -f $(TARGET)
