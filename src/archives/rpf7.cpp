#include <functional>
#include <rpflib/archives/rpf7.h>
#include <zlib.h>
#include <queue>

using namespace rpflib;

RPF7Archive::RPF7Archive(const std::filesystem::path& archivePath, OpenMode openMode, int nameShift) : IRPFArchive(archivePath, openMode), m_NameShift(nameShift)
{
    if (m_NameShift < 0 || m_NameShift > 3)
    {
        printf("WARNING: nameShift %d out of range [0-3], clamping to valid range\n", m_NameShift);
        m_NameShift = std::clamp(m_NameShift, 0, 3);
    }

    m_NameHeapMaxSize = 65536 << m_NameShift;

    if (IsReading())
        OpenArchive();

    if (IsWriting())
        CreateArchive();
}

RPF7Archive::~RPF7Archive()
{
    if (m_FileStream.is_open())
        m_FileStream.close();
}

void RPF7Archive::OpenArchive()
{
    if (!IsReading())
        return;

    if (m_FileStream.is_open())
        return;

    if (!std::filesystem::exists(m_Path) || std::filesystem::is_directory(m_Path))
        return;

    m_FileStream.open(m_Path, std::ios::binary | std::ios::in);

    ReadHeader(m_Header);
    if (m_Header.m_Magic.m_Number != IDENT)
    {
        m_FileStream.close();
        return;
    }

    if (m_Header.m_Encryption != ENCRYPTION_OPEN)
    {
        printf("ERROR! Currently only non-encrypted RPF7 files are supported!\n");
        m_FileStream.close();
        return;
    }

    m_NameShift = (m_Header.m_NameSize >> 28) & 0x3;
    m_NameHeapMaxSize = 65536 << m_NameShift;

    ReadNames();
    ReadEntries();
}

void RPF7Archive::CreateArchive()
{
    if (!IsWriting())
        return;

    if (m_FileStream.is_open())
        return;

    m_FileStream.open(m_Path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!m_FileStream.is_open())
        return;

    m_Header = {};
    m_Header.m_NameSize = 0;
}

void RPF7Archive::CloseArchive()
{
    if (IsWriting())
    {
        WriteHeader();
        WriteEntries();
        WriteNames();
        WriteEntriesData();
    }

    if (m_FileStream.is_open())
        m_FileStream.close();
}

void RPF7Archive::AddEntry(const std::filesystem::path& entryPath, const std::filesystem::path& entryFilePath)
{
    if (!IsWriting())
        return;

    if (!m_FileStream.is_open())
        return;

    if (!entryPath.has_extension())
        return;

    std::istringstream iss(entryPath.string());
    std::string item;

    EntryNode<RPF7Entry>* currentParent = &m_RootNode;
    while (std::getline(iss, item, '/'))
    {
        if (item.empty())
            continue;

        if (currentParent == nullptr)
            continue;

        EntryNode<RPF7Entry>* existingNode = currentParent->Find(item);
        if (existingNode != nullptr)
        {
            currentParent = existingNode;
            continue;
        }

        EntryNode<RPF7Entry>* node = currentParent->Add(item);
        currentParent = node;
    }

    if (currentParent != nullptr)
    {
        currentParent->m_RelativePath = entryPath;
        currentParent->m_FilePath = entryFilePath;
    }
}

RPF7Archive::EntryDataBuffer RPF7Archive::GetEntryData(const std::string& entryPath)
{
    EntryDataBuffer buffer;
    if (!IsReading())
        return buffer;

    if (m_EntryMap.empty())
        return buffer;

    if (!m_FileStream.is_open())
        return buffer;

    const RPF7Entry* entry = m_EntryMap.at(entryPath);
    uint64_t entryFileOffset = entry->m_EntryOffset * RPF7Entry::BLOCK_SIZE;
    uint64_t entryFileSize = entry->GetEntrySize();

    auto oldPosition = m_FileStream.tellg();
    m_FileStream.seekg(entryFileOffset, std::ios::beg);

    buffer.resize(entryFileSize);
    m_FileStream.read((char*)buffer.data(), entryFileSize);

    if (entry->IsCompressed())
    {
        buffer = DecompressData(buffer.data(), buffer.size());
    }

    if (oldPosition > m_FileStream.tellg())
        m_FileStream.seekg(oldPosition, std::ios::beg);

    return buffer;
}

