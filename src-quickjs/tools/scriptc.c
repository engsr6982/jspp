// scriptc: compiles an ES module to quickjs bytecode as a single blob.
//
// Based on quickjs-ng qjsc.c (MIT license, (c) Fabrice Bellard, Ben
// Noordhuis, Saul Ibarra Corretge). Differences from qjsc:
// - qjsc writes one blob per compiled module into one file, while a
//   single-blob reader validates the whole file against the first blob
//   header and rejects the rest. scriptc emits exactly one blob for the
//   entry module.
// - imported modules are compiled for validation only and never written;
//   imports stay runtime-resolved specifiers (native modules by name,
//   ".js" files as sibling ".bin" bytecode when the host enables
//   QjsInitializeFlags::EnableBytecodeImport).
// - relative import specifiers must reference an existing ".js" file.
//
// quickjs bytecode embeds a checksum and a format version, and is only
// readable by the quickjs build that produced it. scriptc links the same
// quickjs library as the jspp runtime, so its output stays loadable there.
//
// usage: scriptc [--version] [-m] [-M name]... -o out.bin in.js
// --version prints the quickjs version this tool was built against.
// relative imports resolve against the working directory; run with the
// working directory set to the entry file's directory. -m forces module
// compilation, since JS_DetectModule may misclassify files whose first
// import is relative.

#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cutils.h"
#include "quickjs.h"

typedef struct {
    char *name;
    char *short_name;
    int flags;
} namelist_entry_t;

typedef struct namelist_t {
    namelist_entry_t *array;
    int count;
    int size;
} namelist_t;

static namelist_t cmodule_list;

/* carved from qjsc.c */
void namelist_add(namelist_t *lp, const char *name, const char *short_name,
                  int flags)
{
    namelist_entry_t *e;
    if (lp->count == lp->size) {
        size_t newsize = lp->size + (lp->size >> 1) + 4;
        namelist_entry_t *a =
            realloc(lp->array, sizeof(lp->array[0]) * newsize);
        /* XXX: check for realloc failure */
        lp->array = a;
        lp->size = newsize;
    }
    e =  &lp->array[lp->count++];
    e->name = strdup(name);
    if (short_name)
        e->short_name = strdup(short_name);
    else
        e->short_name = NULL;
    e->flags = flags;
}

/* carved from qjsc.c */
namelist_entry_t *namelist_find(namelist_t *lp, const char *name)
{
    int i;
    for(i = 0; i < lp->count; i++) {
        namelist_entry_t *e = &lp->array[i];
        if (!strcmp(e->name, name))
            return e;
    }
    return NULL;
}

/* carved from qjsc.c */
void namelist_free(namelist_t *lp)
{
    while (lp->count > 0) {
        namelist_entry_t *e = &lp->array[--lp->count];
        free(e->name);
        free(e->short_name);
    }
    free(lp->array);
    lp->array = NULL;
    lp->size = 0;
}

/* carved from qjsc.c */
static int js_module_dummy_init(JSContext *ctx, JSModuleDef *m)
{
    /* should never be called when compiling JS code */
    abort();
    return -1; // pacify compiler
}

/* carved from quickjs-libc's js_load_file; the buffer is NUL-terminated
   (the quickjs lexer reads the sentinel byte at the end of the input), and
   allocations go through the runtime allocator like all other quickjs memory */
static uint8_t *js_load_file(JSContext *ctx, size_t *pbuf_len, const char *filename)
{
    FILE *f;
    size_t n, len;
    uint8_t *p, *buf, tmp[8192];

    f = fopen(filename, "rb");
    if (!f)
        return NULL;
    buf = NULL;
    len = 0;
    do {
        n = fread(tmp, 1, sizeof(tmp), f);
        p = js_realloc(ctx, buf, len + n + 1);
        if (!p) {
            js_free(ctx, buf);
            fclose(f);
            return NULL;
        }
        memcpy(&p[len], tmp, n);
        buf = p;
        len += n;
        buf[len] = '\0';
    } while (n == sizeof(tmp));
    fclose(f);
    *pbuf_len = len;
    return buf;
}

// minimal error dump; quickjs-libc's js_std_dump_error is not linked here
static void dump_error(JSContext *ctx)
{
    JSValue e = JS_GetException(ctx);
    JSValue msg = JS_GetPropertyStr(ctx, e, "message");
    JSValue stack = JS_GetPropertyStr(ctx, e, "stack");
    const char *m = JS_ToCString(ctx, msg);
    const char *s = JS_ToCString(ctx, stack);
    fprintf(stderr, "scriptc: %s\n", (m && *m) ? m : ((s && *s) ? s : "(error)"));
    if (s && *s && !(m && *m && strstr(s, m))) {
        fprintf(stderr, "scriptc: %s\n", s);
    }
    JS_FreeCString(ctx, s);
    JS_FreeCString(ctx, m);
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, msg);
    JS_FreeValue(ctx, e);
}

