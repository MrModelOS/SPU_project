# SPPU — Search & Prediction Processing Unit v0.3

**Аппаратный ускоритель + программный стек для векторного поиска с предиктивной генерацией**

> Обычный поиск ищет слова. Мы ищем смысл.

SPPU — концептуальный аппаратный ускоритель для similarity search на float-векторах. Проект включает полный стек: от RTL-модели чипа до HTTP API сервера, готового к интеграции.

**v0.3:** Добавлен SEU (Speculative Execution Unit) — предиктивное дерево решений, генерирующее вероятности ветвления параллельно с dot-product движком.

---

## Архитектура

```
┌──────────────────────────────────────────────────────┐
│              User Application / CLI                    │
├──────────────┬───────────────────┬───────────────────┤
│ sppu_search   │  sppu_searchd      │  Python client    │
│  (CLI)        │  (HTTP API)        │  (examples/)      │
├──────────────┴───────────────────┴───────────────────┤
│              libsppu.so  (SDK)                         │
│    sppu_open / sppu_search / sppu_predict_tree        │
├──────────────────────────────────────────────────────┤
│         /dev/sppu  (kernel driver)                     │
│         sppu_driver.ko — emulation / PCI mode        │
├──────────────────────────────────────────────────────┤
│         QEMU SPPU Virtual PCI Device                   │
│         / QEMU sppu-pci + kernel driver                │
├──────────────────────────────────────────────────────┤
│         FPGA Fabric (Zynq-7010 / Artix-7)             │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────┐ │
│  │ sppu_dotprod  │  │ seu_tree     │  │ sppu_dma     │ │
│  │ (dot-product) │  │ (speculative │  │ (DMA engine)│ │
│  │               │  │  execution)  │  │             │ │
│  └──────┬───────┘  └──────┬───────┘  └──────┬──────┘ │
│         └────────┬─────────┴─────────────────┘        │
│           sppu_vecmem (4-port shared BRAM)            │
│              sppu_regs (MMIO register map)             │
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
├ emulator/                         # Эмулятор аппаратуры (QEMU PCI device)
│  ├── sppu_device.h                # Карта регистров (MMIO + ioctl ABI)
│  ├── sppu_pci.c                   # Ядро вычислений (dot-product + SEU)
│  ├── main.c                       # Тестбанк
│  └── Makefile
│
├ kernel_module/                     # Linux kernel driver
│  ├── sppu_driver.c                # PCI + software emulation
│  ├── test_userspace.c             # Raw ioctl тест
│  ├── sppu_driver.ko               # Собранный модуль
│  └── Makefile
│
├ sdk/                               # Userspace SDK
│  ├── include/sppu.h               # Публичный C API
│  ├── include/sppu_backend.h       # Backend API (промежуточный слой)
│  ├── src/libsppu.c                # ioctl-обёртка
│  ├── src/sppu_backend.c           # Backend реализация
│  ├── libsppu.so                   # Разделяемая библиотека
│  ├── libsppu_backend.so           # Backend shared lib
│  ├── examples/sppu_demo.c         # Валидация SDK
│  ├── python/                      # Python bindings (ctypes)
│  │  ├── pyproject.toml
│  │  ├── sppu/
│  │  │  ├── __init__.py
│  │  │  └── bindings.py
│  │  └── examples/sppu_demo.py
│  └── Makefile
│
├ tools/                             # CLI-утилиты
│  ├── sppu_search.c                # Поиск по CSV/бинарной базе
│  ├── Makefile
│  └── test_vectors/small_db.csv    # Тестовые векторы
│
├ benchmark/                         # Производительность
│  ├── bench_simd.h                 # Scalar / SSE4.2 / AVX2 / AVX-512
│  ├── bench_search.c               # Замер throughput
│  ├── bench_run.sh                 # Скрипт запуска
│  └── Makefile
│
├ examples/
│  ├── semantic_search/
│  │  ├── sppu_searchd.c            # HTTP REST API сервер
│  │  └── Makefile
│  └── sppu_client.py               # Python-клиент
│
├ fpga/                              # FPGA реализация (Zynq-7010 / Artix-7)
│  ├── rtl/
│  │  ├── sppu_top.v                # Top-level модуль (UART ↔ AXI-Lite bridge)
│  │  ├── sppu_pynq_top.v           # PYNQ/Zynq wrapper (AXI-Lite bridge, LED status)
│  │  ├── sppu_regs.v               # MMIO регистры (0x00–0x58)
│  │  ├── sppu_dotprod.v            # Dot-product engine (1-cycle/iter pipeline)
│  │  ├── sppu_dma.v                # DMA controller (FIFO ↔ vector memory)
│  │  ├── sppu_vecmem.v             # 4-port shared BRAM (256K × 32-bit)
│  │  └── seu_tree.v                # SEU tree generator (FSM + LFSR probability)
│  ├── sim/
│  │  └── tb_sppu.v                 # Testbench (T1–T20)
│  ├── constraints/
│  │  ├── sppu_artix7.xdc           # Pin constraints (generic Artix-7)
│  │  └── sppu_zynq7010.xdc         # Pin constraints (Zynq-7010, AntMiner XC7Z010)
│  ├── scripts/
│  │  ├── vivado_synth.tcl          # Vivado synthesis flow
│  │  └── vivado_program.tcl        # JTAG programming
│  ├── Makefile                      # sim / synth / program / lint targets
│  └── tb_sppu.vcd                   # Сигнатурные файлы симуляции
│
└ qemu/                              # QEMU virtual PCI device
   ├── README.md
   ├── sppu_qemu.c                   # QEMU device model (vendor=0x1234, dev=0x5780)
   ├── qemu-sppu.sh                  # Скрипт запуска QEMU с SPPU
   └── Makefile
```