IRPFArchive::EntryPathList RPF7Archive::GetEntryList()
{
    EntryPathList pathList;
    if (!IsReading())
        return pathList;

    if (m_EntryMap.empty())
        return pathList;

    std::transform(m_EntryMap.begin(), m_EntryMap.end(), std::back_inserter(pathList), [](const std::pair<std::string, const RPF7Entry*>& pair) { return pair.first; });

    return pathList;
}

bool RPF7Archive::SaveEntryToPath(const std::string& entryPath, const std::filesystem::path& outputPath)
{
    if (!IsReading())
        return false;

    if (m_EntryMap.empty())
        return false;

    if (!DoesEntryExists(entryPath))
        return false;

    EntryDataBuffer buffer = GetEntryData(entryPath);

    std::filesystem::create_directories(outputPath.parent_path());
    std::fstream outputFile(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!outputFile.is_open())
        return false;

    outputFile.write(reinterpret_cast<char*>(buffer.data()), buffer.size());
    outputFile.close();

    return true;
}

bool RPF7Archive::DoesEntryExists(const std::string& entryPath)
{
    if (m_EntryMap.empty())
        return false;

    return m_EntryMap.contains(entryPath);
}

void RPF7Archive::ReadHeader(RPF7Header& header)
{
    if (!IsReading())
        return;

    if (!m_FileStream.is_open())
        return;

    auto oldPosition = m_FileStream.tellg();
    m_FileStream.seekg(0, std::ios::beg);
    m_FileStream.read(reinterpret_cast<char*>(&header), sizeof(header));

    if (oldPosition > m_FileStream.tellg())
        m_FileStream.seekg(oldPosition, std::ios::beg);
}

void RPF7Archive::ReadNames()
{
    if (!IsReading())
        return;

    if (!m_FileStream.is_open())
        return;

    auto oldPosition = m_FileStream.tellg();
    uint32_t namePosition = sizeof(RPF7Header) + (sizeof(RPF7Entry) * m_Header.m_EntryCount);
    m_FileStream.seekg(namePosition, std::ios::beg);

    uint32_t actualNameSize = m_Header.m_NameSize & 0x0FFFFFFF;
    std::vector<uint8_t> nameBuffer(actualNameSize);
    m_FileStream.read(reinterpret_cast<char*>(nameBuffer.data()), nameBuffer.size());

    uint32_t nameMask = (1 << m_NameShift) - 1;

    uint32_t startPosition = 0;
    for (uint32_t i = 0; i < actualNameSize; i++)
    {
        uint8_t c = nameBuffer[i];
        if (c == '\0')
        {
            std::string entryName(nameBuffer.data() + startPosition, nameBuffer.data() + i);

            uint32_t alignedOffset = startPosition >> m_NameShift;
            m_NameMap[alignedOffset] = entryName;

            startPosition = ((i + 1 + nameMask) & ~nameMask);
        }
    }

    if (oldPosition > m_FileStream.tellg())
        m_FileStream.seekg(oldPosition, std::ios::beg);
}

void RPF7Archive::PrintEntryTree(EntryNode<RPF7Entry>* parent, uint16_t&& level)
{
    if (parent == nullptr)
        return;

    EntryNode<RPF7Entry>* currentChild = parent->m_FirstChild;
    while (currentChild != nullptr)
    {
        if (currentChild->m_FirstChild != nullptr)
        {
            PrintEntryTree(currentChild, level + 1);
        }

        currentChild = currentChild->m_NextSibling;
    }
}

