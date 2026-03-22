// =============================================
// crt1.c

// _UNICODE for tchar.h, UNICODE for API
#include <tchar.h>

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define _UNKNOWN_APP    0
#define _CONSOLE_APP    1
#define _GUI_APP        2

#define _MCW_PC         0x00030000 // Precision Control
#define _PC_24          0x00020000 // 24 bits
#define _PC_53          0x00010000 // 53 bits
#define _PC_64          0x00000000 // 64 bits

#ifdef _UNICODE
#define __tgetmainargs __wgetmainargs
#define _tstart _wstart
#define _tmain wmain
#define _runtmain _runwmain
#define get_tenviron _get_wenviron
#else
#define __tgetmainargs __getmainargs
#define _tstart _start
#define _tmain main
#define _runtmain _runmain
#define get_tenviron _get_environ
#endif

typedef struct { int newmode; } _startupinfo;
int __cdecl __tgetmainargs(int *pargc, _TCHAR ***pargv, _TCHAR ***penv, int globb, _startupinfo*);
int __cdecl __getmainargs(int *pargc, char ***pargv, char ***penv, int globb, _startupinfo*);
int __cdecl __wgetmainargs(int *pargc, wchar_t ***pargv, wchar_t ***penv, int globb, _startupinfo*);
int __cdecl get_tenviron(_TCHAR ***penv);
void __cdecl __set_app_type(int apptype);
unsigned int __cdecl _controlfp(unsigned int new_value, unsigned int mask);
extern int _tmain(int argc, _TCHAR * argv[], _TCHAR * env[]);
#ifdef UNICODE
__attribute__((weak)) wchar_t **__cdecl __rt_get_wenviron(void);
#else
__attribute__((weak)) char **__cdecl __rt_get_environ(void);
#endif
__attribute__((weak)) int __cdecl __rt_get_run_argstart(void);

void __tcc_run_on_exit(int ret);
int __tcc_on_exit(void *function, void *arg);
int __tcc_atexit(void (*function)(void));
void __attribute__((noreturn)) __tcc_exit(int code);

#include "crtinit.c"

/* Allow command-line globbing with "int _dowildcard = 1;" in the user source */
int _dowildcard;

static LONG WINAPI catch_sig(EXCEPTION_POINTERS *ex)
{
  return _XcptFilter(ex->ExceptionRecord->ExceptionCode, ex);
}

void _tstart(void)
{
    int ret;
    _TCHAR **env = NULL;

    _startupinfo start_info = {0};
    SetUnhandledExceptionFilter(catch_sig);
    // Sets the current application type
    __set_app_type(_CONSOLE_APP);

    // Set default FP precision to 53 bits (8-byte double)
    // _MCW_PC (Precision control) is not supported on ARM
#if defined __i386__ || defined __x86_64__
    _controlfp(_PC_53, _MCW_PC);
#endif

    __tgetmainargs(&__argc, &__targv, &env, _dowildcard, &start_info);
    run_ctors(__argc, __targv, env);
    ret = _tmain(__argc, __targv, env);
    __tcc_exit(ret);
}

// =============================================
// for 'tcc -run ,,,'

static void run_stdio_init(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}

static _TCHAR **run_get_tenviron(void)
{
    _TCHAR **env = NULL;
    _TCHAR **argv = NULL;
    int argc;
    _startupinfo start_info = {0};

#ifdef UNICODE
    if (__rt_get_wenviron)
        env = __rt_get_wenviron();
    if (!env)
        get_tenviron(&env);
#else
    if (__rt_get_environ)
        env = __rt_get_environ();
    if (!env)
        get_tenviron(&env);
#endif
    if (!env)
        __tgetmainargs(&argc, &argv, &env, 0, &start_info);
    return env;
}

typedef struct run_targv_state {
    int saved_argc;
    _TCHAR **saved_argv;
    _TCHAR **run_argv;
    int active;
} run_targv_state;

static __declspec(thread) run_targv_state *run_targv_tls;

static void free_run_targv(_TCHAR **argv)
{
    int i;

    if (!argv)
        return;
    for (i = 0; argv[i]; ++i)
        free(argv[i]);
    free(argv);
}

static _TCHAR **dup_run_targv_from_tchar(int argc, _TCHAR **argv)
{
    int i;
    _TCHAR **copy = malloc(sizeof(*copy) * (argc + 1));

    if (!copy)
        return NULL;
    for (i = 0; i < argc; ++i) {
        size_t len;

        if (!argv[i]) {
            copy[i] = NULL;
            continue;
        }
        len = _tcslen(argv[i]) + 1;
        copy[i] = malloc(sizeof(*copy[i]) * len);
        if (!copy[i])
            goto fail;
        memcpy(copy[i], argv[i], sizeof(*copy[i]) * len);
    }
    copy[argc] = NULL;
    return copy;
fail:
    copy[i] = NULL;
    free_run_targv(copy);
    return NULL;
}

