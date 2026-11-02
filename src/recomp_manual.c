/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>   /* ptrdiff_t: not in stdlib.h off MSVC */
#include <stdlib.h>   /* getenv, exit: the spin verdict below */
#include <string.h>   /* strcmp: RECOMP_ICALL_FATAL */

/* One diagnostic report, one piece.
 *
 * Each report below is a dozen fprintf calls and the CRT takes its lock per
 * call, so with more than one guest thread running the lines interleave in
 * the middle of a report. Dino Crisis 3 runs six workers once it is past its
 * menus, and its log came out like this:
 *
 *   callers: 0x004DB424 <- 0x002705B4 <- 0x002705B4[ICALL] unresolved jump
 *   target 0x41B0B870 -- 1 time(s) (total calls: 6930990)
 *
 * -- two threads' reports spliced together, with a caller list that stops
 * mid-sentence and an address that belongs to neither line as read. These
 * diagnostics exist to be read during exactly the multi-threaded bring-up
 * that breaks them.
 *
 * The CRT exposes the lock these calls were already taking one at a time, so
 * hold it across the whole report instead. */
#if defined(_MSC_VER)
#  define RECOMP_DIAG_LOCK()   _lock_file(stderr)
#  define RECOMP_DIAG_UNLOCK() _unlock_file(stderr)
#elif defined(__unix__) || defined(__APPLE__)
#  define RECOMP_DIAG_LOCK()   flockfile(stderr)
#  define RECOMP_DIAG_UNLOCK() funlockfile(stderr)
#else
#  define RECOMP_DIAG_LOCK()   ((void)0)
#  define RECOMP_DIAG_UNLOCK() ((void)0)
#endif

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;

typedef void (*recomp_func_t)(void);

/* ── Register state (defined in xbox_memory_layout.c) ──────── */

/* The guest registers (g_eax, g_esp, g_icall_saved_esp, ...) are fields of
 * one thread-local struct, declared once in recomp_cpu.h, which the lifter
 * copies into gen/ beside the code it describes. Take them from there; never
 * redeclare one here. They used to be redeclared by hand, and a declaration
 * without the thread-local storage class linked and silently read other
 * storage, so every read got 0 -- which is why the log below once reported
 * no call site: not because the guest esp was stale, but because this file
 * was not reading the guest esp at all. The names are macros now, so a
 * redeclaration no longer compiles. */
#include "recomp/gen/recomp_cpu.h"

/* Thread-local storage for this file's own state (the call chain below). */
#if defined(_MSC_VER)
#  define RECOMP_MANUAL_TLS __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#  define RECOMP_MANUAL_TLS __thread
#else
#  define RECOMP_MANUAL_TLS _Thread_local
#endif

/* g_eax: recomp_cpu.h. */
/* The guest stack pointer. At the moment an indirect call is refused, the
 * caller has already pushed its guest return address, so the top of the
 * guest stack is the call site -- the one thing the old log did not say. */
/* g_esp: recomp_cpu.h. */
extern ptrdiff_t g_xbox_mem_offset;
/* The lifted code sections, so a value on the guest stack can be told
 * apart from data when naming the callers of a refused call. */
extern uint32_t g_xbox_code_lo;
extern uint32_t g_xbox_code_hi;
/* The esp the dispatch macro captured. Not g_esp, which is stale by the time
 * a refused call is reported. Zero when the title's generated header predates
 * this, and the log then says it has no callers rather than inventing them. */
/* g_icall_saved_esp: recomp_cpu.h. */
/* Which dispatch form was refused: 0 unknown, 1 call, 2 jump. Unknown
 * means this title was lifted before the macros published it. */
/* g_icall_dispatch_form: recomp_cpu.h. */

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    (void)xbox_va;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(uint32_t va)
{
    /*
     * Rate-limited per target, reporting at 1, 10, 100 ...
     *
     * An unresolved target inside a loop is the normal case, not the rare
     * one: Mortal Kombat: Deadly Alliance produced 46843 of these for a
     * single address in one 30-second run, seventeen lines each. That buries
     * every other diagnostic in the log and says nothing the first report did
     * not. The progression is the useful part -- one line says a target was
     * never identified, a run of them says the title is stuck on it.
     */
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }

    RECOMP_DIAG_LOCK();
    fprintf(stderr, "[ICALL] unresolved %starget 0x%08X -- %llu time(s) "
                    "(total calls: %llu)\n",
            g_icall_dispatch_form == 1 ? "call " :
            g_icall_dispatch_form == 2 ? "jump " : "",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);
    if (g_icall_dispatch_form == 2 && hits[i] == 1)
        fprintf(stderr, "  a jump, not a call: if this address is inside a "
                        "function rather than at its start, the guest is doing "
                        "its own control flow (a coroutine, a longjmp, or a "
                        "switch arm) and seeding it as a function will not "
                        "help\n");

    /*
     * Who called it. The lifted caller pushed its return address on the guest
     * stack, so code-range values above esp name the chain -- the same scan
     * the memory watchpoints use. It is a heuristic: stale return addresses
     * from earlier frames show up too, and the first entry is the reliable
     * one. It is still the difference between an address with no context and
     * a function to open, which is what an unresolved target needs.
     */
    {
        const uint8_t *stack = (const uint8_t *)g_xbox_mem_offset
                             + g_icall_saved_esp;
        int shown = 0;
        fprintf(stderr, "  callers:");
        for (i = 0; g_xbox_mem_offset && g_icall_saved_esp
                    && i < 160 && shown < 6; i++) {
            uint32_t v;
            memcpy(&v, stack + (size_t)i * 4, sizeof v);
            if (v >= g_xbox_code_lo && v < g_xbox_code_hi) {
                fprintf(stderr, "%s 0x%08X", shown ? " <-" : "", v);
                shown++;
            }
        }
        if (!shown)
            fprintf(stderr, " (none on the stack)");
        fprintf(stderr, "\n");
    }

    /* The recent-target ring buffer, once per address. What ran just before
     * an unresolved call is context for the first report and noise after. */
    if (hits[i] == 1) {
        fprintf(stderr, "  Recent ICALL targets:\n");
        for (i = 0; i < 16; i++) {
            int idx = (g_icall_trace_idx - 16 + i) & 15;
            if (g_icall_trace[idx])
                fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
        }
    }
    RECOMP_DIAG_UNLOCK();
    fflush(stderr);
}
/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va, uint32_t saved_esp)
{
    /* A power of ten, because the rate limiter above only reaches this
     * function body at 1, 10, 100 ... and the verdict has to land on one of
     * those. A hundred thousand skips of one target is unambiguous: no title
     * makes progress through that. */
    enum { SLOTS = 16, SPIN_VERDICT = 100000 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    static int said_it;   /* the verdict below is said once, not once per target */
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    {
        /* The call site, read off the guest stack. Without it the log
         * says a wild pointer was skipped but not by whom, and the
         * caller is the only thing that leads anywhere.
         *
         * It comes from the esp the dispatch macro captured, not from
         * g_esp. The lifted call site pushes its return address onto the
         * *local* esp and only syncs g_esp at certain points, so g_esp is
         * stale here -- it read 0 exactly when a null target most needed
         * explaining, which sent three rounds of Jet Set Radio Future
         * chasing inferences instead of a call site. saved_esp is the value
         * before that push, so the return address is the dword below it.
         *
         * Only for the call form. A tail jump pushes no return address, so
         * that dword is whatever the frame happened to leave there: Tony
         * Hawk's Pro Skater 2X reported "from 0x00000008" and then "from
         * 0x00000004" for the same million skipped calls, and both are
         * plainly not code. Printing a number that cannot be an address as
         * though it were the caller is worse than printing nothing, because
         * it is the one field a reader goes to next.
         *
         * So: name the form, and where the top of the stack cannot answer,
         * scan for code-range values the way the unresolved-target logger
         * above does. Stale return addresses from earlier frames come back
         * too and the first is the reliable one, but a list of real code
         * addresses is a place to start and 0x00000008 is not. */
        uint32_t caller = 0;
        if (g_icall_dispatch_form != 2 && saved_esp >= 4 && g_xbox_mem_offset)
            caller = *(const uint32_t *)((const uint8_t *)g_xbox_mem_offset
                                         + (saved_esp - 4));
        if (caller < g_xbox_code_lo || caller >= g_xbox_code_hi)
            caller = 0;
        RECOMP_DIAG_LOCK();
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped "
                        "%llu time(s) via a %s",
                va, (unsigned long long)hits[i],
                g_icall_dispatch_form == 2 ? "jump" : "call");
        if (caller)
            fprintf(stderr, " from 0x%08X", caller);
        fprintf(stderr, " (null or wild function pointer, at call #%llu)\n",
                (unsigned long long)g_icall_count);

        /* The stack scan, once per address: the same heuristic the
         * unresolved-target logger uses, and the only thing that names a
         * caller when the return address is not where it should be. */
        if (hits[i] == 1) {
            const uint8_t *stack = (const uint8_t *)g_xbox_mem_offset
                                 + saved_esp;
            int shown = 0, k;
            fprintf(stderr, "  callers:");
            for (k = 0; g_xbox_mem_offset && saved_esp
                        && k < 160 && shown < 6; k++) {
                uint32_t v;
                memcpy(&v, stack + (size_t)k * 4, sizeof v);
                if (v >= g_xbox_code_lo && v < g_xbox_code_hi) {
                    fprintf(stderr, "%s 0x%08X", shown ? " <-" : "", v);
                    shown++;
                }
            }
            if (!shown)
                fprintf(stderr, " (none on the stack)");
            fprintf(stderr, "\n");
        }

        /* RECOMP_ICALL_FATAL=1: fault here instead of skipping, so the crash
         * handler prints the guest call stack that reached this call.
         *
         * The caller printed above is read from the top of the guest stack,
         * which is right only when the lifted call site pushed a return
         * address there and nothing has since moved esp. When it reads 0 --
         * exactly the case where a null target most needs explaining -- the
         * line says a wild pointer was skipped and nothing about by whom. A
         * deliberate fault costs the run and buys the backtrace. */
        {
            static int fatal = -1;
            if (fatal < 0) {
                const char *v = getenv("RECOMP_ICALL_FATAL");
                fatal = v && *v && strcmp(v, "0") != 0;
            }
            if (fatal) {
                fprintf(stderr, "[ICALL] RECOMP_ICALL_FATAL: faulting here for "
                                "a backtrace\n");
                fflush(stderr);
                *(volatile int *)0 = 1;
            }
        }
    }

    /* Past a certain count this stops being a warning and becomes a verdict.
     *
     * A skipped call sets eax to 0 and returns. Zero is a fine answer for a
     * null function pointer in most code -- it reads as NULL, false, or
     * nothing -- but it is also S_OK, so in COM-shaped code a loop that
     * repeats while the result is non-negative can never leave. Panzer
     * Dragoon Orta hung exactly there, three billion skips in forty seconds,
     * with no frame ever presented.
     *
     * The value is deliberately left alone: there is no return value that is
     * right for both conventions, and guessing failure would break the
     * callers for which zero is correct. What can be fixed is the silence.
     * Say plainly that the title is hung, once, and say what to do about it.
     */
    if (hits[i] == SPIN_VERDICT && !said_it) {
        said_it = 1;
        fprintf(stderr,
            "[ICALL] ^^ this is a hang, not slow progress. One target has been\n"
            "        skipped %d times. The skip returns eax = 0, which is also\n"
            "        S_OK, so a loop testing for a non-negative result will\n"
            "        never exit. Try `py -3 -m tools.seed_from_log <log>` to\n"
            "        recover the target as a function, and read\n"
            "        docs/technical/memory-watchpoints.md for who wrote the\n"
            "        pointer. Set RECOMP_ICALL_SPIN_FATAL=1 to stop here\n"
            "        instead of spinning.\n", SPIN_VERDICT);
        fflush(stderr);
        if (getenv("RECOMP_ICALL_SPIN_FATAL")) {
            fprintf(stderr, "[ICALL] RECOMP_ICALL_SPIN_FATAL is set; exiting.\n");
            fflush(stderr);
            exit(3);
        }
    }
    RECOMP_DIAG_UNLOCK();
    fflush(stderr);
}


/* ── TimeSplitters 2: the widescreen camera ───────────────── */

/*
 * TimeSplitters 2 has no 16:9 mode. The toolkit's Hor+ (RECOMP_HOR_PLUS)
 * widens it by scaling the projection register as it is uploaded, but the
 * game culls against its own numbers: past the old 4:3 edges, and through
 * doorways, whole pieces of the level were missing and the sky showed
 * through the walls.
 *
 * The in-game camera is set up in one place, camera_setup(near, far,
 * aspect, fov_degrees). It turns the aspect into the screen's
 * (aspect * width / height), builds the projection from it with
 * build_projection(out, aspect, fovy, near, far), and stores tan(fov/2) at
 * camera+0x31C and aspect * tan(fov/2) -- the horizontal half-width the
 * culling uses -- at camera+0x320. Widening the aspect argument on the
 * way in widens both, so what is drawn and what is kept agree.
 *
 * Only this camera: the other callers of build_projection are the front
 * end's 3D (character select and the like), which is kept at 4:3.
 *
 * Names like camera_setup are the game's functions by what they do. Where
 * each one is depends on the release: each release's
 * config/<region>/overrides.inc wraps its own with the hooks in this file
 * (see the end of it), so nothing in this file is an address.
 */
/* xboxrecomp src/d3d/d3d8_xbox.h */
extern float xbox_D3D8ClaimHorPlus(void);
extern void  xbox_D3D8SetWideFrames(int wide);

#define TS2_MEMF(a) (*(volatile float *)((uintptr_t)(uint32_t)(a) + g_xbox_mem_offset))

/* The factor to multiply the camera's aspect by: 1 when no wider view is
 * wanted, 4/3 for the usual Hor+ 0.75. Claiming it stops the toolkit
 * scaling the register as well, which would widen the view twice. */
static float ts2_camera_widen(void)
{
    static float widen;

    if (widen == 0.0f) {
        float factor = xbox_D3D8ClaimHorPlus();

        widen = (factor > 0.0f && factor != 1.0f) ? 1.0f / factor : 1.0f;
    }
    return widen;
}

/* What the frame now being drawn has run, for the choice below. */
static int g_ts2_frame_camera, g_ts2_frame_front_end;

extern const char *recomp_config_lookup(const char *env_name, const char *key);

static void ts2_aim_note_fov(float fov_degrees);   /* the mouse aim, below */

/*
 * The field of view, as the player likes it: RECOMP_TS2_FOV / ts2_fov, a
 * percentage of the game's own (100, the default, changes nothing; 70..150).
 * Applied to camera_setup's fov_degrees argument, the same way the
 * widescreen aspect is, so the projection and the culling half-width
 * (camera+0x31C/0x320) both follow and nothing pops at the edges. Every
 * field of view the game asks for is scaled, a scope's too, so zooming in
 * still zooms by the game's own ratio; cutscenes framed by the in-game
 * camera are widened as well.
 */
static float ts2_fov_scale(void)
{
    static float scale;

    if (scale == 0.0f) {
        const char *v = recomp_config_lookup("RECOMP_TS2_FOV", "ts2_fov");
        int pct = v && *v ? atoi(v) : 100;

        if (pct < 70)  pct = 70;
        if (pct > 150) pct = 150;
        scale = (float)pct / 100.0f;
        if (pct != 100)
            fprintf(stderr, "[TS2] field of view %d%% of the game's (RECOMP_TS2_FOV)\n", pct);
    }
    return scale;
}

static void ts2_camera_setup(void (*camera_setup)(void))
{
    float widen = ts2_camera_widen();
    float fov_scale = ts2_fov_scale();

    ts2_aim_note_fov(TS2_MEMF(g_esp + 0x10));   /* fov_degrees, for the scope */
    if (fov_scale != 1.0f) {
        float fov = TS2_MEMF(g_esp + 0x10) * fov_scale;
        TS2_MEMF(g_esp + 0x10) = fov > 170.0f ? 170.0f : fov;
    }
    if (widen != 1.0f)
        TS2_MEMF(g_esp + 0x0C) *= widen;   /* [esp] is the return address */
    camera_setup();
    g_ts2_frame_camera = 1;                /* see ts2_present below */
}

/*
 * The front end at 4:3, if asked (RECOMP_TS2_MENUS=43 below). It is laid
 * out for 4:3 and mixes 3D backdrops with 2D panels; left to the
 * renderer's width guess its pieces juddered between stretched and
 * squeezed as they animated. The placement table below fixes that, and
 * this is the fallback: the front end shown at 4:3 between bars, as the
 * console drew it, and the levels and their cutscenes at 16:9.
 *
 * Which screen a frame belongs to, traced frame by frame through a run:
 *
 *   menus, story level select   in-game camera + front_end_scene
 *   in-engine cutscenes         in-game camera
 *   playing                     in-game camera
 *   loading screens             neither
 *
 * front_end_scene sets up the front end's own 3D scene (it builds a
 * camera of its own). The front end draws its backdrop through the
 * in-game camera as well, hence the pair.
 *
 * present is the game's once-a-frame present (it calls
 * D3DDevice_Swap). After a frame is presented, the next is given the shape
 * the finished one called for: a frame's own draws are not all in yet
 * when it starts, and a screen lasts many frames, so the only cost is one
 * frame at each change, which falls on a fade or a load.
 */