---

## Компоненты

### 1. FPGA RTL (`fpga/rtl/`)

Реализация на Xilinx Zynq-7010 (`xc7z010clg400-1`) с возможностью портирования на generic Artix-7 (`xc7a35tcpg236-1`).

| Модуль | Описание |
|--------|----------|
| `sppu_top.v` | Top-level: UART ↔ AXI-Lite bridge, IRQ aggregation |
| `sppu_regs.v` | Register map: SPPU + SEU (0x00–0x58), MMIO via AXI-Lite |
| `sppu_dotprod.v` | Pipeline dot-product engine, 1 iteration per cycle |
| `sppu_dma.v` | DMA controller: FIFO ↔ vector memory |
| `sppu_vecmem.v` | 4-port shared BRAM (256K × 32-bit), DMA has write priority over SEU |
| `sppu_pynq_top.v` | PYNQ/Zynq wrapper: instantiates PS-PL AXI interconnect, exposes LEDs |
| `seu_tree.v` | SEU tree generator: FSM + LFSR for probability scoring |

**SPPU Registers (BAR0, offset 0x00–0x2C):**

| Offset | Name | R/W | Description |
|--------|------|-----|-------------|
| `0x00` | CTRL | RW | Control: start (bit 0), reset (bit 1), irq_en (bit 2) |
| `0x04` | STATUS | RO | Status: 0=READY, 1=BUSY, 2=DONE, 3=ERROR |
| `0x08` | VEC_COUNT | RW | Number of vectors in memory (max 1024) |
| `0x0C` | DIMENSION | RW | Vector dimension (max 768) |
| `0x10` | QUERY_ADDR | RW | Base address of query vector in BRAM |
| `0x14` | RESULT_IDX | RO | Index of best matching vector |
| `0x18` | RESULT_SCORE | RO | Dot-product score of best match |
| `0x1C` | DEVICE_ID | RO | Hardware version / device ID |
| `0x20` | INT_MASK | RW | Interrupt mask |
| `0x24` | INT_STATUS | RW1C | Interrupt status, write 1 to clear |
| `0x28` | DMA_SRC_ADDR | RW | DMA source address |
| `0x2C` | DMA_DST_ADDR | RW | DMA destination address |