static _TCHAR **dup_run_targv_from_char(int argc, char **argv)
{
    int i;
    _TCHAR **copy = malloc(sizeof(*copy) * (argc + 1));

    if (!copy)
        return NULL;
    for (i = 0; i < argc; ++i) {
#ifdef UNICODE
        size_t len;

        if (!argv[i]) {
            copy[i] = NULL;
            continue;
        }
        len = strlen(argv[i]) + 1;
        copy[i] = malloc(sizeof(*copy[i]) * len);
        if (!copy[i] || (size_t)-1 == mbstowcs(copy[i], argv[i], len))
            goto fail;
#else
        size_t len;

        if (!argv[i]) {
            copy[i] = NULL;
            continue;
        }
        len = strlen(argv[i]) + 1;
        copy[i] = malloc(len);
        if (!copy[i])
            goto fail;
        memcpy(copy[i], argv[i], len);
#endif
    }
    copy[argc] = NULL;
    return copy;
fail:
    copy[i] = NULL;
    free_run_targv(copy);
    return NULL;
}

typedef LPWSTR *(WINAPI *run_command_line_to_argv_w_func)(LPCWSTR, int *);

static wchar_t **run_command_line_to_argv_w(int *pargc)
{
    static run_command_line_to_argv_w_func fn;
    static int init;

    if (!init) {
        HMODULE dll = GetModuleHandleA("shell32.dll");
        if (!dll)
            dll = LoadLibraryA("shell32.dll");
        if (dll)
            fn = (run_command_line_to_argv_w_func)(void *)GetProcAddress(dll, "CommandLineToArgvW");
        init = 1;
    }
    return fn ? fn(GetCommandLineW(), pargc) : NULL;
}

static wchar_t *dup_run_wstr(const wchar_t *s)
{
    size_t len = wcslen(s) + 1;
    wchar_t *copy = malloc(sizeof(*copy) * len);

    if (!copy)
        return NULL;
    memcpy(copy, s, sizeof(*copy) * len);
    return copy;
}

static void free_run_wargv(wchar_t **argv)
{
    int i;

    if (!argv)
        return;
    for (i = 0; argv[i]; ++i)
        free(argv[i]);
    free(argv);
}

static int append_run_warg(wchar_t ***pargv, int *pargc, wchar_t *arg)
{
    wchar_t **next = realloc(*pargv, sizeof(**pargv) * (*pargc + 2));

    if (!next)
        return 0;
    *pargv = next;
    next[*pargc] = arg;
    next[*pargc + 1] = NULL;
    ++*pargc;
    return 1;
}

static int run_has_wildcard(const wchar_t *s)
{
    for (; *s; ++s)
        if (*s == L'*' || *s == L'?')
            return 1;
    return 0;
}

static int run_expand_wildcard_arg(const wchar_t *pattern, wchar_t ***pargv, int *pargc)
{
    WIN32_FIND_DATAW data;
    HANDLE h = INVALID_HANDLE_VALUE;
    const wchar_t *p;
    wchar_t **matches = NULL;
    int nmatches = 0, i, j, ok = 0;
    size_t prefix_len = 0;

    for (p = pattern; *p; ++p) {
        if (*p == L'\\' || *p == L'/' || *p == L':')
            prefix_len = (size_t)(p - pattern + 1);
    }

    h = FindFirstFileW(pattern, &data);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        wchar_t *full;
        size_t name_len;

        if (data.cFileName[0] == L'.'
            && (!data.cFileName[1] || (data.cFileName[1] == L'.' && !data.cFileName[2])))
            continue;
        name_len = wcslen(data.cFileName);
        full = malloc(sizeof(*full) * (prefix_len + name_len + 1));
        if (!full)
            goto done;
        memcpy(full, pattern, sizeof(*full) * prefix_len);
        memcpy(full + prefix_len, data.cFileName, sizeof(*full) * (name_len + 1));
        if (!append_run_warg(&matches, &nmatches, full)) {
            free(full);
            goto done;
        }
    } while (FindNextFileW(h, &data));
    for (i = 0; i + 1 < nmatches; ++i) {
        for (j = i + 1; j < nmatches; ++j) {
            if (wcscmp(matches[j], matches[i]) < 0) {
                wchar_t *tmp = matches[i];
                matches[i] = matches[j];
                matches[j] = tmp;
            }
        }
    }
    for (i = 0; i < nmatches; ++i) {
        wchar_t *copy = dup_run_wstr(matches[i]);

        if (!copy || !append_run_warg(pargv, pargc, copy)) {
            free(copy);
            goto done;
        }
    }
    ok = nmatches != 0;