static void ts2_front_end_scene(void (*front_end_scene)(void))
{
    g_ts2_frame_front_end = 1;
    front_end_scene();
}

/*
 * RECOMP_TS2_MENUS / ts2_menus = wide (the default) lays the front end out
 * for 16:9 with the placement table below, so every screen is widescreen;
 * 43 keeps the front end and the loading screens at 4:3 between bars, as
 * above.
 */
extern const char *recomp_config_lookup(const char *env_name, const char *key);

static int ts2_menus_wide(void)
{
    static int wide = -1;

    if (wide < 0) {
        const char *v = recomp_config_lookup("RECOMP_TS2_MENUS", "ts2_menus");

        wide = !(v && (!strcmp(v, "43") || !strcmp(v, "4:3")));
        fprintf(stderr, "[TS2-UI] menus %s (RECOMP_TS2_MENUS=wide|43)\n",
                wide ? "laid out for 16:9" : "at 4:3");
    }
    return wide;
}

/* ── TimeSplitters 2: aiming with the mouse ───────────────── */

/*
 * On the Xbox, TimeSplitters 2 lays two motions over each other on the
 * right stick. Without the aim button the stick turns the camera, and the
 * crosshair and the gun lag a little behind the turn (hip fire). With the
 * aim button held the camera stops and the stick moves the crosshair across
 * the screen instead -- slow and imprecise, because a thumb is. Both exist
 * because a stick says how fast, not where.
 *
 * A mouse says where. So the mouse does not go through the stick at all
 * (xboxrecomp recomp_mouse_take: while this takes the motion, controller
 * 1's right stick is left alone): its counts are added straight to the
 * view angles, once a frame. The game sees a centred right stick, so the
 * crosshair has nothing to lag behind and stays in the middle, and every
 * shot goes where the view points. The aim button is then only needed
 * where the game needs it -- a scope's zoom -- and there the mouse turns
 * the view directly too, slower in proportion to the zoom (the field of
 * view comes from camera_setup, below), so a pixel on screen is the same
 * hand movement zoomed in and out.
 *
 * When this is not taking (the front end, loading screens, and while the
 * addresses below are unknown), the toolkit's mouse look on the stick has
 * the mouse instead, with nothing to switch.
 *
 * Where the angles live is per release and found by playing: see the probe
 * below and docs/mouse-aim.md. Until a release's table has them, they can
 * be given at run time without a rebuild:
 *
 *   RECOMP_TS2_AIM / ts2_aim = yaw=0x...,pitch=0x...[,base=0x...]
 *        [,yaw_sign=1][,pitch_sign=1][,units=1][,wrap=6.283185]
 *        [,pitch_min=-1.5][,pitch_max=1.5][,active=0x...][,active_is=1]
 *        [,cross_x=0x...][,cross_y=0x...][,frame_time=0x...]
 *
 *   base      an address that holds a pointer (player 1); yaw, pitch,
 *             cross_* and active are then offsets from what it points at
 *   units     the angles' units per radian: 1 radians, 57.29578 degrees
 *   wrap      yaw is kept in [0, wrap) when non-zero, or [-wrap/2, wrap/2)
 *             for a game that keeps it there (seen as a negative yaw)
 *   active    a 32-bit value that equals active_is while player 1 is in
 *             control (not paused, not in a cutscene); unknown: always
 *   cross_*   the crosshair's (and hip sway's) offsets, held at 0 while
 *             the mouse aims
 *   frame_time  the game's frame-time scale (a float global, read as an
 *             address, not from base): 0 while the game is paused, so the
 *             mouse does not turn the view behind the pause menu
 *
 *   RECOMP_TS2_MOUSE_AIM / ts2_mouse_aim = native (the default) | stick
 *             stick keeps the toolkit's mouse look on the stick everywhere
 */
#define TS2_MEM32_EARLY(a) (*(volatile uint32_t *)((uintptr_t)(uint32_t)(a) + g_xbox_mem_offset))

extern int recomp_mouse_take(float *dx, float *dy);
extern int recomp_mouse_sensitivity(void);
extern int recomp_mouse_look(void);

typedef struct Ts2Aim {
    uint32_t base;                  /* 0: the fields below are addresses */
    uint32_t yaw, pitch;            /* 0: not known, no native aim */
    uint32_t cross_x, cross_y;      /* 0: not known */
    uint32_t active;                /* 0: not known, always in control */
    uint32_t active_is;
    int      yaw_sign, pitch_sign;  /* +1 or -1: the stick's own sign */
    float    units;                 /* per radian */
    float    wrap;                  /* 0: no wrap */
    float    pitch_min, pitch_max;  /* in the angles' units; equal: no clamp */
    uint32_t frame_time;            /* 0: not known; a global, never from base */
    uint32_t weapons;               /* 0: not known; the weapon table, a global */
    uint32_t pitch_limit;           /* 0: not known; the game's, a float global */
} Ts2Aim;

extern int recomp_input_host_last_sticks(short out[4]);

/* Each release's own, in config/<region>/overrides.inc. */
extern const Ts2Aim k_ts2_aim;

static Ts2Aim g_ts2_aim;
static int    g_ts2_aim_ready, g_ts2_aim_on;
static float  g_ts2_fov_now, g_ts2_fov_base;   /* degrees, from camera_setup */

static void ts2_aim_parse(Ts2Aim *a, const char *s)
{
    char key[16];

    while (s && *s) {
        const char *eq = strchr(s, '='), *end = strchr(s, ',');
        size_t n;

        if (!eq || (end && end < eq))
            break;
        n = (size_t)(eq - s);
        if (n >= sizeof key)
            n = sizeof key - 1;
        memcpy(key, s, n);
        key[n] = 0;
        eq++;
        if      (!strcmp(key, "base"))       a->base      = (uint32_t)strtoul(eq, NULL, 0);
        else if (!strcmp(key, "yaw"))        a->yaw       = (uint32_t)strtoul(eq, NULL, 0);
        else if (!strcmp(key, "pitch"))      a->pitch     = (uint32_t)strtoul(eq, NULL, 0);
        else if (!strcmp(key, "cross_x"))    a->cross_x   = (uint32_t)strtoul(eq, NULL, 0);
        else if (!strcmp(key, "cross_y"))    a->cross_y   = (uint32_t)strtoul(eq, NULL, 0);
        else if (!strcmp(key, "active"))     a->active    = (uint32_t)strtoul(eq, NULL, 0);
        else if (!strcmp(key, "active_is"))  a->active_is = (uint32_t)strtoul(eq, NULL, 0);
        else if (!strcmp(key, "yaw_sign"))   a->yaw_sign   = atoi(eq) < 0 ? -1 : 1;
        else if (!strcmp(key, "pitch_sign")) a->pitch_sign = atoi(eq) < 0 ? -1 : 1;
        else if (!strcmp(key, "units"))      a->units     = (float)atof(eq);
        else if (!strcmp(key, "wrap"))       a->wrap      = (float)atof(eq);
        else if (!strcmp(key, "pitch_min"))  a->pitch_min = (float)atof(eq);
        else if (!strcmp(key, "pitch_max"))  a->pitch_max = (float)atof(eq);
        else if (!strcmp(key, "frame_time")) a->frame_time = (uint32_t)strtoul(eq, NULL, 0);
        else if (!strcmp(key, "weapons"))    a->weapons   = (uint32_t)strtoul(eq, NULL, 0);
        else if (!strcmp(key, "pitch_limit")) a->pitch_limit = (uint32_t)strtoul(eq, NULL, 0);
        else
            fprintf(stderr, "[TS2-AIM] RECOMP_TS2_AIM: unknown key '%s'\n", key);
        s = end ? end + 1 : NULL;
    }
}

static void ts2_aim_init(void)
{
    const char *mode, *over;

    if (g_ts2_aim_ready)
        return;
    g_ts2_aim_ready = 1;
    g_ts2_aim = k_ts2_aim;
    if (!g_ts2_aim.yaw_sign)   g_ts2_aim.yaw_sign = 1;
    if (!g_ts2_aim.pitch_sign) g_ts2_aim.pitch_sign = 1;
    if (g_ts2_aim.units == 0.0f) g_ts2_aim.units = 1.0f;
    over = recomp_config_lookup("RECOMP_TS2_AIM", "ts2_aim");
    if (over && *over)
        ts2_aim_parse(&g_ts2_aim, over);
    mode = recomp_config_lookup("RECOMP_TS2_MOUSE_AIM", "ts2_mouse_aim");
    g_ts2_aim_on = !(mode && !strcmp(mode, "stick")) &&
                   (recomp_mouse_look() || getenv("RECOMP_TS2_AIM_FAKE"));
    if (!g_ts2_aim_on && !recomp_mouse_look())
        return;                                 /* no mouse in play: say nothing */
    if (!g_ts2_aim_on)
        fprintf(stderr, "[TS2-AIM] the mouse turns the right stick (ts2_mouse_aim = stick)\n");
    else if (!g_ts2_aim.yaw || !g_ts2_aim.pitch)
        fprintf(stderr, "[TS2-AIM] native mouse aim: this release's view angles are not "
                        "known yet, so the mouse turns the right stick (docs/mouse-aim.md)\n");
    else
        fprintf(stderr, "[TS2-AIM] native mouse aim: yaw 0x%08X pitch 0x%08X%s%s, %g units "
                        "a radian, pitch %g..%g\n",
                g_ts2_aim.yaw, g_ts2_aim.pitch, g_ts2_aim.base ? " from *base " : "",
                g_ts2_aim.cross_x ? ", crosshair held centred" : "",
                (double)g_ts2_aim.units, (double)g_ts2_aim.pitch_min,
                (double)g_ts2_aim.pitch_max);
    fflush(stderr);
}

/* A field's guest address, through the player pointer when there is one;
 * 0 when that pointer is not set (no level loaded). */
static uint32_t ts2_aim_at(uint32_t field)
{
    uint32_t p;

    if (!field)
        return 0;
    if (!g_ts2_aim.base)
        return field;
    p = TS2_MEM32_EARLY(g_ts2_aim.base);
    /* The heap, or the contiguous window over it (0x80000000 up), where
     * TS2 allocates its players (0x82C14DF0 in Siberia). */
    return ((p >= 0x10000u && p < 0x08000000u) || (p >= 0x80000000u && p < 0x84000000u))
               ? p + field : 0;
}

/* Radians a count, before the zoom: at the default sensitivity (80) about
 * 5,700 counts turn the view once round -- 18 cm on an 800 dpi mouse. */
#define TS2_AIM_RAD_PER_COUNT_80 0.0011f

/* Once a frame, after it is presented (ts2_present): the game's next frame
 * reads the angles as if its own stick handling had put them there. */
/*
 * Whether the game itself would turn this player with the stick now, by the
 * tests its look code makes (PAL FUN_00148EB0, read in Ghidra): the look
 * flag, bit 3 of +0xC; nothing holding the player, +0x998 == 0; and a
 * movement mode whose look is the free one, +0x158 (6 and 9 have none, 7
 * and 8 turn something else within limits of their own). The title screen
 * and the menus run levels of their own, and before the story starts the
 * players' table holds a stale pointer whose bytes are text: +0x998 and the
 * mode rule that out. Same object layout in every release.
 * Returns 1 to aim, 2 for modes 7 and 8 (the stick keeps the mouse there),
 * 0 otherwise (the mouse is dropped).
 */
static int ts2_aim_in_control(uint32_t obj)
{
    uint32_t flags = TS2_MEM32_EARLY(obj + 0xC);
    uint32_t held  = TS2_MEM32_EARLY(obj + 0x998);
    uint32_t mode  = TS2_MEM32_EARLY(obj + 0x158);

    if (!(flags & 8) || held != 0 || mode >= 16 || mode == 6 || mode == 9)
        return 0;
    return mode == 7 || mode == 8 ? 2 : 1;
}

/*
 * The aim mode without the aim button. On the console the button switches
 * TS2 into its aim mode (bit 1 of the player's +0xC, PAL FUN_00140F40): the
 * crosshair shows and the right stick moves it, the camera turning only past
 * half the stick. With the mouse the camera is turned directly and the stick
 * stays centred, so held in the aim mode the crosshair sits in the middle and
 * shots go where the mouse points, with no button: a PC shooter's aiming.
 * So while the mouse is what is aiming, the mode is held on -- except with a
 * scoped weapon (flag 0x20 of its row in the weapon table, rows 0x240 apart,
 * the test FUN_00140F40 makes), where the button is the zoom and stays the
 * button. The pad's right stick moved past a third gives the stick back its
 * console handling, and the mode is put back as it was, if this set it.
 * ts2_aim_mode = button keeps the console's way with the mouse too.
 */
static float ts2_aim_pitch_limit(void)
{
    static float lim = -1.0f;

    if (lim < 0.0f) {
        const char *v = recomp_config_lookup("RECOMP_TS2_PITCH_LIMIT", "ts2_pitch_limit");
        lim = v && *v ? (float)atof(v) : 85.0f;
        if (lim < 10.0f) lim = 10.0f;
        if (lim > 89.0f) lim = 89.0f;
    }
    return lim;
}

static void ts2_aim_mode_auto(uint32_t obj, int moved)
{
    static int mode = -1, mouse_in_use, forced;
    short st[4];
    int want = 0;

    if (mode < 0) {
        const char *m = recomp_config_lookup("RECOMP_TS2_AIM_MODE", "ts2_aim_mode");
        mode = !(m && !strcmp(m, "button"));
    }
    if (moved)
        mouse_in_use = 1;
    if (recomp_input_host_last_sticks(st) &&
        (st[2] > 11000 || st[2] < -11000 || st[3] > 11000 || st[3] < -11000))
        mouse_in_use = 0;
    if (mode && mouse_in_use && g_ts2_aim.weapons) {
        uint32_t body = TS2_MEM32_EARLY(obj + 0x1BC), chr, w;
        chr = body >= 0x10000u ? TS2_MEM32_EARLY(body + 0x124) : 0;
        w = chr >= 0x10000u ? TS2_MEM32_EARLY(chr + 0x438) : 0xFFFFFFFFu;
        /* Only where the game allows the mode itself: a weapon in hand
         * (+0xC8) and none being changed (+0xCC the same). Elsewhere it
         * turns a set bit into a pending one (0x10), which it then honours
         * with whatever is drawn next, zooming a scope in. */
        want = chr >= 0x10000u && w < 256 &&
               TS2_MEM32_EARLY(chr + 0xC8) != 0 &&
               TS2_MEM32_EARLY(chr + 0xC8) == TS2_MEM32_EARLY(chr + 0xCC) &&
               !(TS2_MEM32_EARLY(g_ts2_aim.weapons + w * 0x240) & 0x20);
    }
    if (want)
        TS2_MEM32_EARLY(obj + 0xC) |= 2u;
    else if (forced)
        TS2_MEM32_EARLY(obj + 0xC) &= ~(2u | 0x10u);
    forced = want;

    /* Walking without looking up or down, TS2 levels the view by itself
     * (FUN_00148EB0: +0x150 goes to 2 when the stick's vertical is still and
     * the player moves, and 2 eases the pitch to 0). The mouse never moves
     * the stick, so the view kept sinking back to the horizon as soon as
     * the player walked or was knocked back by a shot. 1 is the state the
     * game is in while the player looks: held there, it leaves the pitch
     * where the mouse put it. */
    if (mouse_in_use)
        TS2_MEM32_EARLY(obj + 0x150) = 1;

    /* The pitch limit, 40 degrees from the player's set-up (FUN_001434F0),
     * is a thumb's: with the mouse it is raised to ts2_pitch_limit (85 by
     * default, short of straight up and down, where the view's matrix
     * would flip), and put back for the pad. */
    if (g_ts2_aim.pitch_limit) {
        static float console_limit = -1.0f;
        float now = TS2_MEMF(g_ts2_aim.pitch_limit);

        if (console_limit < 0.0f && now > 0.0f && now < 90.0f)
            console_limit = now;
        if (console_limit > 0.0f) {
            float lim = mouse_in_use ? ts2_aim_pitch_limit() : console_limit;
            if (now != lim)
                TS2_MEMF(g_ts2_aim.pitch_limit) = lim;
        }
    }
}