**SEU Registers (BAR0, offset 0x30–0x58):**

| Offset | Name | R/W | Description |
|--------|------|-----|-------------|
| `0x30` | SEU_CTRL | RW | SEU control: start (bit 0), reset (bit 1), irq_en (bit 2), tree_16b (bit 3) |
| `0x34` | SEU_STATUS | RO | 0=READY, 1=BUSY, 2=DONE, 3=ERROR |
| `0x38` | SEU_DEPTH | RW | Tree depth (5..8) |
| `0x3C` | SEU_OFFSET | RW | Branch offsets (packed, 32-bit per offset) |
| `0x40` | SEU_TREE_ADDR | RW | Base address in vector memory for tree nodes |
| `0x44` | SEU_TREE_RESULT | RW | Base address for tree result storage |
| `0x48` | SEU_PROB_BASE | RW | Probability configuration base address |
| `0x4C` | SEU_IRQ_STATUS | RW1C | SEU interrupt status |
| `0x50` | SEU_PROB_READ_IDX | RW | Index for probability readback (0..127) |
| `0x54` | SEU_PROB_READBACK | RO | Probability value at index |
| `0x58` | SEU_TREE_ENTRIES_TOTAL | RO | Total entries computed by SEU |

**Simulation (20 test cases, Icarus Verilog):**
```bash
cd fpga && make sim
```

**Synthesis (Vivado, Zynq-7010):**
```bash
cd fpga && make synth
# → build/sppu_zynq7010.bit
# → build/sppu_zynq7010.xsa (for Vitis/PetaLinux)
```

**Pin mapping (`sppu_zynq7010.xdc`):**
- `sys_clk` — pin E3, 50 MHz oscillator
- `sys_rst_n` — pin C12, active-low
- LEDs: H17, K15, J13, N14
- UART (PL debug): TX=D10, RX=A9
- SPI slave (vector data): SCK=B1, CSN=A1, MOSI=C1, MISO=C2
- GPIO expansion: H1, G1

For generic Artix-7, see `sppu_artix7.xdc` (adjust pins to your board schematic).

---

### 2. QEMU Virtual Device (`emulator/`)

PCI-устройство SPPU для QEMU. Позволяет тестировать полный стек (driver → SDK → CLI) без реального FPGA.

**Возможности:**
- MMIO регистры (SPPU: 0x00–0x2C, SEU: 0x30–0x4C)
- Dot-product движок с ускорением
- SEU предиктивное дерево
- DMA engine для загрузки векторов
- Interrupt-driven результаты (IRQ line 1)
- VMState version 3 (совместимо с сохранением состояния QEMU)

**Сборка:**
```bash
cd emulator && make && ./sppu_emulator
```

**Запуск как QEMU PCI-устройство (с кастомным QEMU):**
```bash
qemu-system-x86_64 -device sppu-pci ...
```

Подробности: [`qemu/README.md`](qemu/README.md)

---

### 3. Kernel Module (`kernel_module/`)

Два режима работы:
- `emulation=1` (по умолчанию) — программная эмуляция в ядре через `/dev/sppu`
- `emulation=0` — реальный PCI-драйвер с MMIO-регистрами FPGA

```bash
# Загрузить модуль (эмуляция)
sudo insmod kernel_module/sppu_driver.ko emulation=1
ls -la /dev/sppu

# Загрузить модуль (реальный FPGA, MMIO)
sudo insmod kernel_module/sppu_driver.ko emulation=0

# Выгрузить модуль
sudo rmmod sppu_driver
```