// module loader, based on qjsc's jsc_module_loader. Differences: dependency
// modules are compiled for validation only and never written to the output,
// and import specifiers must reference ".js" files (the host maps ".js" to
// ".bin" at runtime).
static JSModuleDef *scriptc_loader(JSContext *ctx,
                                   const char *module_name, void *opaque)
{
    JSModuleDef *m;
    namelist_entry_t *e;

    /* check if it is a declared C or system module */
    e = namelist_find(&cmodule_list, module_name);
    if (e) {
        /* dummy module; resolved by the host at runtime */
        m = JS_NewCModule(ctx, module_name, js_module_dummy_init);
        return m;
    }

    if (!js__has_suffix(module_name, ".js")) {
        JS_ThrowReferenceError(ctx,
            "import specifier must reference a .js file (or be declared via -M): '%s'",
            module_name);
        return NULL;
    }

    size_t buf_len;
    uint8_t *buf;
    buf = js_load_file(ctx, &buf_len, module_name);
    if (!buf) {
        JS_ThrowReferenceError(ctx, "could not load module filename '%s'",
                               module_name);
        return NULL;
    }

    /* compile the module (validation only: never written to the output) */
    JSValue func_val = JS_Eval(ctx, (char *)buf, buf_len, module_name,
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    js_free(ctx, buf);
    if (JS_IsException(func_val))
        return NULL;

    /* the module is already referenced, so we must free it */
    m = JS_VALUE_GET_PTR(func_val);
    JS_FreeValue(ctx, func_val);
    return m;
}

// entry compilation, based on qjsc's compile_file; the output is a single
// JS_WriteObject blob of the entry module.
static void compile_file(JSContext *ctx, const char *filename,
                         const char *out_filename, int module)
{
    size_t buf_len;
    uint8_t *buf;
    int eval_flags;

    buf = js_load_file(ctx, &buf_len, filename);
    if (!buf) {
        fprintf(stderr, "Could not load '%s'\n", filename);
        exit(1);
    }
    eval_flags = JS_EVAL_FLAG_COMPILE_ONLY;
    if (module < 0) {
        module = (js__has_suffix(filename, ".mjs") ||
                  JS_DetectModule((const char *)buf, buf_len));
    }
    if (module)
        eval_flags |= JS_EVAL_TYPE_MODULE;
    else
        eval_flags |= JS_EVAL_TYPE_GLOBAL;
    JSValue obj = JS_Eval(ctx, (const char *)buf, buf_len, filename, eval_flags);
    if (JS_IsException(obj)) {
        dump_error(ctx);
        exit(1);
    }
    js_free(ctx, buf);

    size_t out_buf_len = 0;
    uint8_t *out_buf = JS_WriteObject(ctx, &out_buf_len, obj, JS_WRITE_OBJ_BYTECODE);
    JS_FreeValue(ctx, obj);
    if (!out_buf) {
        dump_error(ctx);
        exit(1);
    }

    FILE *fo = fopen(out_filename, "wb");
    if (!fo) {
        perror(out_filename);
        exit(1);
    }
    // a truncated output would fail the reader's checksum; remove it instead
    // of leaving a file that looks freshly built
    if (fwrite(out_buf, 1, out_buf_len, fo) != out_buf_len || fclose(fo) != 0) {
        fprintf(stderr, "Could not write '%s'\n", out_filename);
        remove(out_filename);
        exit(1);
    }
    js_free(ctx, out_buf);
}

int main(int argc, char **argv)
{
    int optind = 1;
    const char *out_filename = NULL;
    const char *in_filename = NULL;
    JSRuntime *rt;
    JSContext *ctx;
    int module;

    module = -1;
    while (optind < argc) {
        char *arg = argv[optind];
        if (!strcmp(arg, "--version")) {
            printf("scriptc (quickjs %d.%d.%d)\n", QJS_VERSION_MAJOR,
                   QJS_VERSION_MINOR, QJS_VERSION_PATCH);
            return 0;
        } else if (!strcmp(arg, "-o")) {
            if (++optind >= argc) {
                fprintf(stderr, "scriptc: missing argument for -o\n");
                exit(1);
            }
            out_filename = argv[optind++];
        } else if (!strcmp(arg, "-M")) {
            if (++optind >= argc) {
                fprintf(stderr, "scriptc: missing argument for -M\n");
                exit(1);
            }
            namelist_add(&cmodule_list, argv[optind++], NULL, 0);
        } else if (!strcmp(arg, "-m")) {
            // force module mode; JS_DetectModule may misclassify files whose
            // first import is relative (same as qjsc).
            module = 1;
            optind++;
        } else {
            if (in_filename) {
                fprintf(stderr, "scriptc: expected a single input file\n");
                exit(1);
            }
            in_filename = argv[optind++];
        }
    }
    if (!out_filename || !in_filename) {
        fprintf(stderr, "usage: scriptc [--version] [-m] [-M name]... -o out.bin in.js\n");
        exit(1);
    }

    rt = JS_NewRuntime();
    ctx = JS_NewContext(rt);

    /* loader for ES6 modules */
    JS_SetModuleLoaderFunc(rt, NULL, scriptc_loader, NULL);

    compile_file(ctx, in_filename, out_filename, module);

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    namelist_free(&cmodule_list);
    return 0;
}
