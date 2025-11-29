#include <rpflib/archives/rpf7.h>

int main()
{
    auto archive = rpflib::RPF7Archive::OpenArchive("./t3rm_hurricane.rpf");
    std::filesystem::path outputPath = std::filesystem::current_path() / "test_hurricane_output";
    for (auto& path : archive->GetEntryList())
    {
        std::string relativePath = path.substr(1, path.size());
        std::filesystem::path fullOutputPath = (outputPath / relativePath);
        printf("Create: %d\n", archive->SaveEntryToPath(path, fullOutputPath));
    }
    archive->CloseArchive();

    auto archiveWrite = rpflib::RPF7Archive::CreateArchive("./t3rm_hurricane_test.rpf");
    for (auto& entry : std::filesystem::recursive_directory_iterator(outputPath))
    {
        if (!std::filesystem::is_regular_file(entry))
            continue;

        std::filesystem::path relativePath = std::filesystem::relative(entry, outputPath);
        relativePath = rpflib::RPF7Archive::CorrectEntryPath(relativePath);
        printf("Entry: %s | %s\n", entry.path().string().c_str(), relativePath.string().c_str());

        archiveWrite->AddEntry(relativePath, entry);
    }
    archiveWrite->CloseArchive();

    return 0;
}