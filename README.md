# rpflib by YAMP

**rpflib** is a lightweight C++ library for reading and writing **RPF7 (Rockstar Package File)** archives.
It provides a simple, filesystem-oriented API for extracting archive contents and building new RPF7 files.

This project and research were developed as part of Yet Another M* Platform, an alternative GTA V multiplayer client.

---

## Basic Usage

### Reading an RPF Archive

This example opens an existing RPF file, iterates over all entries, and extracts them while preserving their internal directory structure.

```cpp
#include <filesystem>
#include <cstdio>
#include <rpflib/RPF7Archive.h>

auto archive = rpflib::RPF7Archive::OpenArchive("./example.rpf");

std::filesystem::path outputPath =
    std::filesystem::current_path() / "example_output";

// Iterate through all archive entries
for (auto& path : archive->GetEntryList())
{
    std::string relativePath = path.substr(1, path.size());

    std::filesystem::path fullOutputPath = outputPath / relativePath;

    // Extract entry
    printf("Create: %d\n",
           archive->SaveEntryToPath(path, fullOutputPath));
}

// Always close the archive when finished
archive->CloseArchive();
```

---

### Writing an RPF Archive

This example creates a new RPF archive from a directory structure (for example, files previously extracted from another archive).

```cpp
#include <filesystem>
#include <rpflib/RPF7Archive.h>

auto archiveWrite =
    rpflib::RPF7Archive::CreateArchive("./example.rpf");

std::filesystem::path inputPath =
    std::filesystem::current_path() / "example_input";

for (auto& entry :
     std::filesystem::recursive_directory_iterator(inputPath))
{
    if (!std::filesystem::is_regular_file(entry))
        continue;

    std::filesystem::path relativePath =
        std::filesystem::relative(entry, inputPath);

    // Convert path separators to forward slashes
    relativePath =
        rpflib::RPF7Archive::CorrectEntryPath(relativePath);

    // Add file to archive
    archiveWrite->AddEntry(relativePath, entry);
}

// Closing finalizes and writes the archive
archiveWrite->CloseArchive();
```
