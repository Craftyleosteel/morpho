/** @file debug.c
 *  @author T J Atherton
 *
 *  @brief Debugging, dissassembly and other tools
 */

#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include "compile.h"
#include "vm.h"
#include "debug.h"
#include "morpho.h"
#include "cli.h"

void morpho_runtimeerror(vm *v, errorid id, ...);

/* **********************************************************************
 * Debugging annotations
 * ********************************************************************** */

DEFINE_VARRAY(debugannotation, debugannotation);

/** Retrieve the last annotation */
debugannotation *debug_lastannotation(varray_debugannotation *list) {
    if (list->count>0) return &list->data[list->count-1];
    return NULL;
}

/** Adds an annotation to a list */
void debug_addannotation(varray_debugannotation *list, debugannotation *annotation) {
    varray_debugannotationadd(list, annotation, 1);
}

/** Removes the last annotation */
void debug_stripend(varray_debugannotation *list) {
    if (list->count>0) list->data[list->count-1].content.element.ninstr--;
}

/** Sets the current function */
void debug_setfunction(varray_debugannotation *list, objectfunction *func) {
    debugannotation ann = { .type = DEBUG_FUNCTION, .content.function.function = func};
    debug_addannotation(list, &ann);
}

/** Sets the current class */
void debug_setclass(varray_debugannotation *list, objectclass *klass) {
    debugannotation ann = { .type = DEBUG_CLASS, .content.klass.klass = klass};
    debug_addannotation(list, &ann);
}

/** Sets the current module */
void debug_setmodule(varray_debugannotation *list, value module) {
    debugannotation ann = { .type = DEBUG_MODULE, .content.module.module = module };
    debug_addannotation(list, &ann);
}

/** Pushes an error handler onto the stack */
void debug_pusherr(varray_debugannotation *list, objectdictionary *dict) {
    debugannotation ann = { .type = DEBUG_PUSHERR, .content.errorhandler.handler = dict};
    debug_addannotation(list, &ann);
}

/** Pops an error handler from the stack */
void debug_poperr(varray_debugannotation *list) {
    debugannotation ann = { .type = DEBUG_POPERR };
    debug_addannotation(list, &ann);
}

/** Associates a register with a symbol */
void debug_setreg(varray_debugannotation *list, indx reg, value symbol) {
    if (!MORPHO_ISSTRING(symbol)) return;
    value sym = object_clonestring(symbol);
    debugannotation ann = { .type = DEBUG_REGISTER, .content.reg.reg = reg, .content.reg.symbol = sym };
    debug_addannotation(list, &ann);
}

/** Uses information from a syntaxtreenode to associate a sequence of instructions with source */
void debug_addnode(varray_debugannotation *list, syntaxtreenode *node) {
    if (!node) return; 
    debugannotation *last = debug_lastannotation(list);
    if (last && last->type==DEBUG_ELEMENT &&
        node->line==last->content.element.line &&
        node->posn==last->content.element.posn) {
        last->content.element.ninstr++;
    } else {
        debugannotation ann = { .type = DEBUG_ELEMENT, .content.element.line = node->line, .content.element.posn = node->posn, .content.element.ninstr=1 };
        debug_addannotation(list, &ann);
    }
}

/** Clear debugging list, freeing attached info */
void debug_clearannotationlist(varray_debugannotation *list) {
    for (unsigned int j=0; j<list->count; j++) {
        switch (list->data[j].type) {
            case DEBUG_REGISTER: {
                value sym=list->data[j].content.reg.symbol;
                if (MORPHO_ISOBJECT(sym)) object_free(MORPHO_GETOBJECT(sym));
            }
                break;
            default: break;
        }
    }
    varray_debugannotationclear(list);
}

/* **********************************************************************
 * Disassembler
 * ********************************************************************** */

/** Formatting rules for disassembler */
typedef struct {
    instruction op;
    char *label;
    char *display;
} assemblyrule;

/* Order is not significant here */
assemblyrule assemblyrules[] ={
    { OP_NOP, "nop", "" },
    { OP_MOV, "mov", "rA, rB" },
    { OP_LCT, "lct", "rA, cX" }, // Custom
    { OP_ADD, "add", "rA, rB, rC" },
    { OP_SUB, "sub", "rA, rB, rC" },
    { OP_MUL, "mul", "rA, rB, rC" },
    { OP_DIV, "div", "rA, rB, rC" },
    { OP_POW, "pow", "rA, rB, rC" },
    { OP_NOT, "not", "rA, rB" },
    
    { OP_EQ, "eq ", "rA, rB, rC" },
    { OP_NEQ, "neq", "rA, rB, rC" },
    { OP_LT, "lt ", "rA, rB, rC" },
    { OP_LE, "le ", "rA, rB, rC" },
    
    { OP_PRINT, "print", "rA" },
    
    { OP_B, "b", "+" },
    { OP_BIF, "bif", "rA +" },
    { OP_BIFF, "biff", "rA +" },
    
    { OP_CALL, "call", "rA, B" }, // b literal
    { OP_INVOKE, "invoke", "rA, rB, C" }, // c literal
    
    { OP_RETURN, "return", "rB" }, // c literal

    { OP_CLOSURE, "closure", "rA, pB" }, // b prototype
    
    { OP_LUP, "lup", "rA, uB" }, // b 'u'
    { OP_SUP, "sup", "uA, rB" }, // a 'u', b c|r
    
    { OP_CLOSEUP, "closeup", "rA" },
    { OP_LPR, "lpr", "rA, rB, rC" },
    { OP_SPR, "spr", "rA, rB, rC" },
    
    { OP_LIX, "lix", "rA, rB, rC" },
    { OP_SIX, "six", "rA, rB, rC" },
    
    { OP_LGL, "lgl", "rA, gX" }, //
    { OP_SGL, "sgl", "rA, gX" }, // label b with 'g'
    
    { OP_PUSHERR, "pusherr", "cX" },
    { OP_POPERR, "poperr", "+" },
    
    { OP_CAT, "cat", "rA, rB, rC" },
    { OP_BREAK, "break", "" },
    { OP_END, "end", "" },
    { 0, NULL, "" } // Null terminate the list
};

