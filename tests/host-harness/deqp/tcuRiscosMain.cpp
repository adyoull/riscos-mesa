/*
 * main() for dEQP on the fake RISC OS. riscos-mesa's EGL passes pointers
 * to "SWIs" in 32-bit registers, as on RISC OS, so everything it touches
 * must be below 2 GB: the program is built -no-pie, malloc stays on the
 * brk heap, and dEQP's own main runs on a thread with a stack below 2 GB,
 * as do the threads its tests start. The linker's --wrap=main,
 * --wrap=pthread_create and --wrap=pthread_join (riscos.cmake) route them
 * through here.
 * Part of riscos-mesa, MIT licence.
 */
#include <malloc.h>
#include <pthread.h>
#include <sys/mman.h>

#include <cerrno>
#include <cstdint>

// dEQP's main (tcuMain.cpp), reached through the linker's --wrap=main
extern "C" int __real_main(int argc, char **argv);

extern "C" int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
extern "C" int __real_pthread_join(pthread_t, void **);

// Thread stacks: 8 MB slots from 1.75 GB up to 2 GB, well above the brk heap
// (which must stay free to grow: when brk is blocked glibc falls back to
// mmap, far above 4 GB). A slot is reused once its thread has been joined;
// detached threads keep theirs.
enum
{
    STACK_BASE  = 0x70000000,
    STACK_SIZE  = 8 << 20,
    STACK_STEP  = 9 << 20, // a gap between stacks
    STACK_SLOTS = (0x80000000u - STACK_BASE) / STACK_STEP
};

static pthread_mutex_t g_slot_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_slot_thread[STACK_SLOTS];
static bool g_slot_used[STACK_SLOTS], g_slot_mapped[STACK_SLOTS];

static int take_slot(void)
{
    pthread_mutex_lock(&g_slot_lock);
    for (int i = 0; i < (int)STACK_SLOTS; i++)
    {
        if (g_slot_used[i])
            continue;
        if (!g_slot_mapped[i])
        {
            void *want = (void *)(uintptr_t)(STACK_BASE + (uintptr_t)i * STACK_STEP);
            void *got  = mmap(want, STACK_SIZE, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_STACK, -1, 0);
            if (got != want)
            {
                if (got != MAP_FAILED)
                    munmap(got, STACK_SIZE);
                continue; // something else lives there
            }
            g_slot_mapped[i] = true;
        }
        g_slot_used[i] = true;
        pthread_mutex_unlock(&g_slot_lock);
        return i;
    }
    pthread_mutex_unlock(&g_slot_lock);
    return -1;
}

extern "C" int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *),
                                     void *arg)
{
    pthread_attr_t low;
    int detach = PTHREAD_CREATE_JOINABLE;
    const int slot = take_slot();
    if (slot < 0)
        return EAGAIN;
    if (attr)
        pthread_attr_getdetachstate(attr, &detach);
    pthread_attr_init(&low);
    pthread_attr_setdetachstate(&low, detach);
    pthread_attr_setstack(&low, (void *)(uintptr_t)(STACK_BASE + (uintptr_t)slot * STACK_STEP), STACK_SIZE);
    const int r = __real_pthread_create(thread, &low, start, arg);
    pthread_attr_destroy(&low);
    pthread_mutex_lock(&g_slot_lock);
    if (r == 0 && detach == PTHREAD_CREATE_JOINABLE)
        g_slot_thread[slot] = *thread;
    else if (r != 0)
        g_slot_used[slot] = false;
    pthread_mutex_unlock(&g_slot_lock);
    return r;
}

extern "C" int __wrap_pthread_join(pthread_t thread, void **ret)
{
    const int r = __real_pthread_join(thread, ret);
    if (r == 0)
    {
        pthread_mutex_lock(&g_slot_lock);
        for (int i = 0; i < (int)STACK_SLOTS; i++)
            if (g_slot_used[i] && pthread_equal(g_slot_thread[i], thread))
            {
                g_slot_used[i]   = false;
                g_slot_thread[i] = pthread_t();
                break;
            }
        pthread_mutex_unlock(&g_slot_lock);
    }
    return r;
}

// Before anything is allocated: keep malloc on the brk heap
__attribute__((constructor(101))) static void low_malloc(void)
{
    mallopt(M_ARENA_MAX, 1);
    mallopt(M_MMAP_MAX, 0);
    mallopt(M_TOP_PAD, 64 << 20);
}

static int g_argc, g_status;
static char **g_argv;

static void *run(void *)
{
    g_status = __real_main(g_argc, g_argv);
    return nullptr;
}

extern "C" int __wrap_main(int argc, char **argv)
{
    pthread_t t;
    g_argc = argc;
    g_argv = argv;
    if (pthread_create(&t, nullptr, run, nullptr) != 0) // (wrapped: low stack)
        return 2;
    pthread_join(t, nullptr);
    return g_status;
}
