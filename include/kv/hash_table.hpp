// файл: include/kv/hash_table.hpp
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <vector>

#include "allocator.hpp"

namespace kv {

template <typename Key, typename Value>
struct HashNode {
    Key key;
    Value value;
    HashNode* next;
};

// Односегментная хеш-таблица с цепочками:
//   - capacity_: число bucket’ов
//   - buckets_: вектор указателей HashNode* (по одному списку на корзину)
//   - tableMutex_: для защиты при resize всего массива корзин
//   - nodePool_: пул для выделения узлов
template <typename Key, typename Value, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key> >
class HashTable {
   public:
    HashTable(size_t initial_capacity = 1024)
        : capacity_(initial_capacity > 0 ? initial_capacity : 1),
          buckets_(capacity_, nullptr),
          hash_(),
          keyEqual_(),
          nodePool_(MemoryPool(sizeof(HashNode<Key, Value>), capacity_)),
          size_(0) {}

    ~HashTable() {
        for (size_t i = 0; i < capacity_; ++i) {
            HashNode<Key, Value>* node = buckets_[i];
            while (node) {
                HashNode<Key, Value>* next = node->next;
                node->~HashNode<Key, Value>();
                nodePool_.deallocate(node);
                node = next;
            }
        }
    }

    bool put(const Key& key, const Value& value) {
        std::unique_lock lock(tableMutex_);
        size_t idx = hash_(key) % capacity_;

        HashNode<Key, Value>* node = buckets_[idx];
        while (node) {
            if (keyEqual_(node->key, key)) {
                node->value = value;
                return true;
            }
            node = node->next;
        }

        void* rawNode = nodePool_.allocate();
        auto* newNode = new (rawNode) HashNode<Key, Value>{key, value, buckets_[idx]};
        buckets_[idx] = newNode;
        ++size_;
        bool needRehash = (static_cast<float>(size_) > static_cast<float>(capacity_) * maxLoadFactor_);
        lock.unlock();

        if (needRehash) {
            rehash();
        }

        return true;
    }

    std::optional<Value> get(const Key& key) const {
        std::shared_lock lock(tableMutex_);

        size_t idx = hash_(key) % capacity_;
        HashNode<Key, Value>* node = buckets_[idx];
        while (node) {
            if (keyEqual_(node->key, key)) {
                return node->value;
            }
            node = node->next;
        }
        return std::nullopt;
    }

    bool erase(const Key& key) {
        std::unique_lock lock(tableMutex_);

        size_t idx = hash_(key) % capacity_;
        HashNode<Key, Value>* node = buckets_[idx];
        HashNode<Key, Value>* prev = nullptr;

        while (node) {
            if (keyEqual_(node->key, key)) {
                if (prev == nullptr) {
                    buckets_[idx] = node->next;
                } else {
                    prev->next = node->next;
                }
                node->~HashNode<Key, Value>();
                nodePool_.deallocate(node);
                --size_;
                return true;
            }
            prev = node;
            node = node->next;
        }
        return false;
    }
    size_t size() { return size_; }

   private:
    size_t capacity_;
    std::vector<HashNode<Key, Value>*> buckets_;

    mutable std::shared_mutex tableMutex_;

    Hash hash_;
    KeyEqual keyEqual_;

    MemoryPool nodePool_;

    float maxLoadFactor_ = 0.75f;
    size_t size_;

    void rehash() {
        std::unique_lock lock(tableMutex_);

        size_t newCapacity = capacity_ * 2;
        std::vector<HashNode<Key, Value>*> newBuckets(newCapacity, nullptr);

        for (size_t i = 0; i < capacity_; ++i) {
            HashNode<Key, Value>* node = buckets_[i];
            while (node) {
                HashNode<Key, Value>* next = node->next;
                size_t newIdx = hash_(node->key) % newCapacity;
                node->next = newBuckets[newIdx];
                newBuckets[newIdx] = node;
                node = next;
            }
        }

        capacity_ = newCapacity;
        buckets_.swap(newBuckets);
    };
};

}  // namespace kv
