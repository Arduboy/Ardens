#include "absim.hpp"

namespace absim
{

void atmega32u4_t::reset()
{
    // clear all registers and RAM, reset state

    // preserve button pins
    uint8_t pinb = data[0x23];
    uint8_t pine = data[0x2c];
    uint8_t pinf = data[0x2f];

    memset(&data[0], 0, 32 + 64 + 160 + 2560);

    data[0x23] = pinb;
    data[0x2c] = pine;
    data[0x2f] = pinf;

    pc = 0;

    just_read = 0;
    just_written = 0;

    active = true;
    wakeup_cycles = false;

    memset(&eeprom, 0xff, sizeof(eeprom));

    prev_sreg = 0;

    timer0_divider_cycle = 0;
    timer0_divider = 0;

    for(auto& h : ld_handlers) h = nullptr;
    for(auto& h : st_handlers) h = nullptr;

    st_handlers[0x3f] = eeprom_handle_st_eecr;
    st_handlers[0x49] = pll_handle_st_pllcsr;
    st_handlers[0x4c] = spi_handle_st_spcr_or_spsr;
    st_handlers[0x4d] = spi_handle_st_spcr_or_spsr;
    st_handlers[0x4e] = spi_handle_st_spdr;

    ld_handlers[0x4d] = spi_handle_ld_spsr;

    memset(&timer1, 0, sizeof(timer1));
    memset(&timer3, 0, sizeof(timer3));

    timer1.base_addr = 0x80;
    timer3.base_addr = 0x90;
    timer1.tifrN_addr = 0x36;
    timer3.tifrN_addr = 0x38;
    timer1.timskN_addr = 0x6f;
    timer3.timskN_addr = 0x71;
    timer1.prr_addr = 0x64;
    timer3.prr_addr = 0x65;
    timer1.prr_mask = 1 << 3;
    timer3.prr_mask = 1 << 3;

    pll_lock_cycle = 0;

    spsr_read_after_transmit = false;
    spi_busy = false;
    spi_done = false;
    spi_data_byte = 0;
    spi_clock_cycles = 4;
    spi_clock_cycle = 0;
    spi_bit_progress = 0;

    eeprom_clear_eempe_cycles = 0;
    eeprom_write_addr = 0;
    eeprom_write_data = 0;
    eeprom_program_cycles = 0;

    adc_prescaler_cycle = 0;
    adc_cycle = 0;
    adc_ref = 0;
    adc_result = 0;

    cycle_count = 0;

    update_sleep_min_cycles();
}

}
