# Building on Windows

Windows builds can be created using CMake and Visual Studio:

- CMake is used to produce the official release package, and therefore is the
  recommended build tool. We also recommend using presets because they're
  CI-tested and produce a binary using consistent compiler flags. Run `cmake
  --list-presets` to list the presets.

- Visual Studio 2022 IDE suite with Clang/LLVM compiler, and vcpkg to provide
  dependencies. This is the fully-supported toolchain used to create release
  builds.


## Build using Visual Studio

1. Install Visual Studio Community 2022: <https://visualstudio.microsoft.com/vs/community/>.
    - Select "C++ Clang tools for Windows" in the Visual Studio installer

2. Install/configure vcpkg.
    - Recent versions of Visual Studio already include it[^1], but it needs to
      be initialized. Open a Visual Studio Developer Command Prompt and run the
      following command:

        ``` shell
        vcpkg integrate install
        ```

    - Existing [standalone versions](<https://github.com/Microsoft/vcpkg#quick-start-windows>) should work as well.

3. Follow instructions in [README.md](/README.md).

### Create a debugger build using Visual Studio

Note that the debugger imposes a significant runtime performance penalty.
If you're not planning to use the debugger then the steps above will help
you build a binary optimized for gaming.

## Build using Visual Studio with CMake

### Prerequisites

- Python 3 with pip (only needed if you want to build the
  [offline documentation](#offline-documentation)). Download the installer from
  <https://www.python.org/downloads/> and make sure to check **"Add Python to
  PATH"** during installation.

### Importing the CMake project

1. Remove any existing dosbox-automation projects from Visual Studio.

2. Exit Visual Studio, then delete the `.vs` folder from your local copy of
   the dosbox-automation repository if it exists.

3. Start Visual Studio, then import your local dosbox-automation repository
   folder by selecting the **Open a local folder** item in the startup screen,
   or by selecting the **File / Open / Folder** in the top menu.

4. You'll be greeted by a CMake splash screen, then after the CMake project
   setup has been completed, you'll see the list of available CMake
   configurations in the second dropdown below the menu bar, next to **Local
   Machine**.

5. Select either the **debug-windows** or **release-windows** configuration,
   then select **Build / Build All** or press **F7** to build the project.
   You'll need to do this twice for the first time, and probably after
   changing the CMakeList.txt files.

## Troubleshooting

### Random build failures

Try using **Clean All** from the **Build** menu, then **Rescan Solution**
from the **Project** menu. In most cases, **Build All** should work without
errors after this.

If you're still getting errors, or some of these menu items are not available,
delete the dosbox-automation project in Visual Studio, delete the `.vs` folder
inside the local repo folder, then follow the instructions in
**Importing the CMake project** to reimport the project.

### CMake configurations have disappeared

If the CMake configurations have disappeared from the toolbar after updating
Visual Studio or the local dosbox-automation repository, try **Rescan Solution**
from the **Project** menu.

If that doesn't help, delete the dosbox-automation project in Visual Studio,
delete the `.vs` folder inside the local repo folder, then follow the
instructions in **Importing the CMake project** to reimport the project.

