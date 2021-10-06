// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_DEBUGGING_INTERNAL_LINK_H_
#define ABSL_DEBUGGING_INTERNAL_LINK_H_

#include <elf.h>
#include <link.h>  // for ElfW() macro if available.

#if defined(__FreeBSD__)

using ElfW_Addr = Elf_Addr;
using ElfW_Dyn = Elf_Dyn;
using ElfW_Ehdr = Elf_Ehdr;
using ElfW_Half = Elf_Half;
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
using ElfW_Half = ElfW(Half);
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

#endif  // ABSL_DEBUGGING_INTERNAL_LINK_H_