assemblyrule *debug_getassemblyrule(unsigned int op) {
    for (unsigned int i=0; assemblyrules[i].label!=NULL; i++) if (assemblyrules[i].op==op) return &assemblyrules[i];
    return NULL;
}

typedef enum { NONE, REG, CONST} debugcontents;

/** Shows the contents of a register or constant */
bool debug_showcontents(debugcontents b, int i, value *konst, value *reg) {
    value *table = NULL;
    switch (b) {
        case CONST: table=konst; break;
        case REG: table = reg; break;
        default: break;
    }
    if (!table) return false;
    printf("%s%i=", (b==CONST ? "c" : "r"), i);
    morpho_printvalue(table[i]);
    return true;
}

/** @brief Disassembles a single instruction, writing the output to the console.
 *  @param instruction The instruction to disassemble
 *  @param indx        Instruction index to display
 *  @param konst current constant table
 *  @param reg   current registers */
void debug_disassembleinstruction(instruction instruction, instructionindx indx, value *konst, value *reg) {
    unsigned int op = DECODE_OP(instruction);
    debugcontents mode=NONE, bm=NONE, cm=NONE;
    int nb=0, nc=0;
    printf("%4lu : ", indx);
    int n=0; // Number of characters displayed
    int width=25; // Width of display
    
    assemblyrule *show=debug_getassemblyrule(op);
    if (show) {
        n+=printf("%s ", show->label);
        for (char *c=show->display; *c!='\0'; c++) {
            switch (*c) {
                case 'A': n+=printf("%u", DECODE_A(instruction)); break;
                case 'B': {
                    bm=mode; nb=DECODE_B(instruction); mode=NONE;
                    n+=printf("%u", nb);
                }
                    break;
                case 'X': {
                    bm=mode; nb=DECODE_Bx(instruction); mode=NONE;
                    n+=printf("%u", nb);
                }
                    break;
                case '+': n+=printf("%i", DECODE_sBx(instruction)); break;
                case 'C': {
                    cm=mode; nc=DECODE_C(instruction);
                    n+=printf("%u", DECODE_C(instruction));
                }
                    break;
                case 'c': mode=CONST; n+=printf("%c", *c); break;
                case 'r': mode=REG; n+=printf("%c", *c); break;
                default: n+=printf("%c", *c); break;
            }
        }
        
        /* Show contents if any were produced by this instruction */
        if ((!konst && !reg) || (bm==NONE && cm==NONE)) return;
        for (int k=width-n; k>0; k--) printf(" ");
        printf("; ");
        if (debug_showcontents(bm, nb, konst, reg)) printf(" ");
        debug_showcontents(cm, nc, konst, reg);
    }
}

/** Checks if an instruction matches a label in the current error dictionary, and if so print it. */
void debug_errorlabel(varray_value *errorstack, instructionindx i) {
    objectdictionary *dict = MORPHO_GETDICTIONARY(errorstack->data[errorstack->count-1]);
    
    /* Search the current error handler to see if this line corresponds to a label */
    for (unsigned int k=0; k<dict->dict.capacity; k++) {
        value label = dict->dict.contents[k].key;
        if (!MORPHO_ISNIL(label)) {
            if (MORPHO_GETINTEGERVALUE(dict->dict.contents[k].val)==i) {
                object_print(label);
                printf(":\n");
            }
        }
    }
}

/** Disassembles a program
 *  @param code - program to disassemble
 *  @param matchline - optional line number to match */
void debug_disassemble(program *code, int *matchline) {
    instructionindx entry = program_getentry(code); // The entry point of the function
    instructionindx i=0;
    value *konst=(code->global ? code->global->konst.data : NULL);
    bool silent = matchline;
    
    varray_value errorstack;
    varray_valueinit(&errorstack);
    
    /* Loop over debugging information */
    for (unsigned int j=0; j<code->annotations.count; j++) {
        debugannotation *ann = &code->annotations.data[j];
        
        switch(ann->type) {
            case DEBUG_ELEMENT:
                {
                    if (matchline) {
                        if (ann->content.element.line<(*matchline)) {
                            i+=ann->content.element.ninstr;
                            break;
                        }
                        if (ann->content.element.line>(*matchline)) return;
                    } else if (errorstack.count>0) {
                        debug_errorlabel(&errorstack, i);
                    }
                    
                    for (unsigned int k=0; k<ann->content.element.ninstr; k++, i++) {
                        printf("%s",(i==entry ? "->" : "  "));
                        debug_disassembleinstruction(code->code.data[i], i, konst, NULL);
                        printf("\n");
                    }
                }
                break;
            case DEBUG_FUNCTION:
                {
                    objectfunction *func=ann->content.function.function;
                    konst=func->konst.data;
                    if (silent) break;
                    if (!MORPHO_ISNIL(func->name)) {
                        printf("fn ");
                        morpho_printvalue(func->name);
                        printf(":\n");
                    } else printf("\n");
                }
                break;
            case DEBUG_CLASS:
                {
                    objectclass *klass=ann->content.klass.klass;
                    if (silent) break;
                    if (klass && !MORPHO_ISNIL(klass->name)) {
                        printf("class ");
                        morpho_printvalue(klass->name);
                        printf(":\n");
                    }
                }
                break;
            case DEBUG_PUSHERR:
                {
                    objectdictionary *errdict = ann->content.errorhandler.handler;
                    varray_valuewrite(&errorstack, MORPHO_OBJECT(errdict));
                }
                break;
            case DEBUG_POPERR:
                {
                    if (errorstack.count>0) errorstack.count--;
                }
                break;
            default:
                break;
        }
    }
    
    varray_valueclear(&errorstack);
}

/** Wrapper onto debug_disassemble */
void morpho_disassemble(program *code, int *matchline) {
    debug_disassemble(code, matchline);
}

/* **********************************************************************
 * Retrieve debugging info
 * ********************************************************************** */

