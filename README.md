# KV: Минималистичный in-memory Key-Value Store

Проект представляет собой простой, но функционально полный in-memory key-value store с собственным сетевым сервером.

- Использование не блокирующего ввода-вывода через корутины (асинхронный EventLoop на `epoll`/`select`) для обработки сетевых соединений. 
- Управление памятью на низком уровне через собственный Memory Pool (аллокатор), реализующий быстрый `allocate`/`deallocate` блоков фиксированного размера. 
- Организация параллельного исполнения через пул потоков (`ThreadPool`) с очередью задач и безопасной синхронизацией. 
- Простую, но гибкую систему логирования (уровни логов, вывод в консоль и/или файл). 
- Реализацию хеш‐таблицы с шардированием, для уменьшения конкуренции при одновременном доступе из нескольких потоков. (шардированный map в `Server`)
- Неблокирующий сетевой сервер, обрабатывающий команды `GET`, `SET`, `DEL` в текстовом протоколе.

---

## Содержание

1. [Структура проекта](#структура-проекта)  
2. [Основные модули и их назначение](#основные-модули-и-их-назначение)  
   1. [Конфигурация (`config.hpp`)](#конфигурация-confighpp)  
   2. [Логирование (`logger.hpp`, `logger.cpp`)](#логирование-loggerhpp-loggercpp)  
   3. [Пул потоков (`thread_pool.hpp`, `thread_pool.cpp`)](#пул-потоков-thread_poolhpp-thread_poolcpp)  
   4. [Аллокатор памяти (`allocator.hpp`, `allocator.cpp`)](#аллокатор-памяти-allocatorhpp-allocatorcpp)  
   5. [Асинхронный I/O (корутины) (`coroutine_io.hpp`, `coroutine_io.cpp`)](#асинхронный-io-корутины-coroutine_iohpp-coroutine_iocpp)  
   6. [Хеш-таблица и шардирование (`hash_table.hpp`, `sharded_hash_map.hpp`)](#хеш-таблица-и-шардирование-hash_tablehpp-sharded_hash_maphpp)  
   7. [Сервер (`server.hpp`)](#сервер-serverhpp-servercpp)  
   8. [Точка входа (`main.cpp`)](#точка-входа-maincpp)  
3. [Пример сборки и запуска](#пример-сборки-и-запуска)  
4. [Использование](#использование)  
   1. [Протокол взаимодействия](#протокол-взаимодействия)  
   2. [Пример клиентов](#пример-клиентов)  
5. [Особенности реализации](#особенности-реализации)  
   1. [MemoryPool и низкоуровневое управление памятью](#memorypool-и-низкоуровневое-управление-памятью)  
   2. [ThreadPool и многопоточность](#threadpool-и-многопоточность)  
   3. [Coroutine I/O и EventLoop](#coroutine-io-и-eventloop)  
   4. [Шардированная хеш-таблица](#шардированная-хеш-таблица)  
   5. [Конфигурация и настройки](#конфигурация-и-настройки)  
6. [Настройка логирования](#настройка-логирования)  
7. [Лицензия](#лицензия)  

---

## Структура проекта

```
.
├── CMakeLists.txt               # (по желанию) скрипт для сборки через CMake
├── README.md                    # (этот файл)
├── config.hpp                   # Параметры по умолчанию (порт, размер пула потоков и т.д.)
├── main.cpp                     # Точка входа, инициализация логгера, запуск сервера
├── kv/                          # Пространство имён kv
│   ├── allocator.hpp            # Интерфейс MemoryPool
│   ├── allocator.cpp            # Реализация MemoryPool
│   ├── coroutine_io.hpp         # Интерфейс асинхронного I/O
│   ├── coroutine_io.cpp         # Реализация EventLoop (epoll/`select`), Read/Write Awaitable для Windows/Linux
│   ├── hash_table.hpp           # Модульная хеш-таблица
│   ├── sharded_hash_map.hpp     # Sharded-обёртка над hash_table
│   ├── logger.hpp               # Интерфейс логгера: уровни (TRACE/DEBUG/INFO/WARN/ERROR/FATAL) и макросы `LOG_*`
│   ├── logger.cpp               # Реализация логирования: консоль + файл, безопасность потоков, форматирование timestamp fileciteturn0file1
│   ├── server.hpp               # Интерфейс сетевого сервера: шаблонный класс Server<Key,Value>, содержащий `sharded_map` и логику обработки команд, настройку сокета
│   └── thread_pool.hpp          # Интерфейс ThreadPool: запуск пула
│   └── thread_pool.cpp          # Реализация ThreadPool: блокировка очереди задач (mutex/condition), потоки‐работники, atomic для учёта активных задач 
└── kv_server.log                # Файл логов по умолчанию (генерируется при запуске)
```

---

## Основные модули и их назначение

### Конфигурация (`config.hpp`)
Файл `config.hpp` содержит константы, задающие настройки сервера по умолчанию:  
- `SERVER_PORT` — порт, на котором будет слушать сервер (по умолчанию 5555).  
- `THREAD_POOL_SIZE` — число рабочих потоков в пуле (по умолчанию `std::thread::hardware_concurrency()` или установленное вручную значение).  


### Логирование (`logger.hpp`, `logger.cpp`)
- **`kv::log::LoggerConfig`**: структура, задающая параметры логирования:  
  - `level` — минимальный уровень логирования (`TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL`).  
  - `to_console` — вывод в консоль (stdout/ stderr).  
  - `to_file` — вывод в файл.  
  - `filename` — путь к файлу логов.  
- **Макросы `LOG_TRACE`, `LOG_DEBUG`, `LOG_INFO` и пр.** позволяют указать сообщение, имя файла/номер строки, уровень и сам текст.  

### Пул потоков (`thread_pool.hpp`, `thread_pool.cpp`)
- **`kv::ThreadPool`**: при инициализации создаётся несколько рабочих потоков (по умолчанию – число аппаратных ядер).  
- **Метод `submit(std::function<void()>)`** добавляет задачу, увеличивает счётчик активных задач (`activeTasks_`), и разбудит один поток.  
- Рабочий поток в `workerThread()` ждет по `std::condition_variable`, затем забирает задачу, выполняет её, затем уменьшает `activeTasks_`.  
- При вызове `shutdown()` устанавливается флаг `stop_ = true`, пробуждает все потоки, и ждёт их завершения.

### Аллокатор памяти (`allocator.hpp`, `allocator.cpp`)
- **`kv::MemoryPool`** — пул блоков фиксированного размера (`blockSize`) и заданного числа блоков за одну аллокацию (`blocksCount`).  
- При первом запросе `allocate()` внутри `freeList_` нет узлов — вызывается `allocateBlock()`, выделяется кусок памяти размером `blockSize * blocksCount`, разбивается на `blocksCount` узлов `FreeNode`, и все они встают в список свободных.  
- При `allocate()` берется первый узел из списка `freeList_`.  
- При `deallocate(void*)` указатель возвращается в голову списка.  
- Для защиты списка используется `std::mutex` (`mtx_`).  
- При выгрузке (`~MemoryPool`) все ранее выделенные блоки (`allBlocks_`) освобождаются через `free()`.

### Асинхронный I/O (корутины) (`coroutine_io.hpp`, `coroutine_io.cpp`)
- **`kv::EventLoop`** — синглтон, внутри себя хранит файловый дескриптор `epollFd_` (Linux) или использует `select` (Windows).  
- **Методы `add_reader(int fd, coroutine_handle<>)` / `add_writer(int fd, coroutine_handle<>)`** регистрируют дескрипторы и корутины, ожидающие готовности на чтение/запись. На Linux сначала ставят дескриптор в неблокирующий режим (`fcntl(fd, O_NONBLOCK)`), затем добавляют в `epoll` с флагом `EPOLLIN | EPOLLET` либо `EPOLLOUT | EPOLLET`.  
- **`wait_and_handle_epoll()`**: бесконечный цикл `epoll_wait(...)`, затем для каждого готового дескриптора извлекается соответствующая корутина (`handlers_[fd]`), убирается из мапы и вызывается `handle.resume()`.  
- **`ReadAwaitable` / `WriteAwaitable`**: объекты, возвращаемые функциями `async_read(fd, buf, size)` / `async_write(fd, buf, size)`. При первом `co_await` вызывают `await_suspend`, где размещают себя в `EventLoop`; когда `epoll` сообщает, что дескриптор готов, вызывается `await_resume()`, который либо читает (`::read` / `::recv`) либо пишет (`::write` / `::send`) данные.

### Хеш-таблица и шардирование (`hash_table.hpp`, `sharded_hash_map.hpp`)
- **`kv::HashTable<Key, Value, Hash, KeyEqual>`** — однопоточная реализация хеш-таблицы. Детали реализации ядра хеш-таблицы находятся в `hash_table.hpp` —хеш-таблица с резервированием и динамическим ростом при нагрузке выше определенного порога.
- **`kv::ShardedHashMap<Key, Value, Hash, KeyEqual>`** — обёртка над N сегментами (каждый сегмент — своя `HashTable` + своя мьютекс/спинлок). При операциях `get`, `put`, `erase` вычисляется хеш ключа, берётся сегмент = `(hash % num_segments)`, и под соответствующим мьютексом совершается операция.  
- В `Server` создается `ShardedHashMap<std::string, std::string>` с 4 сегментами по умолчанию.

### Сервер (`server.hpp`, `server.cpp`)
- **Шаблонный класс `kv::Server<Key, Value, Hash = std::hash<Key>, KeyEqual = std::equal_to<Key>>`**:  
  - Конструктор принимает `address: string` и `port: uint16_t`.  
  - Внутри хранится `listenFd_`, а также `ShardedHashMap<Key,Value>` (по умолчанию 4 сегмента).  
  - **`setup_listening_socket()`**:  
    - Создание TCP-сокета (`socket(...)`), установка `SO_REUSEADDR`, `bind()`, `listen()`, затем перевод сокета в неблокирующий режим (`fcntl/` `ioctlsocket`).  
  - **`run()`**:  
    - Вызывает `setup_listening_socket()`.  
    - Запускает `accept_loop()` в отдельном потоке (detach), чтобы не блокировать цикл корутин.  
    - Запускает `EventLoop::instance().run()`, который обрабатывает `async_read` / `async_write`.  
  - **`accept_loop()`**:  
    - Бесконечный цикл `select(listenFd_)` (Linux/Windows), при появлении нового соединения – `accept()`, перевести клиентский FD в неблокирующий режим, сразу вызвать `handle_connection(clientFd)`, который возвращает `Task`.  
  - **`Task handle_connection(SOCKET_TYPE clientFd)`**:  
    - Бесконечный цикл: `n = co_await async_read(clientFd, buffer, 4096)`. Если `n <= 0`, закрыть соединение.  
    - Иначе парсит строку `req` (до `\n`), если начало `"GET "` – извлечь ключ, вызвать `shardedMap_.get(key)`, сформировать ответ (`value + "\n"` или `"NOT_FOUND\n"`), и сделать `co_await async_write(...)`.  
    - Аналогично для `"SET "` – найти пробел, разделяющий ключ и значение, вызвать `shardedMap_.put(key, value)`, и ответ `"STORED\n"`.  
    - Для `"DEL"` – `shardedMap_.erase(key)`, и ответ `"DELETED\n"` либо `"NOT_FOUND\n"`.  
    - Иначе ответ `"ERROR\n"`.  
    - После выхода из цикла закрывается FD (`close(fd)` или `closesocket`). 

### Точка входа (`main.cpp`)
- В `main(int argc, char* argv[])` читается порт из аргумента (или используется `config::SERVER_PORT`).  
- Создаётся `ThreadPool pool(config::THREAD_POOL_SIZE)`.
- Настраивается `LoggerConfig`: уровень `DEBUG`, вывод в консоль и в файл `kv_server.log`.  
- Логгер инициализируется, выводится `LOG_INFO("LaunchKV server on ...")`.  
- Создается `Server<std::string, std::string> server("0.0.0.0", port)`, запускается `server.run()`.  
- После `run()` управление никогда не возвращается (цикл `EventLoop` работает в текущем потоке, а `accept_loop` в фоновой нити), поэтому вызов `pool.shutdown()` – формальность “после завершения работы сервера”.

---

## Пример сборки и запуска

Запуск:

```bash
./kv_server [порт]
```

- Если не указан порт, берётся значение `config::SERVER_PORT` (по умолчанию 5555).  
- Логи будут писаться в файл `kv_server.log` и выводиться в консоль.  

---

## Использование

### Протокол взаимодействия

Сервер ожидает соединения по TCP, принимает текстовые команды, заканчивающиеся символом `\n`. Каждая команда — отдельная строка. Поддерживаются три базовых команды:

1. **GET <key>**  
   - Клиент отправляет: `GET my_key\n`  
   - Если ключ существует, сервер отвечает: `value\n`  
   - Если ключ отсутствует, сервер отвечает: `NOT_FOUND\n`  

2. **SET <key> <value>**  
   - Клиент отправляет: `SET my_key some_value\n`  
   - Сервер сохраняет пару (my_key → some_value) и отвечает: `STORED\n`  

3. **DEL <key>**  
   - Клиент отправляет: `DEL my_key\n`  
   - Если ключ был удалён, сервер отвечает: `DELETED\n`  
   - Если ключ отсутствует, сервер отвечает: `NOT_FOUND\n`  

Во всех остальных случаях (неизвестная команда) сервер отвечает: `ERROR\n`.

### Пример клиентов

#### 1. `telnet` / `nc`

```bash
# Подключаемся к серверу на localhost:5555
telnet 127.0.0.1 5555

# Внутри telnet вводим:
SET foo bar
# Получим: STORED

GET foo
# Получим: bar

DEL foo
# Получим: DELETED

GET foo
# Получим: NOT_FOUND
```

ncat 127.0.0.1 5555

Или с помощью `netcat` (`nc`):
```bash
# Подключаемся к серверу на localhost:5555
ncat 127.0.0.1 5555

# Внутри ncat вводим:
SET foo bar
# Получим: STORED

GET foo
# Получим: bar

DEL foo
# Получим: DELETED

GET foo
# Получим: NOT_FOUND
```

## Особенности реализации

### MemoryPool и низкоуровневое управление памятью

- **`MemoryPool`** реализован в `allocator.cpp`/`allocator.hpp`.  
- При создании объекта задается размер блока `blockSize` и число блоков в одном большом куске `blocksCount`.  
- **Алгоритм**:  
  1. При запросе `allocate()` – если `freeList_` пуст, вызываем `allocateBlock()`.  
  2. `allocateBlock()` делает `std::malloc(blockSize * blocksCount)` и разбивает память на узлы `FreeNode`, каждый из которых содержит указатель `next` на следующий свободный. Все новые узлы прицепляются к `freeList_`.  
  3. `allocate()` извлекает первый узел из `freeList_` и возвращает его адрес.  
  4. `deallocate(ptr)` – помещает данный узел обратно в `freeList_`.  
- Защита потоков обеспечивается `std::mutex mtx_`.  
- Преимущество: быстрая аллокация/делокация одноразмерных блоков без перехождения на `malloc`/`free` каждый раз.  

### ThreadPool и многопоточность

- **`ThreadPool`** в `thread_pool.cpp` / `thread_pool.hpp`.  
- Конструктор создаёт N потоков (N = аргумент или `std::thread::hardware_concurrency()`).  
- Задачи передаются через `submit(std::function<void()>)`.  
- Внутри:  
  - `std::queue<std::function<void()>> tasks_`, защищенная `std::mutex queueMutex_`.  
  - `std::condition_variable condition_`, на которую “спят” воркеры.  
  - `std::atomic<size_t> activeTasks_`, чтобы отслеживать количество выполняемых задач (можно добавить ожидание завершения).  
- Каждый воркер в `workerThread()`:  
  1. Ждёт `condition_.wait(...)`, пока не `stop_` или в `tasks_` не появится задача.  
  2. Если `stop_` и `tasks_` пустой — выходим.  
  3. Иначе забираем задачу, выходим из мьютекса, выполняем задачу (в `try/catch`).  
  4. Уменьшаем `activeTasks_`.  
- При вызове `shutdown()` устанавливаем `stop_ = true`, `condition_.notify_all()`, и ждем `join()` всех потоков. 

### Coroutine I/O и EventLoop

- **Асинхронный ввод-вывод** реализован на базе C++20 корутин.  
- **`EventLoop`** (синглтон) запускается в основном потоке (после `accept_loop`) и вызывает `epoll_wait` (Linux) или `select` (Windows) в бесконечном цикле, а затем пробуждает соответствующие корутины через `handle.resume()`. 
- В `ReadAwaitable::await_suspend(h)`/`WriteAwaitable::await_suspend(h)` корутина регистрируется в `EventLoop`, сохраняя `coroutine_handle`. Когда дескриптор готов, `await_resume()` либо читает (`::read`) либо пишет (`::write`) данные.  
- Благодаря неблокирующему режиму FD (fcntl/`ioctlsocket`) и `EPOLLET`, корутины будут возобновляться только при реальном приходе данных.  

### Шардированная хеш-таблица

- **Класс `kv::ShardedHashMap<Key, Value>`** – контейнер, объединяющий N независимых сегментов `HashTable<Key,Value> + mutex`.  
- При `put/get/erase(key)` вычисляется:  
  1. `hval = Hash{}(key)`  
  2. `idx = hval % num_shards`  
  3. Берётся `std::lock_guard` на `mutexes_[idx]`, вызывается соответствующий метод у `tables_[idx]`.  
- В ядре каждый сегмент — простая хеш-таблица (в `hash_table.hpp`), основанная на методе цепочек.  
- Шардирование позволяет распараллелить доступ к map: потоки, работающие с разными ключами, вероятнее работают с разными сегментами, что снижает конкуренцию. 

### Конфигурация и настройки

- **`config.hpp`** :  
  ```cpp
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
  }
  ```

---

## Настройка логирования

1. **Файл `kv/logger.hpp`** содержит перечисление уровней:
   ```cpp
   enum class Level { TRACE, DEBUG, INFO, WARN, ERROR, FATAL };
   struct LoggerConfig {
       Level level;
       bool to_console;
       bool to_file;
       std::string filename;
   };
   ```
2. **Инициализация**:
   ```cpp
   kv::log::LoggerConfig cfg;
   cfg.level = kv::log::Level::DEBUG;
   cfg.to_console = true;
   cfg.to_file = true;
   cfg.filename = "kv_server.log";
   kv::log::Logger::instance().init(cfg);
   ```
3. **Использование**:
   - `LOG_INFO("Some message");`
   - `LOG_DEBUG("Debug details: x=", x);`
   - При уровне `FATAL` приложение немедленно завершится (`std::exit(1)`). 

---

## Лицензия

Этот проект распространяется под лицензией MIT.  
