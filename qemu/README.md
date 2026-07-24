# SPU QEMU Virtual PCI Device

Виртуальное PCI-устройство SPU для QEMU. Позволяет тестировать полный стек
(SPU driver → SDK → CLI) без реального железа.

## Как это работает

```
┌──────────────────────────────────┐
│         QEMU Guest (Linux)       │
│                                  │
│  spu_driver.ko  ← PCI probe     │
│       ↓                          │
│  /dev/spu       ← ioctl         │
│       ↓                          │
│  libspu.so      ← API           │
│       ↓                          │
│  spu_search     ← CLI поиск     │
├──────────────────────────────────┤
│       QEMU PCI Bus               │
│  ┌──────────────────────────┐    │
│  │ SPU Device (spu_qemu.c)  │    │
│  │ vendor=0x1234 dev=0x5780 │    │
│  │                          │    │
│  │ BAR0: MMIO registers     │    │
│  │ BAR1: Vector memory      │    │
│  │ IRQ:  line 1             │    │
│  │                          │    │
│  │ Dot-product engine       │    │
│  └──────────────────────────┘    │
└──────────────────────────────────┘
```

## Сборка

### Способ 1: Интеграция в дерево QEMU (рекомендуется)

```bash
# 1. Скопировать файл устройства в дерево QEMU
cp spu_qemu.c /usr/src/qemu/hw/misc/

# 2. Добавить в hw/misc/Kconfig:
cat >> /usr/src/qemu/hw/misc/Kconfig << 'EOF'
config SPU_PCI
    bool "SPU Search Processing Unit"
    default y
    depends on PCI
EOF

# 3. Добавить в hw/misc/Makefile.objs:
echo 'obj-$(CONFIG_SPU_PCI) += spu_qemu.o' >> /usr/src/qemu/hw/misc/Makefile.objs

# 4. Пересобрать QEMU
cd /usr/src/qemu/build
make -j$(nproc)

# 5. Проверить наличие устройства
qemu-system-x86_64 -device help | grep spu
```

### Способ 2: Makefile (автоматизация)

```bash
cd qemu/
make    # покажет инструкции
```

## Запуск

```bash
# С ядром и rootfs
./qemu-spu.sh --kernel /boot/vmlinuz-linux --rootfs rootfs.img

# Только с ядром (initramfs)
./qemu-spu.sh --kernel /boot/vmlinuz-linux

# С отладкой (GDB)
./qemu-spu.sh --kernel /boot/vmlinuz-linux --debug
```

## Внутри гостя

```bash
# Монтируем общие папки
mount -t 9p -o trans=virtio spu_module /mnt/module
mount -t 9p -o trans=virtio spu_sdk /mnt/sdk

# Загружаем драйвер (hardware mode, emulation=0)
insmod /mnt/module/spu_driver.ko emulation=0

# Проверяем устройство
lspci | grep 1234
ls -la /dev/spu

# Запускаем SDK demo
cd /mnt/sdk && make && ./examples/spu_demo
```

## Регистры (BAR0)

| Offset | Name | R/W | Описание |
|--------|------|-----|----------|
| 0x00 | CTRL | RW | Управление (start/reset/irq_en) |
| 0x04 | STATUS | RO | Статус (READY/BUSY/DONE) |
| 0x08 | VEC_COUNT | RW | Количество векторов |
| 0x0C | DIMENSION | RW | Размерность |
| 0x18 | RESULT_IDX | RO | Индекс лучшего совпадения |
| 0x1C | RESULT_SCORE | RO | Dot-product score |
| 0x20 | DEVICE_ID | RO | Версия прошивки |
| 0x24 | INT_MASK | RW | Маска прерываний |
| 0x28 | INT_STATUS | RW1C | Статус прерываний |
| 0xFC | MAGIC | RO | 0x53505520 ("SPU ") |

## Лимиты

- До 1000 векторов в памяти
- До 768 измерений на вектор
- Максимум 3 МБ vector memory (BAR1)
