/*
   Source for blc IdaPro plugin
   Copyright (c) 2019 Chris Eagle

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2 of the License, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
   more details.

   You should have received a copy of the GNU General Public License along with
   this program; if not, write to the Free Software Foundation, Inc., 59 Temple
   Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "ida_scope.hh"

#ifdef DEBUG_AST
#define dmsg(x, ...) msg(x, ...)
#else
#define dmsg(x, ...)
#endif

/// \param g is the Architecture and ida interface
ida_scope::ida_scope(ida_arch *g) : ScopeInternal("", g), ida(g) {}

ida_scope::~ida_scope(void) {
}

/// Determine if the given address should be sent to the ida client
/// at all, by checking the hole map and other factors.
/// If it passes, send the query to the client, process the result,
/// and update the cache. If a Symbol is ultimately recovered, return it.
/// \param addr is the address to potentially query
/// \return the matching Symbol or NULL if there is hole
Symbol *ida_scope::ida_query(const Address &addr) const {
   Symbol *sym = NULL;
   uint64_t ea = addr.getOffset();
   string symname;
   
   //It will be the case that scope == this, but scope will not be const
   ida_scope *scope = dynamic_cast<ida_scope*>(glb->symboltab->getGlobalScope());
/*
   if (scope == NULL) {
      dmsg("dynamic_cast<ida_scope*> from global failed\n");
      return NULL;
   }
   else if (scope == this) {
      dmsg("ida_scope::ida_query - I am the scope\n");
   }
   else {
      dmsg("ida_scope::ida_query - I am NOT the scope\n");
   }
*/   
   dmsg("ida_scope::ida_query - 0x%x\n", (uint32_t)addr.getOffset());
	if (addr.getSpace() == ida->getDefaultSpace()) {
      if (is_function_start(ea)) {
         get_func_name(symname, ea);
/*         
         uint64_t got;
         if (is_external_ref(ea, &got)) {
            Address refaddr(addr.getSpace(), got);
            dmsg("ida_scope::ida_query - %s is an external ref, with got entry at 0x%zx\n", symname.c_str(), got);
            sym = scope->addExternalRef(addr, refaddr, symname);
         }
         else {
*/
         dmsg("ida_scope::ida_query - creating FunctionSymbol for 0x%zx(%s)\n", ea, symname.c_str());
         sym = new FunctionSymbol(scope, symname, glb->min_funcsymbol_size);
//         dmsg("ida_scope::ida_query - calling addSymbolInternal (%d)\n", sym == NULL);
         scope->addSymbolInternal(sym);
         // Map symbol to base address of function
         // there is no limit on the applicability of this map within scope
//         dmsg("ida_scope::ida_query - calling addMapPoint\n");
         scope->addMapPoint(sym, addr, Address());
//         }
      }
      else if (is_code_label(ea, symname)) {
         sym = new LabSymbol(scope, symname);
         scope->addSymbolInternal(sym);
         scope->addMapPoint(sym, addr, Address());
      }
	}

   return sym;
}

SymbolEntry *ida_scope::findAddr(const Address &addr,
                                 const Address &usepoint) const {
   SymbolEntry *entry;
   dmsg("ida_scope::findAddr - 0x%x\n", (uint32_t)addr.getOffset());
   entry = ScopeInternal::findAddr(addr, usepoint);
   if (entry == NULL) { // Didn't find symbol
      entry = findContainer(addr, 1, Address());
      if (entry != NULL) {
         return NULL;   // Address is already queried, but symbol doesn't start at our address
      }
      Symbol *sym = ida_query(addr); // Query server
      if (sym != NULL) {
         entry = sym->getMapEntry(addr);
      }
      // entry may be null for certain queries, ghidra may return symbol of size <8 with
      // address equal to START of function, even though the query was for an address INTERNAL to the function
   }
   if ((entry != NULL) && (entry->getAddr() == addr)) {
      return entry;
   }
   return NULL;
}

SymbolEntry *ida_scope::findContainer(const Address &addr, int4 size,
                                      const Address &usepoint) const {
   SymbolEntry *entry;
   dmsg("ida_scope::findContainer(addr:0x%zx, size:%d, usepoint:0x%zx)\n", addr.getOffset(), size, usepoint.getOffset());
   
   //Observed AddrSpace names have been: 'ram', 'register', 'join', 'unique', ''
   AddrSpace *asp = addr.getSpace();
   AddrSpace *usp = usepoint.getSpace();
//   dmsg("   addr space ('%s'), usepoint space ('%s')\n", asp ? asp->getName().c_str() : "", usp ? usp->getName().c_str() : "");
   entry = ScopeInternal::findClosestFit(addr, size, usepoint);
   if (entry == NULL) {
//      dmsg("ida_scope::findContainer returned NULL, call ida_query\n");
      Symbol *sym = ida_query(addr);
      if (sym != NULL) {
         entry = sym->getMapEntry(addr);
      }
      // entry may be null for certain queries, ghidra may return symbol of size <8 with
      // address equal to START of function, even though the query was for an address INTERNAL to the function
   }
   if (entry != NULL) {
      // Entry contains addr, does it contain addr+size
      uintb last = entry->getAddr().getOffset() + entry->getSize() -1;
      if (last >= addr.getOffset() + size - 1) {
         return entry;
      }
   }
   return NULL;
}

