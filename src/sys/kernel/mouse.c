#include "mouse.h"

#ifdef LINUX_BUILD
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int mouse_fd = -1;

void mouse_init() {
    mouse_fd = open("/dev/input/mice", O_RDONLY | O_NONBLOCK);
}

int mouse_poll(int *dx, int *dy, int *left_click) {
    if (mouse_fd == -1) return 0;
    signed char data[3];
    int bytes = read(mouse_fd, data, 3);
    if (bytes == 3) {
        *left_click = data[0] & 0x01;
        *dx = data[1];
        *dy = -data[2];
        return 1;
    }
    return 0;
}
#else
#define KMI1_CR     (*(volatile unsigned int *)0x10007000)
#define KMI1_STAT   (*(volatile unsigned int *)0x10007004)
#define KMI1_DATA   (*(volatile unsigned int *)0x10007008)

void mouse_init() {
    // Enable KMI
    KMI1_CR = 0x04;
    
    // Enable PS/2 mouse data reporting (send 0xF4 to KMI1_DATA)
    KMI1_DATA = 0xF4;
    
    // Wait for ACK (0xFA)
    while ((KMI1_STAT & 0x10) == 0); // Wait for RX Full
    volatile unsigned int ack = KMI1_DATA;
}

int mouse_poll(int *dx, int *dy, int *left_click) {
    // Check if RX is full (first byte of packet)
    if (KMI1_STAT & 0x10) {
        unsigned int byte1 = KMI1_DATA;
        
        // Wait for byte 2
        while ((KMI1_STAT & 0x10) == 0);
        unsigned int byte2 = KMI1_DATA;
        
        // Wait for byte 3
        while ((KMI1_STAT & 0x10) == 0);
        unsigned int byte3 = KMI1_DATA;
        
        // Decode
        *left_click = byte1 & 0x01;
        
        // X movement
        int x = byte2;
        if (byte1 & 0x10) x |= 0xFFFFFF00; // Sign extend
        *dx = x;
        
        // Y movement (PS/2 Y is inverted)
        int y = byte3;
        if (byte1 & 0x20) y |= 0xFFFFFF00; // Sign extend
        *dy = -y;
        
        return 1;
    }
    return 0;
}
#endif
