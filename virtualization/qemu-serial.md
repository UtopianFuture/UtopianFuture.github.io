## QEMU Serial

串口通信是通过总线或信道向设备每次发送一比特的数据。QEMU 模拟的是 16550A UART 芯片，具体实现在 serial.c 文件中。现在分析 serial 是怎么将数据转发到不同的设备中。



background

One drawback of the earlier 8250 UARTs and 16450 UARTs was that interrupts were generated for each byte received. This generated high rates of interrupts as transfer speeds increased. More critically, with only a 1-byte buffer there is a genuine risk that a received byte will be overwritten if interrupt service delays occur. To overcome these shortcomings, the 16550 series UARTs incorporated a 16-byte FIFO buffer with a programmable interrupt trigger of 1, 4, 8, or 14 bytes.