/* RECOMP_TS2_WPN_PROBE=1: every dword of player 1's object (0xD3C) and its
 * character (+0x1BC, +0x124; 0x1580) that changes from one frame to the
 * next, with the frame. For finding the weapon change's timer and the rate
 * of fire (docs/pc-work-plan.md, 8 and 9): play, switch weapons and fire,
 * and the fields that move only then are the ones. Not yet run with more
 * than one weapon in hand. */
static void ts2_weapon_probe(uint32_t obj)
{
    enum { SPAN = 0x1580, PSPAN = 0xD3C };
    static int on = -1, lines;
    static uint32_t last[SPAN / 4], plast[PSPAN / 4], last_chr;
    static unsigned long frame;
    uint32_t body, chr, k;

    if (on < 0)
        on = getenv("RECOMP_TS2_WPN_PROBE") != NULL;
    if (!on || lines > 400000)
        return;
    frame++;
    body = TS2_MEM32_EARLY(obj + 0x1BC);
    chr = body >= 0x10000u ? TS2_MEM32_EARLY(body + 0x124) : 0;
    if (chr < 0x10000u)
        return;
    if (chr != last_chr) {
        for (k = 0; k < SPAN / 4; k++)
            last[k] = TS2_MEM32_EARLY(chr + k * 4);
        for (k = 0; k < PSPAN / 4; k++)
            plast[k] = TS2_MEM32_EARLY(obj + k * 4);
        last_chr = chr;
        fprintf(stderr, "[TS2-WPN] player %08X body %08X chr %08X\n", obj, body, chr);
        return;
    }
    for (k = 0; k < SPAN / 4; k++) {
        uint32_t v = TS2_MEM32_EARLY(chr + k * 4);
        if (v != last[k]) {
            fprintf(stderr, "[TS2-WPN] f%lu chr+0x%03X %08X -> %08X\n", frame, k * 4, last[k], v);
            lines++;
        }
        last[k] = v;
    }
    for (k = 0; k < PSPAN / 4; k++) {
        uint32_t v = TS2_MEM32_EARLY(obj + k * 4);
        if (v != plast[k]) {
            fprintf(stderr, "[TS2-WPN] f%lu P+0x%03X %08X -> %08X\n", frame, k * 4, plast[k], v);
            lines++;
        }
        plast[k] = v;
    }
}

static void ts2_aim_frame(int in_level)
{
    float dx, dy, scale, yaw, pitch;
    uint32_t ya, pa, aa;
    int control;

    ts2_aim_init();
    if (!g_ts2_aim_on || !g_ts2_aim.yaw || !g_ts2_aim.pitch)
        return;                         /* not taking: the stick has the mouse */
    if (!in_level) {
        /* The front end: on the stick the mouse wandered through the menus
         * as the stick would. Until it can point at them, it does nothing. */
        recomp_mouse_take(NULL, NULL);
        return;
    }
    ya = ts2_aim_at(g_ts2_aim.yaw);
    pa = ts2_aim_at(g_ts2_aim.pitch);
    if (!ya || !pa) {
        recomp_mouse_take(NULL, NULL);
        return;
    }
    ts2_weapon_probe(ya - g_ts2_aim.yaw);
    control = g_ts2_aim.base ? ts2_aim_in_control(ya - g_ts2_aim.yaw) : 1;
    if (control == 2)
        return;                         /* a turret or vehicle: the stick has it */
    aa = ts2_aim_at(g_ts2_aim.active);
    if ((aa && TS2_MEM32_EARLY(aa) != g_ts2_aim.active_is) || !control ||
        (g_ts2_aim.frame_time && TS2_MEMF(g_ts2_aim.frame_time) == 0.0f)) {
        recomp_mouse_take(NULL, NULL);  /* paused or in a cutscene: drop it */
        return;
    }
    /* RECOMP_TS2_AIM_FAKE=<dx>,<dy>: that many counts every frame instead of
     * the mouse, for a scripted run (a background window has no pointer). */
    {
        const char *fake = getenv("RECOMP_TS2_AIM_FAKE");
        int moved;

        if (fake && *fake) {
            dx = (float)atof(fake);
            dy = strchr(fake, ',') ? (float)atof(strchr(fake, ',') + 1) : 0.0f;
            moved = dx != 0.0f || dy != 0.0f;
        } else {
            moved = recomp_mouse_take(&dx, &dy);
        }
        ts2_aim_mode_auto(ya - g_ts2_aim.yaw, moved);
        if (!moved)
            return;
    }
    scale = TS2_AIM_RAD_PER_COUNT_80 * (float)recomp_mouse_sensitivity() / 80.0f;
    /* Zoomed in (a scope), the same hand movement covers the same part of
     * the picture: scale by the narrower view. */
    if (g_ts2_fov_now > 1.0f && g_ts2_fov_base > g_ts2_fov_now) {
        /* ts2_zoom_match, 0-100: how much of that; 0 keeps the speed. */
        static float match = -1.0f;
        float r = g_ts2_fov_now / g_ts2_fov_base;

        if (match < 0.0f) {
            const char *v = recomp_config_lookup("RECOMP_TS2_ZOOM_MATCH", "ts2_zoom_match");
            match = (v && *v ? (float)atof(v) : 100.0f) / 100.0f;
            if (match < 0.0f) match = 0.0f;
            if (match > 1.0f) match = 1.0f;
        }
        scale *= 1.0f - match * (1.0f - r);
    }
    scale *= g_ts2_aim.units;
    yaw   = TS2_MEMF(ya);
    {
        /* Kept in the range the game keeps it in: TS2 holds its yaw in
         * [0, 360), so a game that never goes negative is not handed a
         * negative angle; otherwise [-wrap/2, wrap/2). */
        float lo = yaw >= 0.0f ? 0.0f : -g_ts2_aim.wrap * 0.5f;
        yaw += (float)g_ts2_aim.yaw_sign * dx * scale;
        if (g_ts2_aim.wrap > 0.0f) {
            while (yaw >= lo + g_ts2_aim.wrap) yaw -= g_ts2_aim.wrap;
            while (yaw < lo) yaw += g_ts2_aim.wrap;
        }
    }
    pitch = TS2_MEMF(pa) - (float)g_ts2_aim.pitch_sign * dy * scale;  /* screen y is down */
    if (g_ts2_aim.pitch_limit) {
        /* The game's own limit, which ts2_aim_mode_auto has raised. */
        float lim = TS2_MEMF(g_ts2_aim.pitch_limit);
        if (lim > 0.0f && lim < 90.0f) {
            if (pitch > lim)  pitch = lim;
            if (pitch < -lim) pitch = -lim;
        }
    } else if (g_ts2_aim.pitch_max > g_ts2_aim.pitch_min) {
        if (pitch > g_ts2_aim.pitch_max) pitch = g_ts2_aim.pitch_max;
        if (pitch < g_ts2_aim.pitch_min) pitch = g_ts2_aim.pitch_min;
    }
    {
        static unsigned said, n;
        n++;
        if (getenv("RECOMP_TS2_AIM_FAKE") && (n < 4 || n % 600 == 0)) {
            said++;
            fprintf(stderr, "[TS2-AIM] write %u: object 0x%08X yaw %g -> %g, pitch %g -> %g\n",
                    n, ya - g_ts2_aim.yaw, (double)TS2_MEMF(ya), (double)yaw,
                    (double)TS2_MEMF(pa), (double)pitch);
        }
    }
    TS2_MEMF(ya) = yaw;
    TS2_MEMF(pa) = pitch;
    /* The crosshair where the view points: hip sway and the aim mode's
     * offset are the stick's, and the stick is not used for this. */
    if ((aa = ts2_aim_at(g_ts2_aim.cross_x)) != 0) TS2_MEMF(aa) = 0.0f;
    if ((aa = ts2_aim_at(g_ts2_aim.cross_y)) != 0) TS2_MEMF(aa) = 0.0f;
}

/* The in-game camera's field of view each frame (camera_setup's fourth
 * argument), and the widest seen since the front end -- the unzoomed view. */
static void ts2_aim_note_fov(float fov_degrees)
{
    if (!(fov_degrees > 1.0f && fov_degrees < 179.0f))
        return;
    g_ts2_fov_now = fov_degrees;
    if (fov_degrees > g_ts2_fov_base)
        g_ts2_fov_base = fov_degrees;
}

/* ── TimeSplitters 2: finding the view angles (the aim probe) ── */

/*
 * RECOMP_TS2_AIM_PROBE=1 watches guest RAM while you play and narrows it
 * down to the values that behave like the view angles and the crosshair:
 *
 *   turn   changes every frame the right stick is held over, in the same
 *          direction for the same direction of the stick, and not at all
 *          while it rests or is held the other way (yaw: left/right; pitch:
 *          up/down)
 *   lean   is off its resting value, on the stick's side, while the stick
 *          is held, and back at rest when it is let go (hip sway; held
 *          aim mode's crosshair moves like "turn", so probe that with the
 *          aim button held)
 *
 * It reads the sticks the game was given (xboxrecomp
 * recomp_input_host_last_sticks), so a pad, bound keys or an input script
 * (RECOMP_INPUT_SEQ ... rstick_left:3000 ...) all work -- but not the
 * mouse: play the probe with mouse look off. Turn left and right, look up
 * and down, rest in between, for half a minute. Every five seconds it
 * prints what is left, and at the end a ready RECOMP_TS2_AIM line. Float
 * values only; RECOMP_TS2_AIM_PROBE=int tests 32-bit integers instead.
 * It reads 64 MB a frame until the field is small, so the game runs slowly
 * meanwhile; that does not matter for what it measures, but its rules count
 * frames, so a scripted probe holds each turn and each rest for seconds
 * (rests over 30 frames). RECOMP_TS2_AIM_PROBE_AFTER=<frames> skips the
 * level's opening cutscene.
 */
extern ptrdiff_t xbox_GetMemoryOffset(void);

extern size_t    g_xbox_total_ram;
extern int       recomp_input_host_last_sticks(short out[4]);

enum { TS2P_TX = 1, TS2P_TY = 2, TS2P_LX = 4, TS2P_LY = 8 };
enum { TS2P_IDLE, TS2P_X, TS2P_Y, TS2P_OTHER };
#define TS2P_LIST_MAX 4096
#define TS2P_SETTLE   30      /* frames of rest before still means still */

typedef struct Ts2ProbeCand {
    uint32_t addr, prev, rest;          /* raw words */
    uint8_t  flags, sign;               /* sign: bit set = positive relation */
    uint8_t  ev[4];                     /* evidence per category, saturating */
    float    lo, hi, sum_d[2];
    uint32_t n_d[2];
} Ts2ProbeCand;

static struct {
    int       on, ints, failed;
    uint32_t  first, words;             /* in 32-bit words */
    uint32_t *prev, *rest;
    uint8_t  *flags, *sign;
    uint8_t  *ev;                       /* 4 per word, saturating */
    Ts2ProbeCand *list;                 /* once few are left */
    int       list_n;
    int       state, held;              /* frames in this stick state */
    int       sx, sy;                   /* the stick's sign in this state */
    unsigned  frame, said_at;
} g_ts2p;

static double ts2p_val(uint32_t raw)
{
    if (g_ts2p.ints)
        return (double)(int32_t)raw;
    {
        union { uint32_t u; float f; } c;
        c.u = raw;
        return (double)c.f;
    }
}

static int ts2p_valid(uint32_t raw)
{
    if (g_ts2p.ints)
        return 1;
    {
        uint32_t e = (raw >> 23) & 0xFF;
        union { uint32_t u; float f; } c;
        c.u = raw;
        if (raw == 0 || raw == 0x80000000u)
            return 1;
        if (e == 0xFF || e == 0)            /* inf, NaN, denormal */
            return 0;
        return c.f > -1.0e5f && c.f < 1.0e5f;
    }
}

/* The rules for one value this frame; returns its flags after. */
static uint8_t ts2p_rules(uint8_t f, uint8_t *sign, uint8_t *ev, uint32_t raw,
                          uint32_t prev, uint32_t *rest)
{
    double v, d;
    int dir, cat;

    if (!ts2p_valid(raw))
        return 0;
    v = ts2p_val(raw);
    d = v - ts2p_val(prev);
    switch (g_ts2p.state) {
    case TS2P_IDLE:
        /* Half a second for a sway to settle, then nothing may move. */
        if (g_ts2p.held < TS2P_SETTLE)
            break;
        if (raw != prev)
            f &= (uint8_t)~(TS2P_TX | TS2P_TY | TS2P_LX | TS2P_LY);
        else if (g_ts2p.held == TS2P_SETTLE)
            *rest = raw;                    /* resting value of this pause */
        break;
    case TS2P_X:
    case TS2P_Y: {
        int x = g_ts2p.state == TS2P_X;
        uint8_t turn = x ? TS2P_TX : TS2P_TY, other = x ? TS2P_TY : TS2P_TX;
        uint8_t lean = x ? TS2P_LX : TS2P_LY;
        int s = x ? g_ts2p.sx : g_ts2p.sy;

        if (g_ts2p.held < 6)
            break;
        /* The other axis's turn must not move while only this one is held;
         * its lean may settle, so only the turn is strict. */
        if (raw != prev)
            f &= (uint8_t)~other;
        if (f & turn) {
            cat = x ? 0 : 1;
            if (raw == prev) {
                /* Still while the stick is held: not this, unless it has
                 * already moved with the stick and is now at its limit (a
                 * pitch looking all the way up). */
                if (!ev[cat])
                    f &= (uint8_t)~turn;
            } else {
                dir = (d > 0) == (s > 0);
                if (!ev[cat]) {
                    *sign = (uint8_t)((*sign & ~(1 << cat)) | (dir << cat));
                    ev[cat] = 1;
                } else if (((*sign >> cat) & 1) != dir) {
                    /* The wrong way: a kill, unless it is a big jump, which is
                     * an angle wrapping round (-pi..pi, 0..360). */
                    if (g_ts2p.ints || (d < 3.0 && d > -3.0))
                        f &= (uint8_t)~turn;
                } else if (ev[cat] < 255) {
                    ev[cat]++;
                }
            }
        }
        if (f & lean) {
            double off = v - ts2p_val(*rest);
            cat = x ? 2 : 3;
            if (off == 0.0) {
                /* At rest well into a held turn, and never off it: not a
                 * lean. Once it has leant it may stop (aim mode). */
                if (!ev[cat] && g_ts2p.held >= 10)
                    f &= (uint8_t)~lean;
            } else {
                dir = (off > 0) == (s > 0);
                if (!ev[cat])
                    *sign = (uint8_t)((*sign & ~(1 << cat)) | (dir << cat));
                else if (((*sign >> cat) & 1) != dir)
                    f &= (uint8_t)~lean;
                if ((f & lean) && ev[cat] < 255)
                    ev[cat]++;
            }
        }
        break;
    }
    default:
        break;
    }
    return f;
}

static void ts2p_compact(void)
{
    uint32_t i;
    int n = 0;

    g_ts2p.list = (Ts2ProbeCand *)calloc(TS2P_LIST_MAX, sizeof *g_ts2p.list);
    if (!g_ts2p.list) {
        g_ts2p.failed = 1;
        return;
    }
    for (i = 0; i < g_ts2p.words && n < TS2P_LIST_MAX; i++) {
        Ts2ProbeCand *c;
        if (!g_ts2p.flags[i])
            continue;
        c = &g_ts2p.list[n++];
        c->addr = (g_ts2p.first + i) * 4;
        c->flags = g_ts2p.flags[i];
        c->sign = g_ts2p.sign[i];
        c->ev[0] = g_ts2p.ev[i * 4];     c->ev[1] = g_ts2p.ev[i * 4 + 1];
        c->ev[2] = g_ts2p.ev[i * 4 + 2]; c->ev[3] = g_ts2p.ev[i * 4 + 3];
        c->prev = g_ts2p.prev[i];
        c->rest = g_ts2p.rest[i];
        c->lo = c->hi = (float)ts2p_val(c->prev);
    }
    g_ts2p.list_n = n;
    free(g_ts2p.prev);  free(g_ts2p.rest);
    free(g_ts2p.flags); free(g_ts2p.sign); free(g_ts2p.ev);
    g_ts2p.prev = g_ts2p.rest = NULL;
    g_ts2p.flags = g_ts2p.sign = g_ts2p.ev = NULL;
    fprintf(stderr, "[TS2-PROBE] %d candidates left; following them one by one now\n", n);
}

static const char *const k_ts2p_cat[4] = { "yaw (turns with left/right)",
                                           "pitch (turns with up/down)",
                                           "sway x (leans with left/right)",
                                           "sway y (leans with up/down)" };

