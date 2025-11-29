#pragma once

#include <string>
#include <filesystem>
#include <fstream>

namespace rpflib
{
    class IRPFArchive
    {
    public:
        typedef std::vector<uint8_t> EntryDataBuffer;
        typedef std::vector<std::string> EntryPathList;

        enum class OpenMode
        {
            OPEN_MODE_INVALILD = -1,
            OPEN_MODE_READ = 0,
            OPEN_MODE_WRITE
        };

        explicit IRPFArchive(const std::filesystem::path& path, OpenMode openMode)
            : m_Path(path), m_OpenMode(openMode) { }

        virtual ~IRPFArchive() = default;

        virtual void OpenArchive() = 0;
        virtual void CreateArchive() = 0;
        virtual void CloseArchive() = 0;

        virtual void AddEntry(const std::filesystem::path& entryPath, const std::filesystem::path& entryFilePath) = 0;
        virtual EntryDataBuffer GetEntryData(const std::string& entryPath) = 0;
        virtual EntryPathList GetEntryList() = 0;
        virtual bool SaveEntryToPath(const std::string& entryPath, const std::filesystem::path& outputPath) = 0;
        virtual bool DoesEntryExists(const std::string& entryPath) = 0;

        [[nodiscard]] bool IsWriting() const { return m_OpenMode == OpenMode::OPEN_MODE_WRITE; }
        [[nodiscard]] bool IsReading() const { return m_OpenMode == OpenMode::OPEN_MODE_READ; }

    protected:
        const OpenMode m_OpenMode = OpenMode::OPEN_MODE_INVALILD;
        const std::filesystem::path m_Path;
        std::fstream m_FileStream;
    };
}