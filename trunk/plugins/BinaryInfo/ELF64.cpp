/*
Copyright (C) 2006 - 2011 Evan Teran
                          eteran@alum.rit.edu

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "ELF64.h"
#include "Util.h"
#include "Debugger.h"
#include "IDebuggerCore.h"
#include "ByteShiftArray.h"

#include <QVector>
#include <QFile>
#include <QDebug>
#include <cstring>

#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD)
#include <link.h>
#endif

//------------------------------------------------------------------------------
// Name: ELF64(const IRegion::pointer &region)
// Desc: constructor
//------------------------------------------------------------------------------
ELF64::ELF64(const IRegion::pointer &region) : IBinary(region), header_(0) {
}

//------------------------------------------------------------------------------
// Name: ~ELF64()
// Desc: deconstructor
//------------------------------------------------------------------------------
ELF64::~ELF64() {
	delete header_;
}

//------------------------------------------------------------------------------
// Name: validate_header()
// Desc: returns true if this file matches this particular info class
//------------------------------------------------------------------------------
bool ELF64::validate_header() {
	read_header();
	if(header_) {
	#if defined(Q_OS_UNIX)
		if(std::memcmp(header_->e_ident, ELFMAG, SELFMAG) == 0) {
			return header_->e_ident[EI_CLASS] == ELFCLASS64;
		}
	#endif
	}
	return false;
}

//------------------------------------------------------------------------------
// Name: native
// Desc: returns true if this binary is native to the arch edb was built for
//------------------------------------------------------------------------------
bool ELF64::native() const {
#ifdef EDB_X86_64
	return true;
#else
	return false;
#endif
}

//------------------------------------------------------------------------------
// Name: entry_point()
// Desc: returns the entry point if any of the binary
//------------------------------------------------------------------------------
edb::address_t ELF64::entry_point() {
	read_header();
	if(header_) {
	#if defined(Q_OS_UNIX)
		return header_->e_entry;
	#else
		return 0;
	#endif
	}
	return 0;
}

//------------------------------------------------------------------------------
// Name: read_header()
// Desc: reads in enough of the file to get the header
//------------------------------------------------------------------------------
void ELF64::read_header() {
#if defined(Q_OS_UNIX)
	if(header_ == 0) {
		if(region_) {
			header_ = new elf64_header;
	
			if(!edb::v1::debugger_core->read_bytes(region_->start(), header_, sizeof(elf64_header))) {
				std::memset(header_, 0, sizeof(elf64_header));
			}
		}
	}
#endif
}

//------------------------------------------------------------------------------
// Name: header_size()
// Desc: returns the number of bytes in this executable's header
//------------------------------------------------------------------------------
size_t ELF64::header_size() const {
#if defined(Q_OS_UNIX)
	return sizeof(elf64_header);
#else
	return 0;
#endif
}

//------------------------------------------------------------------------------
// Name: debug_pointer()
// Desc: attempts to locate the ELF debug pointer in the target process and
//       returns it, 0 of not found
//------------------------------------------------------------------------------
edb::address_t ELF64::debug_pointer() {
	read_header();
	if(region_) {
	#if defined(Q_OS_UNIX)
		const edb::address_t section_offset = header_->e_phoff;
		const std::size_t count             = header_->e_phnum;

		elf64_phdr section_header;
		for(std::size_t i = 0; i < count; ++i) {
			if(edb::v1::debugger_core->read_bytes(region_->start() + (section_offset + i * sizeof(elf64_phdr)), &section_header, sizeof(elf64_phdr))) {
				if(section_header.p_type == PT_DYNAMIC) {
					try {
						QVector<quint8> buf(section_header.p_memsz);
						if(edb::v1::debugger_core->read_bytes(section_header.p_vaddr, &buf[0], section_header.p_memsz)) {
							const elf64_dyn *dynamic = reinterpret_cast<elf64_dyn *>(&buf[0]);
							while(dynamic->d_tag != DT_NULL) {
								if(dynamic->d_tag == DT_DEBUG) {
									return dynamic->d_un.d_val;
								}
								++dynamic;
							}
						}
					} catch(const std::bad_alloc &) {
						qDebug() << "[Elf64::debug_pointer] no more memory";
						return 0;
					}
				}
			}
		}
	#endif
	}
	return 0;
}

//------------------------------------------------------------------------------
// Name: calculate_main
// Desc: uses a heuristic to locate "main"
//------------------------------------------------------------------------------
edb::address_t ELF64::calculate_main() {

	edb::address_t entry_point = this->entry_point();

	ByteShiftArray ba(13);
	for(int i = 0; i < 50; ++i) {
		quint8 byte;
		if(edb::v1::debugger_core->read_bytes(entry_point + i, &byte, sizeof(byte))) {
			ba << byte;

			// beginning of a call preceeded by a 64-bit mov and followed by a hlt
			if(ba[0] == 0x48 && ba[1] == 0xc7 && ba[7] == 0xe8 && ba[12] == 0xf4) {
				// Seems that this 64-bit mov still has a 32-bit immediate
				const edb::address_t address = *reinterpret_cast<const edb::address_t *>(ba.data() + 3) & 0xffffffff;
				// TODO: make sure that this address resides in an executable region
				qDebug("No main symbol found, calculated it to be " EDB_FMT_PTR " using heuristic", address);
				return address;
			}
		} else {
			break;
		}
	}
	
	return 0;
}