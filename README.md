# SPPU — Search Processing Unit v0.3

**Аппаратный ускоритель + программный стек для векторного поиска с предиктивной генерацией**

> Обычный поиск ищет слова. Мы ищем смысл.

SPPU — концептуальный аппаратный ускоритель для similarity search на float-векторах.
Проект включает полный стек: от RTL-модели чипа до HTTP API сервера готового к интеграции.

**v0.3:** Добавлен SEU (Speculative Execution Unit) — предиктивное дерево решений,
генерирующее вероятности ветвления параллельно с dot-product движком.

---

## Архитектура

```
┌──────────────────────────────────────────────────────┐
│              User Application / CLI                  │
├──────────────┬───────────────────┬───────────────────┤
│ sppu_search   │  sppu_searchd      │  Python client    │
│  (CLI)       │  (HTTP API)       │  (examples/)      │
├──────────────┴───────────────────┴───────────────────┤
│              libsppu.so  (SDK)                        │
│    sppu_open / sppu_search / sppu_predict_tree          │
├──────────────────────────────────────────────────────┤
│         /dev/sppu  (kernel driver)                    │
│         sppu_driver.ko — emulation / PCI mode         │
├──────────────────────────────────────────────────────┤
│         QEMU SPPU Virtual PCI Device                  │
│         / QEMU sppu-pci + kernel driver               │
├──────────────────────────────────────────────────────┤
│         FPGA Fabric (Artix-7)                        │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────┐ │
│  │ sppu_dotprod  │  │ seu_tree     │  │ sppu_dma     │ │
│  │ (dot-product)│  │ (speculative │  │ (DMA engine)│ │
│  │              │  │  execution)  │  │             │ │
│  └──────┬───────┘  └──────┬───────┘  └──────┬──────┘ │
│         └────────┬─────────┴─────────────────┘       │
│           sppu_vecmem (4-port shared BRAM)            │
│              sppu_regs (MMIO register map)            │
└──────────────────────────────────────────────────────┘
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
SPPU_project/
├ Makefile                          # Единая сборка
├ README.md
├ LICENSE                           # Apache 2.0
│
├ emulator/                         # Эмулятор аппаратуры
│  ├── sppu_device.h                 # Карта регистров (MMIO + ioctl ABI)
│  ├── sppu_pci.c                    # Ядро вычислений (dot-product + SEU)
│  ├── main.c                       # Тестбанк
│  └── Makefile
│
├ kernel_module/                    # Linux kernel driver
│  ├── sppu_driver.c                 # PCI + software emulation
│  ├── test_userspace.c             # Raw ioctl тест
│  ├── sppu_driver.ko                # Собранный модуль
│  └── Makefile
│
├ sdk/                              # Userspace SDK
│  ├── include/sppu.h                # Публичный C API
│  ├── src/libsppu.c                 # ioctl-обёртка
│  ├── libsppu.so                    # Разделяемая библиотека
│  ├── examples/sppu_demo.c         # Валидация SDK
│  ├── python/                      # Python bindings (ctypes)
│  │  ├── pyproject.toml
│  │  ├── sppu/__init__.py
│  │  ├── sppu/bindings.py
│  │  └── examples/sppu_demo.py
│  └── Makefile
│
├ tools/                            # CLI-утилиты
│  ├── sppu_search.c                 # Поиск по CSV/бинарной базе
│  └── test_vectors/small_db.csv   # Тестовые векторы
│
├ benchmark/                        # Производительность
│  ├── bench_simd.h                 # Scalar / SSE4.2 / AVX2 / AVX-512
│  ├── bench_search.c              # Замер throughput
│  ├── bench_run.sh                # Скрипт запуска
│  └── Makefile
│
├ examples/
│  ├── semantic_search/
│  │  ├── sppu_searchd.c           # HTTP REST API сервер
│  │  └── Makefile
│  └── sppu_client.py              # Python-клиент
│
├ fpga/                             # FPGA реализация (Artix-7)
│  ├── rtl/
│  │  ├── sppu_top.v               # Top-level модуль
│  │  ├── sppu_pynq_top.v          # PYNQ/Zynq wrapper
│  │  ├── sppu_regs.v              # MMIO регистры (0x00–0x58)
│  │  ├── sppu_dotprod.v           # Dot-product engine
│  │  ├── sppu_dma.v               # DMA controller
│  │  ├── sppu_vecmem.v            # 4-портовая shared BRAM
│  │  └── seu_tree.v              # SEU предиктивное дерево
│  ├── sim/
│  │  └── tb_sppu.v                # Testbench (T1–T20)
│  ├── constraints/
│  │  └── sppu_artix7.xdc          # Pin constraints (Artix-7)
│  ├── scripts/
│  │  ├── vivado_synth.tcl        # Vivado synthesis flow
│  │  └── vivado_program.tcl      # JTAG programming
│  └── Makefile                    # sim / synth / lint targets
│
└ .gitignore
```