static void ts2p_report(int final)
{
    int cat, i, k;

    if (!g_ts2p.list) {
        uint32_t i2, n[4] = { 0, 0, 0, 0 };
        for (i2 = 0; i2 < g_ts2p.words; i2++)
            for (cat = 0; cat < 4; cat++)
                if (g_ts2p.flags[i2] & (1 << cat))
                    n[cat]++;
        fprintf(stderr, "[TS2-PROBE] frame %u: still %u yaw, %u pitch, %u sway x, %u sway y "
                        "candidates -- keep turning and resting\n",
                g_ts2p.frame, n[0], n[1], n[2], n[3]);
        fflush(stderr);
        return;
    }
    for (cat = 0; cat < 4; cat++) {
        int shown = 0;
        const int need = 20;
        fprintf(stderr, "[TS2-PROBE] %s:\n", k_ts2p_cat[cat]);
        /* strongest evidence first: a few passes over a small list */
        for (k = 255; k >= need && shown < 8; k--)
            for (i = 0; i < g_ts2p.list_n && shown < 8; i++) {
                Ts2ProbeCand *c = &g_ts2p.list[i];
                if (!(c->flags & (1 << cat)) || c->ev[cat] != k)
                    continue;
                shown++;
                if (cat < 2)
                    fprintf(stderr, "[TS2-PROBE]   0x%08X  now %g, seen %g..%g, %g a frame at "
                                    "full stick, sign %+d  (evidence %d)\n",
                            c->addr, (double)ts2p_val(TS2_MEM32_EARLY(c->addr)),
                            (double)c->lo, (double)c->hi,
                            c->n_d[cat] ? (double)(c->sum_d[cat] / (float)c->n_d[cat]) : 0.0,
                            ((c->sign >> cat) & 1) ? 1 : -1, c->ev[cat]);
                else
                    fprintf(stderr, "[TS2-PROBE]   0x%08X  rest %g, seen %g..%g  (evidence %d)\n",
                            c->addr, ts2p_val(c->rest), (double)c->lo, (double)c->hi, c->ev[cat]);
            }
        if (!shown)
            fprintf(stderr, "[TS2-PROBE]   none yet\n");
    }
    if (final) {
        Ts2ProbeCand *best[4] = { NULL, NULL, NULL, NULL };
        for (i = 0; i < g_ts2p.list_n; i++)
            for (cat = 0; cat < 4; cat++) {
                Ts2ProbeCand *c = &g_ts2p.list[i];
                int leans = ((c->flags & TS2P_LX) && c->ev[2] >= 20) ||
                            ((c->flags & TS2P_LY) && c->ev[3] >= 20);
                if (!(c->flags & (1 << cat)) || c->ev[cat] < 20)
                    continue;
                /* A sway ramps while the stick is held, so it passes as a
                 * turn too; an angle never comes back to rest. */
                if (cat < 2 && leans)
                    continue;
                if (!best[cat] || c->ev[cat] > best[cat]->ev[cat] ||
                    (c->ev[cat] == best[cat]->ev[cat] &&
                     c->hi - c->lo > best[cat]->hi - best[cat]->lo))
                    best[cat] = c;
            }
        if (best[0] && best[1]) {
            float span = best[0]->hi - best[0]->lo;
            fprintf(stderr, "[TS2-PROBE] try:  RECOMP_TS2_AIM=yaw=0x%08X,pitch=0x%08X,"
                            "yaw_sign=%d,pitch_sign=%d,units=%s,pitch_min=%g,pitch_max=%g%s",
                    best[0]->addr, best[1]->addr,
                    ((best[0]->sign >> 0) & 1) ? 1 : -1, ((best[1]->sign >> 1) & 1) ? 1 : -1,
                    span > 7.0f ? "57.29578" : "1",
                    (double)best[1]->lo, (double)best[1]->hi,
                    span > 6.0f && span < 6.6f ? ",wrap=6.283185"
                    : span > 300.0f && span < 361.0f ? ",wrap=360" : "");
            if (best[2]) fprintf(stderr, ",cross_x=0x%08X", best[2]->addr);
            if (best[3]) fprintf(stderr, ",cross_y=0x%08X", best[3]->addr);
            fprintf(stderr, "\n[TS2-PROBE] the addresses may move between levels: "
                            "RECOMP_FIND_VALUE / RECOMP_WATCH_WRITE then find the "
                            "pointer to give as base= (docs/mouse-aim.md)\n");
        } else {
            fprintf(stderr, "[TS2-PROBE] no yaw and pitch pair yet: play longer, rest "
                            "between turns, or try RECOMP_TS2_AIM_PROBE=int\n");
        }
    }
    fflush(stderr);
}

static void ts2p_begin(void)
{
    const char *v = getenv("RECOMP_TS2_AIM_PROBE");
    size_t ram;

    if (!v || !*v || !strcmp(v, "0")) {
        g_ts2p.on = -1;
        return;
    }
    g_ts2p.ints = !strcmp(v, "int");
    ram = g_xbox_total_ram ? g_xbox_total_ram : (size_t)64 * 1024 * 1024;
    g_ts2p.first = 0x10000u / 4;
    g_ts2p.words = (uint32_t)(ram / 4) - g_ts2p.first;
    g_ts2p.prev  = (uint32_t *)malloc((size_t)g_ts2p.words * 4);
    g_ts2p.rest  = (uint32_t *)malloc((size_t)g_ts2p.words * 4);
    g_ts2p.flags = (uint8_t *)malloc(g_ts2p.words);
    g_ts2p.sign  = (uint8_t *)calloc(g_ts2p.words, 1);
    g_ts2p.ev    = (uint8_t *)calloc((size_t)g_ts2p.words * 4, 1);
    if (!g_ts2p.prev || !g_ts2p.rest || !g_ts2p.flags || !g_ts2p.sign || !g_ts2p.ev) {
        fprintf(stderr, "[TS2-PROBE] not enough memory for the probe\n");
        g_ts2p.on = -1;
        return;
    }
    memcpy(g_ts2p.prev, (const void *)((uintptr_t)(g_ts2p.first * 4) + xbox_GetMemoryOffset()),
           (size_t)g_ts2p.words * 4);
    memcpy(g_ts2p.rest, g_ts2p.prev, (size_t)g_ts2p.words * 4);
    memset(g_ts2p.flags, TS2P_TX | TS2P_TY | TS2P_LX | TS2P_LY, g_ts2p.words);
    g_ts2p.on = 1;
    g_ts2p.state = TS2P_OTHER;
    fprintf(stderr, "[TS2-PROBE] watching %u MB of guest RAM as %s: turn left and right, "
                    "look up and down, rest in between (mouse look off)\n",
            (unsigned)((size_t)g_ts2p.words * 4 >> 20), g_ts2p.ints ? "integers" : "floats");
    fflush(stderr);
}

/* Once a frame, in a level (ts2_present). */
static void ts2_aim_probe_frame(int in_level)
{
    short st[4];
    int state, sx = 0, sy = 0;
    float rx, ry;

    if (g_ts2p.on == 0)
        ts2p_begin();
    if (g_ts2p.on < 0 || g_ts2p.failed || !in_level)
        return;
    /* RECOMP_TS2_AIM_PROBE_AFTER=<frames>: start that many level frames in.
     * A level opens on a cutscene whose camera moves on its own and ignores
     * the stick, which rules the real angles out; at the probe's speed it
     * also lasts minutes, so a scripted probe has to wait it out. */
    {
        static int after = -1, seen;
        if (after < 0) {
            const char *a = getenv("RECOMP_TS2_AIM_PROBE_AFTER");
            after = a ? atoi(a) : 0;
        }
        if (seen < after) {
            if (++seen == after) {
                memcpy(g_ts2p.prev, (const void *)((uintptr_t)(g_ts2p.first * 4) + xbox_GetMemoryOffset()),
                       (size_t)g_ts2p.words * 4);
                fprintf(stderr, "[TS2-PROBE] %d level frames in: measuring from here\n", after);
            }
            return;
        }
    }
    recomp_input_host_last_sticks(st);
    rx = (float)st[2] / 32767.0f;
    ry = (float)st[3] / 32767.0f;
    if (rx > -0.12f && rx < 0.12f && ry > -0.12f && ry < 0.12f)
        state = TS2P_IDLE;
    else if ((rx > 0.6f || rx < -0.6f) && ry > -0.2f && ry < 0.2f)
        state = TS2P_X, sx = rx > 0 ? 1 : -1;
    else if ((ry > 0.6f || ry < -0.6f) && rx > -0.2f && rx < 0.2f)
        state = TS2P_Y, sy = ry > 0 ? 1 : -1;
    else
        state = TS2P_OTHER;
    if (state != g_ts2p.state || sx != g_ts2p.sx || sy != g_ts2p.sy) {
        if (getenv("RECOMP_TS2_AIM_PROBE_TRACE"))
            fprintf(stderr, "[TS2-PROBE] frame %u: stick state %d (%+d,%+d) after %d frames of %d\n",
                    g_ts2p.frame, state, sx, sy, g_ts2p.held, g_ts2p.state);
        g_ts2p.state = state;
        g_ts2p.sx = sx;
        g_ts2p.sy = sy;
        g_ts2p.held = 0;
    } else if (g_ts2p.held < 1000000) {
        g_ts2p.held++;
    }
    g_ts2p.frame++;

    if (!g_ts2p.list) {
        const uint32_t *mem = (const uint32_t *)((uintptr_t)(g_ts2p.first * 4) +
                                                 xbox_GetMemoryOffset());
        uint32_t i, alive = 0;

        for (i = 0; i < g_ts2p.words; i++) {
            uint32_t raw = mem[i];
            uint8_t f = g_ts2p.flags[i];
            if (f) {
                f = ts2p_rules(f, &g_ts2p.sign[i], &g_ts2p.ev[i * 4], raw,
                               g_ts2p.prev[i], &g_ts2p.rest[i]);
                g_ts2p.flags[i] = f;
                alive += f != 0;
            }
            g_ts2p.prev[i] = raw;
        }
        if (alive <= TS2P_LIST_MAX)
            ts2p_compact();
    } else {
        int i;
        for (i = 0; i < g_ts2p.list_n; i++) {
            Ts2ProbeCand *c = &g_ts2p.list[i];
            uint32_t raw = TS2_MEM32_EARLY(c->addr);
            double v = ts2p_val(raw), d = v - ts2p_val(c->prev);

            if (c->flags) {
                c->flags = ts2p_rules(c->flags, &c->sign, c->ev, raw, c->prev, &c->rest);
                if (g_ts2p.held >= 6 && raw != c->prev &&
                    (g_ts2p.state == TS2P_X || g_ts2p.state == TS2P_Y)) {
                    int k = g_ts2p.state == TS2P_Y;
                    c->sum_d[k] += (float)(d < 0 ? -d : d);
                    c->n_d[k]++;
                }
                if ((float)v < c->lo) c->lo = (float)v;
                if ((float)v > c->hi) c->hi = (float)v;
            }
            c->prev = raw;
        }
    }
    if (g_ts2p.frame - g_ts2p.said_at >= 300) {
        g_ts2p.said_at = g_ts2p.frame;
        ts2p_report(g_ts2p.list != NULL);
    }
}

/*
 * Frame interpolation (xboxrecomp hle_d3d8_interp.c, RECOMP_FRAME_INTERP)
 * blends the matrices in TS2's vertex constants, measured from captures:
 * c60-c63 hold the projection alone, set once or twice a frame; c64-c67
 * the model-view of each object; c68-c75 the second and third matrices of
 * a skinned piece's palette. All affine, rows with (0,0,0,1) last. Said
 * once, at the first present, which is before any level is drawn.
 */
extern void xbox_D3D8SetInterpRegisters(int projection, int affine_first, int affine_count);

static void ts2_present(void (*present)(void))
{
    static int interp_said;

    if (!interp_said) {
        interp_said = 1;
        xbox_D3D8SetInterpRegisters(60, 64, 12);
    }
    present();
    xbox_D3D8SetWideFrames(ts2_menus_wide() ||
                           (g_ts2_frame_camera && !g_ts2_frame_front_end));
    /* A frame of a level (or one of its cutscenes): the mouse aims there.
     * The front end resets the unzoomed field of view for the next level. */
    {
        int in_level = g_ts2_frame_camera && !g_ts2_frame_front_end;

        if (g_ts2_frame_front_end)
            g_ts2_fov_base = 0.0f;
        ts2_aim_frame(in_level);
        ts2_aim_probe_frame(in_level);
    }
    g_ts2_frame_camera = g_ts2_frame_front_end = 0;
}

/* ── TimeSplitters 2: where each piece of 2D goes in widescreen ── */

/*
 * TimeSplitters 2 has no 16:9 mode. Its 2D -- menus, HUD, text, backdrops --
 * is positioned on the CPU in 640x480 pixels, and in widescreen the renderer
 * squeezes it back to 4:3 so the 16:9 stretch leaves it in proportion. Which
 * pieces should instead span the picture (a backdrop, a fade) or sit at an
 * edge (a HUD corner) the renderer can only guess from the vertices, and the
 * guess changes as things animate: the menu header juddered between the two.
 *
 * The code that drew a piece says what it is, the same way every frame. All
 * of the game's 2D reaches the GPU through one call, reserve(primitive,
 * format, ?, count), which reserves vertices in the engine's batch for the
 * caller to fill; flush sends the batch to DrawVertices. So:
 *
 *   - the reservation is wrapped, and reads its caller from the guest stack
 *     (a lifted call pushes the real return address first);
 *   - the engine's shared drawing functions above it are wrapped too, so a
 *     piece drawn through one is credited to the UI code that called it --
 *     the *site* is the caller of the outermost wrapped function;
 *   - k_ts2_ui_places says where each known site goes; unknown ones
 *     are left to the renderer (AUTO), exactly as before;
 *   - a reservation whose placement differs from the batch's flushes the
 *     batch first; the renderer is given the batch's placement as soon as
 *     anything is queued (the engine draws the batch by more than one way),
 *     and flush resets it once the batch is drawn, so a draw that
 *     never went through the batch (the glow passes, a movie) does not
 *     inherit a menu's placement.
 *
 * All wrapping, not replacing (manual_scan.py: extern ..._gen), so the lifted
 * bodies still do the work. Without widescreen nothing here changes a draw.
 *
 *   RECOMP_TS2_UI_SITES=1       flush at every site change and tag each batch
 *                               with its site, and print each site's chain
 *                               the first time it draws; with the renderer's
 *                               RECOMP_D3D8_2D_TAGS=1 that gives each site's
 *                               extent. How the table is built.
 *   RECOMP_TS2_UI_PLACE=<list>  site=placement pairs, comma separated, over
 *                               the table: 0008A0C1=stretch,000AB123=left
 *   RECOMP_TS2_UI_DEFAULT=<p>   placement for sites not in the table (auto)
 *
 * The same sites switch effects off: ts2_sun_glare = 0 (RECOMP_TS2_SUN_GLARE)
 * and ts2_weather = 0 (RECOMP_TS2_WEATHER) give the sites in each release's
 * k_ts2_sun_glare_sites / k_ts2_weather_sites the renderer's HIDE, which
 * leaves their draws out, widescreen or not. Siberia's sun glare and snow
 * soften the whole picture, which on the console's screen was atmosphere
 * and at a high resolution reads as a smear.
 */

enum { TS2_UI_AUTO = 0, TS2_UI_STRETCH, TS2_UI_CENTRE, TS2_UI_LEFT, TS2_UI_RIGHT,
       TS2_UI_SIDE, TS2_UI_HIDE = 7 /* XBOX_D3D8_2D_HIDE */ };

/* xboxrecomp src/hle/hle_d3d8_record.h: xbox_D3D8SetTwoDPlacement, recorded in
 * frame captures so a capture replays with these placements. Same values
 * as XBOX_D3D8_2D_*. */
extern void host_SetTwoDPlacement(int placement, uint32_t tag);
extern int  recomp_config_bool(const char *env_name, const char *key, int fallback);

typedef struct Ts2UiPlace { uint32_t site; int placement; } Ts2UiPlace;

/* Where the game draws what: each release's own table, in its
 * config/<region>/overrides.inc. Sites are return addresses in the game's
 * UI code, so they differ between releases. Ends at site 0.
 *
 * Built from the front end, screen by screen: frames captured with
 * RECOMP_TS2_UI_SITES=1, each site drawn alone and each left out with
 * d3d8_replay --each-tag / --each-tag-hidden, and the table tried on the
 * captures with --place before it went in. The rule: whatever covers
 * the screen spans the picture, the side decorations and the header's
 * left and right ends go to the edges of it, and everything a player
 * reads stays in proportion in the middle. A full-screen pass left to the
 * renderer's width guess was sometimes kept at 4:3, and its edge showed
 * as a seam down both old borders on every screen. */
extern const Ts2UiPlace k_ts2_ui_places[];
/* The sites of the effects the player can switch off; each ends at 0. */
extern const uint32_t k_ts2_sun_glare_sites[];
extern const uint32_t k_ts2_weather_sites[];
static int g_ts2_hide_glare, g_ts2_hide_weather;

