# SPPU — Search & Prediction Processing Unit v0.3

**Аппаратный ускоритель + программный стек для векторного поиска с предиктивной генерацией**

> Обычный поиск ищет слова. Мы ищем смысл.

SPPU — концептуальный аппаратный ускоритель для similarity search на float-векторах. Проект включает полный стек: от RTL-модели чипа до HTTP API сервера, готового к интеграции.

**v0.4:** Open-source FPGA toolchain (Yosys + nextpnr-xilinx + prjxray). SEU compact mode (4×4). SA placer null-ptr fix.

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
│  ├── arch/                            # Архитектурная база для nextpnr-xilinx
│  │  └── xc7z010clg400-1/             # Описание кристалла Zynq-7010
│  │     ├── part.yaml                  # Определение тайлов, сайтов, пинов
│  │     ├── part.json                  # JSON-версия описания
│  │     ├── package_pins.csv           # Маппинг пинов → корпуса
│  │     └── required_features.fasm     # Минимальные FASM-функции чипа
│  │  └── README.md                     # Подробности о chipdb
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
│  │  ├── yosys_synth.ys            # Yosys synthesis script
│  │  ├── nextpnr_impl.sh           # nextpnr place & route → FASM
│  │  ├── fasm2bit.sh               # FASM → binary .bit (xc7frames2bit)
│  │  └── program_fpga.sh           # Программирование FPGA через JTAG (openFPGALoader)
│  ├── Makefile                      # sim / synth / fasm / bit / program / lint targets
│  └── tb_sppu.vcd                   # Simulation waveform output
│
├ scripts/                            # Автоматизация сборки и прошивки
│  ├── build_fpga.sh                 # Полная сборка bitstream (Yosys + nextpnr-xilinx)
│  ├── flash_jtag.sh                 # Прошивка FPGA через JTAG (openFPGALoader)
│  ├── create_boot.sh                # Сборка boot.bin и образа SD-карты
│  └── setup_petalinux.sh            # Создание/обновление PetaLinux проекта
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
| `sppu_pynq_top.v` | PYNQ/Zynq wrapper (PS AXI bridge, LEDs, UART debug, irq) |
| `seu_tree.v` | SEU tree generator: FSM + LFSR, compact mode (4 variants × 4 levels, BRAM) |

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
| `0x38` | SEU_DEPTH | RW | Tree depth (2..4, compact mode; HW не меняется) |
| `0x3C` | SEU_OFFSET | RW | Branch offsets (packed, 32-bit per offset) |
| `0x40` | SEU_TREE_ADDR | RW | Base address in vector memory for tree nodes |
| `0x44` | SEU_TREE_RESULT | RW | Base address for tree result storage |
| `0x48` | SEU_PROB_BASE | RW | Probability configuration base address |
| `0x4C` | SEU_IRQ_STATUS | RW1C | SEU interrupt status |
| `0x50` | SEU_PROB_READ_IDX | RW | Index for probability readback (0..15, compact mode) |
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
- `sys_clk` — pin K17 (MRCC), 50 MHz oscillator
- `sys_rst_n` — unconstrained (auto-assign)
- LEDs: H17, K18, J14, M14, L16 (all PL-bank GPIO)
- UART (PL debug): unconstrained (auto-assign)
- PS-side pins (UART, SPI, GPIO0..1) removed from XDC — fabric cannot constrain PS MIO pads

**Important:** Only PL-side pins (sys_clk, LEDs) have PACKAGE_PIN constraints.
All PS MIO pins (UART, SPI, GPIO, sys_rst_n) are unconstrained and assigned by nextpnr.
This is required because nextpnr-xilinx cannot place BELs on PS-side IOB33 tiles.

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

## Прошивка FPGA (Zynq-7010, AntMiner XC7Z010 v1.0)

