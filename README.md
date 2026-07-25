# SPU — Search Processing Unit v0.3

**Аппаратный ускоритель + программный стек для векторного поиска с предиктивной генерацией**

> Обычный поиск ищет слова. Мы ищем смысл.

SPU — концептуальный аппаратный ускоритель для similarity search на float-векторах.
Проект включает полный стек: от RTL-модели чипа до HTTP API сервера готового к интеграции.

**v0.3:** Добавлен SEU (Speculative Execution Unit) — предиктивное дерево решений,
генерирующее вероятности ветвления параллельно с dot-product движком.

---

## Архитектура

```
┌──────────────────────────────────────────────────────┐
│              User Application / CLI                  │
├──────────────┬───────────────────┬───────────────────┤
│ spu_search   │  spu_searchd      │  Python client    │
│  (CLI)       │  (HTTP API)       │  (examples/)      │
├──────────────┴───────────────────┴───────────────────┤
│              libspu.so  (SDK)                        │
│    spu_open / spu_search / spu_predict_tree          │
├──────────────────────────────────────────────────────┤
│         /dev/spu  (kernel driver)                    │
│         spu_driver.ko — emulation / PCI mode         │
├──────────────────────────────────────────────────────┤
│         QEMU SPU Virtual PCI Device                  │
│         / QEMU spu-pci + kernel driver               │
├──────────────────────────────────────────────────────┤
│         FPGA Fabric (Artix-7)                        │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────┐ │
│  │ spu_dotprod  │  │ seu_tree     │  │ spu_dma     │ │
│  │ (dot-product)│  │ (speculative │  │ (DMA engine)│ │
│  │              │  │  execution)  │  │             │ │
│  └──────┬───────┘  └──────┬───────┘  └──────┬──────┘ │
│         └────────┬─────────┴─────────────────┘       │
│           spu_vecmem (4-port shared BRAM)            │
│              spu_regs (MMIO register map)            │
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
SPU_project/
├ Makefile                          # Единая сборка
├ README.md
├ LICENSE                           # Apache 2.0
│
├ emulator/                         # Эмулятор аппаратуры
│  ├── spu_device.h                 # Карта регистров (MMIO + ioctl ABI)
│  ├── spu_pci.c                    # Ядро вычислений (dot-product + SEU)
│  ├── main.c                       # Тестбанк
│  └── Makefile
│
├ kernel_module/                    # Linux kernel driver
│  ├── spu_driver.c                 # PCI + software emulation
│  ├── test_userspace.c             # Raw ioctl тест
│  ├── spu_driver.ko                # Собранный модуль
│  └── Makefile
│
├ sdk/                              # Userspace SDK
│  ├── include/spu.h                # Публичный C API
│  ├── src/libspu.c                 # ioctl-обёртка
│  ├── libspu.so                    # Разделяемая библиотека
│  ├── examples/spu_demo.c         # Валидация SDK
│  └── Makefile
│
├ tools/                            # CLI-утилиты
│  ├── spu_search.c                 # Поиск по CSV/бинарной базе
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
│  │  ├── spu_searchd.c           # HTTP REST API сервер
│  │  └── Makefile
│  └── spu_client.py              # Python-клиент
│
├ fpga/                             # FPGA реализация (Artix-7)
│  ├── rtl/
│  │  ├── spu_top.v               # Top-level модуль
│  │  ├── spu_regs.v              # MMIO регистры (0x00–0x4C)
│  │  ├── spu_dotprod.v           # Dot-product engine
│  │  ├── spu_dma.v               # DMA controller
│  │  ├── spu_vecmem.v            # 4-портовая shared BRAM
│  │  └── seu_tree.v              # SEU предиктивное дерево
│  ├── sim/
│  │  └── tb_spu.v                # Testbench (T1–T16)
│  ├── constraints/
│  │  └── spu_artix7.xdc          # Pin constraints (Artix-7)
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
| `spu_top.v` | Top-level: UART ↔ AXI-Lite bridge, IRQ aggregation |
| `spu_regs.v` | Register map: SPU + SEU (0x00–0x4C) |
| `spu_dotprod.v` | Pipeline dot-product engine (1-cycle/iter) |
| `spu_dma.v` | DMA controller: FIFO ↔ vector memory |
| `spu_vecmem.v` | 4-port shared BRAM (256K × 32-bit), DMA>SEU priority |
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

**Simulation (16 test cases):**
```bash
cd fpga && make sim     # iverilog → all tests pass
```

**Synthesis (Vivado):**
```bash
cd fpga && make synth   # → spu_artix7.bit
```

**Pin mapping:** `fpga/constraints/spu_artix7.xdc`
Adjust pin names/numbers for your board's schematic.

---

### 2. QEMU Virtual Device (`emulator/`)

PCI-устройство SPU для QEMU. Поддерживает:

- MMIO регистры (SPU: 0x00–0x2C, SEU: 0x30–0x4C)
- Dot-product движок (硬件加速)
- SEU предиктивное дерево
- DMA engine для загрузки векторов
- Interrupt-driven результаты (IRQ line)

VMState version: 3 (совместимо с сохранением состояния)

Запуск:
```bash
# Эмулятор standalone
cd emulator && make && ./spu_emulator