/** Finds debugging info asssociated with instruction at indx */
bool debug_infofromindx(program *code, instructionindx indx, value *module, int *line, int *posn, objectfunction **func, objectclass **klass) {
    if (module) *module=MORPHO_NIL; 
    if (func) *func=code->global;
    if (klass) *klass=NULL;
    instructionindx i=0;
    
    for (unsigned int j=0; j<code->annotations.count; j++) {
        debugannotation *ann = &code->annotations.data[j];
        switch (ann->type) {
            case DEBUG_ELEMENT: {
                if (i+ann->content.element.ninstr>indx) {
                    if (line) *line = ann->content.element.line;
                    if (posn) *posn = ann->content.element.posn;
                    return true;
                }
                i+=ann->content.element.ninstr;
            }
                break;
            case DEBUG_FUNCTION: if (func) *func=ann->content.function.function; break;
            case DEBUG_CLASS: if (klass) *klass=ann->content.klass.klass; break;
            case DEBUG_MODULE: if (module) *module=ann->content.module.module; break;
            default: break;
        }
    }
    
    return false;
}

/** Finds the instruction indx corresponding to a particular line of code */
bool debug_indxfromline(program *code, int line, instructionindx *out) {
    instructionindx i=0;
    value module=MORPHO_NIL;
    
    for (unsigned int j=0; j<code->annotations.count; j++) {
        debugannotation *ann = &code->annotations.data[j];
        switch (ann->type) {
            case DEBUG_ELEMENT:
                if (ann->content.element.line==line) {
                    *out=i;
                    return true;
                }
                i+=ann->content.element.ninstr;
                break;
            case DEBUG_MODULE:
                module=ann->content.module.module;
                break;
            default: break;
        }
    }
    return false;
}

/** Finds the instruction index corresponding to the entry point of a function or method */
bool debug_indxfromfunction(program *code, value klassname, value fname, instructionindx *indx) {
    objectclass *cklass=NULL;
    objectfunction *cfunc=NULL;
    instructionindx i=0;
    
    for (unsigned int j=0; j<code->annotations.count; j++) {
        debugannotation *ann = &code->annotations.data[j];
        switch (ann->type) {
            case DEBUG_ELEMENT:
                i+=ann->content.element.ninstr;
                break;
            case DEBUG_FUNCTION:
                cfunc=ann->content.function.function;
                if (MORPHO_ISEQUAL(cfunc->name, fname) &&
                    (MORPHO_ISNIL(klassname) || MORPHO_ISEQUAL(cklass->name, klassname))) {
                    *indx=cfunc->entry;
                    return true;
                }
                break;
            case DEBUG_CLASS:
                cklass=ann->content.klass.klass;
                break;
            default: break;
        }
    }
    
    return false;
}

/** Identifies symbols associated with registers
 * @param[in] code - a program
 * @param[in] func - the function of interest
 * @param[in] indx - (optional) instruction to stop at
 * @param[out] symbols - array of size func->negs; entries will contain associated register names on exit */
bool debug_symbolsforfunction(program *code, objectfunction *func, instructionindx *indx, value *symbols) {
    objectfunction *cfunc=code->global;
    instructionindx i=0;
    
    for (unsigned int j=0; j<func->nregs; j++) symbols[j]=MORPHO_NIL;
    
    for (unsigned int j=0; j<code->annotations.count; j++) {
        debugannotation *ann = &code->annotations.data[j];
        switch (ann->type) {
            case DEBUG_ELEMENT: {
                if (indx && i+ann->content.element.ninstr>*indx) return true;
                i+=ann->content.element.ninstr;
            }
                break;
            case DEBUG_FUNCTION: cfunc=ann->content.function.function; break;
            case DEBUG_REGISTER: {
                if (cfunc==func) {
                    symbols[ann->content.reg.reg]=ann->content.reg.symbol;
                }
            }
                break;
            default: break;
        }
    }
    
    return true;
}

/** Prints all the annotations for a program */
void debug_showannotations(varray_debugannotation *list) {
    indx ix = 0;
    printf("Showing %u annotations.\n", list->count);
    for (unsigned int j=0; j<list->count; j++) {
        printf("%u: ", j);
        debugannotation *ann = &list->data[j];
        switch (ann->type) {
            case DEBUG_CLASS:
                printf("Class: ");
                if (!ann->content.klass.klass) {
                    printf("(none)");
                } else {
                    morpho_printvalue(MORPHO_OBJECT(ann->content.klass.klass));
                }
                break;
            case DEBUG_ELEMENT:
                printf("Element: [%ti] instructions: %i line: %i posn: %i",
                       ix, ann->content.element.ninstr, ann->content.element.line, ann->content.element.posn);
                ix+=ann->content.element.ninstr;
                break;
            case DEBUG_FUNCTION:
                printf("Function: ");
                morpho_printvalue(MORPHO_OBJECT(ann->content.function.function));
                break;
            case DEBUG_MODULE:
                printf("Module: ");
                morpho_printvalue(ann->content.module.module);
                break;
            case DEBUG_PUSHERR:
                printf("Pusherr: ");
                morpho_printvalue(MORPHO_OBJECT(ann->content.errorhandler.handler));
                break;
            case DEBUG_POPERR:
                printf("Poperr: ");
                break;
            case DEBUG_REGISTER:
                printf("Register: %ti ", ann->content.reg.reg);
                morpho_printvalue(ann->content.reg.symbol);
                break;
        }
        printf("\n");
    }
}

/* **********************************************************************
 * Stack traces
 * ********************************************************************** */

/** Prints a stacktrace */
void morpho_stacktrace(vm *v) {
    for (callframe *f = (v->errfp ? v->errfp : v->fp); f!=NULL && f>=v->frame; f--) {
        instructionindx indx = f->pc-v->current->code.data;
        if (indx>0) indx--; /* Because the pc always points to the NEXT instr. */
        
        printf("  ");
        printf("%s", (f==v->fp ? "  in " : "from "));
        
        if (!MORPHO_ISNIL(f->function->name)) morpho_printvalue(f->function->name);
        else printf("global");
        
        int line=0;
        if (debug_infofromindx(v->current, indx, NULL, &line, NULL, NULL, NULL)) {
            printf(" at line %u", line);
        }
        
        printf("\n");
    }
}

