# SPU — Search Processing Unit

**Аппаратный ускоритель + программный стек для векторного поиска**

> Обычный поиск ищет слова. Мы ищем смысл.

SPU — концептуальный аппаратный ускоритель для similarity search на float-векторах.
Проект включает полный стек: от модели чипа с MMIO-регистрами до HTTP API сервера готового к интеграции.

---

## Архитектура

```
┌─────────────────────────────────────────────────┐
│              User Application / CLI             │
├─────────────┬───────────────────┬───────────────┤
│ spu_search  │  spu_searchd     │  Python client│
│  (CLI)      │  (HTTP API)      │  (examples/)  │
├─────────────┴───────────────────┴───────────────┤
│              libspu.so  (SDK)                   │
│         spu_open / spu_load_vector / spu_start  │
├─────────────────────────────────────────────────┤
│         /dev/spu  (kernel driver)               │
│         spu_driver.ko — emulation mode          │
├─────────────────────────────────────────────────┤
│         SPU Hardware Model (emulator)           │
│         MMIO registers, dot-product engine      │
└─────────────────────────────────────────────────┘
```

---

## Быстрый старт

```bash
# Собрать всё одной командой
make

# Запустить тест SDK
make test

# Запустить SIMD-бенчмарк
make run-bench
```

---

## Структура проекта

```
SPU_project/
├── Makefile                         # Единая сборка
├── README.md
├── LICENSE                          # Apache 2.0
│
├── emulator/                        # Эмулятор аппаратуры
│   ├── spu_device.h                 # Карта регистров (MMIO + ioctl ABI)
│   ├── spu_pci.c                    # Ядро вычислений (dot-product engine)
│   ├── main.c                       # Тестбанк
│   └── Makefile
│
├── kernel_module/                   # Linux kernel driver
│   ├── spu_driver.c                 # PCI + software emulation (500 строк)
│   ├── test_userspace.c             # Raw ioctl тест
│   ├── spu_driver.ko                # Собранный модуль
│   └── Makefile
│
├── sdk/                             # Userspace SDK
│   ├── include/spu.h                # Публичный C API
│   ├── src/libspu.c                 # ioctl-обёртка
│   ├── libspu.so                    # Разделяемая библиотека
│   ├── examples/spu_demo.c         # Валидация SDK
│   └── Makefile
│
├── tools/                           # CLI-утилиты
│   ├── spu_search.c                 # Поиск по CSV/бинарной базе
│   └── test_vectors/small_db.csv    # Тестовые векторы
│
├── benchmark/                       # Производительность
│   ├── bench_simd.h                 # Scalar / SSE4.2 / AVX2 / AVX-512
│   ├── bench_search.c              # Замер throughput
│   ├── bench_run.sh                # Скрипт запуска
│   └── Makefile
│
├── examples/
│   ├── semantic_search/
│   │   ├── spu_searchd.c           # HTTP REST API сервер
│   │   └── Makefile
│   └── spu_client.py              # Python-клиент
│
└── .gitignore
```

---

## Компоненты

### 1. Эмулятор (`emulator/`)

Стандalone-тест, симулирующий SPU-чип в userspace.
Реализует MMIO-регистры, DMA-эмуляцию и dot-product движок.

```bash
cd emulator && make && ./spu_emulator
```

Регистры SPU (BAR0, 4 KiB):

| Offset | Имя | Описание |
|--------|-----|----------|
| `0x00` | CTRL | Управление (start/reset/irq_en) |
| `0x04` | STATUS | READY / BUSY / DONE |
| `0x08` | VEC_COUNT | Количество векторов |
| `0x0C` | DIMENSION | Размерность |
| `0x18` | RESULT_IDX | Индекс лучшего совпадения |
| `0x1C` | RESULT_SCORE | Dot-product score |

### 2. Kernel Module (`kernel_module/`)

Два режима работы:
- **emulation=1** (по умолчанию) — программная эмуляция в ядре через `kzalloc`/`vzalloc`
- **emulation=0** — реальный PCI-драйвер с MMIO через `pci_iomap`

```bash
# Загрузить модуль с эмуляцией
sudo insmod kernel_module/spu_driver.ko emulation=1

# Проверить
ls -la /dev/spu

# Выгрузить
sudo rmmod spu_driver
```

### 3. SDK — `libspu.so` (`sdk/`)

Тонкая C-обёртка над ioctl `/dev/spu`:

```c
spu_t *spu = spu_open(NULL);              // открыть /dev/spu
spu_reset(spu);                           // сброс
spu_configure(spu, 1000, 128);            // 1000 векторов, dim=128
spu_load_vector(spu, i, data, 128);       // загрузить вектор
spu_set_target(spu, query, 128);          // задать запрос
spu_start(spu);                           // запуск поиска
spu_wait_result(spu, &idx, &score, &st, 5000);  // ждать результат
spu_close(spu);
```

Ограничения: до 1000 векторов, до 768 измерений (аппаратные лимиты чипа).

### 4. CLI Поиск (`tools/spu_search`)

```bash
# Сгенерировать тестовую базу
./tools/spu_search --generate 5000 128 --db vectors.csv

# Поиск по CSV-базе
./tools/spu_search --db vectors.csv --query "0.1;0.2;0.3;0.4" --top 5

# Поиск из бинарного файла
./tools/spu_search --db vectors.bin --query-file query.bin --top 10
```

Форматы данных:
- **CSV:** каждая строка — вектор, значения через `;`
- **Binary:** `uint32 count` + `uint32 dim` + `count × dim` float32

Автоматическая batch-обработка по 1000 векторов с агрегацией top-K.

### 5. SIMD Бенчмарк (`benchmark/`)

Четыре реализации dot-product с автодетекцией CPU:

| Метод | Ширина | Интрументы |
|-------|--------|------------|
| scalar | 1 float | Базовый C цикл |
| SSE4.2 | 4 float | `_mm_dp_ps` |
| AVX2 | 8 float | `_mm256_dp_ps` + FMA |
| AVX-512 | 16 float | `_mm512_dp_ps` + FMA |

```bash
# Быстрый тест
./benchmark/bench_search --vectors 10000 --dim 128

# Полный прогон
./benchmark/bench_search --full
```

Результаты (Intel i5-1135G7):

| Конфигурация | Scalar | SSE4.2 | AVX2 | AVX-512 | Ускорение |
|---|---|---|---|---|---|
| 10K × dim=128 | 2.56ms | 1.08ms | 0.59ms | 0.59ms | **4.3x** |
| 100K × dim=128 | 26.2ms | 9.18ms | 8.01ms | 6.49ms | **4.0x** |
| 10K × dim=768 | 22.1ms | 5.58ms | 3.87ms | 3.37ms | **6.6x** |

### 6. HTTP Semantic Search API (`examples/semantic_search/`)

Минимальный HTTP-сервер на чистых POSIX sockets + fork().
Без внешних зависимостей — только C11 + libc.

```bash
# Запуск с предзагруженной базой
./examples/semantic_search/spu_searchd --port 8080 --db vectors.bin
```

Эндпоинты:

| Метод | Путь | Описание |
|-------|------|----------|
| GET | `/health` | `{"status":"ok"}` |
| GET | `/status` | `{"count":N,"dim":D,"status":"ready"}` |
| POST | `/load` | `{"file":"/path/to/db.bin"}` — загрузить базу |
| POST | `/search` | `{"vector":[...],"top_k":5}` — поиск |

Пример:
```bash
curl -X POST http://localhost:8080/search \
  -H "Content-Type: application/json" \
  -d '{"vector":[0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8],"top_k":3}'
```

Ответ:
```json
{
  "results": [
    {"index": 0, "score": 2.580000},
    {"index": 4, "score": 2.340000},
    {"index": 8, "score": 1.920000}
  ],
  "count": 3
}
```

### 7. Python-клиент (`examples/spu_client.py`)

```bash
pip install requests
python3 examples/spu_client.py
```

---

## Требования

- Linux (для kernel module и `/dev/spu`)
- GCC с поддержкой C11
- Kernel headers (только для сборки `.ko`)
- Никаких внешних библиотек

## Roadmap

- [x] Эмулятор + MMIO регистры
- [x] Kernel driver (PCI + emulation mode)
- [x] SDK (libspu.so)
- [x] CLI поиск с CSV/binary
- [x] SIMD-бенчмарк (scalar → AVX-512)
- [x] HTTP REST API сервер
- [x] Python-клиент
- [x] QEMU virtual PCI device (`-device spu-pci`)
- [x] DMA engine для аппаратной загрузки векторов
- [x] Interrupt-driven результаты (IRQ handler в драйвере)
- [x] FPGA RTL (Verilog: spu_regs, spu_dotprod, spu_dma, spu_vecmem, spu_top)

## Лицензия

[Apache License 2.0](LICENSE)