void RPF7Archive::ReadEntries()
{
    if (!IsReading())
        return;

    if (!m_FileStream.is_open())
        return;

    auto oldPosition = m_FileStream.tellg();
    m_FileStream.seekg(sizeof(RPF7Header), std::ios::beg);

    m_Entries.resize(m_Header.m_EntryCount);
    m_FileStream.read(reinterpret_cast<char*>(m_Entries.data()), sizeof(RPF7Entry) * m_Entries.size());

    RPF7Entry& rootEntry = m_Entries[0];
    if (!rootEntry.IsDirectory())
    {
        printf("ERROR! Root entry is not a directory!\n");
        return;
    }

    m_RootNode.m_Entry = &rootEntry;
    BuildEntryMapAndNodeTree(rootEntry, &m_RootNode);

    if (oldPosition > m_FileStream.tellg())
        m_FileStream.seekg(oldPosition, std::ios::beg);
}

void RPF7Archive::WriteHeader()
{
    if (!IsWriting())
        return;

    if (!m_FileStream.is_open())
        return;

    m_Header.m_Magic.m_Number = RPF7Archive::IDENT;
    m_Header.m_Encryption = ENCRYPTION_OPEN;
    m_Header.m_EntryCount = GetEntryNodeTotalCount();

    auto oldPosition = m_FileStream.tellp();
    m_FileStream.seekg(0, std::ios::beg);
    m_FileStream.write(reinterpret_cast<char*>(&m_Header), sizeof(RPF7Header));

    if (oldPosition > m_FileStream.tellp())
        m_FileStream.seekp(oldPosition, std::ios::beg);
}

void RPF7Archive::WriteEntries()
{
    if (!IsWriting())
        return;

    if (!m_FileStream.is_open())
        return;

    auto oldPosition = m_FileStream.tellp();
    m_FileStream.seekg(sizeof(RPF7Header), std::ios::beg);

    if (m_NameMap.empty())
        m_NameMap = BuildEntriesNameMap();

    if (m_Entries.empty())
        m_Entries = BuildEntriesListFromNodeTree();

    m_FileStream.write(reinterpret_cast<char*>(m_Entries.data()), sizeof(RPF7Entry) * m_Entries.size());

    if (oldPosition > m_FileStream.tellp())
        m_FileStream.seekp(oldPosition, std::ios::beg);
}

void RPF7Archive::WriteNames()
{
    if (!IsWriting())
        return;

    if (!m_FileStream.is_open())
        return;

    auto oldPosition = m_FileStream.tellp();
    m_FileStream.seekg(sizeof(RPF7Header) + m_Header.m_EntryCount * sizeof(RPF7Entry), std::ios::beg);

    auto currentPosition = m_FileStream.tellp();
    uint32_t nameMask = (1 << m_NameShift) - 1;

    for (auto& name : m_NameMap)
    {
        uint64_t currentOffset = (uint64_t)m_FileStream.tellp() - currentPosition;
        uint32_t expectedShiftedOffset = name.first;
        uint32_t expectedActualOffset = expectedShiftedOffset << m_NameShift;

        if (currentOffset != expectedActualOffset)
        {
            printf("WARNING: Name offset mismatch! Expected %u, got %llu for name '%s'\n", expectedActualOffset, currentOffset, name.second.c_str());
        }

        m_FileStream.write(name.second.c_str(), name.second.size());
        uint8_t zero = 0;
        m_FileStream.write(reinterpret_cast<char*>(&zero), sizeof(uint8_t));

        uint32_t nameLen = name.second.size() + 1;
        uint32_t paddedLen = (nameLen + nameMask) & ~nameMask;
        for (uint32_t i = nameLen; i < paddedLen; i++)
        {
            m_FileStream.write(reinterpret_cast<char*>(&zero), sizeof(uint8_t));
        }
    }

    uint64_t writtenBytes = ((uint64_t)(m_FileStream.tellp() - currentPosition));
    uint64_t paddedBytes = GetEntryNameBlockSize(writtenBytes);

    for (uint64_t i = writtenBytes; i < paddedBytes; i++)
    {
        uint8_t zero = 0;
        m_FileStream.write(reinterpret_cast<char*>(&zero), sizeof(uint8_t));
    }

    m_Header.m_NameSize = paddedBytes | (m_NameShift << 28);
    WriteHeader();

    if (oldPosition > m_FileStream.tellp())
        m_FileStream.seekp(oldPosition, std::ios::beg);
}