/* **********************************************************************
 * Debugger structure
 * ********************************************************************** */

/** Initializes a debugger structure with a specified program */
void debugger_init(debugger *d, program *p) {
    d->singlestep=false;
    
    d->nbreakpoints=0;
    
    d->currentfunc=NULL;
    d->currentline=0;
    d->currentmodule=MORPHO_NIL;
    
    varray_charinit(&d->breakpoints);
    
    int ninstructions = p->code.count;
    if (!varray_charresize(&d->breakpoints, ninstructions)) return;
    memset(d->breakpoints.data, '\0', sizeof(char)*ninstructions);
    d->breakpoints.count=ninstructions;
}

/** Clears a debugger structure */
void debugger_clear(debugger *d) {
    varray_charclear(&d->breakpoints);
}

/** Sets whether single step mode is in operation */
void debugger_setsinglestep(debugger *d, bool singlestep) {
    d->singlestep=singlestep;
}

/** Are we in singlestep mode? */
bool debugger_insinglestep(debugger *d) {
    return d->singlestep;
}

/** Sets a breakpoint */
void debugger_setbreakpoint(debugger *d, instructionindx indx) {
    if (indx>d->breakpoints.count) return;
    d->breakpoints.data[indx]='b';
    d->nbreakpoints++;
}

/** Clears a breakpoint */
void debugger_clearbreakpoint(debugger *d, instructionindx indx) {
    if (indx>d->breakpoints.count) return;
    d->breakpoints.data[indx]='\0';
    d->nbreakpoints--;
}

/** Tests if we should break at a given point */
bool debugger_shouldbreakat(debugger *d, instructionindx indx) {
    if (indx>d->breakpoints.count) return false;
    return (d->breakpoints.data[indx]!='\0');
}

/** Should we break */
bool debug_shouldbreakatpc(vm *v, instruction *pc) {
    if (!v->debug) return false;
    if (debugger_insinglestep(v->debug)) return true;
    instructionindx iindx = pc-v->current->code.data-1;
    if (debugger_shouldbreakat(v->debug, iindx)) return true;
    return false;
}

/** Tests if the debugger is in a mode that could cause breaks at arbitrary instructions */
bool debugger_isactive(debugger *d) {
    return (d->singlestep || (d->nbreakpoints>0));
}

/* **********************************************************************
 * Debugger
 * ********************************************************************** */

/* ---------------
 * Parse commands
 * --------------- */

typedef enum {
    DEBUGTOKEN_ASTERISK,
    DEBUGTOKEN_DOT,
    DEBUGTOKEN_EQ,
    
    DEBUGTOKEN_INTEGER,
    
    DEBUGTOKEN_ADDRESS,
    DEBUGTOKEN_BREAK,
    DEBUGTOKEN_CLEAR,
    DEBUGTOKEN_CONTINUE,
    DEBUGTOKEN_DISASSEMBLE,
    DEBUGTOKEN_GARBAGECOLLECT,
    DEBUGTOKEN_GLOBALS,
    DEBUGTOKEN_G,
    DEBUGTOKEN_HELP,
    DEBUGTOKEN_INFO,
    DEBUGTOKEN_LIST,
    DEBUGTOKEN_PRINT,
    DEBUGTOKEN_QUIT,
    DEBUGTOKEN_REGISTERS,
    DEBUGTOKEN_SET,
    DEBUGTOKEN_STACK,
    DEBUGTOKEN_STEP,
    DEBUGTOKEN_TRACE,
    
    DEBUGTOKEN_SYMBOL,
    
    DEBUGTOKEN_EOF
} debugtokentype;

/** List of commands and corresponding token types */
typedef struct {
    char *string;
    debugtokentype type;
} debuggercommand;

/* Note that these are matched in order so single letter commands
   should come AFTER the full command */
debuggercommand commandlist[] =
{
  { "address", DEBUGTOKEN_ADDRESS },
    
  { "break", DEBUGTOKEN_BREAK },
  { "bt", DEBUGTOKEN_TRACE },
  { "b", DEBUGTOKEN_BREAK },
    
  { "clear", DEBUGTOKEN_CLEAR },
  { "x", DEBUGTOKEN_CLEAR },
    
  { "continue", DEBUGTOKEN_CONTINUE },
  { "c", DEBUGTOKEN_CONTINUE },
    
  { "disassemble", DEBUGTOKEN_DISASSEMBLE },
  { "disassem", DEBUGTOKEN_DISASSEMBLE },
  { "d", DEBUGTOKEN_DISASSEMBLE },
  
  { "garbage", DEBUGTOKEN_GARBAGECOLLECT },
  { "gc", DEBUGTOKEN_GARBAGECOLLECT },
    
  { "globals", DEBUGTOKEN_GLOBALS },
  { "global", DEBUGTOKEN_GLOBALS },
    
  { "g", DEBUGTOKEN_G },
    
  { "help", DEBUGTOKEN_HELP },
  { "h", DEBUGTOKEN_HELP },
    
  { "info", DEBUGTOKEN_INFO },
  { "i", DEBUGTOKEN_INFO },
  
  { "list", DEBUGTOKEN_LIST },
  { "l", DEBUGTOKEN_LIST },
  
  { "print", DEBUGTOKEN_PRINT },
  { "p", DEBUGTOKEN_PRINT },

  { "quit", DEBUGTOKEN_QUIT },
  { "q", DEBUGTOKEN_QUIT },
    
  { "registers", DEBUGTOKEN_REGISTERS },
  { "register", DEBUGTOKEN_REGISTERS },
  { "reg", DEBUGTOKEN_REGISTERS },
  { "r", DEBUGTOKEN_REGISTERS },

  { "stack", DEBUGTOKEN_STACK },
    
  { "step", DEBUGTOKEN_STEP },

  { "set", DEBUGTOKEN_SET },
    
  { "s", DEBUGTOKEN_STEP },
  
  { "trace", DEBUGTOKEN_TRACE },
  { "t", DEBUGTOKEN_TRACE },
    
  { "", DEBUGTOKEN_EOF }
};

