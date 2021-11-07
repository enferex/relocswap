#include <elf.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

[[noreturn]] static void errExit(std::string msg) {
  std::cerr << msg << std::endl;
  exit(EXIT_FAILURE);
}

struct Elf {
  virtual void dumpRelocs() const = 0;
  virtual void swapN(std::ofstream &output, int n) const = 0;
  virtual void parse(std::ifstream &fp) = 0;
};

template <class EhdrT, class ShdrT, class RelT, class RelaT, class SymT>
class ElfT : public Elf {
  // The uint64_t in each pair represents the offset in the file for that reloc.
  std::vector<std::pair<uint64_t, RelT>> relocs;  // Relocs without addends.
  std::vector<std::pair<uint64_t, RelaT>> relocsAddends;  // Relocs + addends.
  std::vector<SymT> symbolTable;
  std::vector<char> stringTable;
  std::vector<char> sectionStringTable;

  void addRels(std::ifstream &fp, const ShdrT &shdr) {
    assert(fp && "Invalid input stream.");
    assert((shdr.sh_type == SHT_REL || shdr.sh_type == SHT_RELA) &&
           "Invalid section header.");
    const auto pos = fp.tellg();

    fp.seekg(shdr.sh_offset);

    if (shdr.sh_type == SHT_REL) {
      for (size_t i = 0; i < shdr.sh_size / shdr.sh_entsize; ++i) {
        RelT rel;
        const auto relOffset = fp.tellg();
        fp.read((char *)&rel, sizeof(RelT));
        if (!fp) errExit("Failed to read relocation.");
        relocs.emplace_back(relOffset, rel);
      }
    } else {
      for (size_t i = 0; i < shdr.sh_size / shdr.sh_entsize; ++i) {
        RelaT relA;
        const auto relAOffset = fp.tellg();
        fp.read((char *)&relA, sizeof(RelaT));
        if (!fp) errExit("Failed to read relocation.");
        relocsAddends.emplace_back(relAOffset, relA);
      }
    }

    fp.seekg(pos);
  }

  void addSectionStringTable(std::ifstream &fp, const EhdrT &hdr) {
    assert(fp && "Invalid input stream.");
    const auto pos = fp.tellg();

    fp.seekg(hdr.e_shoff + (hdr.e_shstrndx * hdr.e_shentsize));
    ShdrT shdr;
    fp.read((char *)&shdr, hdr.e_shentsize);
    if (!fp) errExit("Failed to read the section string table header.");

    fp.seekg(shdr.sh_offset);
    sectionStringTable.resize(shdr.sh_size);
    fp.read(sectionStringTable.data(), shdr.sh_size);
    if (!fp) errExit("Failed to read the section string table.");

    fp.seekg(pos);
  }

  void addStringTable(std::ifstream &fp, const ShdrT &shdr) {
    assert(shdr.sh_type == SHT_STRTAB && "Invalid section header.");
    assert(fp && "Invalid input stream.");
    const auto pos = fp.tellg();
    fp.seekg(shdr.sh_offset);
    stringTable.resize(shdr.sh_size);
    fp.read(stringTable.data(), shdr.sh_size);
    if (!fp) errExit("Failed to read string table.");
    fp.seekg(pos);
  }

  void addSymbolTable(std::ifstream &fp, const ShdrT &shdr) {
    assert(fp && "Invalid input stream.");
    assert(shdr.sh_type == SHT_DYNSYM && "Invalid section header.");
    const auto pos = fp.tellg();
    const int n = shdr.sh_size / shdr.sh_entsize;
    fp.seekg(shdr.sh_offset);
    for (int i = 0; i < n; ++i) {
      SymT sym;
      fp.read((char *)&sym, shdr.sh_entsize);
      if (!fp) errExit("Failed to read symbol table entry.");
      symbolTable.push_back(sym);
    }
    fp.seekg(pos);
  }

  std::string relocSymName(const uint64_t rInfo) const {
    const uint64_t symIdx = (sizeof(RelT) == sizeof(Elf32_Rel))
                                ? ELF32_R_SYM(rInfo)
                                : ELF64_R_SYM(rInfo);
    if (symIdx < symbolTable.size()) {
      const SymT *sym = &symbolTable[symIdx];
      if (sym->st_name < stringTable.size()) return &stringTable[sym->st_name];
    }
    return "N/A";
  }

