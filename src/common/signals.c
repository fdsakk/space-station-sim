/* Obsługa SIGINT, SIGTERM i SIGHUP: funkcja obsługi tylko ustawia flagę,
 * a pętla główna ją sprawdza. */
#include "signals.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "die.h"

/* Numer odebranego sygnału zatrzymania; 0 = brak. Typ sig_atomic_t gwarantuje
 * niepodzielny zapis z funkcji obsługi sygnału. */
static volatile sig_atomic_t stop_signal = 0;

static void on_stop(int sig)
{
    stop_signal = sig;
}

static void set_stop_action(void (*action)(int))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = action;
    sigemptyset(&sa.sa_mask);
    /* Bez SA_RESTART: blokujące wywołania (mq_timedreceive, recvfrom) wracają z EINTR,
     * więc pętla główna od razu widzi flagę. */
    sa.sa_flags = 0;
    /* SIGHUP przychodzi przy zamknięciu terminala; bez obsługi stacja ginie bez sprzątania. */
    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1 ||
        sigaction(SIGHUP, &sa, NULL) == -1) {
        die("sigaction");
    }
}

void signals_setup(void)
{
    set_stop_action(on_stop);
}

pid_t signals_fork(void)
{
    sigset_t stop_set;
    sigset_t previous;
    sigemptyset(&stop_set);
    sigaddset(&stop_set, SIGINT);
    sigaddset(&stop_set, SIGTERM);
    sigaddset(&stop_set, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &stop_set, &previous) == -1) {
        die("sigprocmask");
    }
    pid_t pid = fork();
    int fork_errno = errno;
    /* Dziecko dziedziczy funkcję obsługi rodzica. Sygnał przed exec ustawiłby tylko flagę, którą
     * exec kasuje, i proces nigdy by się nie zakończył. Działanie domyślne po prostu go kończy,
     * także gdy sygnał czekał na odblokowanie. */
    if (pid == 0) {
        set_stop_action(SIG_DFL);
        /* Gdy rodzic zginie bez sprzątania (kill -9), dziecko dostanie SIGTERM i zakończy się
         * normalnie, zamiast działać dalej i np. trzymać port UDP bramy. Działa też po exec. */
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
            die("prctl");
        }
    }
    if (sigprocmask(SIG_SETMASK, &previous, NULL) == -1) {
        die("sigprocmask");
    }
    errno = fork_errno;
    return pid;
}

int signals_stop_requested(void)
{
    return stop_signal != 0;
}