---

## Компоненты

### 1. FPGA RTL (`fpga/rtl/`)

Реализация на Xilinx Artix-7 (xc7a35tcpg236-1):

| Модуль | Описание |
|--------|----------|
| `sppu_top.v` | Top-level: UART ↔ AXI-Lite bridge, IRQ aggregation |
| `sppu_regs.v` | Register map: SPPU + SEU (0x00–0x4C) |
| `sppu_dotprod.v` | Pipeline dot-product engine (1-cycle/iter) |
| `sppu_dma.v` | DMA controller: FIFO ↔ vector memory |
| `sppu_vecmem.v` | 4-port shared BRAM (256K × 32-bit), DMA>SEU priority |
| `seu_tree.v` | SEU tree generator: FSM + LFSR probability scoring |

**SEU Registers (BAR0):**

| Offset | Name | Description |
|--------|------|-------------|
| `0x30` | SEU_CTRL | SEU control (start/reset/irq_en/tree_16b) |
| `0x34` | SEU_STATUS | SEU status (0=ready, 1=busy, 2=done, 3=error) |
| `0x38` | SEU_DEPTH | Tree depth (5..8) |
| `0x3C` | SEU_OFFSET | Branch offsets (packed) |
| `0x40` | SEU_TREE_ADDR | Base address in vector memory for tree |
| `0x44` | SEU_TREE_RESULT | Tree result base address |
| `0x48` | SEU_PROB_BASE | Probability configuration base |
| `0x4C` | SEU_IRQ_STATUS | SEU interrupt status (RW1C) |
| `0x50` | SEU_PROB_READ_IDX | Index for probability readback (0..127) |
| `0x54` | SEU_PROB_READBACK | Probability value at index (RO) |
| `0x58` | SEU_TREE_ENTRIES_TOTAL | Total entries computed (RO) |

**Simulation (20 test cases):**
```bash
cd fpga && make sim     # iverilog → all tests pass (T1–T20)
```

**Synthesis (Vivado):**
```bash
cd fpga && make synth   # → sppu_artix7.bit
```

**Pin mapping:** `fpga/constraints/sppu_artix7.xdc`
Adjust pin names/numbers for your board's schematic.

---

### 2. QEMU Virtual Device (`emulator/`)

PCI-устройство SPPU для QEMU. Поддерживает:

- MMIO регистры (SPPU: 0x00–0x2C, SEU: 0x30–0x4C)
- Dot-product движок (硬件加速)
- SEU предиктивное дерево
- DMA engine для загрузки векторов
- Interrupt-driven результаты (IRQ line)

VMState version: 3 (совместимо с сохранением состояния)

Запуск:
```bash
# Эмулятор standalone
cd emulator && make && ./sppu_emulator

# Как QEMU PCI-устройство (с кастомным QEMU)
qemu-system-x86_64 -device sppu-pci ...
```

---

### 3. Kernel Module (`kernel_module/`)

Два режима работы:
- **emulation=1** (по умолчанию) — программная эмуляция в ядре
- **emulation=0** — реальный PCI-драйвер с MMIO

```bash
sudo insmod kernel_module/sppu_driver.ko emulation=1
ls -la /dev/sppu
sudo rmmod sppu_driver
```

Поддерживает SEU: `ioctl(SEU_CONFIG)`, `ioctl(SEU_START)`, `ioctl(SEU_GET_TREE)`.

---

### 4. SDK — `libsppu.so` (`sdk/`)

Тонкая C-обёртка над ioctl `/dev/sppu`:

```c
/* SPPU: similarity search */
sppu_t *sppu = sppu_open(NULL);
sppu_reset(sppu);
sppu_configure(sppu, 1000, 128);
sppu_load_vector(sppu, i, data, 128);
sppu_set_target(sppu, query, 128);
sppu_start(sppu);
sppu_wait_result(sppu, &idx, &score, &status, 5000);

/* SEU: предиктивная генерация дерева */
float entries[128];
sppu_predict_tree(sppu, depth=6, offset=0xF0F0, entries, 5000);

sppu_close(sppu);
```

Ограничения: до 1000 векторов, до 768 измерений (аппаратные лимиты чипа).

---

### 5. CLI Поиск (`tools/sppu_search`)

```bash
./tools/sppu_search --generate 5000 128 --db vectors.csv
./tools/sppu_search --db vectors.csv --query "0.1;0.2;0.3;0.4" --top 5
./tools/sppu_search --db vectors.bin --query-file query.bin --top 10
```

---

### 6. SIMD Бенчмарк (`benchmark/`)

| Метод | Ширина | Интрументы |
|-------|--------|------------|
| scalar | 1 float | Базовый C цикл |
| SSE4.2 | 4 float | `_mm_dp_ps` |
| AVX2 | 8 float | `_mm256_dp_ps` + FMA |
| AVX-512 | 16 float | `_mm512_dp_ps` + FMA |

```bash
./benchmark/bench_search --vectors 10000 --dim 128
./benchmark/bench_search --full
```

---

### 7. HTTP Semantic Search API (`examples/semantic_search/`)

```bash
./examples/semantic_search/sppu_searchd --port 8080 --db vectors.bin
```

| Метод | Путь | Описание |
|-------|------|----------|
| GET | `/health` | `{"status":"ok"}` |
| GET | `/status` | `{"count":N,"dim":D,"status":"ready"}` |
| POST | `/load` | `{"file":"/path/to/db.bin"}` |
| POST | `/search` | `{"vector":[...],"top_k":5}` |

---

### 8. Python SDK (`sdk/python/`)

Thin ctypes wrapper around libsppu.so:

```bash
cd sdk/python && pip install -e .
```

```python
from sppu import SPPU

with SPPU() as sppu:
    # Vector search
    sppu.configure(vec_count=1000, dimension=128)
    for i, vec in enumerate(vectors):
        sppu.load_vector(i, vec)
    sppu.set_target(query)
    idx, score = sppu.search(timeout_ms=5000)
    print(f"Best match: vector #{idx}, score={score:.4f}")

    # SEU predictive tree
    entries = sppu.predict_tree(depth=6, offset=0xF0F0, timeout_ms=5000)
    print(f"Generated {len(entries)} probability entries")

# Run demo
python3 sdk/python/examples/sppu_demo.py
```

---

## Требования

- Linux (для kernel module и `/dev/sppu`)
- GCC с поддержкой C11
- Icarus Verilog (для симуляции FPGA)
- Vivado (для синтеза на Artix-7)
- Python 3.8+ (для Python SDK, необязательно)
- Никаких внешних библиотек (кроме libc)

## Roadmap

- [x] Эмулятор + MMIO регистры
- [x] Kernel driver (PCI + emulation mode)
- [x] SDK (libsppu.so)
- [x] CLI поиск с CSV/binary
- [x] SIMD-бенчмарк (scalar → AVX-512)
- [x] HTTP REST API сервер
- [x] Python-клиент
- [x] QEMU virtual PCI device (`-device sppu-pci`)
- [x] DMA engine для аппаратной загрузки векторов
- [x] Interrupt-driven результаты (IRQ handler в драйвере)
- [x] FPGA RTL (Verilog: sppu_regs, sppu_dotprod, sppu_dma, sppu_vecmem, sppu_top)
- [x] SEU — Speculative Execution Unit (предиктивное дерево решений)
- [x] SEU: 4-портовая shared BRAM (DMA>SEU приоритет записи)
- [x] SEU: combined IRQ (SPPU_DONE | SEU_DONE, W1C clear)
- [x] SEU testbench: 16 тест-кейсов (T1–T16)
- [x] Artix-7 FPGA deployment (XDC constraints, Vivado scripts)
- [x] PYNQ / Zynq PS integration (AXI-Lite bridge, LED status, sppu_pynq_top.v)
- [x] Python SDK bindings (ctypes/libsppu wrapper, sppu.Python package)
- [x] SEU: runtime probability profiling (PROB_READ_IDX / PROB_READBACK / TREE_ENTRIES_TOTAL)

## Лицензия

[Apache License 2.0](LICENSE)