  void dumpReloc(const RelT &rel) const {
    std::cout << std::hex << rel.r_offset << ", 0x" << rel.r_info
              << relocSymName(rel.r_info) << std::dec;
  }

  void dumpReloc(const RelaT &rel) const {
    std::cout << std::hex << rel.r_offset << ", 0x" << rel.r_info << ", 0x"
              << rel.r_addend << ", " << relocSymName(rel.r_info) << std::dec;
  }

 public:
  void dumpRelocs() const override {
    if (!relocs.empty()) {
      std::cout << "Dynamic relocs (" << relocs.size() << ')' << std::endl;
      int i = 0;
      std::cout << "ELFOffset, RelocOffset, RelocInfo, SymName" << std::endl;
      std::for_each(relocs.begin(), relocs.end(),
                    [&](const std::pair<uint64_t, RelT> &pr) {
                      std::cout << "  " << i++ << ") 0x" << std::hex << pr.first
                                << ", " << std::dec;
                      dumpReloc(pr.second);
                      std::cout << std::endl;
                    });
    }

    if (!relocsAddends.empty()) {
      std::cout << "Dynamic or PLT relocs with addends ("
                << relocsAddends.size() << ')' << std::endl;
      int i = 0;
      std::cout << "ELFOffset, RelocOffset, RelocInfo, RelocAddend, SymName"
                << std::endl;
      std::for_each(relocsAddends.begin(), relocsAddends.end(),
                    [&](const std::pair<uint64_t, RelaT> &pr) {
                      std::cout << "  " << i++ << ") 0x" << std::hex << pr.first
                                << ", " << std::dec;
                      dumpReloc(pr.second);
                      std::cout << std::endl;
                    });
    }
  }

  void swapN(std::ofstream &output, int n) const override {
    assert(n > 0 && "Invalid input.");
    // Swap 'n' pseudo-random relocs.
    for (int i = 0; i < n; ++i) {
      bool useRelocs = false;
      if (!relocs.empty() && !relocsAddends.empty())
        useRelocs = rand() % 2;
      else if (!relocs.empty())
        useRelocs = true;
      else if (!relocsAddends.empty())
        useRelocs = false;
      else  // Both sets are empty.
        return;

      // Choose what reloc collection to use.
      if (useRelocs) {  // Swap 2 relocs.
        const size_t aIdx = rand() % relocs.size();
        const size_t bIdx = rand() % relocs.size();
        RelT a = relocs[aIdx].second;
        RelT b = relocs[bIdx].second;

        // Swap a with b. (Do not modify r_info).
        std::swap(a.r_offset, b.r_offset);
        output.seekp(relocs[aIdx].first);
        output.write((char *)&a, sizeof(RelT));
        output.seekp(relocs[bIdx].first);
        output.write((char *)&b, sizeof(RelT));
        std::cout << "Swapped reloc " << aIdx << " with " << bIdx << std::endl;
      } else {  // Else, swap 2 relocs with addends.
        const size_t aIdx = rand() % relocsAddends.size();
        const size_t bIdx = rand() % relocsAddends.size();
        RelaT a = relocsAddends[aIdx].second;
        RelaT b = relocsAddends[bIdx].second;

        // Swap a with b. (Do not modify r_info).
        std::swap(a.r_offset, b.r_offset);
        std::swap(a.r_addend, b.r_addend);
        output.seekp(relocsAddends[aIdx].first);
        output.write((char *)&a, sizeof(RelaT));  // a = b
        output.seekp(relocsAddends[bIdx].first);
        output.write((char *)&b, sizeof(RelaT));  // b = a
        std::cout << "Swapped reloc with addend " << aIdx << " with " << bIdx
                  << std::endl;
      }
    }
  }

  bool isSection(size_t idx, const std::string &name) const {
    return (idx < sectionStringTable.size()) &&
           (name == &sectionStringTable[idx]);
  }

