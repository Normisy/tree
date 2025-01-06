#pragma once
#include <algorithm>
#include <array>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <openssl/sha.h>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace boost::serialization {

    // Specialization for std::filesystem::path
    template <typename Archive>
    void serialize(Archive& ar, std::filesystem::path& p,
        unsigned int const version)
    {
        if constexpr (Archive::is_saving::value) {
            ar& p.string();
        }
        else {
            std::string buf;
            ar& buf;
            p = buf;
        }
    }

} // namespace boost::serialization

class MerkleTree {
private:
    struct FileNode {

        std::string hashString;
        std::filesystem::path filepath;
        std::array<unsigned char, 32> hash;
        FileNode* parent = nullptr;
        FileNode* firstChild = nullptr;
        FileNode* next = nullptr;
        uint64_t childNum = 0;

        bool isFolder();

        bool isDiff(FileNode const* other);

        FileNode(std::string const& time, std::filesystem::path const& path);

        FileNode(std::string s);

        FileNode(unsigned char* h);

        FileNode(FileNode* other);

        FileNode();

        // ���л�����
        template <class Archive>
        void serialize(Archive& ar, unsigned int const version)
        {
            ar& hashString;
            ar& filepath;
            ar& childNum;
            ar& boost::serialization::make_array(hash.data(), hash.size());
        }
    };

    std::filesystem::path folder_;
    FileNode* root_ = nullptr;

    FileNode* buildTree(const std::filesystem::path& p)
    {
        assert(std::filesystem::is_directory(p) || std::filesystem::is_directory(folder_ / p));
        std::queue<FileNode*> total; // ��˳�򹹽�

        std::vector<std::filesystem::path> paths;
        for (auto const& i : std::filesystem::directory_iterator(folder_ / p)) {
            paths.push_back(std::filesystem::relative(i, folder_));  //���·��
        }

        // ά��һ������ȶ���˳��ʹ�õ����������ļ���˳����ܲ�һ�£�
        std::sort(paths.begin(), paths.end());

        uint64_t cnt = 0;
        for (auto const& i : paths) {
            if (std::filesystem::is_directory(folder_ / i)) {
                FileNode* now = buildTree(i);
                cnt += now->childNum;
                std::string h(reinterpret_cast<char*>(now->hash.data()), 32);
                now->filepath = i;  //���·��
                total.push(now);
            }
            else {
                std::string t =
                    std::to_string(std::filesystem::last_write_time(folder_ / i)
                        .time_since_epoch()
                        .count()); // ����޸�ʱ��
                FileNode* now = new FileNode(t, i);
                cnt++;
                std::string h(reinterpret_cast<char*>(now->hash.data()), 32);

                total.push(now);
            }
        }

        std::ostringstream fatherstr;

        FileNode* firstChild = nullptr;

        while (total.size() > 0) {
            FileNode* curr = total.front();
            total.pop();
            if (firstChild == nullptr)
                firstChild = curr;

            curr->next = (total.empty()) ? nullptr : total.front();
            std::string h(reinterpret_cast<char*>(curr->hash.data()), 32);
            fatherstr << h;
        }

        FileNode* current = new FileNode(fatherstr.str());
        current->filepath = p;
        current->firstChild = firstChild;
        current->childNum = cnt;

        while (firstChild != nullptr) {
            firstChild->parent = current;
            firstChild = firstChild->next;
        }

        return current;
    }

    FileNode* findFile(FileNode* folder, std::filesystem::path const& relative)
    { // ��һ������㿪ʼѰ�ҵ�ǰ�ļ�����·��Ϊrelative�Ľ��
        if (folder == nullptr)  return nullptr;
        FileNode* root = folder->firstChild;
        while (root != nullptr && root->filepath != relative)
            root = root->next;
        return root;
    }

    FileNode* findPre(FileNode* folder, std::filesystem::path const& relative) {
        if (folder == nullptr)  return nullptr;
        FileNode* root = folder->firstChild;

        while (root != nullptr && root->next->filepath != relative)
            root = root->next;
        return root;
    }

    void recomputeHash(FileNode* root)
    { // root�Ǿ����޸ĵ��ļ����
        if (root == nullptr)
            return;
        FileNode* iter = root->parent->firstChild;
        std::stringstream rehash;
        while (iter != nullptr) {
            std::string h(reinterpret_cast<char*>(iter->hash.data()), 32);
            rehash << h;

            iter = iter->next;
        }
        std::string f = rehash.str();
        SHA256(reinterpret_cast<unsigned char const*>(f.c_str()), f.size(),
            root->parent->hash.data());
    }

    // �����ļ��ڵ�, insert it to keep ascending property.
    void addNode(FileNode* newNode, FileNode* folder)
    {
        // Initializes newNode's parent.
        newNode->parent = folder;

        // Inserts newNode to ascending linked list.
        auto head = std::make_unique<FileNode>(); // Virtual node
        head->next = folder->firstChild;

        auto* p{ head.get() };
        while (p->next != nullptr && newNode->filepath >= p->next->filepath) {
            p = p->next;
        }
        newNode->next = p->next;
        p->next = newNode;

        folder->firstChild = head->next;

        recomputeHash(newNode);
    }

    bool deleteNode(FileNode* folder, std::filesystem::path const& file)
    { // ɾ���ļ����
        FileNode* f = findFile(folder, file);
        FileNode* pre = findPre(folder, file);
        if (f == nullptr) return false;

        if (pre == nullptr) {  //folder��firstChild���
            folder->firstChild = f->next;
            delete f;
            recomputeHash(folder->firstChild);
            return true;
        }
        else {
            pre->next = f->next;
            delete f;
            recomputeHash(folder->firstChild);
            return true;
        }
    }

