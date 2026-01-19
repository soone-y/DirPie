# DirPie (Directory Pie)

DirPie is a **directory size visualization tool** for the Windows environment.
It visualizes the relationship between folder hierarchy and disk usage as a pie chart,
with the primary goal of allowing users to see **which parts consume disk space at a glance**.

This tool is intended for users who want to analyze disk usage or understand storage consumption in development environments.

In short, it helps with storage organization.

> For information about distributing the executable file (`.exe`), see `licence.md`.

---

## What You Can Understand

By using DirPie, you can intuitively grasp the following:

* Which folders consume large amounts of storage
* At which directory levels storage imbalance occurs
* The relative size of child directories compared to their parent directory
* Areas that may be candidates for deletion or cleanup

This tool is designed to visually capture **ratios and structure**, which are difficult to understand from numerical lists alone.

---

## How to Use (Conceptual Flow)

1. Select a target directory
2. Check the storage distribution using the pie chart
3. Follow a sector (slice) of interest to navigate into a subdirectory
4. Return to the parent directory as needed

The interaction model is closer to **exploration** than to traditional file management.
DirPie is intended to be used as a **storage structure viewer**, not as a file manager.

---

## How to Read the Visualization

* **Entire circle**

  * Total size of the current directory
* **Each sector (slice)**

  * Immediate subdirectories or grouped files
* **Sector size**

  * Proportional representation of disk usage
* **Colors**

  * Used only to improve visual distinction (no semantic meaning)

This design prioritizes **trend recognition** rather than precise numerical accuracy.

---

## Internal Processing Overview

Internally, DirPie performs roughly the following steps:

1. Directory traversal

   * Recursively enumerates folders and files
2. Size aggregation

   * Calculates total size for each node (directory)
3. Structure retention

   * Stores the hierarchy as a tree structure
4. Rendering conversion

   * Converts size ratios into angles and renders them as a pie chart
5. Interaction handling

   * Switches the displayed node according to user input

**DirPie does not read file contents.**
Only file size and path information are retrieved.

* No operations modify the file system (such as writing or deleting files)
* No network communication is performed
* No external services or cloud integrations are used

---

## About AI-Generated Code

This project contains code that was **generated or assisted by AI tools (such as ChatGPT)**.

* Design decisions, adjustments, and final judgments were made by a human developer

Please use the software with this understanding.

---

## Development Status and Policy

* **Feature additions are currently frozen**
* The main objectives at this stage are:

  * Improving stability
  * Internal cleanup
  * Enhancing readability and maintainability
* No major specification changes or new features are planned

---

## License

For details on usage, modification, and redistribution,
refer to **`licence.md`**.

This README provides only a summary;
all legal terms are consolidated in `licence.md`.

---

## Disclaimer

* The author assumes no responsibility for any damage caused by the use of this software
* Use at your own risk

---

## Author / Credits

* Author: (to be added)
* This is a personal project

---

## Build Information

This project is built as a **Windows native application**.
It does not require external runtimes or scripting environments and assumes a native C++ build.

---

### Requirements

* OS: Windows 10 / 11
* Architecture: x64
* Language: C++
* API: Windows API (Win32)

The following development environments are assumed:

* Visual Studio (MSVC)
* Windows-compatible C++ compilers such as MinGW-w64

> In all cases, the **Windows SDK** is required.

---

### Build Philosophy

* The goal is to produce a single executable file (`.exe`)
* The project does not depend on special build systems such as CMake
* Both IDE-based builds and direct command-line builds are assumed

As long as you have an environment that can
**compile C++ source files as a Windows application**, the project can be built.

---

### Notes on Character Encoding and Settings

* The codebase assumes Unicode (UTF-16)
* Wide-character (`W`) versions of Windows API functions are used
* Unicode must be enabled in the compiler settings

---

### Dependencies

* External libraries are generally not used
* Only APIs and libraries included with Windows are relied upon

No additional DLLs need to be bundled.

---

### Additional Notes

* Issues may arise due to differences in build environments
* Environment-specific problems should be handled at the user's discretion