void RPF7Archive::WriteEntriesData()
{
    if (!IsWriting())
        return;

    if (!m_FileStream.is_open())
        return;

    std::vector<std::string> compressionExtensionExclude = {".rpf", ".bik", ".awc"};

    uint64_t currentPosition = m_FileStream.tellp();
    m_FileStream.seekp(GetEntryDataBlockSize(currentPosition), std::ios::beg);

    std::function<void(const EntryNode<RPF7Entry>*)> recurseEntryWrite = [&](const EntryNode<RPF7Entry>* parent)
    {
        if (!parent)
            return;

        for (auto currentChild = parent->m_FirstChild; currentChild != nullptr; currentChild = currentChild->m_NextSibling)
        {
            if (currentChild->HasChildren())
                recurseEntryWrite(currentChild);

            if (!currentChild->m_RelativePath.has_extension())
                continue;

            bool needToCompress =
                std::find(compressionExtensionExclude.begin(), compressionExtensionExclude.end(), currentChild->m_RelativePath.extension().string()) == compressionExtensionExclude.end();
            needToCompress = needToCompress && !currentChild->m_Entry->m_IsResource;

            auto fileData = GetFileData(currentChild->m_FilePath);
            uint64_t fileDataSize = fileData.size();
            if (needToCompress)
                fileData = CompressData(fileData.data(), fileData.size());

            if (currentChild->m_Entry->m_IsResource || needToCompress)
                currentChild->m_Entry->m_EntrySize = fileData.size();
            else
                currentChild->m_Entry->m_EntrySize = 0;

            currentChild->m_Entry->m_EntryOffset = (((uint64_t)m_FileStream.tellp()) / RPF7Entry::BLOCK_SIZE);

            // write file data then fill up the gaps
            m_FileStream.write(reinterpret_cast<char*>(fileData.data()), fileData.size());
            for (uint64_t i = 0; i < (GetEntryDataBlockSize(fileData.size()) - fileData.size()); i++)
            {
                uint8_t zero = 0;
                m_FileStream.write(reinterpret_cast<char*>(&zero), sizeof(uint8_t));
            }
        }
    };
    recurseEntryWrite(&m_RootNode);

    WriteEntries();
}

RPF7Entry RPF7Archive::CreateDirectoryEntry()
{
    RPF7Entry newEntry{};
    newEntry.m_EntrySize = 0;
    newEntry.m_EntryOffset = RPF7Entry::DIR_OFFSET;
    newEntry.m_IsResource = 0;
    newEntry.m_NameOffset = 0;

    newEntry.m_DirectoryEntry.m_EntriesCount = 0;
    newEntry.m_DirectoryEntry.m_EntriesIndex = 0;

    return newEntry;
}

RPF7Entry RPF7Archive::CreateFileEntry(const std::filesystem::path& path)
{
    RPF7Entry newEntry{};
    newEntry.m_EntrySize = 0;
    newEntry.m_EntryOffset = 0;
    newEntry.m_NameOffset = 0;

    uint32_t virtualFlags = 0;
    uint32_t physicalFlags = 0;
    newEntry.m_IsResource = IsFileAResource(path, virtualFlags, physicalFlags);

    newEntry.m_FileEntry.m_RealSize = 0;
    newEntry.m_FileEntry.m_Encrypted = 0;

    if (newEntry.m_IsResource)
    {
        newEntry.m_ResourceEntry.m_VirtualFlags = virtualFlags;
        newEntry.m_ResourceEntry.m_PhysicalFlags = physicalFlags;
    }
    else
    {
        newEntry.m_FileEntry.m_RealSize = GetFileSize(path);
        newEntry.m_FileEntry.m_Encrypted = 0;
    }

    return newEntry;
}

