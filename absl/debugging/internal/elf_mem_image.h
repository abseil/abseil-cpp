/*
 * Copyright 2017 The Abseil Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Allow dynamic symbol lookup for in-memory Elf images.

#ifndef ABSL_DEBUGGING_INTERNAL_ELF_MEM_IMAGE_H_
#define ABSL_DEBUGGING_INTERNAL_ELF_MEM_IMAGE_H_

// Including this will define the __GLIBC__ macro if glibc is being
// used.
#include <climits>
#include <cstdint>

#include "absl/base/config.h"

// Maybe one day we can rewrite this file not to require the elf
// symbol extensions in glibc, but for right now we need them.
#ifdef ABSL_HAVE_ELF_MEM_IMAGE
#error ABSL_HAVE_ELF_MEM_IMAGE cannot be directly set
#endif

#if defined(__ELF__) && !defined(__native_client__) && !defined(__asmjs__) && \
    !defined(__wasm__)
#define ABSL_HAVE_ELF_MEM_IMAGE 1
#endif

#ifdef ABSL_HAVE_ELF_MEM_IMAGE

#include <link.h>  // for ElfW

#if defined(__FreeBSD__)

using ElfW_Addr = Elf_Addr;
using ElfW_Dyn = Elf_Dyn;
using ElfW_Ehdr = Elf_Ehdr;
using ElfW_Off = Elf_Off;
using ElfW_Phdr = Elf_Phdr;
using ElfW_Shdr = Elf_Shdr;
using ElfW_Sym = Elf_Sym;
using ElfW_Verdaux = Elf_Verdaux;
using ElfW_Verdef = Elf_Verdef;
using ElfW_Versym = Elf_Versym;
using ElfW_Word = Elf_Word;

#if INTPTR_MAX == INT64_MAX
using ElfW_Xword = Elf64_Xword;
using ElfW_auxv_t = Elf64_Auxinfo;
#elif INTPTR_MAX == INT32_MAX
using ElfW_Xword = Elf32_Xword;
using ElfW_auxv_t = Elf32_Auxinfo;
#else
#error "Unsupported architecture."
#endif

#else

using ElfW_Addr = ElfW(Addr);
using ElfW_Dyn = ElfW(Dyn);
using ElfW_Ehdr = ElfW(Ehdr);
using ElfW_Off = ElfW(Off);
using ElfW_Phdr = ElfW(Phdr);
using ElfW_Shdr = ElfW(Shdr);
using ElfW_Sym = ElfW(Sym);
using ElfW_Verdaux = ElfW(Verdaux);
using ElfW_Verdef = ElfW(Verdef);
using ElfW_Versym = ElfW(Versym);
using ElfW_Word = ElfW(Word);
using ElfW_Xword = ElfW(Xword);
using ElfW_auxv_t = ElfW(auxv_t);

#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {

// An in-memory ELF image (may not exist on disk).
class ElfMemImage {
 private:
  // Sentinel: there could never be an elf image at &kInvalidBaseSentinel.
  static const int kInvalidBaseSentinel;

 public:
  // Sentinel: there could never be an elf image at this address.
  static constexpr const void *const kInvalidBase =
      static_cast<const void *>(&kInvalidBaseSentinel);

  // Information about a single vdso symbol.
  // All pointers are into .dynsym, .dynstr, or .text of the VDSO.
  // Do not free() them or modify through them.
  struct SymbolInfo {
    const char *name;        // E.g. "__vdso_getcpu"
    const char *version;     // E.g. "LINUX_2.6", could be ""
                             // for unversioned symbol.
    const void *address;     // Relocated symbol address.
    const ElfW_Sym *symbol;  // Symbol in the dynamic symbol table.
  };

  // Supports iteration over all dynamic symbols.
  class SymbolIterator {
   public:
    friend class ElfMemImage;
    const SymbolInfo *operator->() const;
    const SymbolInfo &operator*() const;
    SymbolIterator &operator++();
    bool operator!=(const SymbolIterator &rhs) const;
    bool operator==(const SymbolIterator &rhs) const;

   private:
    SymbolIterator(const void *const image, int index);
    void Update(int incr);
    SymbolInfo info_;
    int index_;
    const void *const image_;
  };

  explicit ElfMemImage(const void *base);
  void Init(const void *base);
  bool IsPresent() const { return ehdr_ != nullptr; }
  const ElfW_Phdr *GetPhdr(int index) const;
  const ElfW_Sym *GetDynsym(int index) const;
  const ElfW_Versym *GetVersym(int index) const;
  const ElfW_Verdef *GetVerdef(int index) const;
  const ElfW_Verdaux *GetVerdefAux(const ElfW_Verdef *verdef) const;
  const char *GetDynstr(ElfW_Word offset) const;
  const void *GetSymAddr(const ElfW_Sym *sym) const;
  const char *GetVerstr(ElfW_Word offset) const;
  int GetNumSymbols() const;

  SymbolIterator begin() const;
  SymbolIterator end() const;

  // Look up versioned dynamic symbol in the image.
  // Returns false if image is not present, or doesn't contain given
  // symbol/version/type combination.
  // If info_out is non-null, additional details are filled in.
  bool LookupSymbol(const char *name, const char *version, int symbol_type,
                    SymbolInfo *info_out) const;

  // Find info about symbol (if any) which overlaps given address.
  // Returns true if symbol was found; false if image isn't present
  // or doesn't have a symbol overlapping given address.
  // If info_out is non-null, additional details are filled in.
  bool LookupSymbolByAddress(const void *address, SymbolInfo *info_out) const;

 private:
  const ElfW_Ehdr *ehdr_;
  const ElfW_Sym *dynsym_;
  const ElfW_Versym *versym_;
  const ElfW_Verdef *verdef_;
  const ElfW_Word *hash_;
  const char *dynstr_;
  size_t strsize_;
  size_t verdefnum_;
  ElfW_Addr link_base_;  // Link-time base (p_vaddr of first PT_LOAD).
};

}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_HAVE_ELF_MEM_IMAGE

#endif  // ABSL_DEBUGGING_INTERNAL_ELF_MEM_IMAGE_H_
