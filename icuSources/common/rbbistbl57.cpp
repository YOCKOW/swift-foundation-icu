// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
***************************************************************************
*   Copyright (C) 2002-2014 International Business Machines Corporation
*   and others. All rights reserved.
***************************************************************************
*
**********************************************************************
*   Legacy version from ICU 57, only for use by Apple RuleBasedTokenizer
*   originally added per rdar://37249396 Add ICU 57 version of RBBI classes,
*   urbtok57 interfaces for access via RBT, and better tests
**********************************************************************
*/

#include <_foundation_unicode/utypes.h>

#if !UCONFIG_NO_BREAK_ITERATION

#include <_foundation_unicode/unistr.h>
#include <_foundation_unicode/uniset.h>
#include <_foundation_unicode/uchar.h>
#include <_foundation_unicode/parsepos.h>

#include "umutex.h"

#include "rbbirb57.h"
#include "rbbinode.h"


//
//  RBBISymbolTableEntry_deleter    Used by the UHashTable to delete the contents
//                                  when the hash table is deleted.
//
U_CDECL_BEGIN
static void U_CALLCONV RBBISymbolTableEntry_deleter(void *p) {
    icu::RBBISymbolTableEntry *px = (icu::RBBISymbolTableEntry *)p;
    delete px;
}
U_CDECL_END



U_NAMESPACE_BEGIN

RBBISymbolTable57::RBBISymbolTable57(RBBIRuleScanner57 *rs, const UnicodeString &rules, UErrorCode &status)
    :fRules(rules), fRuleScanner(rs), ffffString(UChar(0xffff))
{
    fHashTable       = NULL;
    fCachedSetLookup = NULL;
    
    fHashTable = uhash_open(uhash_hashUnicodeString, uhash_compareUnicodeString, NULL, &status);
    // uhash_open checks status
    if (U_FAILURE(status)) {
        return;
    }
    uhash_setValueDeleter(fHashTable, RBBISymbolTableEntry_deleter);
}



RBBISymbolTable57::~RBBISymbolTable57()
{
    uhash_close(fHashTable);
}


//
//  RBBISymbolTable57::lookup       This function from the abstract symbol table inteface
//                                looks up a variable name and returns a UnicodeString
//                                containing the substitution text.
//
//                                The variable name does NOT include the leading $.
//
const UnicodeString  *RBBISymbolTable57::lookup(const UnicodeString& s) const
{
    RBBISymbolTableEntry  *el;
    RBBINode              *varRefNode;
    RBBINode              *exprNode;
    RBBINode              *usetNode;
    const UnicodeString   *retString;
    RBBISymbolTable57       *This = (RBBISymbolTable57 *)this;   // cast off const

    el = (RBBISymbolTableEntry *)uhash_get(fHashTable, &s);
    if (el == NULL) {
        return NULL;
    }

    varRefNode = el->val;
    exprNode   = varRefNode->fLeftChild;     // Root node of expression for variable
    if (exprNode->fType == RBBINode::setRef) {
        // The $variable refers to a single UnicodeSet
        //   return the ffffString, which will subsequently be interpreted as a
        //   stand-in character for the set by RBBISymbolTable57::lookupMatcher()
        usetNode = exprNode->fLeftChild;
        This->fCachedSetLookup = usetNode->fInputSet;
        retString = &ffffString;
    }
    else
    {
        // The variable refers to something other than just a set.
        // return the original source string for the expression
        retString = &exprNode->fText;
        This->fCachedSetLookup = NULL;
    }
    return retString;
}



//
//  RBBISymbolTable57::lookupMatcher   This function from the abstract symbol table
//                                   interface maps a single stand-in character to a
//                                   pointer to a Unicode Set.   The Unicode Set code uses this
//                                   mechanism to get all references to the same $variable
//                                   name to refer to a single common Unicode Set instance.
//
//    This implementation cheats a little, and does not maintain a map of stand-in chars
//    to sets.  Instead, it takes advantage of the fact that  the UnicodeSet
//    constructor will always call this function right after calling lookup(),
//    and we just need to remember what set to return between these two calls.
const UnicodeFunctor *RBBISymbolTable57::lookupMatcher(UChar32 ch) const
{
    UnicodeSet *retVal = NULL;
    RBBISymbolTable57 *This = (RBBISymbolTable57 *)this;   // cast off const
    if (ch == 0xffff) {
        retVal = fCachedSetLookup;
        This->fCachedSetLookup = 0;
    }
    return retVal;
}