bool RPF7Archive::IsFileAResource(const std::filesystem::path& path, uint32_t& virtualFlags, uint32_t& physicalFlags)
{
    if (!std::filesystem::exists(path))
        return false;

    std::fstream resourceStream(path, std::ios::in | std::ios::binary);
    if (!resourceStream.is_open())
        return false;

    uint32_t magic = 0;
    uint32_t flags = 0;

    resourceStream.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
    resourceStream.read(reinterpret_cast<char*>(&flags), sizeof(uint32_t));
    resourceStream.read(reinterpret_cast<char*>(&virtualFlags), sizeof(uint32_t));
    resourceStream.read(reinterpret_cast<char*>(&physicalFlags), sizeof(uint32_t));
    resourceStream.close();

    return magic == RPF7Archive::RESOURCE_IDENT;
}

uint64_t RPF7Archive::GetEntryNodeTotalCount()
{
    std::function<uint64_t(const EntryNode<RPF7Entry>*, int&&)> recursiveNodeCount = [&](const EntryNode<RPF7Entry>* parent, int&& count = 0) -> uint64_t
    {
        if (!parent)
            return 0;

        count += parent->GetChildrenCount();
        for (auto currentChild = parent->m_FirstChild; currentChild != nullptr; currentChild = currentChild->m_NextSibling)
        {
            if (currentChild->HasChildren())
                recursiveNodeCount(currentChild, std::move(count));
        }

        return count;
    };

    return recursiveNodeCount(&m_RootNode, 1); // we start from 1 because root node counts as one already
}

std::string RPF7Archive::GetEntryName(uint32_t index)
{
    if (m_NameMap.empty())
        return "";

    if (!m_NameMap.contains(index))
        return "";

    return m_NameMap[index];
}

std::string RPF7Archive::GetEntryName(const RPF7Entry& entry)
{
    return GetEntryName(entry.m_NameOffset);
}

uint32_t RPF7Archive::GetEntryNameOffset(const std::string& entryName)
{
    if (m_NameMap.empty())
        return 0;

    for (auto& name : m_NameMap)
        if (name.second == entryName)
            return name.first;

    return 0;
}

void RPF7Archive::BuildEntryMapAndNodeTree(const RPF7Entry& parentEntry, EntryNode<RPF7Entry>* parentNode, std::vector<std::string>&& pathStack)
{
    if (m_Entries.empty())
        return;

    if (!parentEntry.IsDirectory())
        return;

    std::string parentName = GetEntryName(parentEntry);
    pathStack.push_back(parentName);

    for (int i = 0; i < parentEntry.m_DirectoryEntry.m_EntriesCount; i++)
    {
        uint32_t entryArrayIdx = parentEntry.m_DirectoryEntry.m_EntriesIndex + i;
        const RPF7Entry& childEntry = m_Entries[entryArrayIdx];

        std::string entryName = GetEntryName(childEntry);
        EntryNode<RPF7Entry>* addedEntry = nullptr;

        std::ostringstream parentPath;
        std::copy(pathStack.begin(), pathStack.end(), std::ostream_iterator<std::string>(parentPath, "/"));
        std::filesystem::path fullPath((parentPath.str() + entryName));

        if (fullPath.has_extension())
            m_EntryMap[fullPath.string()] = &childEntry;

        if (parentNode && (parentNode->Find(entryName) == nullptr))
        {
            addedEntry = parentNode->Add(entryName);
            addedEntry->m_Entry = const_cast<RPF7Entry*>(&childEntry);
        }

        if (childEntry.IsDirectory())
        {
            BuildEntryMapAndNodeTree(childEntry, addedEntry, std::move(pathStack));
        }
    }

    pathStack.pop_back();
}

