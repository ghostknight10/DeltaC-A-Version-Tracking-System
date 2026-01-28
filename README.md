
# DeltaC

## Project Description

DeltaC is a GTK-based desktop application developed in C. It features a modern user interface with a sidebar, a resizable main content area, and a dark/light mode toggle. The application is designed to provide a responsive and visually appealing experience, for managing file versions for your projects.

## Features

*   **Dark/Light Mode Toggle**: Switch between dark and light themes for a personalized viewing experience.
*   **Sidebar Navigation**: A dedicated sidebar for navigation or displaying contextual information.
*   **Resizable Paned View**: A horizontal pane allows users to adjust the width of the sidebar and main content area.
*   **Custom Styling**: Utilizes GTK's CSS capabilities for a custom look and feel.
*   **Dynamic UI Layout**: Uses GTK4's modern layout containers like `GtkBox` and `GtkPaned` for flexible UI construction.

## Dependencies

To build and run DeltaC, you will need the following:

*   **GTK 4**: The GIMP Toolkit, version 3, is used for building the graphical user interface.
*   **GCC (GNU Compiler Collection)**: The C compiler used to compile the source code.
*   **GDB (GNU Debugger)**: For debugging the application (primarily relevant for VS Code integration).
*   **MSYS2**: A software distribution and building platform for Windows, providing a Unix-like environment and package manager (Pacman) to easily install GCC, GTK, and other development tools.

## Installation and Setup (Windows with MSYS2)

1.  **Install MSYS2**: Download and install MSYS2 from msys2.org. Follow the installation instructions, including updating the package database and core packages.

2.  **Install Dependencies via Pacman**: Open an MSYS2 MinGW 64-bit terminal and install the necessary packages:
    ```bash
    pacman -Syu
    pacman -S mingw-w64-x86_64-gtk3 mingw-w64-x86_64-gcc mingw-w64-x86_64-gdb
    ```
    This command installs GTK3, GCC, and GDB for the 64-bit Windows environment.

3.  **Verify Paths**: Ensure that the `C:\msys64\mingw64\bin` directory is in your system's `PATH` environment variable, or specifically configured in your IDE/editor.

## Building the Project

Navigate to the project's root directory in your terminal (preferably an MSYS2 MinGW 64-bit terminal) and use the following command to build the executable:

```bash
gcc -g -I'${workspaceFolder}/include' -o 'main.exe' 'src/main.c' $(pkg-config --cflags --libs gtk3)
```

*   `-g`: Includes debugging information in the executable.
*   `-I'${workspaceFolder}/include'`: Adds the `include` directory to the compiler's search path for header files (e.g., `sidebar.h`, `context_menu.h`).
*   `-o 'main.exe'`: Specifies the output executable file name as `main.exe`.
*   `'src/main.c'`: The main source file to compile.
*   `$(pkg-config --cflags --libs gtk3)`: This command is crucial for GTK applications. `pkg-config` retrieves the necessary compiler flags (`--cflags`) and linker flags (`--libs`) for GTK 3, ensuring all required GTK libraries are correctly linked.

## Running the Project

After successful compilation, you can run the application from the terminal:

```bash
./main.exe
```

**Important Note for Windows Users**: The application explicitly sets the `GSK_RENDERER` environment variable to `"cairo"` using `_putenv_s("GSK_RENDERER", "cairo");` in `main.c`. This is done to prevent potential screen flickering issues on some Windows systems by forcing a software rendering backend. This setting takes effect before GTK is initialized.

## VS Code Configuration

The project includes `.vscode/tasks.json` and `.vscode/launch.json` for seamless integration with Visual Studio Code.

*   **`tasks.json`**: Defines a build task named "Build GTK Program (GCC)" that executes the `gcc` command mentioned above. This task is set as the default build task.
*   **`launch.json`**: Configures a debugger launch profile named "(GDB) Launch GTK Program". It automatically runs the "Build GTK Program (GCC)" task before launching the `main.exe` executable with GDB.

To build and run/debug from VS Code:

1.  **Build**: Press `Ctrl+Shift+B` (or `Cmd+Shift+B` on macOS) to run the default build task.
2.  **Run/Debug**: Press `F5` to build the project (if not already built) and then launch it in debug mode.

Ensure that the `miDebuggerPath` in `launch.json` and the `command` and `env.PATH` in `tasks.json` correctly point to your MSYS2 installation paths (e.g., `C:\\msys64\\mingw64\\bin\\gdb.exe`, `C:\\msys64\\usr\\bin\\bash.exe`).

---
=======
This project is a Version Checking System built using the GTK library (GTK4). It provides a simple GUI-based interface where users can load files, compare versions, and visually inspect the differences. The system highlights changed content and allows easy version tracking.

Features

Built entirely using GTK4 (GUI-based, not terminal-based)

Supports adding and comparing file versions

Side-by-side version comparison window

Differences are underlined / highlighted for quick identification

Clean and easy-to-use interface

To run : 
Open MSYS2mingw terminal.Type make, this will compile and build the app. Run the app myapp.exe using the terminal itself because it will include all the required libraries and dependencies otherwise you can instead use the zip file and run the pre-built myapp.exe as it has all the DLL files already present in the folder, it will run without problems.



## credits:

Ayaan Sharma:(BC2025017)

context menu

file version list

GUI

revert changes

Yug Porwal ( BC2025121)

File difference algorithm

GUI

Sidebar

revert changes

Kedar Mallaya(BC2025043)

GUI

ideation Head