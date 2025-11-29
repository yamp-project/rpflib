#pragma once

#include <string>

namespace rpflib
{
    template<typename EntryType>
    struct EntryNode
    {
        using EntryNodeType = EntryNode<EntryType>;

        EntryType* m_Entry = nullptr;
        EntryNodeType* m_Parent = nullptr;
        EntryNodeType* m_FirstChild = nullptr;
        EntryNodeType* m_NextSibling = nullptr;

        std::string m_Name {};
        uint32_t m_ChildrenCount = 0;
        std::filesystem::path m_RelativePath { };
        std::filesystem::path m_FilePath { };

        EntryNode() = default;
        EntryNode(const std::string& name, EntryNode* parent) :
            m_Name(name), m_Parent(parent) { }
        ~EntryNode() = default;

        [[nodiscard]] EntryNodeType* Find(const std::string& item) const
        {
            EntryNodeType* current = m_FirstChild;
            while (current && current->m_Name != item)
                current = current->m_NextSibling;

            return current;
        }

        [[nodiscard]] EntryNodeType* GetLastChild() const
        {
            EntryNodeType* nextChild = m_FirstChild;
            EntryNodeType* child = nullptr;

            while(nextChild != nullptr)
            {
                child = nextChild;
                nextChild = nextChild->m_NextSibling;
            }

            return child;
        }

        [[nodiscard]] uint32_t GetChildrenCount() const
        {
            return m_ChildrenCount;
        }

        EntryNodeType* Add(const std::string& name)
        {
            EntryNodeType* newItem = new EntryNodeType(name, this);
            if(m_FirstChild == nullptr)
                m_FirstChild = newItem;
            else
                GetLastChild()->m_NextSibling = newItem;

            m_ChildrenCount++;
            return newItem;
        }

        bool HasChildren() const { return m_FirstChild != nullptr; }
    };
}