static Ts2UiPlace g_ts2_ui_extra[64];   /* RECOMP_TS2_UI_PLACE */
static int        g_ts2_ui_extra_count;
static int        g_ts2_ui_default = TS2_UI_AUTO;
static int        g_ts2_ui_mode = -1;   /* 0 off, 1 placing, 2 placing and tagging sites */

/* The batch: what is queued in it came from this site, with this placement. */
static int      g_ts2_batch_place = TS2_UI_AUTO;
static uint32_t g_ts2_batch_site;

/* The placement the renderer has now, so it is only told of changes (each
 * one is a chunk in a frame capture). */
static int      g_ts2_host_place = -1;
static uint32_t g_ts2_host_tag;
/* Set while a reservation flushes ahead of itself: the flush must not reset
 * the renderer's placement then (ts2_ui_reserve). */
static int      g_ts2_keep_host;

static void ts2_ui_host_place(int place, uint32_t tag)
{
    if (place == g_ts2_host_place && tag == g_ts2_host_tag)
        return;
    g_ts2_host_place = place;
    g_ts2_host_tag = tag;
    host_SetTwoDPlacement(place, tag);
}

/* The callers of the wrapped drawing functions now running, outermost first. */
static RECOMP_MANUAL_TLS uint32_t g_ts2_chain[8];
static RECOMP_MANUAL_TLS int      g_ts2_depth;

#define TS2_MEM32(a) (*(volatile uint32_t *)((uintptr_t)(uint32_t)(a) + g_xbox_mem_offset))

static int ts2_ui_parse_place(const char *s)
{
    if (!strncmp(s, "stretch", 7)) return TS2_UI_STRETCH;
    if (!strncmp(s, "centre", 6) || !strncmp(s, "center", 6)) return TS2_UI_CENTRE;
    if (!strncmp(s, "left", 4)) return TS2_UI_LEFT;
    if (!strncmp(s, "right", 5)) return TS2_UI_RIGHT;
    if (!strncmp(s, "side", 4)) return TS2_UI_SIDE;
    return TS2_UI_AUTO;
}

static int ts2_ui_table_size(void)
{
    int n = 0;

    while (k_ts2_ui_places[n].site)
        n++;
    return n;
}

static int ts2_ui_mode(void)
{
    const char *v;

    if (g_ts2_ui_mode >= 0)
        return g_ts2_ui_mode;
    g_ts2_ui_mode = recomp_config_bool("RECOMP_WIDESCREEN", "widescreen", 0) ? 1 : 0;
    g_ts2_hide_glare = !recomp_config_bool("RECOMP_TS2_SUN_GLARE", "ts2_sun_glare", 1);
    g_ts2_hide_weather = !recomp_config_bool("RECOMP_TS2_WEATHER", "ts2_weather", 1);
    if (g_ts2_hide_glare || g_ts2_hide_weather) {
        g_ts2_ui_mode = 1;
        fprintf(stderr, "[TS2-UI] left out:%s%s\n", g_ts2_hide_glare ? " sun glare" : "",
                g_ts2_hide_weather ? " falling snow and rain" : "");
    }
    v = getenv("RECOMP_TS2_UI_SITES");
    if (v && *v && strcmp(v, "0") != 0)
        g_ts2_ui_mode = 2;
    v = getenv("RECOMP_TS2_UI_DEFAULT");
    if (v && *v)
        g_ts2_ui_default = ts2_ui_parse_place(v);
    v = getenv("RECOMP_TS2_UI_PLACE");
    while (v && *v && g_ts2_ui_extra_count < (int)(sizeof g_ts2_ui_extra / sizeof g_ts2_ui_extra[0])) {
        char *end = NULL;
        unsigned long site = strtoul(v, &end, 16);

        if (!end || *end != '=')
            break;
        g_ts2_ui_extra[g_ts2_ui_extra_count].site = (uint32_t)site;
        g_ts2_ui_extra[g_ts2_ui_extra_count].placement = ts2_ui_parse_place(end + 1);
        g_ts2_ui_extra_count++;
        v = strchr(end, ',');
        if (v)
            v++;
    }
    if (g_ts2_ui_mode)
        fprintf(stderr, "[TS2-UI] 2D placement by call site: %d table entries, %d from "
                "RECOMP_TS2_UI_PLACE%s\n",
                ts2_ui_table_size(),
                g_ts2_ui_extra_count, g_ts2_ui_mode == 2 ? "; tagging sites" : "");
    return g_ts2_ui_mode;
}

static int ts2_ui_place_for(uint32_t site)
{
    int i;

    for (i = 0; g_ts2_hide_glare && k_ts2_sun_glare_sites[i]; i++)
        if (k_ts2_sun_glare_sites[i] == site)
            return TS2_UI_HIDE;
    for (i = 0; g_ts2_hide_weather && k_ts2_weather_sites[i]; i++)
        if (k_ts2_weather_sites[i] == site)
            return TS2_UI_HIDE;
    for (i = 0; i < g_ts2_ui_extra_count; i++)
        if (g_ts2_ui_extra[i].site == site)
            return g_ts2_ui_extra[i].placement;
    for (i = 0; k_ts2_ui_places[i].site; i++)
        if (k_ts2_ui_places[i].site == site)
            return k_ts2_ui_places[i].placement;
    return g_ts2_ui_default;
}

/* Tagging mode: say once which chain a new site came through. */
static void ts2_ui_note_site(uint32_t site, uint32_t leaf)
{
    static uint32_t seen[512];
    static int seen_count;
    int i;

    for (i = 0; i < seen_count; i++)
        if (seen[i] == site)
            return;
    if (seen_count < (int)(sizeof seen / sizeof seen[0]))
        seen[seen_count++] = site;
    RECOMP_DIAG_LOCK();
    fprintf(stderr, "[TS2-UI] site %08X:", site);
    for (i = 0; i < g_ts2_depth && i < 8; i++)
        fprintf(stderr, " %08X ->", g_ts2_chain[i]);
    fprintf(stderr, " %08X -> reserve\n", leaf);
    RECOMP_DIAG_UNLOCK();
    fflush(stderr);
}

/* Run the batch flush from here. A lifted function returns with `ret`, so
 * it needs a return address on the guest stack to pop. */
static void ts2_ui_flush(void (*flush)(void))
{
    g_esp -= 4;
    TS2_MEM32(g_esp) = 0;
    flush();
}

static void ts2_ui_reserve(void (*reserve)(void), void (*flush)(void))
{
    if (ts2_ui_mode()) {
        uint32_t leaf = TS2_MEM32(g_esp);
        uint32_t site = g_ts2_depth ? g_ts2_chain[0] : leaf;
        int place = ts2_ui_place_for(site);

        int change = place != g_ts2_batch_place || place == TS2_UI_SIDE ||
                     (g_ts2_ui_mode == 2 && site != g_ts2_batch_site);

        if (g_ts2_ui_mode == 2)
            ts2_ui_note_site(site, leaf);
        if (change) {
            /* The queue holds another site's pieces: draw them under their
             * own placement before this one's join it. A SIDE site's pieces
             * are drawn one by one, since the renderer pins each draw to the
             * edge it is nearer and one draw holding both hands' ammo would
             * be pinned to neither. The renderer keeps the old placement
             * through the reservation below (see there). */
            g_ts2_keep_host = 1;
            ts2_ui_flush(flush);
            g_ts2_keep_host = 0;
        }
        /* The engine keeps more than one batch (by format, it seems), and
         * flush does not always draw the one that holds the last pieces:
         * the reservation itself draws it, when the next piece does not fit
         * it, by its own way (a function under reserve calls the draw). So the
         * renderer holds the old batch's placement until that has happened,
         * and takes the new one after, for the pieces now queued -- whose
         * batch may be drawn by either way. Setting the new one first put
         * the right hand's ammo bars, the last piece before the next site,
         * under that site's placement and left them in the 4:3 layout. */
        reserve();
        if (change) {
            g_ts2_batch_place = place;
            g_ts2_batch_site = site;
        }
        ts2_ui_host_place(g_ts2_batch_place,
                          g_ts2_ui_mode == 2 ? g_ts2_batch_site : 0);
        return;
    }
    reserve();
}

static void ts2_ui_batch_flush(void (*flush)(void))
{
    if (g_ts2_ui_mode <= 0) {
        flush();
        return;
    }
    ts2_ui_host_place(g_ts2_batch_place, g_ts2_ui_mode == 2 ? g_ts2_batch_site : 0);
    flush();
    /* The batch is empty again: a draw that never went through it (a glow
     * pass, a movie) must not inherit its placement -- unless a reservation
     * is flushing ahead of itself, when another batch may still be drawn
     * under this placement (ts2_ui_reserve). */
    if (!g_ts2_keep_host)
        ts2_ui_host_place(TS2_UI_AUTO, 0);
}

/* The engine's shared drawing functions: the helpers that reserve vertices
 * for many callers (quads, sprites, text), and the ones above them that the
 * menu and HUD code call. Whoever called the outermost of them is the site.
 * Each release wraps its own: ts2_ui_enter(), the lifted body, ts2_ui_leave(). */
static void ts2_ui_enter(void)
{
    if (g_ts2_ui_mode > 0) {
        if (g_ts2_depth < 8)
            g_ts2_chain[g_ts2_depth] = TS2_MEM32(g_esp);
        g_ts2_depth++;
    }
}

static void ts2_ui_leave(void)
{
    if (g_ts2_ui_mode > 0 && g_ts2_depth > 0)
        g_ts2_depth--;
}

/* ── TimeSplitters 2: extra playable characters, declared by mods ── */

/*
 * A mod adds a playable character by taking a roster slot and a chr id that
 * has files on the disc but nobody plays (docs/formats/roster.md, "Adding a
 * character (mods)"). It says so in <mod>/ts2/characters.ini, read once at
 * start-up from every folder in RECOMP_MODS that has one, in that order
 * (ts2_chars_resolve says who wins when two want the same thing). Its model
 * goes where that chr id's files are (ob/chrs/chrNN.xbr in chr.pak,
 * ob/chrincs/chrNN.xbr in chrinc.pak), its portrait into the front end's
 * pak, as loose files the game is served from the mod (ts2_paks_mount,
 * below); this is the part the game's tables need:
 *
 *   [Agent Static]
 *   slot = next               ; a spare slot (127-158 on PAL), or 0-126 to
 *                             ; take over the character there
 *   chr = 70                  ; chr id: its model, definition and files
 *   name = Agent Static       ; in every language; name.fr = ... and so on
 *   bio = Nobody knows ...    ; the Gallery's description; bio.fr = ... too
 *   portrait = textures/misc/chrimage_agentstatic.xbt   ; a file in the paks
 *   background = neo          ; the set's chrblank_<set>, shown while locked
 *   unlock = start            ; start, never, or keep (the slot's own rule)
 *   stats = 61                ; optional: Stamina/Agility/Accuracy of this chr id
 *   sounds = 61               ; optional: this chr id's voice set
 *
 * Without such a file nothing below changes anything. All of it is data in
 * the game's own tables, written once before the game's main() runs:
 *
 *   - the roster entry (slot * 0x20): chr id, portrait, background, unlock
 *     kind (1 "always"); every screen that lists characters walks this table
 *     and asks the unlock rule, so the entry appears in Arcade's Select
 *     Character list, the bot list and the Gallery with no code change;
 *   - the name: the definition (chr * 0x44) names its character by a text
 *     id, and every place that shows a character's name (the select screen,
 *     the Gallery, the bot list, the scoreboard) looks that id up in the
 *     language's string table; that id's entry in each of the five tables
 *     is pointed at the new name, in guest memory; the Gallery's description
 *     is text bio_base + chr id, and is repointed the same way;
 *   - the portrait: the roster names it by an index into the texture list,
 *     whose entry holds a path ("host0:textures/misc/chrimage_ped3.raw" for
 *     the .xbt of that name in a pak). The entry's path is pointed at the new
 *     file's name, so the old portrait stays on the disc, untouched;
 *   - optionally the stats (+0x2C class, +0x30-0x38 per difficulty, +0x3C
 *     agility) and the sound set (+0x04) copied from another character.
 *
 * The roster is the disc's 127 entries and, on PAL, 32 spare slots after
 * them: config/pal/xbe_patches.json moves it into a section of its own and
 * repoints the code before the lift. A spare slot is the Consultant's entry
 * (the disc's one hidden character, slot 126) with a portrait of its own:
 * unlock kind 0, which hides it from every list, so without a mod nothing
 * changes. Two things the patches cannot do are done here:
 *
 *   - The Gallery draws the first n entries, n the number whose kind is not
 *     0, so a hidden slot among the shown ones (slot 126, below the spare
 *     slots) would take a shown one's cell. Once a mod's characters are in,
 *     the hidden entries are moved behind the shown ones (ts2_chars_compact);
 *     nothing keeps a roster index past a screen, only chr ids.
 *   - sub_0008FAE0, which draws a Select Character item's portrait, finds
 *     its character by chr id in the first 127 entries only (its count is
 *     an imm8 the patches cannot raise). ts2_chars_item_draw moves an entry
 *     past them into that range for the call.
 *
 * The Gallery (Options > Player Options > View Gallery) shows the roster as
 * one 14 x 9 grid, which 126 characters fill; a 127th would start a tenth
 * row below the panel. ts2_gallery_build widens it to as many columns as
 * nine rows need and ts2_gallery_draw draws its cells smaller to match.
 */
typedef struct Ts2Roster {
    uint32_t roster;          /* 0x20-byte entries: chr id, portrait, background, unlock[5] */
    int      roster_count;    /* with the spare slots */
    int      spare_first;     /* the first spare slot: the disc's count */
    int      search_count;    /* the entries sub_0008FAE0's chr id search covers */
    uint32_t defs;            /* 0x44-byte character definitions, by chr id */
    int      def_count;
    uint32_t textures;        /* 0x10-byte texture list entries, at their +8: the path;
                               * -8 how to load, -4 load flags, +4 the handle */
    int      texture_count;
    int      blank_first;     /* chrblank_<set> textures, in the texture list */
    int      blank_count;
    uint32_t strings;         /* one string table per language */
    int      languages;
    int      string_count;
    uint32_t gallery_columns; /* the Gallery grid's column and row counts */
    uint32_t gallery_rows;
    uint32_t gallery_scale;   /* the float the Gallery scales its cells by */
    int      bio_base;        /* text id of chr id 0's Gallery description */
} Ts2Roster;

/* Each release's, in its config/<region>/overrides.inc; roster 0 means not
 * found in that release yet. */
extern const Ts2Roster k_ts2_roster;

/* xboxrecomp src/kernel: kernel_path.c, xbox_memory_layout.c */
extern int         xbox_mods_count(void);
extern const char *xbox_mods_dir(int i);
extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);

#define TS2_MEM16(a) (*(volatile int16_t *)((uintptr_t)(uint32_t)(a) + g_xbox_mem_offset))
#define TS2_PTR8(a)  ((char *)((uintptr_t)(uint32_t)(a) + g_xbox_mem_offset))

enum { TS2_CHR_MAX = 64, TS2_LANGS = 5, TS2_SLOT_NEXT = -2 };

typedef struct Ts2Char {
    char title[64];
    int  slot, chr, stats_from, sounds_from, unlock;  /* unlock: -1 keep, else kind */
    int  mod;                                         /* the RECOMP_MODS folder it came from */
    int  skip;                                        /* lost to another, or invalid */
    char name[TS2_LANGS][64];                         /* Latin-1; "" = name[0] */
    char bio[TS2_LANGS][400];
    char portrait[128];
    char background[32];
} Ts2Char;

static Ts2Char g_ts2_chars[TS2_CHR_MAX];
static int     g_ts2_char_count;
static int     g_ts2_gallery_cols;     /* 0: the game's own grid */
static int     g_ts2_gallery_base;     /* the game's own column count */

static char *ts2_trim(char *s)
{
    char *e;

    while (*s == ' ' || *s == '\t')
        s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        *--e = 0;
    return s;
}

/* The game's strings are Latin-1 (e.g. "Pr\xe4zision"); the file is UTF-8. */
static void ts2_to_latin1(char *out, size_t n, const char *in)
{
    size_t o = 0;
    const unsigned char *s = (const unsigned char *)in;

    while (*s && o + 1 < n) {
        unsigned c = *s++;
        if (c >= 0xC0 && c < 0xE0 && (*s & 0xC0) == 0x80) {
            c = ((c & 0x1F) << 6) | (*s++ & 0x3F);
            if (c > 0xFF)
                c = '?';
        } else if (c >= 0x80) {
            while ((*s & 0xC0) == 0x80)
                s++;
            c = '?';
        }
        out[o++] = (char)c;
    }
    out[o] = 0;
}