**Поддерживаемые ioctl/SEU-команды:**
- `SEU_CONFIG` — настройка параметров дерева (depth, offset)
- `SEU_START` — запуск предиктивной генерации дерева
- `SEU_GET_TREE` — чтение результата дерева

---

### 4. SDK — `libsppu.so` (`sdk/`)

Тонкая C-обёртка над ioctl `/dev/sppu`.

**C API:**
```c
/* similarity search */
sppu_t *sppu = sppu_open(NULL);
sppu_reset(sppu);
sppu_configure(sppu, 1000, 128);       /* vec_count, dimension */
for (int i = 0; i < 1000; i++)
    sppu_load_vector(sppu, i, data[i], 128);
sppu_set_target(sppu, query, 128);
sppu_start(sppu);
sppu_wait_result(sppu, &idx, &score, &status, 5000);
sppu_close(sppu);

/* SEU: predictive tree generation */
float entries[128];
sppu_predict_tree(sppu, 6, 0xF0F0, entries, 5000);
```

**Ограничения:**
- до 1000 векторов в памяти
- до 768 измерений на вектор (аппаратные лимиты FPGA)

**Сборка SDK:**
```bash
cd sdk && make
```

---

### 5. CLI Поиск (`tools/sppu_search`)

```bash
# Сгенерировать тестовую базу
./tools/sppu_search --generate 5000 128 --db vectors.csv

# Поиск по CSV
./tools/sppu_search --db vectors.csv --query "0.1;0.2;0.3;0.4" --top 5

# Поиск по бинарной базе
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
./benchmark/bench_search --full          # все наборы параметров
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

Thin ctypes wrapper around `libsppu.so`:

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

## Прошивка FPGA (Zynq-7010)

### Требования
- Xilinx Vivado (WebPACK бесплатен для Artix-7/Zynq-7000)
- JTAG-адаптер (Digilent JTAG-HS3 или аналог)
- Плата AntMiner XC7Z010 v1.0 (или другая с Zynq-7010)

### Синтез bitstream
```bash
cd fpga
make synth
```

### Программирование через Vivado (JTAG)
```bash
vivado -mode batch -source scripts/vivado_program.tcl \
  -tclargs "build/sppu_zynq7010.bit"
```

Или в интерактивном режиме: `Open Hardware Manager → Connect → Program Device`.

### Программирование через openFPGALoader
```bash
sudo apt install openfpgaloader
make program-ocd   # из каталога fpga/
```

### Загрузка при старте (boot.bin)
Для автоматической загрузки FPGA при включении платы:
1. В Vitis импортируйте `.xsa` из `build/`
2. Создайте FSBL, настройте boot image (`boot.bif`)
3. Соберите `boot.bin` через `bootgen`
4. Поместите `boot.bin` на SD-карту (позиция 0)

### Загрузка Linux-драйвера (после прошивки)
```bash
sudo insmod kernel_module/sppu_driver.ko emulation=0
ls -la /dev/sppu
```

---

## Требования

- Linux (для kernel module и `/dev/sppu`)
- GCC с поддержкой C11
- Icarus Verilog (для симуляции FPGA)
- Vivado (для синтеза на Zynq-7010)
- Python 3.8+ (для Python SDK, необязательно)
- Никаких внешних библиотек (кроме libc)

---

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
- [x] SEU testbench: 20 тест-кейсов (T1–T20)
- [x] Zynq-7010 FPGA deployment (XDC constraints, Vivado scripts)
- [x] Generic Artix-7 FPGA deployment (XDC constraints, Vivado scripts)
- [x] PYNQ / Zynq PS integration (AXI-Lite bridge, LED status, sppu_pynq_top.v)
- [x] Python SDK bindings (ctypes/libsppu wrapper, sppu.Python package)
- [x] SEU: runtime probability profiling (PROB_READ_IDX / PROB_READBACK / TREE_ENTRIES_TOTAL)

---

## Лицензия

[Apache License 2.0](LICENSE)