typedef struct {
    debugtokentype type;
    char *start;
    int length;
} debugtoken;

typedef struct {
    char *start;
    char *current;
} debuglexer;

/** Initialize the lexer */
void debuglexer_init(debuglexer *l, char *start) {
    l->start=start;
    l->current=start;
}

/** Are we at the end? */
bool debuglexer_isatend(debuglexer *l) {
    return (*(l->current) == '\0');
}

/** @brief Checks if a character is a digit. Doesn't advance. */
bool debuglexer_isdigit(char c) {
    return (c>='0' && c<= '9');
}

/** @brief Checks if a character is alphanumeric or underscore.  Doesn't advance. */
bool debuglexer_isalpha(char c) {
    return (c>='a' && c<= 'z') || (c>='A' && c<= 'Z') || (c=='_');
}

/** @brief Returns the next character */
char debuglexer_peek(debuglexer *l) {
    return *(l->current);
}

/** Advance by one character */
char debuglexer_advance(debuglexer *l) {
    char c=*(l->current);
    l->current++;
    return c;
}

/** Skip whitespace */
bool debuglexer_skipwhitespace(debuglexer *l, debugtoken *tok) {
    do {
        switch (debuglexer_peek(l)) {
            case ' ': case '\t': case '\r':
                debuglexer_advance(l);
                break;
            default:
                return true;
        }
    } while (true);
    return false;
}

/** Record a token */
void debuglexer_recordtoken(debuglexer *l, debugtokentype type, debugtoken *tok) {
    tok->type=type;
    tok->start=l->start;
    tok->length=(int) (l->current - l->start);
}

/** Lex an integer */
bool debuglexer_integer(debuglexer *l, debugtoken *tok) {
    while (debuglexer_isdigit(debuglexer_peek(l))) debuglexer_advance(l);
    debuglexer_recordtoken(l, DEBUGTOKEN_INTEGER, tok);
    return true;
}

/** Case indep comparison of a with command */
bool debuglexer_comparesymbol(char *a, char *command) {
    for (int i=0; command[i]!='\0'; i++) {
        char c = tolower(a[i]);
        if (c!=command[i]) return false;
        if (c=='\0') return false;
    }
    return true;
}

/** Determines if a token matches a command */
debugtokentype debuglexer_matchkeyword(debuglexer *l) {
    for (int i=0; commandlist[i].type!=DEBUGTOKEN_EOF; i++) {
        if (debuglexer_comparesymbol(l->start, commandlist[i].string)) return commandlist[i].type;
    }
    
    return DEBUGTOKEN_SYMBOL;
}

/** Lex a symbol */
static bool debuglexer_symbol(debuglexer *l, debugtoken *tok, bool match) {
    while (debuglexer_isalpha(debuglexer_peek(l)) || debuglexer_isdigit(debuglexer_peek(l))) debuglexer_advance(l);
    
    debugtokentype type = DEBUGTOKEN_SYMBOL;
    if (match) type=debuglexer_matchkeyword(l);
    
    /* It's a symbol for now... */
    debuglexer_recordtoken(l, type, tok);
    
    return true;
}

/** Lex */
bool debuglex(debuglexer *l, debugtoken *tok, bool command) {
    /* Handle leading whitespace */
    if (! debuglexer_skipwhitespace(l, tok)) return false; /* Check for failure */
    
    l->start=l->current;
    
    if (debuglexer_isatend(l)) {
        debuglexer_recordtoken(l, DEBUGTOKEN_EOF, tok);
        return true;
    }
    
    char c = debuglexer_advance(l);
    if (debuglexer_isalpha(c)) return debuglexer_symbol(l, tok, command);
    if (debuglexer_isdigit(c)) return debuglexer_integer(l, tok);
    
    switch(c) {
        /* Single character tokens */
        case '*': debuglexer_recordtoken(l, DEBUGTOKEN_ASTERISK, tok); return true;
        case '.': debuglexer_recordtoken(l, DEBUGTOKEN_DOT, tok); return true;
        case '?': debuglexer_recordtoken(l, DEBUGTOKEN_HELP, tok); return true;
        case '=': debuglexer_recordtoken(l, DEBUGTOKEN_EQ, tok); return true;
        default:
            break;
    }
    
    return false;
}

/** Copies a token to a null-terminated string */
void debugger_tokentostring(debugtoken *tok, char *string) {
    strncpy(string, tok->start, tok->length);
    string[tok->length]='\0';
}

/** Converts a token to an integer; returns true on success */
bool debugger_tokentoint(debugtoken *tok, int *out) {
    if (tok->type==DEBUGTOKEN_INTEGER) {
        char str[tok->length+1];
        debugger_tokentostring(tok, str);
        *out=atoi(str);
        return true;
    }
    return false;
}

/** Attempts to parse an integer */
bool debugger_parseint(debuglexer *lex, debugtoken *tok, int *out) {
    return (debuglex(lex, tok, false) &&
            debugger_tokentoint(tok, out));
}

/** Advances the lexer and matches a specified token type */
bool debugger_parsematch(debuglexer *lex, debugtokentype match) {
    debugtoken tok;
    return (debuglex(lex, &tok, false) &&
            tok.type==match);
}