static void ts2_chars_read(const char *path, int mod)
{
    static const char *const langs[TS2_LANGS] = { "en", "fr", "de", "it", "es" };
    FILE *f = fopen(path, "r");
    char line[512];
    Ts2Char *c = NULL;

    if (!f) {
        fprintf(stderr, "[TS2-CHR] cannot read %s\n", path);
        return;
    }
    while (fgets(line, sizeof line, f)) {
        char *s = ts2_trim(line), *eq, *key, *val, *semi;

        if (!*s || *s == ';' || *s == '#')
            continue;
        if (*s == '[') {
            if (g_ts2_char_count == TS2_CHR_MAX) {
                fprintf(stderr, "[TS2-CHR] more than %d characters; the rest are ignored\n",
                        TS2_CHR_MAX);
                c = NULL;
                continue;
            }
            c = &g_ts2_chars[g_ts2_char_count++];
            memset(c, 0, sizeof *c);
            c->slot = c->chr = c->stats_from = c->sounds_from = -1;
            c->unlock = 1;
            c->mod = mod;
            snprintf(c->title, sizeof c->title, "%s", ts2_trim(s + 1));
            if (strchr(c->title, ']'))
                *strchr(c->title, ']') = 0;
            continue;
        }
        eq = strchr(s, '=');
        if (!c || !eq)
            continue;
        *eq = 0;
        key = ts2_trim(s);
        val = eq + 1;
        semi = strchr(val, ';');       /* a comment after the value */
        if (semi)
            *semi = 0;
        val = ts2_trim(val);
        if (!strcmp(key, "slot"))            c->slot = !strcmp(val, "next") ? TS2_SLOT_NEXT : atoi(val);
        else if (!strcmp(key, "chr"))        c->chr = atoi(val);
        else if (!strcmp(key, "stats"))      c->stats_from = atoi(val);
        else if (!strcmp(key, "sounds"))     c->sounds_from = atoi(val);
        else if (!strcmp(key, "portrait"))   snprintf(c->portrait, sizeof c->portrait, "%s", val);
        else if (!strcmp(key, "background")) snprintf(c->background, sizeof c->background, "%s", val);
        else if (!strcmp(key, "unlock"))
            c->unlock = !strcmp(val, "never") ? 0 : !strcmp(val, "keep") ? -1 : 1;
        else if (!strncmp(key, "name", 4) || !strncmp(key, "bio", 3)) {
            int bio = key[0] == 'b', n = bio ? 3 : 4, l;
            for (l = 0; l < TS2_LANGS; l++)
                if (key[n] == 0 ? l == 0 : (key[n] == '.' && !strcmp(key + n + 1, langs[l])))
                    break;
            if (l < TS2_LANGS && bio)
                ts2_to_latin1(c->bio[l], sizeof c->bio[l] - 1, val);
            else if (l < TS2_LANGS)
                ts2_to_latin1(c->name[l], sizeof c->name[l], val);
        }
    }
    fclose(f);
}

/* A copy of s in guest memory, which the game keeps a pointer to. */
static uint32_t ts2_guest_string(const char *s)
{
    uint32_t va = xbox_HeapAlloc((uint32_t)strlen(s) + 1, 4);

    if (va)
        memcpy(TS2_PTR8(va), s, strlen(s) + 1);
    return va;
}

/* Text id `text` in every language's table, pointed at a copy of the
 * language's own string, else the first one's. */
static void ts2_set_text(const Ts2Roster *r, int text, char (*str)[400], char (*name)[64])
{
    int l;

    if (text <= 0 || text >= r->string_count)
        return;
    for (l = 0; l < r->languages && l < TS2_LANGS; l++) {
        const char *v = str ? (str[l][0] ? str[l] : str[0]) : (name[l][0] ? name[l] : name[0]);
        uint32_t table = TS2_MEM32(r->strings + 4u * (uint32_t)l);
        uint32_t s = ts2_guest_string(v);
        if (table && s)
            TS2_MEM32(table + 4u * (uint32_t)text) = s;
    }
}

static void ts2_char_apply(const Ts2Roster *r, const Ts2Char *c)
{
    uint32_t entry = r->roster + (uint32_t)c->slot * 0x20;
    uint32_t def = r->defs + (uint32_t)c->chr * 0x44;
    int text = TS2_MEM16(def);

    TS2_MEM32(entry) = (uint32_t)c->chr;
    if (c->unlock >= 0)
        TS2_MEM32(entry + 0x0C) = (uint32_t)c->unlock;

    /* The name, by its text id. Another definition naming the same id would
     * be renamed too; none of the unplayed ids shares one (roster.md). */
    if (c->name[0][0])
        ts2_set_text(r, text, NULL, (char (*)[64])c->name);
    /* The Gallery's description ends in a line break, as the game's do. */
    if (c->bio[0][0] && r->bio_base) {
        Ts2Char tmp = *c;
        int l;
        for (l = 0; l < TS2_LANGS; l++)
            if (tmp.bio[l][0] && tmp.bio[l][strlen(tmp.bio[l]) - 1] != '\n')
                strcat(tmp.bio[l], "\n");
        ts2_set_text(r, r->bio_base + c->chr, tmp.bio, NULL);
    }

    /* The portrait: the slot's texture list entry now names the mod's file.
     * The list's paths are "host0:<path in the pak, ending .raw>" and the
     * game finds the .xbt of that name. */
    if (c->portrait[0]) {
        int tex = (int)TS2_MEM32(entry + 4);
        char path[160];
        char *dot;

        snprintf(path, sizeof path, "host0:%s", c->portrait);
        dot = strrchr(path, '.');
        if (dot && strchr(dot, '/') == NULL)
            strcpy(dot, ".raw");
        else
            strncat(path, ".raw", sizeof path - strlen(path) - 1);
        if (tex >= 0 && tex < r->texture_count) {
            uint32_t t = r->textures + (uint32_t)tex * 0x10;
            /* A spare slot's entry has load flags 0, so no level loads it;
             * it takes how and when slot 0's portrait is loaded (the front
             * end's chrimage_* textures, flags 3). */
            if (TS2_MEM32(t - 4) == 0) {
                uint32_t like = r->textures + TS2_MEM32(r->roster + 4) * 0x10;
                TS2_MEM32(t - 8) = TS2_MEM32(like - 8);
                TS2_MEM32(t - 4) = TS2_MEM32(like - 4);
            }
            TS2_MEM32(t) = ts2_guest_string(path);
        }
    }

    /* The set background shown while locked: chrblank_<set>.raw. */
    if (c->background[0]) {
        char want[48];
        int t;

        snprintf(want, sizeof want, "chrblank_%s.raw", c->background);
        for (t = r->blank_first; t < r->blank_first + r->blank_count; t++) {
            const char *p = TS2_PTR8(TS2_MEM32(r->textures + (uint32_t)t * 0x10));
            const char *slash = strrchr(p, '/');
            if (slash && !strcmp(slash + 1, want)) {
                TS2_MEM32(entry + 8) = (uint32_t)t;
                break;
            }
        }
    }

    if (c->stats_from >= 0 && c->stats_from < r->def_count) {
        uint32_t from = r->defs + (uint32_t)c->stats_from * 0x44;
        uint32_t o;
        for (o = 0x2C; o <= 0x3C; o += 4)
            TS2_MEM32(def + o) = TS2_MEM32(from + o);
    }
    if (c->sounds_from >= 0 && c->sounds_from < r->def_count)
        TS2_MEM32(def + 4) = TS2_MEM32(r->defs + (uint32_t)c->sounds_from * 0x44 + 4);

    fprintf(stderr, "[TS2-CHR] %s: roster slot %d = chr id %d (chr%02d.xbr), name text %d "
            "\"%s\", portrait %d \"%s\", background %u, unlock kind %u\n",
            c->title, c->slot, c->chr, c->chr + 1, text, c->name[0],
            (int)TS2_MEM32(entry + 4), c->portrait[0] ? c->portrait : "(the slot's own)",
            (unsigned)TS2_MEM32(entry + 8), (unsigned)TS2_MEM32(entry + 0x0C));
}

/* Every mod's sections, merged. A section is skipped, with a line saying
 * why, when its chr id or slot is out of range, when an earlier section
 * (an earlier mod in RECOMP_MODS, or earlier in the same file) already has
 * its chr id, or when its numbered slot is already an earlier section's.
 * Then `slot = next` sections, in the same order, take the free spare slots
 * no remaining section names, until the 32 run out. Numbered slots are
 * placed first so a later mod's `slot = 130` does not lose to an earlier
 * mod's `next`, which can take another spare slot instead. */
static void ts2_chars_resolve(const Ts2Roster *r)
{
    int i, j, slot;

    for (i = 0; i < g_ts2_char_count; i++) {
        Ts2Char *c = &g_ts2_chars[i];
        if (c->chr < 0 || c->chr >= r->def_count ||
            (c->slot != TS2_SLOT_NEXT && (c->slot < 0 || c->slot >= r->roster_count))) {
            fprintf(stderr, "[TS2-CHR] %s (mod %d): needs slot 0-%d or next (a free spare "
                    "slot, %d-%d) and chr 0-%d; skipped\n", c->title, c->mod,
                    r->roster_count - 1, r->spare_first, r->roster_count - 1, r->def_count - 1);
            c->skip = 1;
            continue;
        }
        for (j = 0; j < i && !c->skip; j++) {
            const Ts2Char *o = &g_ts2_chars[j];
            if (o->skip)
                continue;
            if (o->chr == c->chr) {
                fprintf(stderr, "[TS2-CHR] %s (mod %d, %s): chr id %d is already [%s]'s "
                        "(mod %d, %s); skipped. One of the two needs another chr id.\n",
                        c->title, c->mod, xbox_mods_dir(c->mod), c->chr, o->title, o->mod,
                        xbox_mods_dir(o->mod));
                c->skip = 1;
            } else if (c->slot != TS2_SLOT_NEXT && o->slot == c->slot) {
                fprintf(stderr, "[TS2-CHR] %s (mod %d, %s): roster slot %d is already [%s]'s "
                        "(mod %d, %s); skipped. slot = next takes a free one.\n", c->title,
                        c->mod, xbox_mods_dir(c->mod), c->slot, o->title, o->mod,
                        xbox_mods_dir(o->mod));
                c->skip = 1;
            }
        }
    }
    for (i = 0; i < g_ts2_char_count; i++) {
        Ts2Char *c = &g_ts2_chars[i];
        if (c->skip || c->slot != TS2_SLOT_NEXT)
            continue;
        for (slot = r->spare_first; slot < r->roster_count; slot++) {
            int taken = 0;
            for (j = 0; j < g_ts2_char_count && !taken; j++)
                taken = j != i && !g_ts2_chars[j].skip && g_ts2_chars[j].slot == slot;
            if (!taken)
                break;
        }
        if (slot < r->roster_count) {
            c->slot = slot;
        } else {
            fprintf(stderr, "[TS2-CHR] %s (mod %d, %s): every spare slot (%d-%d) is taken; "
                    "skipped\n", c->title, c->mod, xbox_mods_dir(c->mod), r->spare_first,
                    r->roster_count - 1);
            c->skip = 1;
        }
    }
}

/* The shown entries (unlock kind not 0) first, in their order, then the
 * hidden ones: the Gallery draws the first n entries, n the shown count, so
 * a hidden slot among them (126, the Consultant, before the spare slots)
 * would take a cell from the last shown one. The lists of characters walk
 * the table in order and skip hidden entries, so their order is unchanged;
 * what the game keeps of a character beyond a screen (profile, bot sets,
 * players) is its chr id, not its slot. */
static void ts2_chars_compact(const Ts2Roster *r)
{
    static uint32_t hidden[256 * 8];
    int i, k, shown = 0, n = 0;

    if (r->roster_count > 256)
        return;
    for (i = 0; i < r->roster_count; i++) {
        uint32_t e = r->roster + (uint32_t)i * 0x20;
        if (TS2_MEM32(e + 0x0C)) {
            if (shown != i)
                for (k = 0; k < 8; k++)
                    TS2_MEM32(r->roster + (uint32_t)shown * 0x20 + 4u * k) = TS2_MEM32(e + 4u * k);
            shown++;
        } else {
            for (k = 0; k < 8; k++)
                hidden[n * 8 + k] = TS2_MEM32(e + 4u * k);
            n++;
        }
    }
    for (i = 0; i < n; i++)
        for (k = 0; k < 8; k++)
            TS2_MEM32(r->roster + (uint32_t)(shown + i) * 0x20 + 4u * k) = hidden[i * 8 + k];
    fprintf(stderr, "[TS2-CHR] roster: %d shown, then %d hidden (%d slots)\n",
            shown, n, r->roster_count);
}

static void ts2_paks_init(void);

/* The game's main(argc, argv): the mods are indexed and the tables patched
 * before it reads them. */
static void ts2_chars_main(void (*main_fn)(void))
{
    const Ts2Roster *r = &k_ts2_roster;
    int i, m, files = 0, applied = 0;

    ts2_paks_init();
    for (m = 0; m < xbox_mods_count(); m++) {
        char path[1024];
        FILE *f;
        snprintf(path, sizeof path, "%s/ts2/characters.ini", xbox_mods_dir(m));
        f = fopen(path, "r");
        if (!f)
            continue;
        fclose(f);
        files++;
        if (!r->roster) {
            fprintf(stderr, "[TS2-CHR] %s: the roster has not been found in this "
                    "release; no characters added\n", path);
            continue;
        }
        fprintf(stderr, "[TS2-CHR] characters from %s (mod %d)\n", path, m);
        ts2_chars_read(path, m);
    }
    if (g_ts2_char_count) {
        ts2_chars_resolve(r);
        for (i = 0; i < g_ts2_char_count; i++) {
            if (g_ts2_chars[i].skip)
                continue;
            ts2_char_apply(r, &g_ts2_chars[i]);
            applied++;
        }
        if (applied)
            ts2_chars_compact(r);
        fprintf(stderr, "[TS2-CHR] %d of %d characters, from %d mods, added\n", applied,
                g_ts2_char_count, files);
    }
    fflush(stderr);
    main_fn();
}

/* sub_0008FAE0(list, item, rect) draws one item of the Select Character
 * list: it looks the item's chr id (+0x18) up in the roster for the
 * portrait, among the first 127 entries only (the bound is an imm8, which
 * the XBE patches cannot raise; config/pal/xbe_patches.json). For an item
 * whose character is further on, its entry and the last one in reach trade
 * places for the call. The draw reads that one entry, by chr id, and keeps
 * nothing. Without a mod's characters this only calls the game. */
static void ts2_chars_item_draw(void (*draw)(void))
{
    const Ts2Roster *r = &k_ts2_roster;
    uint32_t item, chr, a, b, t;
    int i, k = -1;

    if (!g_ts2_char_count || !r->roster || r->search_count >= r->roster_count) {
        draw();
        return;
    }
    item = TS2_MEM32(g_esp + 8);            /* [esp] is the return address */
    chr = item ? TS2_MEM32(item + 0x18) : 0xFFFFFFFFu;
    for (i = 0; i < r->roster_count && k < 0; i++)
        if (TS2_MEM32(r->roster + (uint32_t)i * 0x20) == chr)
            k = i;
    if (k < r->search_count) {
        draw();
        return;
    }
    a = r->roster + (uint32_t)k * 0x20;
    b = r->roster + (uint32_t)(r->search_count - 1) * 0x20;
    for (i = 0; i < 0x20; i += 4) {
        t = TS2_MEM32(a + i); TS2_MEM32(a + i) = TS2_MEM32(b + i); TS2_MEM32(b + i) = t;
    }
    draw();
    for (i = 0; i < 0x20; i += 4) {
        t = TS2_MEM32(a + i); TS2_MEM32(a + i) = TS2_MEM32(b + i); TS2_MEM32(b + i) = t;
    }
}

/* The Gallery's grid: the build sets 14 columns and 9 rows after the fact.
 * With more shown entries (unlock kind not 0) than cells, it gets as many
 * columns as 9 rows need; the cursor code already copes with a part-filled
 * last row (it wraps by rows * columns - count). */
static void ts2_gallery_build(void (*build)(void))
{
    const Ts2Roster *r = &k_ts2_roster;
    uint32_t cols, rows;
    int i, shown = 0;

    build();
    if (!g_ts2_char_count || !r->roster)
        return;
    for (i = 0; i < r->roster_count; i++)
        if (TS2_MEM32(r->roster + (uint32_t)i * 0x20 + 0x0C))
            shown++;
    cols = TS2_MEM32(r->gallery_columns);
    rows = TS2_MEM32(r->gallery_rows);
    if (cols && rows && (uint32_t)shown > cols * rows) {
        g_ts2_gallery_base = (int)cols;
        g_ts2_gallery_cols = (int)((shown + rows - 1) / rows);
        TS2_MEM32(r->gallery_columns) = (uint32_t)g_ts2_gallery_cols;
    }
}

/* The cells are a fixed fraction (a float in .rdata, 0.45 of a portrait's
 * size) of the portrait apart, so a fifteenth column runs past the panel's
 * right edge. For the draw alone, that fraction is made 14/15 of itself
 * (the game's columns over the new count):
 * the grid keeps its width and every cell its place in it. Other code reads
 * the same float, hence put back straight after. */
