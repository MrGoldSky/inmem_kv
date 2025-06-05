#pragma once

#include <memory>
#include <thread>
#include <vector>

#include "config.hpp"
#include "kv/hash_table.hpp"

namespace kv {

/*
    Класс ShardedHashMap хранит numShards независимых HashTable и
    делегирует в них операции put/get/erase в зависимости от ключа.
*/
template <typename Key, typename Value, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
class ShardedHashMap {
   public:
    explicit ShardedHashMap(size_t numShards = kv::config::HASH_MAP_SHARDS) : numShards_(numShards) {
        shards_.reserve(numShards_);
        for (size_t i = 0; i < numShards_; ++i) {
            shards_.push_back(
                std::make_unique<HashTable<Key, Value, Hash, KeyEqual>>());
        }
    };

    // Вставка или обновление. Возвращает true, если успешно.
    bool put(const Key& key, const Value& value) {
        size_t idx = getShardIndex(key);
        return shards_[idx]->put(key, value);
    }

    // Чтение: если есть, вернёт std::optional с копией value, иначе пустой optional.
    std::optional<Value> get(const Key& key) const {
        size_t idx = getShardIndex(key);
        return shards_[idx]->get(key);
    }

    // Удаление: true, если элемент был и удалён, false, если элемента не было.
    bool erase(const Key& key) {
        size_t idx = getShardIndex(key);
        return shards_[idx]->erase(key);
    }

    size_t size() const {
        size_t total = 0;
        for (const auto& tablePtr : shards_) {
            total += tablePtr->size();
        }
        return total;
    };

   private:
    size_t getShardIndex(const Key& key) const {
        return hash_(key) % numShards_;
    }

    size_t numShards_;
    std::vector<std::unique_ptr<HashTable<Key, Value, Hash, KeyEqual>>> shards_;
    Hash hash_;
};

}  // namespace kv