//
// RBBISymbolTable57::parseReference   This function from the abstract symbol table interface
//                                   looks for a $variable name in the source text.
//                                   It does not look it up, only scans for it.
//                                   It is used by the UnicodeSet parser.
//
//                                   This implementation is lifted pretty much verbatim
//                                   from the rules based transliterator implementation.
//                                   I didn't see an obvious way of sharing it.
//
UnicodeString   RBBISymbolTable57::parseReference(const UnicodeString& text,
                                                ParsePosition& pos, int32_t limit) const
{
    int32_t start = pos.getIndex();
    int32_t i = start;
    UnicodeString result;
    while (i < limit) {
        UChar c = text.charAt(i);
        if ((i==start && !u_isIDStart(c)) || !u_isIDPart(c)) {
            break;
        }
        ++i;
    }
    if (i == start) { // No valid name chars
        return result; // Indicate failure with empty string
    }
    pos.setIndex(i);
    text.extractBetween(start, i, result);
    return result;
}



//
// RBBISymbolTable57::lookupNode      Given a key (a variable name), return the
//                                  corresponding RBBI Node.  If there is no entry
//                                  in the table for this name, return NULL.
//
RBBINode       *RBBISymbolTable57::lookupNode(const UnicodeString &key) const{

    RBBINode             *retNode = NULL;
    RBBISymbolTableEntry *el;

    el = (RBBISymbolTableEntry *)uhash_get(fHashTable, &key);
    if (el != NULL) {
        retNode = el->val;
    }
    return retNode;
}


//
//    RBBISymbolTable57::addEntry     Add a new entry to the symbol table.
//                                  Indicate an error if the name already exists -
//                                    this will only occur in the case of duplicate
//                                    variable assignments.
//
void            RBBISymbolTable57::addEntry  (const UnicodeString &key, RBBINode *val, UErrorCode &err) {
    RBBISymbolTableEntry *e;
    /* test for buffer overflows */
    if (U_FAILURE(err)) {
        return;
    }
    e = (RBBISymbolTableEntry *)uhash_get(fHashTable, &key);
    if (e != NULL) {
        err = U_BRK_VARIABLE_REDFINITION;
        return;
    }

    e = new RBBISymbolTableEntry;
    if (e == NULL) {
        err = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    e->key = key;
    e->val = val;
    uhash_put( fHashTable, &e->key, e, &err);
}

// RBBISymbolTableEntry::RBBISymbolTableEntry() - from standard rbbistbl.cpp
// RBBISymbolTableEntry::~RBBISymbolTableEntry()  - from standard rbbistbl.cpp


//
//  RBBISymbolTable57::print    Debugging function, dump out the symbol table contents.
//
#ifdef RBBI_DEBUG
void RBBISymbolTable57::rbbiSymtablePrint() const {
    RBBIDebugPrintf("Variable Definitions\n"
           "Name               Node Val     String Val\n"
           "----------------------------------------------------------------------\n");

    int32_t pos = UHASH_FIRST;
    const UHashElement  *e   = NULL;
    for (;;) {
        e = uhash_nextElement(fHashTable,  &pos);
        if (e == NULL ) {
            break;
        }
        RBBISymbolTableEntry  *s   = (RBBISymbolTableEntry *)e->value.pointer;

        RBBI_DEBUG_printUnicodeString(s->key, 15);
        RBBIDebugPrintf("   %8p   ", (void *)s->val);
        RBBI_DEBUG_printUnicodeString(s->val->fLeftChild->fText);
        RBBIDebugPrintf("\n");
    }

    RBBIDebugPrintf("\nParsed Variable Definitions\n");
    pos = -1;
    for (;;) {
        e = uhash_nextElement(fHashTable,  &pos);
        if (e == NULL ) {
            break;
        }
        RBBISymbolTableEntry  *s   = (RBBISymbolTableEntry *)e->value.pointer;
        RBBI_DEBUG_printUnicodeString(s->key);
        s->val->fLeftChild->printTree(true);
        RBBIDebugPrintf("\n");
    }
}
#endif





U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_BREAK_ITERATION */
