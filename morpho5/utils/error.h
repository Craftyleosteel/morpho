/** @file error.h
 *  @author T J Atherton
 *
 *  @brief Morpho error handling
*/

#ifndef error_h
#define error_h

#include <stdarg.h>
#include "build.h"
#include "varray.h"

/* **********************************************************************
 * Error type definitions
 * ********************************************************************** */

/** @brief Identifier for errors. */
typedef char * errorid;

/* --------------------------------
 * Categories of error
 * -------------------------------- */

/** Identifies the category of error that has occurred */
typedef enum {
    ERROR_NONE, /** No error. */
    
/* Execution should continue */
    ERROR_INFO, /** Informational messages generated. */
    ERROR_WARNING, /** Warnings generated. */
    
/* Execution should not continue */
    ERROR_HALT, /** or has occured. Should return to the user as fast as possible. */
    ERROR_EXIT, /** Unrecoverable error has occured; Morpho will exit quickly. */
    
/* Other kinds of error */
    ERROR_LEX, /** or generated by the lexer */
    ERROR_PARSE, /** or generated by the parser */
    ERROR_COMPILE /** or generated by the compiler */
} errorcategory;

/* --------------------------------
 * Macros to scrutinize these types
 * -------------------------------- */

/** Checks if execution should continue after the error */
#define ERROR_SHOULDCONTINUE(cat) (cat < ERROR_HALT)

/** Did an operation succeed without errors? */
#define ERROR_SUCCEEDED(err) ((err).cat == ERROR_NONE)

/** Is this a runtime error? */
#define ERROR_ISRUNTIMEERROR(err) ((err).cat <= ERROR_EXIT)

/* ----------------------------------------------------
 * or struct, containing information about the error
 * ---------------------------------------------------- */

/** @brief A type used by public-facing morpho functions */
typedef errorcategory morphoerror;

/** @brief A static container for error messages. */
typedef struct {
    errorcategory cat;
    errorid id;
    int line, posn;
    char msg[MORPHO_ERRORSTRINGSIZE];
} error;

/** Set line and posn to this value if they can't be determined */
#define ERROR_POSNUNIDENTIFIABLE -1

/* --------------------------------
 * or definitions
 * -------------------------------- */

/** @brief Definition of an error message. */
typedef struct {
    errorcategory cat;
    char *msg;
} errordefinition;

/** A varray of errordefinitions */
DECLARE_VARRAY(errordefinition, errordefinition)

/* **********************************************************************
 * or related macros
 * ********************************************************************** */

/** Macro to place in code that should be unreachable */
#ifdef MORPHO_DEBUG
void morpho_unreachable(const char *explanation);
#define UNREACHABLE(x) morpho_unreachable(x)
#else
#define UNREACHABLE(x)
#endif

/* --------------------------------
 * Exit codes
 * -------------------------------- */

/** Exit codes from BSD standard */
#define BSD_EX_SOFTWARE 70

/* **********************************************************************
 * General error codes
 * ********************************************************************** */

#define ERROR_ALLOCATIONFAILED            "Alloc"
#define ERROR_ALLOCATIONFAILED_MSG        "Memory allocation failed."

#define ERROR_INTERNALERROR               "Intrnl"
#define ERROR_INTERNALERROR_MSG           "Internal error (contact developer)."

/* **********************************************************************
* VM error messages
* ********************************************************************** */

#define VM_INVLDOP                        "InvldOp"
#define VM_INVLDOP_MSG                    "Invalid operands."

#define VM_CNCTFLD                        "CnctFld"
#define VM_CNCTFLD_MSG                    "Concatenation failed."

#define VM_UNCALLABLE                     "Uncallable"
#define VM_UNCALLABLE_MSG                 "Can only call a function or method."

#define VM_GLBLRTRN                       "GlblRtrn"
#define VM_GLBLRTRN_MSG                   "Return encountered outside a function or method."

#define VM_INSTANTIATEFAILED              "InstFail"
#define VM_INSTANTIATEFAILED_MSG          "Could not instantiate object."

#define VM_NOTANOBJECT                    "NotAnObj"
#define VM_NOTANOBJECT_MSG                "Not an object."

#define VM_OBJECTLACKSPROPERTY            "ObjLcksPrp"
#define VM_OBJECTLACKSPROPERTY_MSG        "Object lacks property or method '%s'."

#define VM_NOINITIALIZER                  "NoInit"
#define VM_NOINITIALIZER_MSG              "Cannot instantiate with arguments because class '%s' does not provide an initializer."

#define VM_NOTANINSTANCE                  "NotAnInst"
#define VM_NOTANINSTANCE_MSG              "Can only invoke methods on objects."

#define VM_CLASSLACKSPROPERTY             "ClssLcksMthd"
#define VM_CLASSLACKSPROPERTY_MSG         "Class lacks method '%s'."

#define VM_INVALIDARGS                    "InvldArgs"
#define VM_INVALIDARGS_MSG                "Expected %u arguments but got %u."

#define VM_NOTINDEXABLE                   "NotIndxbl"
#define VM_NOTINDEXABLE_MSG               "Value or object not indexable."

#define VM_OUTOFBOUNDS                    "IndxBnds"
#define VM_OUTOFBOUNDS_MSG                "Index out of bounds."

#define VM_NONNUMINDX                     "NonNmIndx"
#define VM_NONNUMINDX_MSG                 "Non-numerical array index."

#define VM_ARRAYWRONGDIM                  "ArrayDim"
#define VM_ARRAYWRONGDIM_MSG              "Incorrect number of dimensions for array."

#define VM_DBGQUIT                        "DbgQuit"
#define VM_DBGQUIT_MSG                    "Program terminated by user in debugger."

#define VM_DVZR                           "DvZr"
#define VM_DVZR_MSG                       "Division by zero."

/* **********************************************************************
 * Prototypes
 * ********************************************************************** */

void error_init(error *err);
void error_clear(error *err);

void morpho_writeerrorwithid(error *err, errorid id, int line, int posn, ...);
void morpho_writeerrorwithidvalist(error *err, errorid id, int line, int posn, va_list args);
void morpho_defineerror(errorid id, errorcategory cat, char *message);

errorid morpho_geterrorid(error *err);

void error_initialize(void);
void error_finalize(void);

#endif /* error_h */