static void ts2_gallery_draw(void (*draw)(void))
{
    const Ts2Roster *r = &k_ts2_roster;
    float scale;

    if (!g_ts2_gallery_cols || !r->gallery_scale ||
        TS2_MEM32(r->gallery_columns) != (uint32_t)g_ts2_gallery_cols) {
        draw();
        return;
    }
    scale = TS2_MEMF(r->gallery_scale);
    TS2_MEMF(r->gallery_scale) = scale * (float)g_ts2_gallery_base / (float)g_ts2_gallery_cols;
    draw();
    TS2_MEMF(r->gallery_scale) = scale;
}

/* ── TimeSplitters 2: loose mod files inside the paks ────────── */

/*
 * Every file the game reads (models, textures, sounds, levels) is inside a
 * .pak on the disc, and a level does not share them: each story and arcade
 * level pak carries its own copy of every character in it. The toolkit's
 * RECOMP_MODS replaces whole files, so a mod used to be a set of rebuilt
 * paks, and two mods that both touched chr.pak could not both apply. Here a
 * mod folder may also hold single files from inside paks, laid out as the
 * tools/ts2mod.py source is:
 *
 *   files/<path in a pak>                that file, in whichever pak holds it
 *   paks/<pak under data>/<path in a pak>  that file in that pak only
 *                                         (paks/chr.pak/ob/chrs/chr71.xbr)
 *
 * and every RECOMP_MODS folder's are served at once. For one name in one
 * pak the first mod in RECOMP_MODS that supplies it wins, a mod's paks/<pak>
 * before its files/, then the pak's own copy (which may itself be a whole
 * pak from a mod: the loose files stack on top of it).
 *
 * How the game finds a file (code, PAL): sub_00085DB0 mounts a pak -- opens
 * it, reads its directory into memory and fills one 32-byte record of the
 * table at 0x358A38 (count at 0x358A18): +0 the open file, +0xC the entry
 * count, +0x10 the whole pak's address when it is read into memory (the
 * animation paks), +0x14 a P4CK directory (60-byte entries: name[48],
 * position, length, packed length) or +0x18 a P8CK one (12-byte entries:
 * name pointer, length, position, sorted by name: it is binary searched),
 * +0x1C the pak's path. sub_00086300 and sub_000860A0 look a name up in
 * every mounted P4CK and P8CK directory and return the entry, leaving the
 * record that held it in 0x358A30. Everything that reads a file takes its
 * length and position from that entry, through four functions only:
 * sub_00086430 (the length, for the buffer), sub_000863C0 (the address in
 * memory, for a pak read into memory), sub_000864B0 (seek and read) and
 * sub_00086880 (an overlapped read, for the character-select previews and
 * streamed loads). Nothing else touches the table.
 *
 * So a mod's file goes into the directory itself, as the pak is mounted
 * (ts2_paks_mount): a pak's entry for it gets the mod file's length (so the
 * size the game allocates is right everywhere) and a position that is no
 * offset in the pak; a name the pak lacks gets a new entry (a copy of the
 * directory with it inserted in order). A pak read into memory gets a
 * position that points at a copy of the file in guest memory, so
 * sub_000863C0 hands that out with no hook. The two read functions are
 * wrapped (ts2_paks_read, ts2_paks_read_async): they ask the game's own
 * lookups which entry the name resolves to, and serve the mod's file when it
 * is one of these.
 *
 * Where files/ applies. Texture numbers are global, but textures/0478.xbt
 * is another picture in story/l_20_ST.pak and arcade/l_43_AR.pak than in
 * chr.pak (the only clash among the disc's textures and models: every other
 * name has one content across all 68 paks). A files/ entry therefore
 * replaces a pak's copy only when that copy is as long as chr.pak's (read
 * from chr.pak's directory once at start-up); a name chr.pak lacks is
 * replaced everywhere. For a clashing name, use paks/<pak>/. A files/ name
 * a pak lacks is added to every P8CK pak not read into memory (level paks,
 * chr, gun, sounds), so the game finds it whichever level is loaded; P4CK
 * paks are searched before P8CK ones whatever their mount order, so a copy
 * added to them would hide a level's own. paks/<pak>/ adds to that pak
 * whatever its kind.
 *
 * The mods are indexed once, at start-up (xboxrecomp xbox_mods_walk); a
 * mount only compares the pak's directory against that index, and a read
 * opens the one host file it needs.
 */
typedef struct Ts2Paks {
    uint32_t table;          /* the mounted paks: 32-byte records (above) */
    uint32_t count;          /* dword: records in use */
    int      max;            /* records the table holds */
    uint32_t current;        /* dword: the record of the last lookup's file */
    uint32_t read_pending;   /* dword: sub_00086880 issued a read the game still polls */
    void   (*find_p4)(void); /* (name): its P4CK entry in any mounted pak, or 0 */
    void   (*find_p8)(void); /* (name): its P8CK entry, or 0 */
} Ts2Paks;

/* Each release's, in its config/<region>/overrides.inc; table 0 means not
 * found in that release yet, and nothing here runs. */
extern const Ts2Paks k_ts2_paks;

/* xboxrecomp src/kernel/kernel.h */
typedef void (*xbox_mods_walk_fn)(const char *rel, const char *host_path,
                                  unsigned long long size, void *ctx);
extern int         xbox_mods_walk(int mod, const char *sub, xbox_mods_walk_fn fn, void *ctx);
#ifdef _WIN32
extern int xbox_translate_path(const char *xbox_path, wchar_t *host_path_buf, unsigned long buf_size);
#else
extern int xbox_translate_path(const char *xbox_path, char *host_path_buf, uint32_t buf_size);
#endif

enum {
    TS2_PAK_NAME = 48,          /* a P4CK name's room, and as much as the lookups compare */
    TS2_PAK_RECORDS = 32,       /* at least the game's table (25) */
    TS2_PAK_FILE_MAX = 64 << 20
};
#define TS2_PAK_MOD_POS 0x80000000u   /* a mod file's position: no offset in any pak */

typedef struct Ts2ModFile {
    char     name[TS2_PAK_NAME];   /* its path in a pak, as the mod spells it */
    char     lower[TS2_PAK_NAME];  /* the same, lower case: what is compared */
    char     pak[96];              /* paks/<this>: "chr.pak", "arcade/l_101.pak" (lower
                                    * case); "" for files/ */
    char    *host;
    uint32_t size;
    int      mod;
    int      ref_len;              /* files/: chr.pak's copy's length; -1 if it has none */
    uint32_t guest_name;           /* copies in guest memory, made when first needed */
    uint32_t guest_data;
} Ts2ModFile;

typedef struct Ts2PakRec {
    uint32_t  dir, count;         /* the directory and count as last patched */
    uint32_t  buf, buf_bytes;     /* guest copy of a directory that gained entries */
    int       n, cap;             /* entries served from mods */
    uint32_t *entry;
    int      *file;
    uint32_t *pos;                /* what was written to each, to tell a re-read apart */
} Ts2PakRec;

static Ts2ModFile *g_ts2_mf;
static int         g_ts2_mf_count, g_ts2_mf_cap;
static int         g_ts2_paks_ready;
static int         g_ts2_paks_live;      /* some mounted entry is a mod's */
static Ts2PakRec   g_ts2_recs[TS2_PAK_RECORDS];

static void ts2_lower(char *out, size_t n, const char *in)
{
    size_t i;
    for (i = 0; in[i] && i + 1 < n; i++)
        out[i] = (char)(in[i] >= 'A' && in[i] <= 'Z' ? in[i] - 'A' + 'a' : in[i]);
    out[i] = 0;
}

static void ts2_mf_add(const char *name, const char *pak, const char *host,
                       unsigned long long size, int mod)
{
    Ts2ModFile *f;

    if (strlen(name) >= TS2_PAK_NAME || strlen(pak) >= sizeof f->pak) {
        fprintf(stderr, "[TS2-MODS] %s: a name in a pak is at most %d characters; left out\n",
                host, TS2_PAK_NAME - 1);
        return;
    }
    if (size > TS2_PAK_FILE_MAX) {
        fprintf(stderr, "[TS2-MODS] %s: over %d MB; left out\n", host, TS2_PAK_FILE_MAX >> 20);
        return;
    }
    if (g_ts2_mf_count == g_ts2_mf_cap) {
        int cap = g_ts2_mf_cap ? g_ts2_mf_cap * 2 : 256;
        Ts2ModFile *grown = (Ts2ModFile *)realloc(g_ts2_mf, (size_t)cap * sizeof *grown);
        if (!grown)
            return;
        g_ts2_mf = grown;
        g_ts2_mf_cap = cap;
    }
    f = &g_ts2_mf[g_ts2_mf_count];
    memset(f, 0, sizeof *f);
    snprintf(f->name, sizeof f->name, "%s", name);
    ts2_lower(f->lower, sizeof f->lower, name);
    ts2_lower(f->pak, sizeof f->pak, pak);
    f->host = (char *)malloc(strlen(host) + 1);
    if (!f->host)
        return;
    strcpy(f->host, host);
    f->size = (uint32_t)size;
    f->mod = mod;
    f->ref_len = -1;
    g_ts2_mf_count++;
}

static void ts2_mf_walk_files(const char *rel, const char *host, unsigned long long size, void *ctx)
{
    ts2_mf_add(rel, "", host, size, *(const int *)ctx);
}

/* paks/<pak path under data, ending .pak>/<path in that pak> */
static void ts2_mf_walk_paks(const char *rel, const char *host, unsigned long long size, void *ctx)
{
    const char *p = rel;

    for (;;) {
        const char *slash = strchr(p, '/');
        if (!slash)
            break;
        if (slash - p >= 4) {
            char ext[5];
            ts2_lower(ext, sizeof ext, slash - 4);
            if (!strncmp(ext, ".pak", 4)) {
                char pak[128];
                snprintf(pak, sizeof pak, "%.*s", (int)(slash - rel), rel);
                ts2_mf_add(slash + 1, pak, host, size, *(const int *)ctx);
                return;
            }
        }
        p = slash + 1;
    }
    fprintf(stderr, "[TS2-MODS] %s: under paks/ but not in a <name>.pak folder; left out\n", host);
}

/* By name, then mod (the first in RECOMP_MODS first), then paks/ before files/. */
static int ts2_mf_order(const void *pa, const void *pb)
{
    const Ts2ModFile *a = (const Ts2ModFile *)pa, *b = (const Ts2ModFile *)pb;
    int c = strcmp(a->lower, b->lower);

    if (c)
        return c;
    if (a->mod != b->mod)
        return a->mod < b->mod ? -1 : 1;
    if (!a->pak[0] != !b->pak[0])
        return a->pak[0] ? -1 : 1;
    return strcmp(a->pak, b->pak);
}

