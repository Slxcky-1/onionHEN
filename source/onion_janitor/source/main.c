/* onion_janitor - resident payload.
 *
 * After an emulator closes (or is switched away from), the system and ShadowMount+ both leave a mount behind
 * on the same "data" folder inside the emulator's sandbox. Two such leftovers at the same time make the next
 * launch fail with "Device busy" ("can't start the game or app"). This removes the leftovers of every
 * closed emulator about SLOT_GRACE_MS after it exits, so that at most one set is ever present at a launch.
 * Once the sandbox is gone ShadowMount+ releases its own /system_ex/app link by itself.
 *
 * Safety rules:
 *  - only title IDs found in HOMEBREW_DIR at startup are ever touched;
 *  - a sandbox slot is never touched if it has an app0 mount, if its app is running and it is that app's
 *    newest slot;
 *  - a leftover must stay stale for SLOT_GRACE_MS before it is removed;
 *  - an app link is only removed as a fallback, LINK_GRACE_MS after we saw the app running and it closed;
 *  - if the mount table or process table cannot be read, nothing is done.
 * Log: /data/OnionHEN/janitor.log */
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/user.h>
#include <time.h>
#include <unistd.h>

typedef struct app_info {
  uint32_t app_id;
  uint64_t unknown1;
  uint32_t app_type;
  char title_id[10];
  char unknown2[0x3c];
} app_info_t;
int sceKernelGetAppInfo(int pid, app_info_t *info);
int sceUserServiceInitialize(const void *params);

#define LOG_PATH "/data/OnionHEN/janitor.log"
#define HOMEBREW_DIR "/data/homebrew"
#define SELF_NAME "onion_janitor.elf"
#define POLL_US 250000
#define SLOT_GRACE_MS 1500
#define LINK_GRACE_MS 15000
#define RETRY_MS 3000
#define MAX_SLOTS 64
#define MAX_TITLES 32
#define SANDBOX "/mnt/sandbox/"

static char g_titles[MAX_TITLES][10];
static int g_nt = 0;

typedef struct {
  char name[40];
  int title;
  int has_app0;
  int newest;
} slot_t;

typedef struct {
  char name[40];
  int64_t stale_since;
  int64_t last_try;
} track_t;

static FILE *g_log;
static track_t g_track[MAX_SLOTS];
static int g_seen_running[MAX_TITLES];
static int g_was_running[MAX_TITLES];
static int64_t g_link_idle_since[MAX_TITLES];
static struct statfs *g_mbuf;
static size_t g_mcap;
static char *g_pbuf;
static size_t g_pcap;

