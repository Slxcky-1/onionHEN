/* onion_fwpin - one-shot firmware pin payload.
 *
 * Runs once at OnionHEN boot (after the daemon starts).
 * Floor mode (always): deletes any staged PS5UPDATE.PUP from /update/ so
 *   Sony cannot force-install a newer firmware that breaks the jailbreak.
 * Full mode (when /dev/nfs/etc/hosts exists): pins /etc/hosts so
 *   update.playstation.com and dl.playstation.com resolve to 127.0.0.1.
 *   Backs up the original hosts file to HOSTS_BAK first.
 *
 * Kill switch: create empty file /data/OnionHEN/fwpin.disabled to skip all.
 * Log: /data/OnionHEN/fwpin.log
 */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define SELF_NAME   "onion_fwpin.elf"
#define LOG_PATH    "/data/OnionHEN/fwpin.log"
#define KILL_SWITCH "/data/OnionHEN/fwpin.disabled"
#define PUP_PATH    "/update/PS5UPDATE.PUP"
#define HOSTS_NFS   "/dev/nfs/etc/hosts"
#define HOSTS_BAK   "/data/OnionHEN/hosts.fwpin.bak"

static const char *const kUpdateHosts[] = {
    "update.playstation.com",
    "dl.playstation.com",
};
#define N_UPDATE_HOSTS 2

static FILE *g_log;

static void say(const char *fmt, ...) {
    if (!g_log) return;
    struct timespec rt;
    clock_gettime(CLOCK_REALTIME, &rt);
    struct tm tm;
    time_t t = rt.tv_sec;
    gmtime_r(&t, &tm);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", tm.tm_hour, tm.tm_min, tm.tm_sec,
            (int)(rt.tv_nsec / 1000000));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

/* Returns 1 if host is already present in the hosts file content, 0 otherwise. */
static int hosts_has(const char *content, const char *host) {
    const char *p = content;
    while ((p = strstr(p, host)) != NULL) {
        /* Make sure it is not in a comment and looks like a real entry */
        const char *line = p;
        while (line > content && line[-1] != '\n') line--;
        if (*line != '#') return 1;
        p++;
    }
    return 0;
}

static void pin_hosts(void) {
    /* Read current hosts */
    FILE *f = fopen(HOSTS_NFS, "r");
    if (!f) {
        say("full mode: cannot open %s: %s", HOSTS_NFS, strerror(errno));
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 65536) {
        say("full mode: hosts file too large or unreadable (%ld bytes)", sz);
        fclose(f);
        return;
    }
    char *buf = malloc((size_t)sz + 512);
    if (!buf) { fclose(f); say("full mode: OOM"); return; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';

    /* Check if already pinned */
    int already = 1;
    for (int i = 0; i < N_UPDATE_HOSTS; i++)
        if (!hosts_has(buf, kUpdateHosts[i])) { already = 0; break; }
    if (already) {
        say("full mode: hosts already pinned");
        free(buf);
        return;
    }

    /* Backup original */
    if (access(HOSTS_BAK, F_OK) != 0) {
        FILE *bak = fopen(HOSTS_BAK, "w");
        if (bak) { fwrite(buf, 1, rd, bak); fclose(bak); say("full mode: backup saved to %s", HOSTS_BAK); }
        else say("full mode: warning: could not write backup: %s", strerror(errno));
    }

    /* Append pin entries */
    FILE *w = fopen(HOSTS_NFS, "a");
    if (!w) {
        say("full mode: cannot open %s for writing: %s", HOSTS_NFS, strerror(errno));
        free(buf);
        return;
    }
    fprintf(w, "\n# onion_fwpin: block update servers\n");
    for (int i = 0; i < N_UPDATE_HOSTS; i++)
        fprintf(w, "127.0.0.1 %s\n", kUpdateHosts[i]);
    fclose(w);
    say("full mode: pinned %d host(s) in %s", N_UPDATE_HOSTS, HOSTS_NFS);
    free(buf);
}

int main(void) {
    (void)syscall(SYS_thr_set_name, -1, SELF_NAME);

    g_log = fopen(LOG_PATH, "a");
    say("onion_fwpin started");

    if (access(KILL_SWITCH, F_OK) == 0) {
        say("kill switch active (%s) — exiting", KILL_SWITCH);
        fclose(g_log);
        return 0;
    }

    /* Floor mode: delete any staged update */
    if (access(PUP_PATH, F_OK) == 0) {
        if (unlink(PUP_PATH) == 0)
            say("floor: deleted staged update %s", PUP_PATH);
        else
            say("floor: failed to delete %s: %s", PUP_PATH, strerror(errno));
    } else {
        say("floor: no staged update found");
    }

    /* Full mode: pin hosts if /dev/nfs is available */
    if (access(HOSTS_NFS, F_OK) == 0)
        pin_hosts();
    else
        say("full mode: /dev/nfs not available — skipped (floor mode only)");

    say("done");
    if (g_log) fclose(g_log);
    return 0;
}
