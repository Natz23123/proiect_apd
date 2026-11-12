#include "vl53l1_platform.h"
#include "main.h"
#include <string.h>

extern I2C_HandleTypeDef hi2c1;

#define I2C_TIMEOUT 100

int8_t VL53L1_WriteMulti(uint16_t dev, uint16_t index, uint8_t *pdata, uint32_t count) {
    return (HAL_I2C_Mem_Write(&hi2c1, dev, index, I2C_MEMADD_SIZE_16BIT,
                              pdata, (uint16_t)count, I2C_TIMEOUT) == HAL_OK) ? 0 : -1;
}

int8_t VL53L1_ReadMulti(uint16_t dev, uint16_t index, uint8_t *pdata, uint32_t count) {
    return (HAL_I2C_Mem_Read(&hi2c1, dev, index, I2C_MEMADD_SIZE_16BIT,
                             pdata, (uint16_t)count, I2C_TIMEOUT) == HAL_OK) ? 0 : -1;
}

int8_t VL53L1_WrByte(uint16_t dev, uint16_t index, uint8_t data) {
    return VL53L1_WriteMulti(dev, index, &data, 1);
}

int8_t VL53L1_WrWord(uint16_t dev, uint16_t index, uint16_t data) {
    uint8_t b[2] = { (uint8_t)(data >> 8), (uint8_t)(data & 0xFF) };
    return VL53L1_WriteMulti(dev, index, b, 2);
}

int8_t VL53L1_WrDWord(uint16_t dev, uint16_t index, uint32_t data) {
    uint8_t b[4] = { (uint8_t)(data >> 24), (uint8_t)(data >> 16),
                     (uint8_t)(data >> 8),  (uint8_t)(data & 0xFF) };
    return VL53L1_WriteMulti(dev, index, b, 4);
}

int8_t VL53L1_RdByte(uint16_t dev, uint16_t index, uint8_t *data) {
    return VL53L1_ReadMulti(dev, index, data, 1);
}

int8_t VL53L1_RdWord(uint16_t dev, uint16_t index, uint16_t *data) {
    uint8_t b[2];
    int8_t s = VL53L1_ReadMulti(dev, index, b, 2);
    *data = ((uint16_t)b[0] << 8) | b[1];
    return s;
}

int8_t VL53L1_RdDWord(uint16_t dev, uint16_t index, uint32_t *data) {
    uint8_t b[4];
    int8_t s = VL53L1_ReadMulti(dev, index, b, 4);
    *data = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
            ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
    return s;
}

int8_t VL53L1_WaitMs(uint16_t dev, int32_t wait_ms) {
    (void)dev;
    HAL_Delay((uint32_t)wait_ms);
    return 0;
}