static int64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void say(const char *fmt, ...) {
  if (!g_log)
    return;
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

static int is_title_id(const char *name) {
  size_t l = strlen(name);
  if (l != 9) return 0;
  if (strncmp(name, "PPSA", 4) != 0 && strncmp(name, "CUSA", 4) != 0) return 0;
  for (int i = 4; i < 9; i++)
    if (name[i] < '0' || name[i] > '9') return 0;
  return 1;
}

/* Scan HOMEBREW_DIR for PPSA/CUSA title directories at startup. */
static void scan_titles(void) {
  DIR *d = opendir(HOMEBREW_DIR);
  if (!d) return;
  struct dirent *e;
  while ((e = readdir(d)) != NULL && g_nt < MAX_TITLES) {
    if (e->d_type != DT_DIR && e->d_type != DT_UNKNOWN) continue;
    if (!is_title_id(e->d_name)) continue;
    int dup = 0;
    for (int i = 0; i < g_nt; i++)
      if (strcmp(g_titles[i], e->d_name) == 0) { dup = 1; break; }
    if (!dup) {
      memcpy(g_titles[g_nt], e->d_name, 10);
      g_nt++;
    }
  }
  closedir(d);
}

/* The returned table is a shared buffer: valid until the next get_mounts() call, never free it. */
static int get_mounts(struct statfs **out) {
  int n = getfsstat(NULL, 0, MNT_NOWAIT);
  if (n <= 0)
    return -1;
  size_t need = (size_t)n + 32;
  if (need > g_mcap) {
    free(g_mbuf);
    g_mbuf = calloc(need, sizeof(*g_mbuf));
    g_mcap = g_mbuf ? need : 0;
    if (!g_mbuf)
      return -1;
  }
  n = getfsstat(g_mbuf, (long)(g_mcap * sizeof(*g_mbuf)), MNT_NOWAIT);
  if (n <= 0)
    return -1;
  *out = g_mbuf;
  return n;
}

static int title_of_slot(const char *slot) {
  for (int i = 0; i < g_nt; i++) {
    size_t l = strlen(g_titles[i]);
    if (strncmp(slot, g_titles[i], l) == 0 && slot[l] == '_')
      return i;
  }
  return -1;
}

/* running[t] = 1 if a process of title t exists; returns -1 if the process table cannot be read */
static int get_running(int running[MAX_TITLES], int *other_janitor) {
  memset(running, 0, sizeof(int) * MAX_TITLES);
  if (other_janitor)
    *other_janitor = 0;
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  size_t sz = 0;
  if (sysctl(mib, 4, NULL, &sz, NULL, 0) != 0 || sz == 0)
    return -1;
  sz += sz / 8 + 4096;
  if (sz > g_pcap) {
    free(g_pbuf);
    g_pbuf = malloc(sz);
    g_pcap = g_pbuf ? sz : 0;
    if (!g_pbuf)
      return -1;
  }
  sz = g_pcap;
  if (sysctl(mib, 4, g_pbuf, &sz, NULL, 0) != 0)
    return -1;
  pid_t self = getpid();
  for (char *p = g_pbuf; p < g_pbuf + sz;) {
    struct kinfo_proc *ki = (struct kinfo_proc *)p;
    if (ki->ki_structsize <= 0)
      break;
    p += ki->ki_structsize;
    if (other_janitor && ki->ki_pid != self &&
        strcmp(ki->ki_comm, SELF_NAME) == 0)
      *other_janitor = 1;
    app_info_t ai;
    memset(&ai, 0, sizeof(ai));
    if (sceKernelGetAppInfo(ki->ki_pid, &ai) < 0)
      continue;
    char tid[11];
    memcpy(tid, ai.title_id, 10);
    tid[10] = '\0';
    for (int t = 0; t < g_nt; t++)
      if (strcmp(tid, g_titles[t]) == 0)
        running[t] = 1;
  }
  return 0;
}

/* rmdir path, then its parents up to and including the sandbox slot folder */
static void remove_up(const char *path) {
  char tmp[512];
  snprintf(tmp, sizeof(tmp), "%s", path);
  const size_t floor = strlen(SANDBOX) + 1;
  while (strlen(tmp) > floor) {
    if (rmdir(tmp) != 0) {
      if (errno != ENOENT)
        say("  could not remove folder %s: %s", tmp, strerror(errno));
      break;
    }
    char *s = strrchr(tmp, '/');
    if (!s)
      break;
    *s = '\0';
  }
}

static void cleanup_slot(const char *slot) {
  char prefix[96];
  snprintf(prefix, sizeof(prefix), SANDBOX "%s/", slot);
  const size_t pl = strlen(prefix);
  static char done[32][512];
  int nd = 0;

  say("cleaning stale sandbox %s", slot);
  for (int pass = 0; pass < 8; pass++) {
    struct statfs *fs = NULL;
    int n = get_mounts(&fs);
    if (n <= 0)
      break;
    int acted = 0;
    for (int i = n - 1; i >= 0; i--) {
      if (strcmp(fs[i].f_fstypename, "nullfs") != 0 ||
          strncmp(fs[i].f_mntonname, prefix, pl) != 0)
        continue;
      if (strncmp(fs[i].f_mntonname + pl, "app0", 4) == 0)
        continue;
      if (unmount(fs[i].f_mntonname, MNT_FORCE) == 0) {
        say("  unmounted %s (was from %s)", fs[i].f_mntonname,
            fs[i].f_mntfromname);
        if (nd < 32)
          snprintf(done[nd++], sizeof(done[0]), "%s", fs[i].f_mntonname);
        acted++;
      } else {
        say("  could not unmount %s: %s", fs[i].f_mntonname, strerror(errno));
      }
    }
    if (!acted)
      break;
  }
  for (int i = nd - 1; i >= 0; i--)
    remove_up(done[i]);
  char base[96];
  snprintf(base, sizeof(base), SANDBOX "%s", slot);
  struct stat st;
  if (stat(base, &st) != 0)
    say("  sandbox folder %s is gone", base);
  else
    say("  sandbox folder %s is still there", base);
}

static track_t *track_find(const char *name, int create) {
  track_t *freeslot = NULL;
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (g_track[i].name[0] && strcmp(g_track[i].name, name) == 0)
      return &g_track[i];
    if (!g_track[i].name[0] && !freeslot)
      freeslot = &g_track[i];
  }
  if (create && freeslot) {
    memset(freeslot, 0, sizeof(*freeslot));
    snprintf(freeslot->name, sizeof(freeslot->name), "%s", name);
    return freeslot;
  }
  return NULL;
}