    void changeHash(FileNode* root, std::string const& time,
        std::filesystem::path const& path)
    { // �����ļ�����¹�ϣֵ
        std::string hashString = time + '|' + path.string();
        SHA256(reinterpret_cast<unsigned char const*>(hashString.c_str()),
            hashString.size(), root->hash.data());
        recomputeHash(root);
    }

    // ��A�������ļ��У�B�Ǳ�ͬ���ļ���
    // ����׼����Ϊû�������¼��������ƣ�ֻ�����ҵ��������·����A�е�����B�е��ļ�·���������·����B�в���A�е��ļ�·����Ȼ�����B��������ɾ��
    // ֮��A��B������ȫ��Ӧ���ˣ���A��Bһһ��Ӧ�ر������ȶԹ�ϣֵ�������ϣֵ��һ�£���ô���Ǹ��µ�ǰָ�������洢�����·�����ļ�����

    void syncTree(FileNode* A, FileNode* B)
    { // ��ϣ���ĸ��£������ļ��ĸ��£�������ά����ǰ�ļ��й�ϣ����������

        if (!A || !B || !A->isFolder() || !B->isFolder()) {
            throw std::runtime_error("node error(use error)");
            return;
        }

        // �ҵ� A �д��ڵ� B �в����ڵ��ļ����ļ���
        std::unordered_map<std::string, FileNode*> B_map;
        FileNode* currentB = B->firstChild;
        while (currentB != nullptr) {
            B_map[currentB->filepath.string()] = currentB;
            currentB = currentB->next;
        }

        FileNode* currentA = A->firstChild;
        while (currentA) {
            if (B_map.find(currentA->filepath.string()) == B_map.end()) {
                // B�в�����A���ļ���㣬��ӵ� B
                FileNode* newNode = new FileNode(currentA->hashString);
                addNode(newNode, B);
            }
            else
                B_map.erase(
                    currentA->filepath
                    .string()); // ɾ������A�еĽ�㣬����ʣ�µľ���B����A���޵����н����
            currentA = currentA->next;
        }

        // �ҵ� B �д��ڵ� A �в����ڵ��ļ����ļ���
        for (auto const& [filepath, node] : B_map) {
            deleteNode(B, filepath); // ɾ�� B �ж�����ļ����ļ���
        }

        // �������������ȶԹ�ϣֵ�������һ�������
        currentA = A->firstChild;
        currentB = B->firstChild;
        while (currentA && currentB) {
            if (currentA->filepath == currentB->filepath) {
                if (currentA->isDiff(currentB)) {
                    // Hash ��һ�£����� B �е��ļ�
                    std::string time = std::to_string(
                        std::filesystem::last_write_time(currentA->filepath)
                        .time_since_epoch()
                        .count());
                    changeHash(currentB, time, currentA->filepath);
                }
                // �ݹ���ô������ļ���
                if (currentA->isFolder() && currentB->isFolder()) {
                    syncTree(currentA, currentB);
                }
                currentA = currentA->next;
                currentB = currentB->next;
            }
            else {
                // ���������·����ƥ����������Ӧ��������
                throw std::runtime_error("match error(code error)");
                break;
            }
        }
    }

    // A��ȻΪ�����ļ���
    void syncFile(FileNode* A, FileNode* B, std::filesystem::path const& rootA,
        std::filesystem::path const& rootB);

    void deleteTree(FileNode* node)
    {
        if (node == nullptr)
            return;
        deleteTree(node->firstChild);
        deleteTree(node->next);
        delete node;
    }

    // ���л���ϣ�����ļ���
    void writeTree(std::ofstream& ofile) const
    {
        if (!ofile) {
            throw std::runtime_error("ostream error");
            return;
        }
        boost::archive::text_oarchive oa(ofile);
        oa << root_;
        ofile.close();
        std::cout << "��ɱ���" << std::endl;
    }

    // boost�ⲻ֧��˫��ָ�룬���ֻ���浥������һ�������ָ�����¹���
    void ptrHelper(FileNode* root)
    {
        if (root == nullptr || root->firstChild == nullptr)
            return;
        FileNode* iter = root->firstChild;
        while (iter != nullptr) {
            iter->parent = root;
            ptrHelper(root); // �������ļ��н��
            iter = iter->next;
        }
    }

    void rebuildTree(std::ifstream& ifile)
    {
        assert(root_ == nullptr);
        if (!ifile) {
            throw std::runtime_error("istream error");
            return;
        }
        boost::archive::text_iarchive ia(ifile);
        ia >> root_;
        ptrHelper(root_);
        ifile.close();
        std::cout << "��ɶ�ȡ�͹���" << std::endl;
    }

public:
    MerkleTree() = default;

    MerkleTree(std::string dir_path);

    bool isSame(MerkleTree* other)
    {
        return root_->hash == other->root_->hash;
    }

    void updateTree(MerkleTree* old)
    {
        syncTree(root_, old->root_);
    }

    void readTree(std::string const& filepath)
    {
        std::ifstream ifile(filepath);
        rebuildTree(ifile);
    }

    void makeTree(std::string const& filepath)
    {
        std::ofstream ofile(filepath);
        writeTree(ofile);
    }

    ~MerkleTree()
    {
        deleteTree(root_);
    }

    void sync_from(MerkleTree const& other);

    template <class Archive>
    void serialize(Archive& ar, unsigned int const version)
    {
        ar& folder_;
    }
};