### Требования
- Yosys ≥0.40 (https://github.com/YosysHQ/yosys)
- nextpnr-xilinx (https://github.com/gatecat/nextpnr-xilinx, commit `8f178fc6`)
- prjxray (https://github.com/SymbiFlow/prjxray) — `fasm2frames.py` для FASM → .bit
- xc7frames2bit — из prjxray-tools (конвертация frames → .bit)
- openFPGALoader (`apt install openfpgaloader` или собрать из исходников)
- Python ≥3.8 с пакетами `fasm`, `prjxray` (pip-установка)
- prjxray-db (https://github.com/SymbiFlow/prjxray-db) — база для xc7z010clg400-1
- JTAG-адаптер (Digilent JTAG-HS3 или аналог)
- Плата AntMiner XC7Z010 v1.0
- Linux-система (для сборки и работы скриптов)

### Генерация архитектурной базы (chipdb)

nextpnr-xilinx требует бинарную архитектурную базу данных
`chipdb.bin` для каждой целевой модели кристалла.

Для XC7Z010 (`xc7z010clg400-1`) файл **уже готов** в репозитории:
`fpga/arch/xc7z010clg400-1/chipdb.bin` (57 MB).

Если нужно пересобрать (после обновления prjxray-db или смены nextpnr-версии):

```bash
# 1. Клонируйте Project X-Ray и nextpnr-xilinx источник
git clone https://github.com/Xilinx/prjxray.git
git clone https://github.com/YosysHQ/nextpnr-xilinx.git
cd nextpnr-xilinx
git submodule update --init --recursive

# 2. Соберите bbaexport.py и bba-инструменты
mkdir build && cd build
cmake .. && make -j$(nproc)
cp bba/bba /usr/local/bin/

# 3. Экспортируйте BBA-описание устройства
python3 ../xilinx/python/bbaexport.py \
  --xray ../prjxray-db/zynq7 \
  --metadata ../nextpnr-xilinx-meta/zynq7 \
  --device xc7z010clg400-1 \
  --constids ../xilinx/constids.inc \
  --bba fpga/arch/xc7z010clg400-1/xc7z010clg400-1.bba

# 4. Соберите бинарную базу
bba --le fpga/arch/xc7z010clg400-1/xc7z010clg400-1.bba \
    fpga/arch/xc7z010clg400-1/chipdb.bin
```

Файлы `part.yaml`, `part.json`, `package_pins.csv`, `required_features.fasm`
(описание кристалла из prjxray-db) используются bbaexport.py
на этапе генерации. Подробнее — `fpga/arch/README.md`.

### Open-source сборка bitstream (Yosys + nextpnr-xilinx)

Проект использует полностью открытый toolchain для 7-серии Xilinx:
`Yosys → nextpnr-xilinx → fasm2frames.py → xc7frames2bit → .bit`

#### Подготовка окружения

```bash
# 1. Установите prjxray и fasm2frames.py
git clone --depth 1 https://github.com/SymbiFlow/prjxray.git /tmp/prjxray
pip install --break-system-packages -e /tmp/prjxray fasm

# 2. Клонируйте prjxray-db (уже в /tmp/prjxray-db или скачайте)
git clone --depth 1 https://github.com/SymbiFlow/prjxray-db.git /tmp/prjxray-db

# 3. nextpnr-xilinx уже собран в репозитории (ветка xilinx-upstream, 8f178fc6)
#    При необходимости пересобрать:
#    cmake -S /tmp/gatecat-nextpnr -B /tmp/gatecat-nextpnr/build \
#      -DARCH=xilinx -DBUILD_PYTHON=OFF -DUSE_OPENMP=OFF
#    cmake --build /tmp/gatecat-nextpnr/build --target nextpnr-xilinx -j$(nproc)
```

**Известные патчи в nextpnr-xilinx** (применены в `placer1.cc`):
- Исправлен segfault в SA placer (`place_initial`): null-ptr dereference при rip-up
  занятого BEL, не привязанного к клетке. Добавлена проверка `bound_cell != nullptr`.

#### Сборка

```bash
cd fpga

# 1. Полная сборка bitstream
make bit
# Результат: build/sppu_zynq7010.bit (2 MB)

# 2. Или пошагово:
make synth   # Yosys → JSON
make fasm    # nextpnr place & route → FASM
make bit     # FASM → .bit (fasm2frames.py + xc7frames2bit)

# 3. Прошивка через JTAG
make program
```

#### Вручную

```bash
# 1. Yosys synthesis
yosys -p 'read_verilog rtl/sppu_regs.v; read_verilog rtl/sppu_dotprod.v;
         read_verilog rtl/sppu_dma.v; read_verilog rtl/sppu_vecmem.v;
         read_verilog rtl/seu_tree.v; read_verilog rtl/sppu_top.v;
         read_verilog rtl/sppu_pynq_top.v;
         synth_xilinx -top sppu_pynq -family xc7; write_json build/sppu_pynq_top.json'

# 2. nextpnr place & route
nextpnr-xilinx --chipdb arch/xc7z010clg400-1/chipdb.bin \
  --xdc constraints/sppu_zynq7010.xdc \
  --json build/sppu_pynq_top.json \
  --fasm build/sppu_pynq_top.fasm \
  --placer heap --router router2 --timing-allow-fail

# 3. FASM → .bit
python3 /tmp/prjxray/utils/fasm2frames.py \
  --db-root /tmp/prjxray-db/zynq7 \
  --part xc7z010clg400-1 \
  build/sppu_pynq_top.fasm build/sppu_pynq_top.frames
xc7frames2bit --part_file arch/xc7z010clg400-1/part.yaml \
  --part_name xc7z010clg400-1 \
  --frm_file build/sppu_pynq_top.frames \
  --output_file build/sppu_zynq7010.bit

# 4. Прошивка FPGA через JTAG
openFPGALoader -b digilent_a build/sppu_zynq7010.bit
```

#### Результаты сборки

| Конфигурация | SEU | Ячейки | Тайминг | Размер .bit |
|-------------|-----|--------|---------|-------------|
| Без SEU | заглушка | 3440 | 81-117 MHz | 2.0 MB |
| С SEU (4×4) | упрощённый | 6952 | 53-63 MHz | 2.0 MB |

#### Известные проблемы

- **SEU full mode (16×8)**: heap placer не находит законного размещения для ~13000 FF
  SEU-модуля; SA placer segfault. Решение: compact mode 4 варианта × 4 уровня.
- **SA placer segfault**: исправлен в локальной сборке (null-ptr в `place_initial`).
- **Wire integrity warning**: `wire != wire2, name = INT_L_X0Y99/ER1BEG_S0` — нефатально,
  P&R успешен.

### Вручную без PetaLinux (минимальный boot.bin)

Если PetaLinux/Vitis не установлены:

```bash
# 1. Соберите FSBL с XSA через Vivado SDK или Vitis:
#    - Импортируйте fpga/build/sppu_zynq7010.xsa
#    - Создайте приложение "Zynq FSBL"
#    - Скомпилируйте → fsbl.elf

# 2. Создайте boot.bif:
cat > boot.bif << 'EOF'
the_ROM_image:
{
  [bootloader] fsbl.elf
  sppu_zynq7010.bit
}
EOF

# 3. Соберите boot.bin:
bootgen -image boot.bif -o boot.bin -w

# 4. Запишите на SD-карту:
sudo dd if=boot.bin of=/dev/sdX bs=512 seek=0 conv=notrunc
```

---

## Требования

- Linux (для kernel module и `/dev/sppu`)
- GCC с поддержкой C11
- Icarus Verilog (для симуляции FPGA)
- Yosys + nextpnr-xilinx (для открытого синтеза, необязательно, альтернатива Vivado)
- Python 3.8+ (для Python SDK и FASM-конвертации)
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
- [x] v0.4: Open-source FPGA toolchain (Yosys + nextpnr-xilinx + prjxray)
- [x] v0.4: nextpnr-xilinx SA placer null-ptr fix (placer1.cc)
- [x] v0.4: SEU compact mode (4×4, BRAM) для pack/place/routing в открытом toolchain
- [x] v0.4: chipdb для xc7z010clg400-1 (bbasm + prjxray-db)
- [x] v0.4: FASM → BIT pipeline (fasm2frames.py + xc7frames2bit)
- [ ] SEU full mode (16×8) в открытом toolchain

---

## Лицензия

[Apache License 2.0](LICENSE)