ExternRefSymbol *ida_scope::findExternalRef(const Address &addr) const {
   ExternRefSymbol *sym;
   dmsg("ida_scope::findExternalRef - 0x%x\n", (uint32_t)addr.getOffset());
   sym = ScopeInternal::findExternalRef(addr);
   if (sym == NULL) {
      // Check if this address has already been queried,
      // (returning a symbol other than an external ref symbol)
      SymbolEntry *entry;
      entry = findContainer(addr, 1, Address());
      if (entry == NULL) {
         sym = dynamic_cast<ExternRefSymbol *>(ida_query(addr));
      }
   }
   return sym;
}

Funcdata *ida_scope::findFunction(const Address &addr) const {
   dmsg("ida_scope::findFunction - 0x%x\n", (uint32_t)addr.getOffset());
   Funcdata *fd = ScopeInternal::findFunction(addr);
   if (fd == NULL) {
      FunctionSymbol *sym;
      sym = dynamic_cast<FunctionSymbol *>(ida_query(addr));
      if (sym != NULL) {
         fd = sym->getFunction();
      }
   }
   return fd;
}

LabSymbol *ida_scope::findCodeLabel(const Address &addr) const {
   SymbolEntry *overlap = queryContainer(addr, 1, addr);
   if (overlap) {
      Symbol *sym = overlap->getSymbol();
      LabSymbol *lsym = dynamic_cast<LabSymbol*>(sym);
      if (lsym) {
         return lsym;
      }
   }
   LabSymbol *sym;
   dmsg("ida_scope::findCodeLabel - 0x%x\n", (uint32_t)addr.getOffset());
   sym = ScopeInternal::findCodeLabel(addr);
   if (sym == NULL) {
      // Check if this address has already been queried,
      // (returning a symbol other than a code label)
      SymbolEntry *entry;
      entry = findAddr(addr, Address());
      if (entry == NULL) {
         string symname;
         get_name(symname, addr.getOffset(), 0);
         if (!symname.empty()) {
            sym = glb->symboltab->getGlobalScope()->addCodeLabel(addr, symname);
         }
      }
   }
   return sym;
}

Funcdata *ida_scope::resolveExternalRefFunction(ExternRefSymbol *sym) const {
   Funcdata *fd = NULL;
   const Scope *basescope = ida->symboltab->mapScope(this, sym->getRefAddr(), Address());
   // Truncate search at this scope, we don't want
   // the usual remote_query if the function isn't in cache
   // this won't recover external functions, but will just
   // return the externalref symbol again
   stackFunction(basescope, this, sym->getRefAddr(), &fd);
   dmsg("ida_scope::resolveExternalRefFunction - %d\n", fd == NULL);
   if (fd == NULL) {
      fd = findFunction(sym->getRefAddr());
   }
   return fd;
}

SymbolEntry *ida_scope::addSymbol(const string &name, Datatype *ct,
                                  const Address &addr, const Address &usepoint) {
   // We do not inform Ghidra of the new symbol, we just
   // stick it in the cache.  This allows the mapglobals action
   // to build global variables that Ghidra knows nothing about
   dmsg("ida_scope::addSymbol - %s\n", name.c_str());
   return ScopeInternal::addSymbol(name, ct, addr, usepoint);
}

string ida_scope::buildVariableName(const Address &addr,
                                 const Address &pc,
                                 Datatype *ct, int4 &index, uint4 flags) const {
   string name;
   if (is_named_addr(addr.getOffset(), name)) {
      return name;
   }
   name = ScopeInternal::buildVariableName(addr, pc, ct, index, flags);
   dmsg("ida_scope::buildVariableName - 0x%x -> %s\n", (uint32_t)addr.getOffset(), name.c_str());
   return name;
}

string ida_scope::buildUndefinedName(void) const {
   dmsg("ida_scope::buildUndefinedName\n");
   return ScopeInternal::buildUndefinedName();
}

void ida_scope::findByName(const string &name, vector<Symbol *> &res) const {
   dmsg("ida_scope::findByName - %s\n", name.c_str());
   return ScopeInternal::findByName(name, res);
}