std::vector<RPF7Entry> RPF7Archive::BuildEntriesListFromNodeTree()
{
    std::vector<RPF7Entry> entryList;
    entryList.reserve(GetEntryNodeTotalCount());

    std::function<void(EntryNode<RPF7Entry>*, uint32_t&&)> recursiveBuild = [&](EntryNode<RPF7Entry>* parent, uint32_t&& idx = 1)
    {
        if (parent == nullptr)
            return;

        std::vector<EntryNode<RPF7Entry>*> sortedChildren;
        EntryNode<RPF7Entry>* currentChild = parent->m_FirstChild;
        while (currentChild != nullptr)
        {
            sortedChildren.push_back(currentChild);
            currentChild = currentChild->m_NextSibling;
        }

        // Build entries for sorted children
        std::sort(sortedChildren.begin(), sortedChildren.end(), [](const EntryNode<RPF7Entry>* a, const EntryNode<RPF7Entry>* b) { return a->m_Name < b->m_Name; });

        for (auto* child : sortedChildren)
        {
            RPF7Entry* newEntry = &entryList.emplace_back();
            child->m_Entry = newEntry;

            bool isFile = child->m_Name.find(".") != std::string::npos;
            *newEntry = isFile ? CreateFileEntry(child->m_FilePath) : CreateDirectoryEntry();
            newEntry->m_NameOffset = GetEntryNameOffset(child->m_Name);

            if (isFile)
                m_EntryMap[child->m_RelativePath.string()] = newEntry;
        }

        idx += sortedChildren.size();
        for (auto* child : sortedChildren)
        {
            if (child->m_FirstChild != nullptr)
            {
                child->m_Entry->m_DirectoryEntry.m_EntriesCount = child->GetChildrenCount();
                child->m_Entry->m_DirectoryEntry.m_EntriesIndex = idx;
                recursiveBuild(child, std::move(idx));
            }
        }
    };

    RPF7Entry* rootEntry = &entryList.emplace_back();
    *rootEntry = CreateDirectoryEntry();
    rootEntry->m_DirectoryEntry.m_EntriesIndex = 1;
    rootEntry->m_DirectoryEntry.m_EntriesCount = m_RootNode.GetChildrenCount();

    recursiveBuild(&m_RootNode, 1);

    return entryList;
}

std::map<uint32_t, std::string> RPF7Archive::BuildEntriesNameMap()
{
    std::map<std::string, uint32_t> entryNameMap;
    entryNameMap[""] = 0;

    std::function<void(EntryNode<RPF7Entry>*)> recursiveBuild = [&](EntryNode<RPF7Entry>* parent)
    {
        std::vector<EntryNode<RPF7Entry>*> sortedChildren;
        EntryNode<RPF7Entry>* currentChild = parent->m_FirstChild;
        while (currentChild != nullptr)
        {
            sortedChildren.push_back(currentChild);
            currentChild = currentChild->m_NextSibling;
        }

        std::sort(sortedChildren.begin(), sortedChildren.end(), [](const EntryNode<RPF7Entry>* a, const EntryNode<RPF7Entry>* b) { return a->m_Name < b->m_Name; });

        for (auto* child : sortedChildren)
        {
            if (!entryNameMap.contains(child->m_Name))
                entryNameMap[child->m_Name] = 0;

            if (child->m_FirstChild != nullptr)
            {
                recursiveBuild(child);
            }
        }
    };
    recursiveBuild(&m_RootNode);

    uint32_t nameMask = (1 << m_NameShift) - 1;
    uint32_t byteOffset = 0;
    std::map<uint32_t, std::string> reverseEntryNameMap;

    for (auto& entry : entryNameMap)
    {
        uint32_t shiftedOffset = byteOffset >> m_NameShift;

        uint32_t nameLen = entry.first.size() + 1;
        uint32_t alignedLen = (nameLen + nameMask) & ~nameMask;

        if (byteOffset + alignedLen > m_NameHeapMaxSize)
        {
            throw std::runtime_error("RPF7Archive::BuildEntriesNameMap: Name heap size exceeded maximum limit.");
        }

        entry.second = shiftedOffset;
        reverseEntryNameMap[shiftedOffset] = entry.first;

        byteOffset += alignedLen;
    }

    return reverseEntryNameMap;
}

