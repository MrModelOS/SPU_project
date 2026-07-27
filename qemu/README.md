# SPPU QEMU Virtual PCI Device

Виртуальное PCI-устройство SPPU для QEMU. Позволяет тестировать полный стек
(SPPU driver → SDK → CLI) без реального железа.

## Как это работает

```
┌──────────────────────────────────┐
│         QEMU Guest (Linux)       │
│                                  │
│  sppu_driver.ko  ← PCI probe     │
│       ↓                          │
│  /dev/sppu       ← ioctl         │
│       ↓                          │
│  libsppu.so      ← API           │
│       ↓                          │
│  sppu_search     ← CLI поиск     │
├──────────────────────────────────┤
│       QEMU PCI Bus               │
│  ┌──────────────────────────┐    │
│  │ SPPU Device (sppu_qemu.c)  │    │
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
cp sppu_qemu.c /usr/src/qemu/hw/misc/

# 2. Добавить в hw/misc/Kconfig:
cat >> /usr/src/qemu/hw/misc/Kconfig << 'EOF'
config SPPU_PCI
    bool "SPPU Search Processing Unit"
    default y
    depends on PCI
EOF

# 3. Добавить в hw/misc/Makefile.objs:
echo 'obj-$(CONFIG_SPPU_PCI) += sppu_qemu.o' >> /usr/src/qemu/hw/misc/Makefile.objs

# 4. Пересобрать QEMU
cd /usr/src/qemu/build
make -j$(nproc)

# 5. Проверить наличие устройства
qemu-system-x86_64 -device help | grep sppu
```

### Способ 2: Makefile (автоматизация)

```bash
cd qemu/
make    # покажет инструкции
```

## Запуск

```bash
# С ядром и rootfs
./qemu-sppu.sh --kernel /boot/vmlinuz-linux --rootfs rootfs.img

# Только с ядром (initramfs)
./qemu-sppu.sh --kernel /boot/vmlinuz-linux

# С отладкой (GDB)
./qemu-sppu.sh --kernel /boot/vmlinuz-linux --debug
```

## Внутри гостя

```bash
# Монтируем общие папки
mount -t 9p -o trans=virtio sppu_module /mnt/module
mount -t 9p -o trans=virtio sppu_sdk /mnt/sdk

# Загружаем драйвер (hardware mode, emulation=0)
insmod /mnt/module/sppu_driver.ko emulation=0

# Проверяем устройство
lspci | grep 1234
ls -la /dev/sppu

# Запускаем SDK demo
cd /mnt/sdk && make && ./examples/sppu_demo
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
| 0xFC | MAGIC | RO | 0x53505520 ("SPPU ") |

## Лимиты

- До 1000 векторов в памяти
- До 768 измерений на вектор
- Максимум 3 МБ vector memory (BAR1)
