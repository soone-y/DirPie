# DirPie (Directory Pie)

DirPie is a **directory size visualization tool** for the Windows environment.
It displays the relationship between folder hierarchy and disk usage as a pie chart,
with the goal of allowing users to **see where disk space is being consumed at a glance**.

It is primarily intended for disk cleanup and understanding storage usage in development environments.

---

## What You Can Understand

By using DirPie, you can intuitively grasp:

* Which folders consume large amounts of storage
* At which directory levels storage imbalance occurs
* The relative size of child directories compared to their parent directory
* Areas that may be candidates for deletion or cleanup

This tool is designed to visualize **ratios and structure**, which are difficult to understand from numerical lists alone.

---

## Usage Concept

1. Select a target directory
2. Check the storage distribution using the pie chart
3. Follow a sector (slice) of interest to navigate into subdirectories
4. Return to the parent directory as needed

The interaction model is closer to **exploration**.
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

Note: The design prioritizes **trend recognition** over precise numerical accuracy.

---

## Download / How to Run

### Using the Prebuilt Executable (Recommended)

The distributable executable is available via **GitHub Releases**.

* Download `DirPie.exe` from the Releases page
* Supported OS: Windows 10 / 11 (x64)
* This software **does not read file contents, modify files, or delete anything**
  (only file size and path information are used)

Note: The executable is not placed in the repository root.

---

### Building from Source

* Source code: `src/DirPie4.cpp`
* Build helper script: `scripts/build.ps1` (for PowerShell)

This project is a native Windows application written in C++.
It can be built using Visual Studio (MSVC) or MinGW-w64 with the Windows SDK.

---

## Directory Structure

```text
/
├─ src/        Source code
├─ scripts/    Build scripts
├─ README.md
├─ README_en.md
└─ LICENSE.md
```

The distributable `.exe` file is provided **only** via GitHub Releases.

---

## File Verification (Optional)

To verify that the distributed `DirPie.exe` has not been modified,
the SHA-256 hash value is published.

### Verification (PowerShell)

```powershell
Get-FileHash .\DirPie.exe -Algorithm SHA256
```

Make sure the `Hash` value matches the SHA-256 listed on the GitHub Releases page.

---

## Internal Processing Overview

Internally, DirPie performs roughly the following steps:

1. Directory traversal (recursive enumeration)
2. Size aggregation (per directory)
3. Hierarchy retention as a tree structure
4. Conversion of size ratios into angles for rendering
5. Switching the displayed node based on user interaction

**DirPie does not read file contents.**

* No file system modifications (write/delete) are performed
* No network communication is performed
* No external services or cloud integrations are used

---

## Development Status and Policy

* **Feature additions are currently frozen**
* The main objectives are:

  * Improving stability
  * Internal cleanup
  * Enhancing readability and maintainability

No major specification changes or new features are planned.

---

## About AI-Generated Code

This project contains code that was **generated or assisted by AI tools (such as ChatGPT)**.

Design decisions, adjustments, and final judgments were made by a human developer.

---

## License

For details on usage, modification, and redistribution,
refer to **`LICENSE.md`**.

This README provides a summary only.

---

## Disclaimer

* The author assumes no responsibility for any damage caused by the use of this software
* Use at your own risk

---

## Author / Credits

* Author: soone-y (GitHub)
* This is a personal project