RPF7Archive::EntryDataBuffer RPF7Archive::CompressData(uint8_t* data, uint64_t dataLength)
{
    EntryDataBuffer deflateBuffer(dataLength);

    z_stream defstream;
    defstream.zalloc = Z_NULL;
    defstream.zfree = Z_NULL;
    defstream.opaque = Z_NULL;
    defstream.next_in = (Bytef*)data;
    defstream.avail_in = (uInt)dataLength;
    defstream.next_out = (Bytef*)deflateBuffer.data();
    defstream.avail_out = (uInt)deflateBuffer.size();

    deflateInit2(&defstream, Z_BEST_COMPRESSION, Z_DEFLATED, -15, MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);
    deflate(&defstream, Z_FINISH);
    deflateEnd(&defstream);

    deflateBuffer.resize(deflateBuffer.size() - defstream.avail_out);
    return deflateBuffer;
}

RPF7Archive::EntryDataBuffer RPF7Archive::DecompressData(uint8_t* data, uint64_t dataLength)
{
    constexpr uint64_t CHUNK_SIZE = 128;
    EntryDataBuffer inflateBuffer(CHUNK_SIZE);
    EntryDataBuffer fullInflatedBuffer;

    z_stream defstream;
    defstream.zalloc = Z_NULL;
    defstream.zfree = Z_NULL;
    defstream.opaque = Z_NULL;
    defstream.next_in = (Bytef*)data;
    defstream.avail_in = (uInt)dataLength;
    defstream.next_out = (Bytef*)inflateBuffer.data();
    defstream.avail_out = (uInt)inflateBuffer.size();

    inflateInit2(&defstream, -15);
    int ret = 0;
    do
    {
        defstream.next_out = inflateBuffer.data();
        defstream.avail_out = inflateBuffer.size();

        ret = inflate(&defstream, 0);

        fullInflatedBuffer.insert(fullInflatedBuffer.end(), inflateBuffer.data(), inflateBuffer.data() + (CHUNK_SIZE - defstream.avail_out));
    } while (ret == Z_OK);
    inflateEnd(&defstream);

    return fullInflatedBuffer;
}

std::filesystem::path RPF7Archive::CorrectEntryPath(const std::filesystem::path& entryPath)
{
    std::string relativePathStr = entryPath.string();
    if (relativePathStr.front() != '/' && relativePathStr.front() != '\\')
        relativePathStr.insert(0, "/");

    std::replace(relativePathStr.begin(), relativePathStr.end(), '\\', '/');
    return std::filesystem::path(relativePathStr);
}

RPF7Archive::EntryDataBuffer RPF7Archive::GetFileData(const std::filesystem::path& filePath)
{
    EntryDataBuffer fileBuffer;
    if (!std::filesystem::exists(filePath))
        return fileBuffer;

    if (!std::filesystem::is_regular_file(filePath))
        return fileBuffer;

    uint64_t fileSize = GetFileSize(filePath);
    fileBuffer.resize(fileSize);

    std::fstream tmpStream(filePath, std::ios::binary | std::ios::in);
    if (!tmpStream.is_open())
        return fileBuffer;

    tmpStream.read(reinterpret_cast<char*>(fileBuffer.data()), fileSize);
    return fileBuffer;
}

uint64_t RPF7Archive::GetFileSize(const std::filesystem::path& filePath)
{
    if (!std::filesystem::exists(filePath))
        return 0;

    if (!std::filesystem::is_regular_file(filePath))
        return 0;

    std::fstream tmpStream(filePath, std::ios::binary | std::ios::in);
    if (!tmpStream.is_open())
        return 0;

    tmpStream.seekg(0, std::ios::end);
    uint64_t size = tmpStream.tellg();
    tmpStream.close();

    return size;
}