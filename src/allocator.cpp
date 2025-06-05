#include "kv/allocator.hpp"

#include <cassert>
#include <cstdlib>

namespace kv {

MemoryPool::MemoryPool(size_t blockSize, size_t blocksCount) : blockSize_(blockSize),
                                                               blocksCount_(blocksCount),
                                                               freeList_(nullptr) {
    assert(blockSize_ >= sizeof(FreeNode));
}
MemoryPool::~MemoryPool() {
    for (size_t i = 0; i < allBlocks_.size(); ++i) {
        std::free(allBlocks_[i]);
    }
    freeList_ = nullptr;
}

void MemoryPool::allocateBlock() {
    size_t totalSize = blockSize_ * blocksCount_;
    void* block = std::malloc(totalSize);

    if (block == nullptr)
        throw std::bad_alloc();

    allBlocks_.push_back(block);

    char* ptr = static_cast<char*>(block);
    for (size_t i = 0; i < blocksCount_; ++i) {
        FreeNode* node = reinterpret_cast<FreeNode*>(ptr + i * blockSize_);
        node->next = freeList_;
        freeList_ = node;
    }
}

void* MemoryPool::allocate() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!freeList_) {
        allocateBlock();
    }

    FreeNode* node = freeList_;
    freeList_ = freeList_->next;
    return node;
}

void MemoryPool::deallocate(void* ptr) {
    if (!ptr) return;
    std::lock_guard<std::mutex> lock(mtx_);
    FreeNode* node = reinterpret_cast<FreeNode*>(ptr);
    node->next = freeList_;
    freeList_ = node;
}

}  // namespace kv