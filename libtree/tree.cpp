#include "MerkleTree.h"

bool MerkleTree::FileNode::isFolder()
{
    FileNode* p = this->parent;
    std::filesystem::path path = this->filepath;
    while (p != nullptr) {
        path = p->filepath / path;
        p = p->parent;
    }
    if (std::filesystem::is_directory(path)) {
        return true;
    }
    else return false;
}

bool MerkleTree::FileNode::isDiff(FileNode const* other)
{
    return this->hash != other->hash;
}

MerkleTree::FileNode::FileNode(std::string const& time,
    std::filesystem::path const& path)
    : filepath(path)
{
    hashString = time + '|' + path.string();
    SHA256(reinterpret_cast<unsigned char const*>(hashString.c_str()),
        hashString.size(), hash.data());
}
MerkleTree::FileNode::FileNode(std::string s) : hashString(std::move(s))
{
    SHA256(reinterpret_cast<unsigned char const*>(hashString.c_str()),
        hashString.size(), hash.data());
}

MerkleTree::FileNode::FileNode(unsigned char* h)
{
    std::memcpy(hash.data(), h, 32);
}

MerkleTree::FileNode::FileNode(FileNode* other)
    : hashString(other->hashString), parent(other->parent),
    firstChild(other->firstChild), next(other->next),
    childNum(other->childNum), hash{ other->hash }
{
}

MerkleTree::FileNode::FileNode()
{
    hashString = "";
    SHA256(reinterpret_cast<unsigned char const*>(hashString.c_str()),
        hashString.size(), hash.data());
}

void MerkleTree::sync_from(MerkleTree const& other)
{
    syncFile(root_, other.root_, root_->filepath, other.root_->filepath);
}

MerkleTree::MerkleTree(std::string dir_path)
{
    namespace fs = std::filesystem;

    if (!fs::is_directory(dir_path)) {
        throw std::runtime_error{ "path isn't a directory" };
    }
    folder_ = std::move(dir_path);
    root_ = buildTree(folder_); // �ݹ齨��
}

void MerkleTree::syncFile(FileNode* A, FileNode* B,
    std::filesystem::path const& rootA,
    std::filesystem::path const& rootB)
{
    namespace fs = std::filesystem;

    // Ҫ���ݸ�Ŀ¼�ľ���·������Ȼ�޷���λ�ļ�
    // Should be fixed here: logic error
    if (!A || !B || !A->isFolder() || !B->isFolder()) {
        throw std::runtime_error("node error(use error)");
        return;
    }

    // B��Aû��
    FileNode* currentB = B->firstChild;
    while (currentB) {
        FileNode* correspondingA = findFile(A, currentB->filepath);
        if (!correspondingA) {
            // A �в����ڣ�ɾ��B�н���Ӧ���ļ����ļ���
            std::filesystem::path targetPath = rootB / currentB->filepath;
            if (currentB->isFolder()) {
                std::filesystem::remove_all(targetPath); // ɾ���ļ���
            }
            else {
                std::filesystem::remove(targetPath); // ɾ���ļ�
            }
            deleteNode(currentB, currentB->filepath); // ɾ����ϣ���ж�Ӧ�ڵ�
        }
        currentB = currentB->next;
    }

    // A��Bû��
    FileNode* currentA = A->firstChild;
    while (currentA) {
        FileNode* correspondingB = findFile(B, currentA->filepath);
        if (!correspondingB) {
            // B �в����ڣ����� A ���ļ����ļ��е� B
            auto sourcePath = rootA / currentA->filepath;
            auto targetPath = rootB / currentA->filepath;

            if (currentA->isFolder()) {
                fs::create_directories(
                    targetPath); // �������ļ��У������ڱ������ʱ�ݹ����
            }
            else {
                fs::copy(sourcePath, targetPath,
                    fs::copy_options::overwrite_existing); // �����ļ�
            }
            FileNode* newNode = new FileNode(currentA->hashString);
            addNode(newNode, B);
        }
        currentA = currentA->next;
    }

    // ͬʱ���� A �� B������ϣֵ��ͬ�Ľڵ㲢����
    currentA = A->firstChild;
    currentB = B->firstChild;
    while (currentA && currentB) {
        if (currentA->filepath == currentB->filepath) {
            if (currentA->isDiff(currentB)) {
                // ��ϣֵ��ͬ�����Ǹ��� B ���ļ�
                auto sourcePath = rootA / currentA->filepath;
                auto targetPath = rootB / currentB->filepath;
                fs::copy(sourcePath, targetPath,
                    fs::copy_options::overwrite_existing); // ���Ǹ���
                std::string time = std::to_string(
                    fs::last_write_time(sourcePath).time_since_epoch().count());
                changeHash(currentB, time,
                    currentB->filepath); // ���� B �Ĺ�ϣֵ
            }
            // �ݹ鴦����Ŀ¼
            if (currentA->isFolder() && currentB->isFolder()) {
                syncFile(currentA, currentB, rootA, rootB);
            }
            currentA = currentA->next;
            currentB = currentB->next;
        }
        else {
            throw std::runtime_error("match error(code error)");
        }
    }
}