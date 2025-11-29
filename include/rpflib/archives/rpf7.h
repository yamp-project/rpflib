#pragma once

#include <map>

#include <rpflib/archive.h>
#include <rpflib/entry_node.h>

namespace rpflib
{
    enum EncryptionType : uint32_t
    {
        ENCRYPTION_NONE = 0,
        ENCRYPTION_OPEN = 0x4E45504F, //no encryption
        ENCRYPTION_AES  = 0x0FFFFFF9, //aes encryption
        ENCRYPTION_NG   = 0x0FEFFFFF //whitebox aes encryption
    };

#pragma pack(push, 1)
    struct RPF7Header
    {
        union
        {
            uint8_t m_Raw[4];
            uint32_t m_Number;
        } m_Magic;

        uint32_t m_EntryCount;
        uint32_t m_NameSize;

        EncryptionType m_Encryption;
    };

    struct RPF7Entry
    {
        static const uint64_t DIR_OFFSET    = 0x007FFFFF;
        static const uint16_t BLOCK_SIZE    = 0x00000200;
        static const uint32_t MAX_FILE_SIZE = 0x00FFFFFF;

        uint64_t m_NameOffset   : 16;
        uint64_t m_EntrySize    : 24;
        uint64_t m_EntryOffset  : 23;
        uint64_t m_IsResource   : 1;

        union
        {
            struct
            {
                uint32_t m_EntriesIndex;
                uint32_t m_EntriesCount;
            } m_DirectoryEntry;

            struct
            {
                uint32_t m_VirtualFlags;
                uint32_t m_PhysicalFlags;
            } m_ResourceEntry;

            struct
            {
                uint32_t m_RealSize;
                uint32_t m_Encrypted; //0 if non-encrypted
            } m_FileEntry;
        };

        [[nodiscard]] bool IsDirectory() const  { return m_EntryOffset == DIR_OFFSET; }
        [[nodiscard]] bool IsResource() const   { return m_IsResource; }
        [[nodiscard]] bool IsFile() const       { return !IsDirectory() && !IsResource(); }
        [[nodiscard]] bool IsCompressed() const { return IsFile() && m_EntrySize != 0 && m_EntrySize != m_FileEntry.m_RealSize; }

        [[nodiscard]] uint64_t GetEntrySize() const { return m_EntrySize == 0 ? m_FileEntry.m_RealSize : m_EntrySize; }
    };
#pragma pack(pop)

    class RPF7Archive : public IRPFArchive
    {
    public:
        static const uint32_t IDENT             = 0x52504637;
        static const uint32_t RESOURCE_IDENT    = 0x37435352;

        ~RPF7Archive() final;

        static std::unique_ptr<RPF7Archive> OpenArchive(const std::filesystem::path& archivePath)
        {
            return std::unique_ptr<RPF7Archive>(new RPF7Archive(archivePath, OpenMode::OPEN_MODE_READ));
        }

        static std::unique_ptr<RPF7Archive> CreateArchive(const std::filesystem::path& outputFile)
        {
            return std::unique_ptr<RPF7Archive>(new RPF7Archive(outputFile, OpenMode::OPEN_MODE_WRITE));
        }

        void CloseArchive() override;

        void AddEntry(const std::filesystem::path& entryPath, const std::filesystem::path& entryFilePath) override;
        EntryDataBuffer GetEntryData(const std::string &entryPath) override;
        EntryPathList GetEntryList() override;
        bool SaveEntryToPath(const std::string& entryPath, const std::filesystem::path& outputPath) override;
        bool DoesEntryExists(const std::string& entryPath) override;

        static EntryDataBuffer CompressData(uint8_t* data, uint64_t dataLength);
        static EntryDataBuffer DecompressData(uint8_t* data, uint64_t dataLength);
        static std::filesystem::path CorrectEntryPath(const std::filesystem::path& entryPath);
        static EntryDataBuffer GetFileData(const std::filesystem::path& filePath);
        static uint64_t GetFileSize(const std::filesystem::path& filePath);
        static uint64_t GetEntryNameBlockSize(uint64_t nameSize) { return ((nameSize + 15) / 16) * 16; }
        static uint64_t GetEntryDataBlockSize(uint64_t dataSize) { return ((dataSize + 511) / 512) * 512; }
        static void PrintEntryTree(EntryNode<RPF7Entry>* parent, uint16_t&& level = 0);

        uint64_t GetEntryNodeTotalCount();
        EntryNode<RPF7Entry>* GetRootEntryNode() { return &m_RootNode; }

    private:
        RPF7Archive(const std::filesystem::path& archivePath, OpenMode openMode);

        void OpenArchive() override;
        void CreateArchive() override;

        void ReadHeader(RPF7Header& header);
        void ReadNames();
        void ReadEntries();

        void WriteHeader();
        void WriteEntries();
        void WriteNames();
        void WriteEntriesData();

        RPF7Entry CreateDirectoryEntry();
        RPF7Entry CreateFileEntry(const std::filesystem::path& path);
        bool IsFileAResource(const std::filesystem::path& path, uint32_t& virtualFlags, uint32_t& physicalFlags);

        // void BuildNodeTree(const RPF7Entry& parentEntry, EntryNode<RPF7Entry>* parentNode);
        // void BuildEntryMap(const RPF7Entry& parentEntry, std::vector<std::string>&& pathStack = std::vector<std::string>());
        void BuildEntryMapAndNodeTree(const RPF7Entry& parentEntry, EntryNode<RPF7Entry>* parentNode, std::vector<std::string>&& pathStack = std::vector<std::string>());
        std::vector<RPF7Entry> BuildEntriesListFromNodeTree();
        std::map<uint32_t, std::string> BuildEntriesNameMap();

        [[nodiscard]] std::string GetEntryName(uint32_t index);
        [[nodiscard]] std::string GetEntryName(const RPF7Entry& entry);
        [[nodiscard]] uint32_t GetEntryNameOffset(const std::string& entryName);

        RPF7Header m_Header;
        EntryNode<RPF7Entry> m_RootNode;
        std::vector<RPF7Entry> m_Entries;
        std::map<uint32_t, std::string> m_NameMap;
        std::map<std::string, const RPF7Entry*> m_EntryMap;
    };
}