/** Parse a breakpoint command */
bool debugger_parsebreakpoint(vm *v, debugger *debug, debuglexer *lex, instructionindx *out) {
    debugtoken tok;
    bool instruction=false; // Detect if we're parsing an instruction
    bool success=false;
    debugtoken symbol[2];
    int nsymbol=0;
    
    while (!debuglexer_isatend(lex) && nsymbol<2) {
        if (!debuglex(lex, &tok, false)) return false;
        
        switch (tok.type) {
            case DEBUGTOKEN_ASTERISK:
            case DEBUGTOKEN_ADDRESS:
                instruction=true;
                break;
            case DEBUGTOKEN_INTEGER:
            {
                int iindx;
                if (!debugger_tokentoint(&tok, &iindx)) return false;
                if (instruction) { // The integer is an instruction index
                    *out = iindx;
                    return true;
                } else if (debug_indxfromline(v->current, iindx, out)) return true;
            }
                break;
            case DEBUGTOKEN_SYMBOL:
                symbol[nsymbol]=tok;
                nsymbol++;
                break;
            default:
                break;
        }
    }
    
    if (nsymbol>0) { // Process function or method names
        value fnname = object_stringfromcstring(symbol[nsymbol-1].start, symbol[nsymbol-1].length);
        value klassname = object_stringfromcstring(symbol[0].start, symbol[0].length);
        
        if (debug_indxfromfunction(v->current, (nsymbol>1 ? klassname : MORPHO_NIL), fnname, out)) success=true;
        
        morpho_freeobject(fnname);
        morpho_freeobject(klassname);
    }

    return success;
}

/* Parses a string into a value */
bool debugger_parsevalue(char *in, value *out) {
    lexer l;
    parser p;
    syntaxtree tree;
    error err;
    bool success=false;
    error_init(&err);
    syntaxtree_init(&tree);
    lex_init(&l, in, 1);
    parse_init(&p, &l, &err, &tree);
    if (parse(&p) && tree.tree.count>0) {
        syntaxtreenode node = tree.tree.data[tree.entry];
        
        if (SYNTAXTREE_ISLEAF(node.type)) {
            if (MORPHO_ISSTRING(node.content)) {
                *out = object_clonestring(node.content);
            } else *out = node.content;
            
            success=true;
        }
    }
    
    syntaxtree_clear(&tree);
    return success;
}

/* ----------------------
 * Debugger functionality
 * ---------------------- */

/** Shows the contents of the registers for a given frame */
void debug_showregisters(vm *v, callframe *frame) {
    unsigned int nreg=frame->function->nregs;
    value symbols[nreg];
    instructionindx cinstr=frame->pc-v->current->code.data;
    bool sym = debug_symbolsforfunction(v->current, frame->function, &cinstr, symbols);
    
    printf("Register contents:\n");
    value *reg = v->stack.data + frame->roffset;
    for (unsigned int i=0; i<nreg; i++) {
        printf("  r%u: ", i);
        morpho_printvalue(reg[i]);
        if (sym && !MORPHO_ISNIL(symbols[i])) {
            printf(" (");
            morpho_printvalue(symbols[i]);
            printf(")");
        }
        printf("\n");
    }
}

/** Shows the contents of the stack */
void debug_showstack(vm *v) {
    /* Determine points on the stack that correspond to different function calls. */
    ptrdiff_t fbounds[MORPHO_CALLFRAMESTACKSIZE];
    callframe *f;
    unsigned int k=0;
    for (f=v->frame; f!=v->fp; f++) {
        fbounds[k]=f->roffset;
        k++;
    }
    fbounds[k]=f->roffset;
    
    f=v->frame; k=0;
    printf("Stack contents:\n");
    for (unsigned int i=0; i<v->fp->roffset+v->fp->function->nregs; i++) {
        if (i==fbounds[k]) {
            printf("---");
            if (f->function) morpho_printvalue(f->function->name);
            printf("\n");
            k++; f++;
        }
        printf("  s%u: ", i);
        morpho_printvalue(v->stack.data[i]);
        printf("\n");
    }
}

/** Shows current symbols */
void debug_showsymbols(vm *v) {
    for (callframe *f=v->fp; f>=v->frame; f--) {
        printf("in %s", (f==v->frame ? "global" : ""));
        if (!MORPHO_ISNIL(f->function->name)) morpho_printvalue(f->function->name);
        printf(":\n");
        
        value symbols[f->function->nregs];
        instructionindx indx = f->pc-v->current->code.data;
        
        debug_symbolsforfunction(v->current, f->function, &indx, symbols);
        
        for (int i=0; i<f->function->nregs; i++) {
            if (!MORPHO_ISNIL(symbols[i])) {
                printf("  ");
                morpho_printvalue(symbols[i]);
                printf("=");
                morpho_printvalue(v->stack.data[f->roffset+i]);
                printf("\n");
            }
        }
        
    }
}

/** Prints a global */
void debug_showglobal(vm *v, int id) {
    if (id>=0 && id<v->globals.count) {
        printf("  g%u:", id);
        morpho_printvalue(v->globals.data[id]);
        printf("\n");
    } else printf("Invalid global number.\n");
}

/** Prints list of globals */
void debug_showglobals(vm *v) {
    printf("Globals:\n");
    for (unsigned int i=0; i<v->globals.count; i++) {
        printf("  g%u: ", i);
        morpho_printvalue(v->globals.data[i]);
        printf("\n");
    }
}

/** Attempts to find a symbol. If successful val is updated to give its storage location */
bool debug_findsymbol(vm *v, debugtoken *tok, callframe **frame, value *symbol, value **val) {
    value matchstr = object_stringfromcstring(tok->start, tok->length);
    
    for (callframe *f=v->fp; f>=v->frame; f--) {
        value symbols[f->function->nregs];
        instructionindx indx = f->pc-v->current->code.data;
        
        debug_symbolsforfunction(v->current, f->function, &indx, symbols);
        
        for (int i=0; i<f->function->nregs; i++) {
            if (!MORPHO_ISNIL(symbols[i]) && MORPHO_ISEQUAL(symbols[i], matchstr)) {
                if (frame) *frame = f;
                if (symbol) *symbol = symbols[i];
                if (val) *val = &v->stack.data[f->roffset+i];
                morpho_freeobject(matchstr);
                return true;
            }
        }
    }
    
    morpho_freeobject(matchstr);
    return false;
}

