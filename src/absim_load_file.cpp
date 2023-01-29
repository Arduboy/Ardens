#include "absim.hpp"

#include <string>
#include <fstream>
#include <algorithm>

#include <elfio/elfio.hpp>

extern "C" char* __cxa_demangle(const char* MangledName, char* Buf, size_t * N, int* Status);

namespace absim
{

static std::string demangle(char const* sym)
{
    size_t size = 0;
    int status = -1;
    char* t = __cxa_demangle(sym, nullptr, &size, &status);
    if(status != 0)
        return sym;
    std::string r(t);
    std::free(t);
    return r;
}

static int convert_hex_char(int c)
{
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int get_hex_byte(std::istream& f)
{
    if(f.eof()) return -1;
    int hi = convert_hex_char(f.get());
    if(hi < 0) return -1;
    if(f.eof()) return -1;
    int lo = convert_hex_char(f.get());
    if(lo < 0) return -1;
    return lo + hi * 16;
}

static char const* load_hex(atmega32u4_t& cpu, std::string const& fname)
{
    std::ifstream f(fname, std::ios::in);

    if(f.fail())
        return "Unable to open file";

    memset(&cpu.prog, 0, sizeof(cpu.prog));
    memset(&cpu.decoded_prog, 0, sizeof(cpu.decoded_prog));
    memset(&cpu.disassembled_prog, 0, sizeof(cpu.disassembled_prog));
    memset(&cpu.breakpoints, 0, sizeof(cpu.breakpoints));
    memset(&cpu.breakpoints_rd, 0, sizeof(cpu.breakpoints_rd));
    memset(&cpu.breakpoints_wr, 0, sizeof(cpu.breakpoints_wr));

    while(!f.eof())
    {
        while(f.get() != ':')
            if(f.eof())
                return "Intel HEX: unexpected EOF";
        uint8_t checksum = 0;
        int count = get_hex_byte(f);
        if(count < 0)
            return "Intel HEX bad byte count";
        checksum += (uint8_t)count;
        int addr_hi = get_hex_byte(f);
        int addr_lo = get_hex_byte(f);
        if(addr_lo < 0 || addr_hi < 0)
            return "Intel HEX: bad address";
        checksum += (uint8_t)addr_lo;
        checksum += (uint8_t)addr_hi;
        int addr = addr_lo + addr_hi * 256;
        int type = get_hex_byte(f);
        checksum += (uint8_t)type;
        if(type < 0)
            return "Intel HEX: bad type";
        if(type > 1)
            return "Intel HEX: unsupported type";
        if(type == 1)
        {
            if(count != 0)
                return "Intel HEX: non-zero byte count at end-of-file record";
            break;
        }
        if(type == 0)
        {
            for(int i = 0; i < count; ++i)
            {
                int data = get_hex_byte(f);
                if(data < 0)
                    return "Intel HEX: bad data";
                checksum += (uint8_t)data;
                if(addr + i >= cpu.prog.size())
                    return "Too many instructions!";
                if(addr + i > cpu.last_addr)
                    cpu.last_addr = addr + i;
                cpu.prog[addr + i] = (uint8_t)data;
            }
        }
        checksum = uint8_t(-checksum);
        int check = get_hex_byte(f);
        if(checksum != check)
            return "Intel HEX: bad checksum";
    }

    cpu.decode();

    return nullptr;
}

struct Elf32_Sym
{
    uint32_t  st_name;
    uint32_t  st_value;
    uint32_t  st_size;
    uint8_t   st_info;
    uint8_t   st_other;
    uint16_t  st_shndx;
};

static void try_insert_symbol(elf_data_t::map_type& m, elf_data_symbol_t& sym)
{
    auto it = m.find(sym.addr);
    if(it == m.end())
    {
        m[sym.addr] = std::move(sym);
        return;
    }
    // if new is WEAK don't replace old with it
    if(sym.bind == 2) return;
    auto& oldsym = it->second;
    // if new is NOTYPE and old is not NOTYPE don't replace
    if(sym.type == 0 && oldsym.type != 0) return;
    // if old is GLOBAL don't replace it
    if(oldsym.bind == 1) return;
    m[sym.addr] = std::move(sym);
}

static char const* load_elf(arduboy_t& a, std::string const& fname)
{
    using namespace ELFIO;
    elfio reader;

    if(!reader.load(fname))
        return "ELF: Unable to load file";

    auto& cpu = a.cpu;

    a.elf.reset();
    memset(&cpu.prog, 0, sizeof(cpu.prog));
    memset(&cpu.decoded_prog, 0, sizeof(cpu.decoded_prog));
    memset(&cpu.disassembled_prog, 0, sizeof(cpu.disassembled_prog));
    memset(&cpu.breakpoints, 0, sizeof(cpu.breakpoints));
    memset(&cpu.breakpoints_rd, 0, sizeof(cpu.breakpoints_rd));
    memset(&cpu.breakpoints_wr, 0, sizeof(cpu.breakpoints_wr));

    if(reader.get_machine() != 0x0053)
        return "ELF: machine not EM_AVR";

    if(reader.get_class() != 0x1)
        return "ELF: not a 32-bit ELF file";

    auto elf_ptr = std::make_unique<elf_data_t>();
    auto& elf = *elf_ptr;

    bool found_text = false;

    section const* sec_symtab = nullptr;
    section const* sec_strtab = nullptr;

    int sec_index_data = -1;
    int sec_index_bss = -1;
    int sec_index_text = -1;

    constexpr uint32_t ram_offset = 0x800000;

    for(unsigned i = 0; i < reader.sections.size(); ++i)
    {
        auto const* sec = reader.sections[i];
        auto name = sec->get_name();
        auto size = sec->get_size();
        auto data = sec->get_data();
        auto addr = sec->get_address();

        if(name == ".text")
        {
            sec_index_text = (int)i;
            if(size > cpu.PROG_SIZE_BYTES)
                return "ELF: Section .text too large";
            memcpy(&cpu.prog, data, size);
            cpu.last_addr = (uint16_t)size;
            found_text = true;
        }

        if(name == ".data")
        {
            sec_index_data = (int)i;
            elf.data_begin = uint16_t(addr - ram_offset);
            elf.data_end = uint16_t(elf.data_begin + size);
        }

        if(name == ".bss")
        {
            sec_index_bss = (int)i;
            elf.bss_begin = uint16_t(addr - ram_offset);
            elf.bss_end = uint16_t(elf.data_begin + size);
        }

        if(name == ".symtab") sec_symtab = sec;
        if(name == ".strtab") sec_strtab = sec;
    }

    if(!found_text)
        return "ELF: No .text section";

    if(sec_symtab && sec_strtab && sec_index_data >= 0 && sec_index_bss >= 0)
    {
        char const* str = sec_strtab->get_data();
        int num_syms = (int)sec_symtab->get_size() / 16;
        Elf32_Sym const* ptr = (Elf32_Sym const*)sec_symtab->get_data();
        for(int i = 0; i < num_syms; ++i, ++ptr)
        {
            uint8_t sym_type = ptr->st_info & 0xf;
            uint8_t sym_bind = ptr->st_info >> 4;
            // limit to OBJECT or FUNC or NOTYPE
            if(sym_type > 2)
                continue;
            auto sec_index = ptr->st_shndx;
            bool text = false;
            if(sec_index == sec_index_text)
                text = true;
            else if(sec_index != sec_index_data && sec_index != sec_index_bss)
                continue;
            if(sym_type == 0 && !text)
                continue;
            char const* name = &str[ptr->st_name];
            elf_data_symbol_t sym;
            sym.name = demangle(name);
            auto addr = ptr->st_value;
            if(!text) addr -= ram_offset;
            sym.addr = (uint16_t)addr;
            sym.size = (uint16_t)ptr->st_size;
            sym.type = sym_type;
            sym.bind = sym_bind;
            try_insert_symbol(text ? elf.text_symbols : elf.data_symbols, sym);
        }
    }

    cpu.decode();

    // note object text symbols in disassembly
    for(auto const& kv : elf.text_symbols)
    {
        auto const& sym = kv.second;
        if(sym.type != 1) continue;
        auto addr = sym.addr;
        auto addr_end = addr + sym.size;
        while(addr < addr_end)
        {
            auto i = cpu.addr_to_disassembled_index(addr);
            cpu.disassembled_prog[i].object = true;
            addr += 2;
        }
    }

    a.elf.swap(elf_ptr);

    return nullptr;
}

char const* arduboy_t::load_file(char const* filename)
{
    std::string fname(filename);

    if(fname.substr(fname.size() - 4) == ".hex")
    {
        reset();
        return load_hex(cpu, fname);
    }

    if(fname.substr(fname.size() - 4) == ".elf")
    {
        reset();
        return load_elf(*this, fname);
    }

    return nullptr;
}

}
