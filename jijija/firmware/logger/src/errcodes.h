#pragma once
// ERROR record codes (organ 14: v0 = code, v1 = arg). Documented in docs/data-protocol.md.
enum : uint8_t {
  ERR_SD_WRITE = 1,     // arg: failed writes so far
  ERR_QUEUE_FULL = 2,   // arg: records dropped since last report
  ERR_MPU_I2C = 3,
  ERR_BH1750_I2C = 4,
};