/** Prints a specified symbol */
bool debug_printsymbol(vm *v, debugtoken *tok) {
    value symbol, *val;
    callframe *frame;
    if (!debug_findsymbol(v, tok, &frame, &symbol, &val)) return false;
    
    objectstring prntlabel = MORPHO_STATICSTRING(MORPHO_PRINT_METHOD);
    morpho_printvalue(symbol);
    
    printf(" (in %s", (frame==v->frame ? "global" : ""));
    if (frame->function->klass &&
        !MORPHO_ISNIL(frame->function->klass->name)) {
        morpho_printvalue(frame->function->klass->name);
        printf(".");
    }
    if (!MORPHO_ISNIL(frame->function->name)) {
        morpho_printvalue(frame->function->name);
    } else printf("anonymous");
    printf(") ");
    
    printf("= ");

    if (MORPHO_ISOBJECT(*val)) {
        value printmethod, out;
        if (morpho_lookupmethod(*val, MORPHO_OBJECT(&prntlabel), &printmethod)) {
            morpho_invoke(v, *val, printmethod, 0, NULL, &out);
        }
    } else {
        morpho_printvalue(*val);
    }
    printf("\n");
    
    return true;
}

/** Return the previous instruction index */
instructionindx debug_previnstruction(vm *v) {
    if (v->fp->pc>v->current->code.data) return v->fp->pc-v->current->code.data-1;
    return 0;
}

/** Return the current instruction index */
instructionindx debug_currentinstruction(vm *v) {
    return v->fp->pc-v->current->code.data-1;
}

#include "linedit.h"
#include "cli.h"

/* ----------------------
 * Debugger functionality
 * ---------------------- */

#ifdef MORPHO_COLORTERMINAL
#define DEBUG_COLOR CLI_GREENCODE
#else
#define DEBUG_COLOR ""
#endif

/** Prints the location information */
void debugger_printlocation(vm *v, debugger *debug, instructionindx indx) {
    value module=MORPHO_NIL;
    int line=0;
    objectfunction *fn=NULL;
    objectclass *klass=NULL;
    debug_infofromindx(v->current, indx, &module, &line, NULL, &fn, &klass);
    
    printf("in ");
    
    if (klass) {
        morpho_printvalue(klass->name);
        printf(".");
    }
    
    if (!MORPHO_ISNIL(fn->name)) morpho_printvalue(fn->name);
    else if (v->current->global==fn) printf("global");
    else printf("anonymous fn");
    
    if (!MORPHO_ISNIL(module)) {
        printf(" in \"");
        morpho_printvalue(module);
        printf("\"");
    }
    printf(" at line %i ", line);
    
    printf("[instruction %ti]", indx);
    
}

/** Display the morpho banner */
void debugger_banner(vm *v, debugger *debug) {
    printf("%s---Morpho debugger---%s\n", DEBUG_COLOR, CLI_NORMALTEXT);
    printf("Type '?' or 'h' for help.\n");
    printf("%s ", (debug->singlestep ? "Single stepping" : "Breakpoint"));
    debugger_printlocation(v, debug, debug->iindx);
    printf("\n");
}

/** Display the resume text */
void debugger_resumebanner(debugger *debug) {
    printf("%s---Resuming----------%s\n", DEBUG_COLOR, CLI_NORMALTEXT);
}

/** Process a set/clear breakpoint */
void debugger_breakpoint(vm *v, debugger *debug, debuglexer *lex, bool set) {
    instructionindx breakpoint;
    if (debugger_parsebreakpoint(v, debug, lex, &breakpoint)) {
        if (set) debugger_setbreakpoint(debug, breakpoint);
        else debugger_clearbreakpoint(debug, breakpoint);
    } else printf("Invalid breakpoint target.\n");
}

/** Debugger help */
void debugger_help(debugger *debug) {
    printf("Available commands:\n");
    printf("  [b]reakpoint, [c]ontinue, [d]isassemble, [g]arbage collect,\n"
           "  [?]/[h]elp, [i]nfo, [l]ist, [p]rint, [q]uit, [s]tep, \n"
           "  [t]race, [x]clear\n");
}

/** Find the address of an object in a register */
void debugger_address(vm *v, debugger *debug, debuglexer *lex) {
    debugtoken tok;
    int rindx;
    
    if (!debugger_parseint(lex, &tok, &rindx)) return;
        
    if (rindx>=0 && rindx<v->fp->function->nregs) {
        value *reg = v->stack.data + v->fp->roffset;
        if (MORPHO_ISOBJECT(reg[rindx])) {
            printf("Object in register %i at %p.\n", rindx, (void *) MORPHO_GETOBJECT(reg[rindx]));
        }
    } else printf("Invalid register.\n");
}

/** Displays active breakpoints */
void debugger_showbreakpoints(vm *v, debugger *debug) {
    printf("Active breakpoints:\n");
    for (instructionindx i=0; i<debug->breakpoints.count; i++) {
        if (debug->breakpoints.data[i]!='\0') {
            printf("  Breakpoint ");
            debugger_printlocation(v, debug, i);
            printf("\n");
        } else if (DECODE_OP(v->current->code.data[i])==OP_BREAK) {
            printf("  Break ");
            debugger_printlocation(v, debug, i);
            printf("\n");
        }
    }
}

/** Shows one or more globals */
void debugger_globals(vm *v, debugger *debug, debuglexer *lex) {
    debugtoken token; // Record tokens
    int id;
    if (debugger_parseint(lex, &token, &id)) {
        debug_showglobal(v, id);
    } else {
        debug_showglobals(v);
    }
}