static void poll_once(void) {
  scan_titles();
  int running[MAX_TITLES];
  if (get_running(running, NULL) != 0)
    return;
  struct statfs *fs = NULL;
  int n = get_mounts(&fs);
  if (n <= 0)
    return;

  slot_t slots[MAX_SLOTS];
  int ns = 0;
  int link_mounted[MAX_TITLES] = {0};
  int title_newest[MAX_TITLES];
  for (int t = 0; t < g_nt; t++)
    title_newest[t] = -1;

  for (int i = 0; i < n; i++) {
    if (strcmp(fs[i].f_fstypename, "nullfs") != 0)
      continue;
    const char *on = fs[i].f_mntonname;
    if (strncmp(on, SANDBOX, strlen(SANDBOX)) == 0) {
      const char *p = on + strlen(SANDBOX);
      size_t l = strcspn(p, "/");
      char name[40];
      if (l == 0 || l >= sizeof(name))
        continue;
      memcpy(name, p, l);
      name[l] = '\0';
      int t = title_of_slot(name);
      if (t < 0)
        continue;
      slot_t *s = NULL;
      for (int k = 0; k < ns; k++)
        if (strcmp(slots[k].name, name) == 0)
          s = &slots[k];
      if (!s) {
        if (ns >= MAX_SLOTS)
          continue;
        s = &slots[ns++];
        memset(s, 0, sizeof(*s));
        snprintf(s->name, sizeof(s->name), "%s", name);
        s->title = t;
      }
      s->newest = i;
      if (strcmp(p + l, "/app0") == 0)
        s->has_app0 = 1;
      if (i > title_newest[t])
        title_newest[t] = i;
    } else if (strncmp(on, "/system_ex/app/", 15) == 0) {
      for (int t = 0; t < g_nt; t++)
        if (strcmp(on + 15, g_titles[t]) == 0)
          link_mounted[t] = 1;
    }
  }

  for (int t = 0; t < g_nt; t++) {
    if (running[t] && !g_was_running[t])
      say("%s started", g_titles[t]);
    if (!running[t] && g_was_running[t])
      say("%s exited", g_titles[t]);
    if (running[t])
      g_seen_running[t] = 1;
    g_was_running[t] = running[t];
  }

  int live_slot[MAX_TITLES] = {0};
  int present[MAX_SLOTS] = {0};
  int64_t now = now_ms();
  for (int k = 0; k < ns; k++) {
    slot_t *s = &slots[k];
    int t = s->title;
    int live = s->has_app0 || (running[t] && s->newest == title_newest[t]);
    track_t *tr = track_find(s->name, 1);
    if (!tr)
      continue;
    present[tr - g_track] = 1;
    if (live) {
      live_slot[t] = 1;
      tr->stale_since = 0;
      continue;
    }
    if (tr->stale_since == 0) {
      tr->stale_since = now;
    } else if (now - tr->stale_since >= SLOT_GRACE_MS &&
               now - tr->last_try >= RETRY_MS) {
      tr->last_try = now;
      cleanup_slot(s->name);
    }
  }
  for (int i = 0; i < MAX_SLOTS; i++)
    if (g_track[i].name[0] && !present[i])
      memset(&g_track[i], 0, sizeof(g_track[i]));

  for (int t = 0; t < g_nt; t++) {
    if (!link_mounted[t]) {
      g_seen_running[t] = running[t] ? 1 : 0;
      g_link_idle_since[t] = 0;
      continue;
    }
    if (running[t] || live_slot[t] || !g_seen_running[t]) {
      g_link_idle_since[t] = 0;
      continue;
    }
    if (g_link_idle_since[t] == 0) {
      g_link_idle_since[t] = now;
    } else if (now - g_link_idle_since[t] >= LINK_GRACE_MS) {
      char link[64];
      snprintf(link, sizeof(link), "/system_ex/app/%s", g_titles[t]);
      if (unmount(link, MNT_FORCE) == 0)
        say("unmounted app link %s that stayed behind", link);
      else
        say("could not unmount %s: %s", link, strerror(errno));
      g_link_idle_since[t] = 0;
      g_seen_running[t] = 0;
    }
  }
}

int main(void) {
  (void)syscall(SYS_thr_set_name, -1, SELF_NAME);
  sceUserServiceInitialize(NULL);
  int running[MAX_TITLES];
  int other = 0;
  if (get_running(running, &other) == 0 && other)
    return 0;
  g_log = fopen(LOG_PATH, "w");
  scan_titles();
  say("started: watching %d emulator titles (auto-detected from %s), stale grace %d ms, poll %d ms",
      g_nt, HOMEBREW_DIR, SLOT_GRACE_MS, POLL_US / 1000);
  for (int t = 0; t < g_nt; t++)
    say("  watching: %s", g_titles[t]);
  for (;;) {
    poll_once();
    usleep(POLL_US);
  }
  return 0;
}
