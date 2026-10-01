#ifndef AS7343_REGS_H
#define AS7343_REGS_H

/* 7-bit I2C address (datasheet §9.1) */
#define AS7343_I2C_ADDR              0x39u

/* Device identification — register bank 1 */
#define AS7343_REG_ID                0x5Au
#define AS7343_DEVICE_ID_EXPECTED    0x81u


/* Register bank selection */
#define AS7343_REG_CFG0              0xBFu
#define AS7343_CFG0_REG_BANK_MASK    0x10u


/* Power and measurement control — register bank 0 */
#define AS7343_REG_ENABLE            0x80u
#define AS7343_ENABLE_PON_MASK       0x01u
#define AS7343_ENABLE_SP_EN_MASK     0x02u
#define AS7343_ENABLE_WEN_MASK       0x08u
#define AS7343_ENABLE_FDEN_MASK      0x40u


/* Exposure configuration — register bank 0 */

/* Gain configuration */
#define AS7343_REG_CFG1             0xC6u
#define AS7343_CFG1_AGAIN_MASK       0x1Fu
#define AS7343_CFG1_AGAIN_POS        0u

/* Unshifted gain field values */
#define AS7343_GAIN_0_5X             0u
#define AS7343_GAIN_1X               1u
#define AS7343_GAIN_2X               2u
#define AS7343_GAIN_4X               3u
#define AS7343_GAIN_8X               4u
#define AS7343_GAIN_16X              5u
#define AS7343_GAIN_32X              6u
#define AS7343_GAIN_64X              7u
#define AS7343_GAIN_128X             8u
#define AS7343_GAIN_256X             9u
#define AS7343_GAIN_512X             10u
#define AS7343_GAIN_1024X            11u
#define AS7343_GAIN_2048X            12u

/* Integration timing */
#define AS7343_REG_ATIME            0x81u
#define AS7343_REG_ASTEP_L          0xD4u
#define AS7343_REG_ASTEP_H          0xD5u

#define AS7343_ATIME_MAX            255u
#define AS7343_ASTEP_MAX            65534u

/*
 * Integration time = (ATIME + 1) * (ASTEP + 1) * 2.78 us.
 *
 * Clear ENABLE.SP_EN before changing exposure settings.
 * ATIME and ASTEP must not both be zero.
 * ASTEP = 65535 is reserved.
 * Gain codes outside 0–12 are not supported.
 */


/* Automatic channel sequencing — register bank 0 */
#define AS7343_REG_CFG20                  0xD6u
#define AS7343_CFG20_AUTO_SMUX_MASK        0x60u
#define AS7343_CFG20_AUTO_SMUX_POS         5u

/* Unshifted automatic channel-readout field values */
#define AS7343_AUTO_SMUX_6_CHANNEL        0u
#define AS7343_AUTO_SMUX_12_CHANNEL       2u
#define AS7343_AUTO_SMUX_18_CHANNEL       3u

/*
 * Clear ENABLE.SP_EN before changing channel sequencing.
 * AUTO_SMUX value 1 is reserved.
 *
 * Use 18-channel mode to acquire the full spectral set.
 * It produces 18 result slots across three measurement cycles,
 * including repeated VIS/CLEAR and FD readings.
 * It does not represent 18 distinct spectral bands.
 */


/* Measurement status — register bank 0 */
#define AS7343_REG_STATUS2                  0x90u
#define AS7343_STATUS2_AVALID_MASK          0x40u
#define AS7343_STATUS2_ASAT_DIGITAL_MASK    0x10u
#define AS7343_STATUS2_ASAT_ANALOG_MASK     0x08u

/* Status associated with latched spectral data */
#define AS7343_REG_ASTATUS                  0x94u
#define AS7343_ASTATUS_ASAT_MASK            0x80u
#define AS7343_ASTATUS_AGAIN_MASK           0x0Fu
#define AS7343_ASTATUS_AGAIN_POS            0u

/* Spectral data: 18 results, each stored low byte first */
#define AS7343_REG_DATA_START               0x95u
#define AS7343_REG_DATA_END                 0xB8u

#define AS7343_DATA_SLOT_COUNT              18u
#define AS7343_DATA_BYTES_PER_SLOT          2u
#define AS7343_DATA_BYTE_COUNT              36u
#define AS7343_FRAME_BYTE_COUNT             37u

