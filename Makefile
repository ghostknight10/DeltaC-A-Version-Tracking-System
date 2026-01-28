# Compiler and flags
CC = gcc
# -Wall = all warnings. -Iinclude = look in 'include' folder for headers
CFLAGS = -Wall -Iinclude $(shell pkg-config --cflags gtk4)
LDFLAGS = $(shell pkg-config --libs gtk4)

# Project files

# List all your .c files *with their full path*
# (I'm assuming you use context_menu.c based on your screenshot)
SOURCES = main.c src/sidebar.c src/context_menu.c src/diff_logic.c src/diff_view.c src/myers_diff.c

# List all your .h files *with their full path*
# (Assumes you moved context_menu.h to the include/ folder)
HEADERS = include/sidebar.h include/context_menu.h

# This *automatically* creates the list of .o files
# This will correctly become: src/main.o src/sidebar.o src/context_menu.o
OBJECTS = $(notdir $(SOURCES:.c=.o))

# The name of your final program
EXECUTABLE = myapp.exe

# Default target: build the executable
all: $(EXECUTABLE)

# Rule to *link* the executable
# This only runs if any of the .o files have changed
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

# A "pattern rule" to compile a .c file from 'src/' into a .o file in the current directory
# This tells make: "To build a file like %.o, you need src/%.c and all the HEADERS"
%.o: src/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to clean up *all* built files
clean:
	# Use -f to force removal and ignore errors if files don't exist
	rm -f $(OBJECTS) $(EXECUTABLE)

# Tell make that 'all' and 'clean' are not actual files
.PHONY: all clean