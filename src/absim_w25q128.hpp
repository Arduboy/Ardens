#include "absim.hpp"

namespace absim
{

void w25q128_t::erase_all_data()
{
	memset(&data, 0xff, sizeof(data));
}

void w25q128_t::reset()
{
	enabled = false;
	write_enabled = false;
	reading_status = false;
	processing_command = false;

	reading = 0;
	programming = 0;
	erasing_sector = 0;

	busy_ps_rem = 0;
	current_addr = 0;
}

FORCEINLINE void w25q128_t::set_enabled(bool e)
{
	enabled = e;
	if(!e)
	{
		reading_status = false;
		reading = false;
		programming = false;
		processing_command = false;
	}
}

FORCEINLINE void w25q128_t::advance(uint64_t ps)
{
	if(ps >= busy_ps_rem)
		busy_ps_rem = 0;
	else
		busy_ps_rem -= ps;
}

FORCEINLINE uint8_t w25q128_t::spi_transceive(uint8_t byte)
{
	if(!enabled) return 0;

	uint8_t data_to_send = 0;

	if(reading)
	{
		if(reading <= 3)
		{
			current_addr = (current_addr << 8) | byte;
			++reading;
		}
		if(reading >= 4)
		{
			data_to_send = data[current_addr];
			++current_addr;
			current_addr &= 0xffffff;
		}
	}
	else if(programming)
	{
		if(programming == 4)
		{
			uint32_t page = current_addr & 0xffff00;
			data[current_addr] &= byte;
			++current_addr;
			current_addr = page | (current_addr & 0xff);
			data_to_send = 0;
			busy_ps_rem = 700ull * 1000 * 1000; // 0.7 ms
			programming = 0;
		}
		else
		{
			current_addr = (current_addr << 8) | byte;
			++programming;
		}
	}
	else if(reading_status)
	{
		if(busy_ps_rem != 0)
			data_to_send = 0x1;
	}
	else if(erasing_sector)
	{
		if(erasing_sector <= 3)
		{
			current_addr = (current_addr << 8) | byte;
			++erasing_sector;
		}
		if(erasing_sector == 4)
		{
			current_addr &= 0xfff000;
			memset(&data[current_addr], 0xff, 0x1000);
			busy_ps_rem = 100ull * 1000 * 1000 * 1000; // 100 ms
			erasing_sector = 0;
		}
	}
	else if(!processing_command && busy_ps_rem == 0)
	{
		processing_command = true;
		switch(byte)
		{
		case 0x02: // program page
			if(!write_enabled) break;
			programming = 1;
			current_addr = 0;
			break;
		case 0x03: // read data
			reading = 1;
			current_addr = 0;
			break;
		case 0x04: // write disable
			write_enabled = false;
			break;
		case 0x05: // read status register 1
			reading_status = true;
			break;
		case 0x06: // write enable
			write_enabled = true;
			break;
		case 0x20: // sector erase
			if(!write_enabled) break;
			erasing_sector = 1;
			current_addr = 0;
			break;
		default:
			break;
		}
	}

	return data_to_send;
}

}