/*
 * STATUS2.AVALID indicates spectral measurement completion.
 * STATUS2 is read-only; do not write to clear AVALID.
 *
 * Reading ASTATUS latches all 36 spectral data bytes.
 * Read 37 consecutive bytes starting at ASTATUS:
 * one status byte followed by 18 low-byte/high-byte pairs.
 *
 * ASTATUS supplies the gain and saturation status associated
 * with the latched data. Preserve these with the sample.
 *
 * Verify fresh, complete three-cycle acquisition before
 * accepting a frame; a successful I2C read alone is not enough.
 */


/* Autozero configuration — register bank 0 */
#define AS7343_REG_AZ_CONFIG                0xDEu

/* Whole-register values */
#define AS7343_AZERO_DISABLED               0u
#define AS7343_AZERO_EVERY_CYCLE            1u
#define AS7343_AZERO_FIRST_CYCLE_ONLY       255u

/*
 * Values 1–254: perform autozero every N integration cycles.
 * Value 255: perform autozero only before the first cycle.
 * Value 0: disable autozero; not recommended.
 *
 * Documented default: 255.
 * Typical autozero duration: 15 ms.
 * Allow for this overhead when determining acquisition timeouts.
 *
 * Configure while spectral acquisition is stopped.
 */


/* Interrupt enables — register bank 0 */
#define AS7343_REG_INTENAB                  0xF9u
#define AS7343_INTENAB_ASIEN_MASK           0x80u
#define AS7343_INTENAB_SP_IEN_MASK          0x08u
#define AS7343_INTENAB_F_IEN_MASK           0x04u
#define AS7343_INTENAB_SIEN_MASK            0x01u

/* Sleep-after-interrupt configuration — register bank 0 */
#define AS7343_REG_CFG3                     0xC7u
#define AS7343_CFG3_SAI_MASK                0x10u

/*
 * Version one uses polling:
 * clear the defined INTENAB enable fields and CFG3.SAI.
 *
 * Preserve reserved bits when updating these registers.
 * In particular, CFG3 has a documented reserved low-nibble
 * default of 0xC; do not blindly overwrite CFG3 with zero.
 *
 * Sleep between samples using ENABLE.PON instead of SAI.
 */


/* LED driver (LDR pin) — register bank 0 */
#define AS7343_REG_LED                      0xCDu
#define AS7343_LED_ACT_MASK                 0x80u

/*
 * LED_ACT turns on an LED connected to the LDR pin. Keep it off:
 * any light it emits would be measured as ambient light.
 */


/* Software reset — register bank 0 */
#define AS7343_REG_CONTROL                  0xFAu
#define AS7343_CONTROL_SW_RESET_MASK        0x08u

/*
 * Setting SW_RESET forces a device power-on reset.
 *
 * After reset:
 * - Respect the device initialization period.
 * - Re-establish register-bank selection.
 * - Reapply the required sensor configuration.
 *
 * CONTROL contains command fields. Do not use a generic
 * read-modify-write operation that could replay other commands.
 *
 * A reset is not required between normal samples.
 */


/* Result-slot indices for AUTO_SMUX 18-channel mode */

/* Cycle 1 */
#define AS7343_SLOT_FZ                      0u
#define AS7343_SLOT_FY                      1u
#define AS7343_SLOT_FXL                     2u
#define AS7343_SLOT_NIR                     3u
#define AS7343_SLOT_CLEAR_1                 4u
#define AS7343_SLOT_FD_1                    5u

/* Cycle 2 */
#define AS7343_SLOT_F2                      6u
#define AS7343_SLOT_F3                      7u
#define AS7343_SLOT_F4                      8u
#define AS7343_SLOT_F6                      9u
#define AS7343_SLOT_CLEAR_2                 10u
#define AS7343_SLOT_FD_2                    11u

/* Cycle 3 */
#define AS7343_SLOT_F1                      12u
#define AS7343_SLOT_F7                      13u
#define AS7343_SLOT_F8                      14u
#define AS7343_SLOT_F5                      15u
#define AS7343_SLOT_CLEAR_3                 16u
#define AS7343_SLOT_FD_3                    17u

/*
 * These indices apply only to AUTO_SMUX 18-channel mode.
 * Each slot contains one decoded 16-bit reading.
 *
 * CLEAR and FD readings repeat across the three cycles.
 * FD slots contain raw ADC readings, not flicker classifications.
 */

#endif