  void parse(std::ifstream &fp) override {
    assert(fp && "Invalid fp state.");

    // Read the header.
    EhdrT hdr;
    fp.read((char *)&hdr, sizeof(EhdrT));
    if (!fp) errExit("Failed to read ELF header.");

    // Read the section string table.
    addSectionStringTable(fp, hdr);

    // Read the sections.
    fp.seekg(hdr.e_shoff);
    for (size_t i = 0; i < hdr.e_shnum; ++i) {
      ShdrT shdr;
      fp.read((char *)&shdr, sizeof(ShdrT));
      if (!fp) errExit("Failed to read section header.");

      // Read in specific sections.
      if ((shdr.sh_type == SHT_REL || shdr.sh_type == SHT_RELA) &&
          (isSection(shdr.sh_name, ".rel.dyn") ||
           isSection(shdr.sh_name, ".rela.dyn") ||
           isSection(shdr.sh_name, ".rela.plt")))
        addRels(fp, shdr);
      else if (shdr.sh_type == SHT_STRTAB && isSection(shdr.sh_name, ".dynstr"))
        addStringTable(fp, shdr);
      else if (shdr.sh_type == SHT_DYNSYM && isSection(shdr.sh_name, ".dynsym"))
        addSymbolTable(fp, shdr);
    }
  }
};

using Elf32 = ElfT<Elf32_Ehdr, Elf32_Shdr, Elf32_Rel, Elf32_Rela, Elf32_Sym>;
using Elf64 = ElfT<Elf64_Ehdr, Elf64_Shdr, Elf64_Rel, Elf64_Rela, Elf64_Sym>;

static void usage(const char *execname) {
  std::cout
      << "Usage: " << execname << " [-h] [-d] [-n NUM] [-o OUTFILE] FILE"
      << std::endl
      << "  -h:         This help message." << std::endl
      << "  -d:         Dump relocs." << std::endl
      << "  -n NUM:     Swap 'num' number of relocs." << std::endl
      << "  -o OUTFILE: Output file (required to shuffle the relocs in FILE)."
      << std::endl
      << "  FILE:       Input ELF file, if -o is specified the relocs in FILE "
         "will "
         "be shuffled and output to the file specified in OUTFILE."
      << std::endl;
}

static Elf *parseElf(std::ifstream &fp) {
  assert(fp && "Invalid input.");
  char buf[EI_NIDENT] = {0};
  fp.read(buf, EI_NIDENT);
  if (!fp || (memcmp(buf, ELFMAG, SELFMAG) != 0) ||
      (buf[EI_CLASS] != ELFCLASS32 && buf[EI_CLASS] != ELFCLASS64))
    errExit("Failed to read ELF header.");
  fp.seekg(0);

  Elf *elf;
  if (buf[EI_CLASS] == ELFCLASS32)
    elf = new Elf32();
  else
    elf = new Elf64();
  elf->parse(fp);

  return elf;
}

int main(int argc, char **argv) {
  int opt;
  int nSwaps = 1;
  bool doDump = false;
  const char *outFname = nullptr;
  srand(time(NULL));
  while ((opt = getopt(argc, argv, "dhn:o:")) != -1) {
    switch (opt) {
      case 'd':
        doDump = true;
        break;
      case 'h':
        usage(argv[0]);
        return 0;
      case 'n':
        nSwaps = std::atoi(optarg);
        break;
      case 'o':
        outFname = optarg;
        break;
      default:
        errExit("Error: Unrecognized argument, see help (-h).");
    }
  }

  if (nSwaps < 0) nSwaps = 0;

  if (optind + 1 != argc) {
    std::cerr << "Missing filename argument (see -h for help)" << std::endl;
    return 0;
  }
  const char *fname = argv[optind];
  std::ifstream fp(fname);
  if (!fp) errExit(std::string("Failed to open input file ") + fname);

  auto elf = parseElf(fp);
  assert(elf && "Failed to parse ELF file.");
  if (doDump) elf->dumpRelocs();
  if (outFname && nSwaps > 0) {
    std::ofstream outFile(outFname, std::ofstream::trunc);
    if (!outFile) errExit(std::string("Failed to open/truncate ") + outFname);

    // Copy input to output.
    if (!std::filesystem::copy_file(
            fname, outFname, std::filesystem::copy_options::overwrite_existing))
      errExit(std::string("Failed to replicate ") + fname);

    // Swap 'n' relocs.
    elf->swapN(outFile, nSwaps);
  }

  return 0;
}