# Как QEMU PCI-устройство (с кастомным QEMU)
qemu-system-x86_64 -device spu-pci ...
```

---

### 3. Kernel Module (`kernel_module/`)

Два режима работы:
- **emulation=1** (по умолчанию) — программная эмуляция в ядре
- **emulation=0** — реальный PCI-драйвер с MMIO

```bash
sudo insmod kernel_module/spu_driver.ko emulation=1
ls -la /dev/spu
sudo rmmod spu_driver
```

Поддерживает SEU: `ioctl(SEU_CONFIG)`, `ioctl(SEU_START)`, `ioctl(SEU_GET_TREE)`.

---

### 4. SDK — `libspu.so` (`sdk/`)

Тонкая C-обёртка над ioctl `/dev/spu`:

```c
/* SPU: similarity search */
spu_t *spu = spu_open(NULL);
spu_reset(spu);
spu_configure(spu, 1000, 128);
spu_load_vector(spu, i, data, 128);
spu_set_target(spu, query, 128);
spu_start(spu);
spu_wait_result(spu, &idx, &score, &status, 5000);

/* SEU: предиктивная генерация дерева */
float entries[128];
spu_predict_tree(spu, depth=6, offset=0xF0F0, entries, 5000);

spu_close(spu);
```

Ограничения: до 1000 векторов, до 768 измерений (аппаратные лимиты чипа).

---

### 5. CLI Поиск (`tools/spu_search`)

```bash
./tools/spu_search --generate 5000 128 --db vectors.csv
./tools/spu_search --db vectors.csv --query "0.1;0.2;0.3;0.4" --top 5
./tools/spu_search --db vectors.bin --query-file query.bin --top 10
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
./examples/semantic_search/spu_searchd --port 8080 --db vectors.bin
```

| Метод | Путь | Описание |
|-------|------|----------|
| GET | `/health` | `{"status":"ok"}` |
| GET | `/status` | `{"count":N,"dim":D,"status":"ready"}` |
| POST | `/load` | `{"file":"/path/to/db.bin"}` |
| POST | `/search` | `{"vector":[...],"top_k":5}` |

---

## Требования

- Linux (для kernel module и `/dev/spu`)
- GCC с поддержкой C11
- Icarus Verilog (для симуляции FPGA)
- Vivado (для синтеза на Artix-7)
- Никаких внешних библиотек (кроме libc)

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
- [x] SEU — Speculative Execution Unit (предиктивное дерево решений)
- [x] SEU: 4-портовая shared BRAM (DMA>SEU приоритет записи)
- [x] SEU: combined IRQ (SPU_DONE | SEU_DONE, W1C clear)
- [x] SEU testbench: 16 тест-кейсов (T1–T16)
- [x] Artix-7 FPGA deployment (XDC constraints, Vivado scripts)
- [ ] PYNQ / Zynq PS integration (AXI-HPM ↔ AXI-Slave bridge)
- [ ] Python SDK bindings
- [ ] SEU: runtime probability profiling via DMA readback

## Лицензия

[Apache License 2.0](LICENSE)