done:
    if (h != INVALID_HANDLE_VALUE)
        FindClose(h);
    free_run_wargv(matches);
    return ok;
}

static wchar_t **build_run_wargv(int *prun_argc)
{
    wchar_t **cmd_argv = NULL, **run_argv = NULL;
    int cmd_argc, run_argc = 0, base, i;

    if (!__rt_get_run_argstart)
        return NULL;
    base = __rt_get_run_argstart();
    if (base < 0)
        return NULL;
    cmd_argv = run_command_line_to_argv_w(&cmd_argc);
    if (!cmd_argv || base >= cmd_argc)
        goto fail;
    for (i = base; i < cmd_argc; ++i) {
        wchar_t *copy;

        if (_dowildcard && i > base && run_has_wildcard(cmd_argv[i])) {
            if (run_expand_wildcard_arg(cmd_argv[i], &run_argv, &run_argc))
                continue;
        }
        copy = dup_run_wstr(cmd_argv[i]);
        if (!copy || !append_run_warg(&run_argv, &run_argc, copy)) {
            free(copy);
            goto fail;
        }
    }
    LocalFree(cmd_argv);
    *prun_argc = run_argc;
    return run_argv;
fail:
    if (cmd_argv)
        LocalFree(cmd_argv);
    free_run_wargv(run_argv);
    return NULL;
}

#ifndef UNICODE
static _TCHAR **dup_run_targv_from_wchar(int argc, wchar_t **argv)
{
    int i;
    _TCHAR **copy = malloc(sizeof(*copy) * (argc + 1));

    if (!copy)
        return NULL;
    for (i = 0; i < argc; ++i) {
        size_t len;

        if (!argv[i]) {
            copy[i] = NULL;
            continue;
        }
        len = wcstombs(NULL, argv[i], 0);
        if ((size_t)-1 == len)
            goto fail;
        copy[i] = malloc(len + 1);
        if (!copy[i] || (size_t)-1 == wcstombs(copy[i], argv[i], len + 1))
            goto fail;
    }
    copy[argc] = NULL;
    return copy;
fail:
    copy[i] = NULL;
    free_run_targv(copy);
    return NULL;
}
#endif

static void restore_run_targv(int ret, void *opaque)
{
    run_targv_state *state = (run_targv_state *)opaque;

    (void)ret;
    if (!state || !state->active)
        return;
    __argc = state->saved_argc;
    __targv = state->saved_argv;
    free_run_targv(state->run_argv);
    state->run_argv = NULL;
    state->active = 0;
    if (run_targv_tls == state)
        run_targv_tls = NULL;
}

static void restore_run_targv_atexit(void)
{
    restore_run_targv(0, run_targv_tls);
}

int _runtmain(int argc, /* as tcc passed in */ char **argv)
{
    int ret;
    int run_argc = argc;
    _TCHAR **env = run_get_tenviron();
    run_targv_state argv_state = { __argc, __targv, NULL, 0 };
    wchar_t **run_wargv = NULL;

    run_wargv = build_run_wargv(&run_argc);
    if (run_wargv) {
#ifdef UNICODE
        argv_state.run_argv = dup_run_targv_from_tchar(run_argc, run_wargv);
#else
        argv_state.run_argv = dup_run_targv_from_wchar(run_argc, run_wargv);
#endif
        free_run_wargv(run_wargv);
    }
    if (!argv_state.run_argv) {
        argv_state.run_argv = dup_run_targv_from_char(argc, argv);
        run_argc = argc;
    }
    if (!argv_state.run_argv)
        return 1;
    run_targv_tls = &argv_state;
    if (__tcc_atexit(restore_run_targv_atexit))
        goto fail;
    __argc = run_argc;
    __targv = argv_state.run_argv;
    argv_state.active = 1;
#if defined __i386__ || defined __x86_64__
    _controlfp(_PC_53, _MCW_PC);
#endif
    run_stdio_init();
    run_ctors(__argc, __targv, env);
    ret = _tmain(__argc, __targv, env);
    run_dtors();
    __tcc_run_on_exit(ret);
    return ret;
fail:
    run_targv_tls = NULL;
    free_run_targv(argv_state.run_argv);
    return 1;
}

// =============================================