/* The first mod file of that (lower-case) name, or -1. */
static int ts2_mf_first(const char *lower)
{
    int lo = 0, hi = g_ts2_mf_count;

    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (strcmp(g_ts2_mf[mid].lower, lower) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < g_ts2_mf_count && !strcmp(g_ts2_mf[lo].lower, lower) ? lo : -1;
}

/* Which mod file serves `lower` in `pak`: the first, in RECOMP_MODS order,
 * that is paks/<pak>/'s, or files/'s where files/ applies. have: the pak
 * holds that name already, have_len long. may_add: files/ may add a name
 * to this pak. -1 for none. */
static int ts2_mf_pick(int first, const char *pak, int have, uint32_t have_len, int may_add)
{
    int i;

    for (i = first; i >= 0 && i < g_ts2_mf_count &&
                    !strcmp(g_ts2_mf[i].lower, g_ts2_mf[first].lower); i++) {
        const Ts2ModFile *f = &g_ts2_mf[i];
        if (f->pak[0]) {
            if (!strcmp(f->pak, pak))
                return i;
        } else if (have ? (f->ref_len < 0 || (uint32_t)f->ref_len == have_len) : may_add) {
            return i;
        }
    }
    return -1;
}

static int ts2_file_same(const char *a, const char *b, uint32_t size)
{
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    char ba[65536], bb[65536];
    int same = fa && fb;

    while (same && size) {
        size_t n = size < sizeof ba ? size : sizeof ba;
        same = fread(ba, 1, n, fa) == n && fread(bb, 1, n, fb) == n && !memcmp(ba, bb, n);
        size -= (uint32_t)n;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return same;
}

/* chr.pak's lengths, as the game will see it (a mod's whole chr.pak if one
 * has it), for the files/ rule above. */
static void ts2_paks_reference(void)
{
    unsigned char head[20];
    uint32_t dir_off, count, names_off, names_len, k;
    unsigned char *names = NULL, *dir = NULL;
    FILE *f = NULL;
    int i, any = 0;

    for (i = 0; i < g_ts2_mf_count && !any; i++)
        any = !g_ts2_mf[i].pak[0];
    if (!any)
        return;
    {
#ifdef _WIN32
        wchar_t host[1024];
        if (xbox_translate_path("D:\\data\\chr.pak", host, 1024))
            f = _wfopen(host, L"rb");
#else
        char host[1024];
        if (xbox_translate_path("D:\\data\\chr.pak", host, sizeof host))
            f = fopen(host, "rb");
#endif
    }
    if (!f || fread(head, 1, sizeof head, f) != sizeof head || memcmp(head, "P8CK", 4)) {
        fprintf(stderr, "[TS2-MODS] cannot read chr.pak's directory: files/ replaces every "
                "copy of a name\n");
        if (f)
            fclose(f);
        return;
    }
    memcpy(&dir_off, head + 4, 4);
    memcpy(&count, head + 8, 4);
    memcpy(&names_off, head + 12, 4);
    memcpy(&names_len, head + 16, 4);
    names = (unsigned char *)malloc((size_t)names_len + 1);
    dir = (unsigned char *)malloc((size_t)count * 12 + 1);
    if (names && dir && count < 0x100000 && names_len < 0x1000000 &&
        !fseek(f, (long)names_off, SEEK_SET) && fread(names, 1, names_len, f) == names_len &&
        !fseek(f, (long)dir_off, SEEK_SET) && fread(dir, 1, (size_t)count * 12, f) == count * 12) {
        names[names_len] = 0;
        for (k = 0; k < count; k++) {
            uint32_t name_off, len;
            char lower[TS2_PAK_NAME];
            int first;
            memcpy(&name_off, dir + 12 * k, 4);
            memcpy(&len, dir + 12 * k + 4, 4);
            if (name_off >= names_len)
                continue;
            ts2_lower(lower, sizeof lower, (const char *)names + name_off);
            for (first = ts2_mf_first(lower); first >= 0 && first < g_ts2_mf_count &&
                                              !strcmp(g_ts2_mf[first].lower, lower); first++)
                if (!g_ts2_mf[first].pak[0])
                    g_ts2_mf[first].ref_len = (int)len;
        }
    }
    free(names);
    free(dir);
    fclose(f);
}

/* Two mods that supply the same name for the same pak with different
 * contents: the earlier one wins. New textures take ids from a hash of the
 * picture (ts2gltf, ts2kitbash), so for them this is a rare collision rather
 * than a choice, and worth a line. */
static void ts2_paks_collisions(void)
{
    int i, j;

    for (i = 0; i < g_ts2_mf_count; i++) {
        for (j = i + 1; j < g_ts2_mf_count && !strcmp(g_ts2_mf[j].lower, g_ts2_mf[i].lower); j++) {
            const Ts2ModFile *a = &g_ts2_mf[i], *b = &g_ts2_mf[j];
            int k, shown = 0, id;
            if (a->mod == b->mod || (a->pak[0] && b->pak[0] && strcmp(a->pak, b->pak)))
                continue;
            for (k = i + 1; k < j && !shown; k++)   /* reported against an earlier one */
                shown = g_ts2_mf[k].mod == a->mod && g_ts2_mf[k].mod != b->mod &&
                        !strcmp(g_ts2_mf[k].pak, a->pak);
            if (shown || (a->size == b->size && ts2_file_same(a->host, b->host, a->size)))
                continue;
            fprintf(stderr, "[TS2-MODS] %s: mod %d (%s) and mod %d (%s) both supply it for %s, "
                    "and they differ; mod %d's is used\n", a->lower, a->mod, xbox_mods_dir(a->mod),
                    b->mod, xbox_mods_dir(b->mod), a->pak[0] ? a->pak : b->pak[0] ? b->pak : "every pak",
                    a->mod);
            if (sscanf(a->lower, "textures/%d.xbt", &id) == 1 && id >= 8000 && id <= 9999)
                fprintf(stderr, "[TS2-MODS]   texture %d is a new id picked by hash in both: "
                        "rebuild one of the two mods to move it\n", id);
        }
    }
}

/* Index every mod's files/ and paks/, once. */
static void ts2_paks_init(void)
{
    int m, mods;

    if (g_ts2_paks_ready)
        return;
    g_ts2_paks_ready = 1;
    if (!k_ts2_paks.table)
        return;
    mods = xbox_mods_count();
    for (m = 0; m < mods; m++) {
        int before = g_ts2_mf_count, in_files;
        xbox_mods_walk(m, "paks", ts2_mf_walk_paks, &m);
        in_files = g_ts2_mf_count;
        xbox_mods_walk(m, "files", ts2_mf_walk_files, &m);
        if (g_ts2_mf_count > before)
            fprintf(stderr, "[TS2-MODS] mod %d (%s): %d files in paks/, %d in files/\n", m,
                    xbox_mods_dir(m), in_files - before, g_ts2_mf_count - in_files);
    }
    if (!g_ts2_mf_count)
        return;
    qsort(g_ts2_mf, (size_t)g_ts2_mf_count, sizeof *g_ts2_mf, ts2_mf_order);
    ts2_paks_reference();
    ts2_paks_collisions();
    fflush(stderr);
}

/* The mod file's bytes, up to n, into guest memory at dst. */
static uint32_t ts2_mf_load(const Ts2ModFile *f, uint32_t dst, uint32_t n)
{
    FILE *fp = fopen(f->host, "rb");
    size_t got = 0;

    if (n > f->size)
        n = f->size;
    if (fp) {
        got = fread(TS2_PTR8(dst), 1, n, fp);
        fclose(fp);
    }
    if (got != n)
        fprintf(stderr, "[TS2-MODS] %s: read %u of %u bytes\n", f->host, (unsigned)got, (unsigned)n);
    return (uint32_t)got;
}

typedef struct Ts2DirEnt {
    char     name[TS2_PAK_NAME];
    uint32_t name_va, len, pos, packed;
    int      file;               /* the mod file it serves, or -1 */
} Ts2DirEnt;

static int ts2_dirent_order(const void *pa, const void *pb)
{
    /* As the game's lookup compares (strncmp, so by unsigned bytes). */
    return strcmp(((const Ts2DirEnt *)pa)->name, ((const Ts2DirEnt *)pb)->name);
}

/* "host0:data/arcade/l_101.pak" -> "arcade/l_101.pak", lower case. */
static void ts2_pak_rel(const char *path, char *out, size_t n)
{
    char lower[160];

    ts2_lower(lower, sizeof lower, path);
    path = lower;
    if (!strncmp(path, "host0:", 6))
        path += 6;
    while (*path == '/' || *path == '\\')
        path++;
    if (!strncmp(path, "data/", 5) || !strncmp(path, "data\\", 5))
        path += 5;
    snprintf(out, n, "%s", path);
}

/* The record's directory still holds what was written to it. */
static int ts2_paks_intact(const Ts2PakRec *pr, uint32_t dir, uint32_t count, int p4)
{
    int k;

    if (pr->dir != dir || pr->count != count)
        return 0;
    for (k = 0; k < pr->n; k++)
        if (TS2_MEM32(pr->entry[k] + (p4 ? 0x30 : 8)) != pr->pos[k])
            return 0;
    return 1;
}

/* Puts the mods' files into record r's directory (r: a pak just mounted). */
static void ts2_paks_patch(int r)
{
    const Ts2Paks *P = &k_ts2_paks;
    uint32_t rec = P->table + 32u * (uint32_t)r;
    uint32_t count = TS2_MEM32(rec + 0x0C), base = TS2_MEM32(rec + 0x10);
    uint32_t p4dir = TS2_MEM32(rec + 0x14), p8dir = TS2_MEM32(rec + 0x18);
    int p4 = p4dir != 0, resident = base != 0;
    uint32_t dir = p4 ? p4dir : p8dir, esz = p4 ? 60u : 12u, target, k;
    Ts2PakRec *pr = &g_ts2_recs[r];
    Ts2DirEnt *ents;
    unsigned char *seen;
    char pak[160];
    int n = 0, replaced = 0, added = 0, i;

    if (!dir || count > 0x40000)
        return;
    if (pr->n && ts2_paks_intact(pr, dir, count, p4))
        return;
    ts2_pak_rel(TS2_PTR8(TS2_MEM32(rec + 0x1C)), pak, sizeof pak);
    ents = (Ts2DirEnt *)malloc(((size_t)count + (size_t)g_ts2_mf_count) * sizeof *ents);
    seen = (unsigned char *)calloc((size_t)g_ts2_mf_count + 1, 1);
    if (!ents || !seen) {
        free(ents);
        free(seen);
        return;
    }
    pr->n = 0;

    for (k = 0; k < count; k++) {
        uint32_t e = dir + k * esz;
        Ts2DirEnt *d = &ents[n++];
        char lower[TS2_PAK_NAME];
        int first;
        if (p4) {
            memcpy(d->name, TS2_PTR8(e), TS2_PAK_NAME);
            d->name[TS2_PAK_NAME - 1] = 0;
            d->name_va = 0;
            d->pos = TS2_MEM32(e + 0x30);
            d->len = TS2_MEM32(e + 0x34);
            d->packed = TS2_MEM32(e + 0x38);
        } else {
            d->name_va = TS2_MEM32(e);
            snprintf(d->name, sizeof d->name, "%s", TS2_PTR8(d->name_va));
            d->len = TS2_MEM32(e + 4);
            d->pos = TS2_MEM32(e + 8);
            d->packed = 0;
        }
        d->file = -1;
        ts2_lower(lower, sizeof lower, d->name);
        first = ts2_mf_first(lower);
        if (first >= 0) {
            seen[first] = 1;
            d->file = ts2_mf_pick(first, pak, 1, d->len, 0);
        }
    }
    /* Names the pak lacks. */
    for (i = 0; i < g_ts2_mf_count; i++) {
        int f;
        if (seen[i] || (i && !strcmp(g_ts2_mf[i].lower, g_ts2_mf[i - 1].lower)))
            continue;
        f = ts2_mf_pick(i, pak, 0, 0, !p4 && !resident);
        if (f < 0)
            continue;
        if (!p4 && !g_ts2_mf[f].guest_name)
            g_ts2_mf[f].guest_name = ts2_guest_string(g_ts2_mf[f].name);
        if (!p4 && !g_ts2_mf[f].guest_name)
            continue;
        memset(&ents[n], 0, sizeof ents[n]);
        snprintf(ents[n].name, sizeof ents[n].name, "%s", g_ts2_mf[f].name);
        ents[n].name_va = g_ts2_mf[f].guest_name;
        ents[n].file = f;
        n++;
        added++;
    }
    /* What each served entry says: the mod file's length, no packing, and a
     * position that is no offset in the pak (a pak read into memory: the
     * file's copy in guest memory, as an offset from the pak's). */
    for (i = 0; i < n; i++) {
        Ts2ModFile *f;
        if (ents[i].file < 0)
            continue;
        f = &g_ts2_mf[ents[i].file];
        if (resident) {
            if (!f->guest_data) {
                f->guest_data = xbox_HeapAlloc(f->size ? f->size : 16, 16);
                if (f->guest_data)
                    ts2_mf_load(f, f->guest_data, f->size);
            }
            if (!f->guest_data || ((f->guest_data - base) & 0xF)) {
                fprintf(stderr, "[TS2-MODS] data/%s: no room for %s in memory; left out\n",
                        pak, f->name);
                ents[i].file = -1;
                if ((uint32_t)i >= count) {    /* an added one: dropped */
                    ents[i--] = ents[--n];
                    added--;
                }
                continue;
            }
            ents[i].pos = f->guest_data - base;
        } else {
            ents[i].pos = TS2_PAK_MOD_POS | ((uint32_t)ents[i].file << 4);
        }
        ents[i].len = f->size;
        ents[i].packed = 0;
        if ((uint32_t)i < count)
            replaced++;
    }

    target = dir;
    if (added) {
        uint32_t bytes = (uint32_t)n * esz;
        if (pr->buf_bytes < bytes) {
            uint32_t buf = xbox_HeapAlloc(bytes + 64 * esz, 16);
            if (!buf) {
                fprintf(stderr, "[TS2-MODS] data/%s: no memory for %d added files\n", pak, added);
                n = (int)count;
                added = 0;
            } else {
                pr->buf = buf;
                pr->buf_bytes = bytes + 64 * esz;
            }
        }
        if (added) {
            target = pr->buf;
            if (!p4)
                qsort(ents, (size_t)n, sizeof *ents, ts2_dirent_order);
        }
    }
    for (i = 0; i < n; i++) {
        uint32_t e = target + (uint32_t)i * esz;
        if (target == dir && ents[i].file < 0)
            continue;              /* unchanged */
        if (p4) {
            if (target != dir) {
                memset(TS2_PTR8(e), 0, TS2_PAK_NAME);
                memcpy(TS2_PTR8(e), ents[i].name, strlen(ents[i].name));
            }
            TS2_MEM32(e + 0x30) = ents[i].pos;
            TS2_MEM32(e + 0x34) = ents[i].len;
            TS2_MEM32(e + 0x38) = ents[i].packed;
        } else {
            TS2_MEM32(e) = ents[i].name_va;
            TS2_MEM32(e + 4) = ents[i].len;
            TS2_MEM32(e + 8) = ents[i].pos;
        }
        if (ents[i].file >= 0) {
            if (pr->n == pr->cap) {
                int cap = pr->cap ? pr->cap * 2 : 64;
                uint32_t *ne = (uint32_t *)realloc(pr->entry, (size_t)cap * sizeof *ne);
                uint32_t *np = ne ? (uint32_t *)realloc(pr->pos, (size_t)cap * sizeof *np) : NULL;
                int *nf = np ? (int *)realloc(pr->file, (size_t)cap * sizeof *nf) : NULL;
                if (ne) pr->entry = ne;
                if (np) pr->pos = np;
                if (!nf)
                    break;
                pr->file = nf;
                pr->cap = cap;
            }
            pr->entry[pr->n] = e;
            pr->file[pr->n] = ents[i].file;
            pr->pos[pr->n] = ents[i].pos;
            pr->n++;
        }
    }
    if (target != dir) {
        TS2_MEM32(rec + (p4 ? 0x14 : 0x18)) = target;
        TS2_MEM32(rec + 0x0C) = (uint32_t)n;
    }
    pr->dir = target;
    pr->count = TS2_MEM32(rec + 0x0C);
    if (pr->n)
        g_ts2_paks_live = 1;
    if (replaced || added) {
        static int verbose = -1;
        if (verbose < 0)
            verbose = getenv("RECOMP_TS2_MODS_TRACE") && atoi(getenv("RECOMP_TS2_MODS_TRACE"));
        fprintf(stderr, "[TS2-MODS] data/%s: %d files from mods, %d of them added%s\n",
                pak, replaced + added, added, verbose ? "" : " (RECOMP_TS2_MODS_TRACE=1 lists them)");
        for (i = 0; verbose && i < pr->n; i++) {
            const Ts2ModFile *f = &g_ts2_mf[pr->file[i]];
            fprintf(stderr, "[TS2-MODS]   %s from mod %d (%s%s%s)\n", f->name, f->mod,
                    f->pak[0] ? "paks/" : "files/", f->pak, f->pak[0] ? "/" : "");
        }
        fflush(stderr);
    }
    free(ents);
    free(seen);
}

/* sub_00085DB0(path, read_into_memory, heap) mounts a pak and returns its
 * record (the last one; a P4CK pak mounted again reuses its own record and
 * still returns the last). Every record holding that path is looked at
 * afterwards; one whose directory still holds what was written is left. */
static void ts2_paks_mount(void (*mount)(void))
{
    const Ts2Paks *P = &k_ts2_paks;
    char path[160];
    uint32_t eax, count;
    int r;

    if (!g_ts2_paks_ready)
        ts2_paks_init();
    if (!g_ts2_mf_count || !P->table) {
        mount();
        return;
    }
    snprintf(path, sizeof path, "%s", TS2_PTR8(TS2_MEM32(g_esp + 4)));
    mount();
    eax = g_eax;
    count = TS2_MEM32(P->count);
    for (r = 0; r < (int)count && r < P->max && r < TS2_PAK_RECORDS; r++) {
        uint32_t name = TS2_MEM32(P->table + 32u * (uint32_t)r + 0x1C);
        if (name && !strcmp(TS2_PTR8(name), path))
            ts2_paks_patch(r);
    }
    g_eax = eax;
}

/* The mod file a directory entry serves, or -1: the record the lookup
 * named first, then the others. */
static int ts2_paks_file_of(uint32_t entry)
{
    const Ts2Paks *P = &k_ts2_paks;
    uint32_t count = TS2_MEM32(P->count), cur = TS2_MEM32(P->current);
    int first = cur >= P->table ? (int)((cur - P->table) / 32u) : -1, pass, r, k;

    for (pass = 0; pass < 2; pass++)
        for (r = pass ? 0 : first; r >= 0 && r < (int)count && r < TS2_PAK_RECORDS; r++) {
            const Ts2PakRec *pr = &g_ts2_recs[r];
            for (k = 0; k < pr->n; k++)
                if (pr->entry[k] == entry)
                    return pr->file[k];
            if (!pass)
                break;
        }
    return -1;
}

/* The entry the game's lookups give a name: P4CK paks first, as every
 * reader asks them. */
static uint32_t ts2_paks_find(uint32_t name)
{
    const Ts2Paks *P = &k_ts2_paks;
    uint32_t e;
    int pass;

    for (pass = 0; pass < 2; pass++) {
        g_esp -= 4;
        TS2_MEM32(g_esp) = name;
        g_esp -= 4;
        TS2_MEM32(g_esp) = 0;              /* return address: none */
        (pass ? P->find_p8 : P->find_p4)();
        g_esp += 4;                        /* the argument; the callee popped the return */
        e = g_eax;
        if (e)
            return e;
    }
    return 0;
}

/* sub_000864B0(name, buffer, size) reads a file from the mounted paks: up
 * to size bytes (all of size from a P4CK pak), the count read returned. A
 * mod's file is read from the mod instead. */
static void ts2_paks_read(void (*read)(void))
{
    uint32_t name, buf, size, e;
    int f;

    if (!g_ts2_paks_live || !TS2_MEM32(k_ts2_paks.count)) {
        read();
        return;
    }
    name = TS2_MEM32(g_esp + 4);
    buf = TS2_MEM32(g_esp + 8);
    size = TS2_MEM32(g_esp + 12);
    e = ts2_paks_find(name);
    f = e ? ts2_paks_file_of(e) : -1;
    if (f < 0) {
        read();
        return;
    }
    g_eax = ts2_mf_load(&g_ts2_mf[f], buf, size);
    g_esp += 4;                            /* ret */
}

/* sub_00086880(name, buffer, size) starts an overlapped read of a file in
 * the mounted paks (size rounded up to 2048) and returns non-zero once it
 * is under way; sub_00086970 polls it, and reports it done when no read is
 * pending. A mod's file is read there and then. */
static void ts2_paks_read_async(void (*start)(void))
{
    uint32_t name, buf, size, e;
    int f;

    if (!g_ts2_paks_live || !TS2_MEM32(k_ts2_paks.count)) {
        start();
        return;
    }
    name = TS2_MEM32(g_esp + 4);
    buf = TS2_MEM32(g_esp + 8);
    size = TS2_MEM32(g_esp + 12);
    e = ts2_paks_find(name);
    f = e ? ts2_paks_file_of(e) : -1;
    if (f < 0) {
        start();
        return;
    }
    ts2_mf_load(&g_ts2_mf[f], buf, (size + 0x7FFu) & ~0x7FFu);
    TS2_MEM32(k_ts2_paks.read_pending) = 0;
    g_eax = 1;
    g_esp += 4;                            /* ret */
}

/* ── TimeSplitters 2: where this release's functions are ─────── */

/*
 * Everything above is the same in every release of the game; where it
 * attaches is not. config/<region>/overrides.inc, one per release, wraps
 * that release's functions with the hooks above (its camera_setup with
 * ts2_camera_setup, and so on) and holds its k_ts2_ui_places. Which release
 * src/recomp/gen was lifted from is in src/recomp/ts2_region.h, which
 * scripts/build.py writes with the lift.
 *
 * The lifter decides what not to generate by reading this file as text
 * (xboxrecomp tools/recomp/manual_scan.py) and does not follow the #include
 * below, so scripts/build.py hands it this file and the release's
 * overrides.inc as one: each release's functions, and only those, are
 * wrapped. A function goes into a release's overrides.inc only once it has
 * been found in that release by what it does. A release that has not found
 * one leaves its hook unused, and the lifted function runs as it is.
 */
#include "ts2_region.h"

#if defined(TS2_REGION_PAL)
#include "../config/pal/overrides.inc"
#elif defined(TS2_REGION_US)
#include "../config/us/overrides.inc"
#else
#error "src/recomp/ts2_region.h names no release this file knows; lift with scripts/build.py"
#endif