/** Information */
void debugger_info(vm *v, debugger *debug, debuglexer *lex) {
    debugtoken token; // Record tokens
    if (!debuglex(lex, &token, true)) return;
    
    switch (token.type) {
        case DEBUGTOKEN_ASTERISK:
        case DEBUGTOKEN_ADDRESS:
            debugger_address(v, debug, lex);
            break;
        case DEBUGTOKEN_BREAK:
            debugger_showbreakpoints(v, debug);
            break;
        case DEBUGTOKEN_GLOBALS:
        case DEBUGTOKEN_G:
            debugger_globals(v, debug, lex);
            break;
        case DEBUGTOKEN_REGISTERS:
            debug_showregisters(v, v->fp);
            break;
        case DEBUGTOKEN_STACK:
        case DEBUGTOKEN_STEP:
            debug_showstack(v);
            break;
        case DEBUGTOKEN_HELP:
        default:
            printf("Valid info commands: \n");
            printf("  info address n: Displays the address of register n.\n");
            printf("  info break: Displays all breakpoints.\n");
            printf("  info globals: Displays the contents of all globals.\n");
            printf("  info global n: Displays the contents of global n.\n");
            printf("  info registers: Displays the contents of all registers.\n");
            printf("  info stack: Displays the stack.\n");
            break;
    }
}

/** Source listing */
void debugger_list(vm *v) {
    int line=0;
    value module=MORPHO_NIL;
    
    if (debug_infofromindx(v->current, debug_previnstruction(v), &module, &line, NULL, NULL, NULL)) {
        cli_list((MORPHO_ISSTRING(module) ? MORPHO_GETCSTRING(module): NULL), line-5, line+5);
    }
}

/** Prints a symbol or all symbols in view */
void debugger_print(vm *v, debugger *debug, debuglexer *lex) {
    debugtoken tok;
    if (debuglex(lex, &tok, false) &&
        tok.type==DEBUGTOKEN_SYMBOL) {
        tok.start[tok.length]='\0';
        if (!debug_printsymbol(v, &tok)) printf("Symbol '%s' not found.\n", tok.start);
    } else {
        debug_showsymbols(v);
    }
}

/** Sets a variable or register */
bool debugger_set(vm *v, debugger *debug, debuglexer *lex) {
    debugtoken tok;
    value *dest=NULL;
    value val=MORPHO_NIL;
    bool success=false;
    
    if (debuglex(lex, &tok, false)) {
        if (debuglexer_matchkeyword(lex)==DEBUGTOKEN_REGISTERS) {
            int r;
            if (debugger_parseint(lex, &tok, &r)) {
                if (r>=0 && r<v->fp->function->nregs) {
                    dest = v->stack.data+v->fp->roffset+r;
                } else {
                    printf("Invalid register.\n");
                    return false;
                }
            }
        } else if (tok.type==DEBUGTOKEN_SYMBOL) {
            if (!debug_findsymbol(v, &tok, NULL, NULL, &dest)) {
                printf("Symbol not found.\n");
                return false;
            }
        }
    }
    
    if (dest) {
        if (!debugger_parsematch(lex, DEBUGTOKEN_EQ)) return false;

        if (debugger_parsevalue(lex->current, &val)) {
            *dest = val;
            success=true;
        } else printf("Couldn't parse expression.\n");
    } else printf("Invalid target.\n");

    return success;
}

/* ----------------------
 * The debugger itself
 * ---------------------- */

/** Morpho debugger */
void debugger_enter(vm *v) {
    debugger *debug = v->debug;
    lineditor edit;
    
    debug->iindx = debug_currentinstruction(v);
    int oline=debug->currentline;
    objectfunction *ofunc=debug->currentfunc;
    
    /** Fetch info from annotations */
    debug_infofromindx(v->current, debug->iindx, &debug->currentmodule, &debug->currentline, NULL, &debug->currentfunc, NULL);
    
    /** If we're in single step mode, only stop when we've changed line OR if a breakpoint is explicitly set */
    if (debugger_insinglestep(debug) &&
        oline==debug->currentline &&
        ofunc==debug->currentfunc &&
        !debugger_shouldbreakat(debug, debug->iindx)) return;
    
    linedit_init(&edit);
    linedit_setprompt(&edit, "@>");
    
    debugger_banner(v, debug);
    
    debuglexer lex;   // Lexer to read commands
    debugtoken token; // Record tokens
    for (bool stop=false; !stop; ) {
        char *input = linedit(&edit);
        if (!input) continue;
        
        debuglexer_init(&lex, input);
        debuglex(&lex, &token, true);
        
        switch (token.type) {
            case DEBUGTOKEN_BREAK:
                debugger_breakpoint(v, debug, &lex, true);
                break;
            case DEBUGTOKEN_CLEAR:
                debugger_breakpoint(v, debug, &lex, false);
                break;
            case DEBUGTOKEN_CONTINUE:
                debugger_setsinglestep(debug, false);
                stop=true;
                break;
            case DEBUGTOKEN_DISASSEMBLE:
                debug_disassemble(v->current, &debug->currentline);
                break;
            case DEBUGTOKEN_GARBAGECOLLECT:
            case DEBUGTOKEN_G:
                vm_collectgarbage(v);
                break;
            case DEBUGTOKEN_HELP:
                debugger_help(debug);
                break;
            case DEBUGTOKEN_INFO:
                debugger_info(v, debug, &lex);
                break;
            case DEBUGTOKEN_LIST:
                debugger_list(v);
                break;
            case DEBUGTOKEN_PRINT:
                debugger_print(v, debug, &lex);
                break;
            case DEBUGTOKEN_QUIT:
                morpho_runtimeerror(v, VM_DBGQUIT);
                return;
            case DEBUGTOKEN_SET:
                debugger_set(v, debug, &lex);
                break;
            case DEBUGTOKEN_STEP:
                debugger_setsinglestep(debug, true);
                stop=true;
                break;
            case DEBUGTOKEN_TRACE:
                morpho_stacktrace(v);
                break;
            default:
                printf("Unrecognized debugger command\n");
                break;
        }
    }
    
    debugger_resumebanner(debug);
    linedit_clear(&edit);
}

/* **********************************************************************
 * Run a program with debugging active
 * ********************************************************************** */

/** Run a program with debugging
 * @param[in] v - the virtual machine to use
 * @param[in] p - program to run
 * @returns true on success, false otherwise */
bool morpho_debug(vm *v, program *p) {
    debugger debug;

    debugger_init(&debug, p);
    v->debug=&debug;
    
    bool success=morpho_run(v, p);
    
    debugger_clear(&debug);
    
    return success;
}
