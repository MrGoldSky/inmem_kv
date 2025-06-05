// файл: include/kv/allocator.hpp
#pragma once
#include <cstddef>
#include <mutex>
#include <new>
#include <vector>

namespace kv {

/*
    Простая реализация memory pool-а для объектов фиксированного размера.
    Каждый раз, когда список свободных ячеек пуст, мы выделяем новый большой блок (через malloc)
    и разрезаем его на chunks размером blockSize_ -> кладём в свободный список.
*/

class MemoryPool {
   public:
    MemoryPool(size_t blockSize, size_t blocksCount);
    ~MemoryPool();

    void* allocate();
    void deallocate(void* ptr);

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

   private:
    struct FreeNode {
        FreeNode* next;
    };

    size_t blockSize_;
    size_t blocksCount_;

    FreeNode* freeList_;

    std::vector<void*> allBlocks_;

    std::mutex mtx_;

    void allocateBlock();
};

}  // namespace kv
