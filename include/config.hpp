#pragma once

#include <cstddef>
#include <cstdint>

namespace kv::config {

// Порт TCP‐сервера (можно изменить на любой свободный)
inline constexpr std::uint16_t SERVER_PORT = 4000;

// Размерность пула потоков по умолчанию.
inline constexpr std::size_t THREAD_POOL_SIZE = 4;

// Количество сегментов (shards) в sharded hash map.
inline constexpr std::size_t HASH_MAP_SHARDS = 16;

// Максимальная длина ключа (в байтах), если вы лимитируете строковые ключи.
inline constexpr std::size_t MAX_KEY_SIZE = 128;

// Максимальный размер значения (value) в байтах.
inline constexpr std::size_t MAX_VALUE_SIZE = 1024 * 10;  // 10 KB

// Лимит одновременных соединений (можно использовать для балансировки).
inline constexpr std::size_t MAX_CONNECTIONS = 1024;

// Логический флаг: включать ли расширенную (debug) трассировку.
inline constexpr bool ENABLE_DEBUG_LOG = true;

}  // namespace kv::config
