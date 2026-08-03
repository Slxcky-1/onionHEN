#include <onion/activation.h>

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <onion/log.h>
#include <onion/notify.h>

#include "crypto_util.h"
#include "device.h"
#include "license.h"
#include "sha256.h"

#ifndef ONION_ACTIVATION_TCP_PORT
#define ONION_ACTIVATION_TCP_PORT 9099
#endif

/* Bound device_id namespace so kylin-core licenses are not interchangeable. */
static const char kDeviceIdNamespace[] = "onionhen-activation-v1";

static void
set_err(char *err, size_t err_size, const char *msg) {
  if(err != NULL && err_size > 0) {
    snprintf(err, err_size, "%s", msg != NULL ? msg : "");
  }
}

int
onion_activation_get_device_id(char *out, size_t out_size) {
  char serial[256];
  char hex[65];
  SHA256_CTX ctx;
  unsigned char digest[32];

  if(out == NULL || out_size < ONION_ACTIVATION_DEVICE_ID_LENGTH) {
    return -1;
  }
  if(onion_activation_device_serial(serial, sizeof(serial)) < 0) {
    return -1;
  }
  sha256_init(&ctx);
  sha256_update(&ctx, (const unsigned char *)kDeviceIdNamespace,
                strlen(kDeviceIdNamespace));
  sha256_update(&ctx, (const unsigned char *)serial, strlen(serial));
  sha256_final(&ctx, digest);
  if(onion_act_hex_encode(digest, 32, hex, sizeof(hex)) < 0) {
    return -1;
  }
  if(snprintf(out, out_size, "sha256:%s", hex) >= (int)out_size) {
    return -1;
  }
  return 0;
}

static int
read_file_alloc(const char *path, char **out_body, size_t max_size) {
  FILE *fp;
  long size;
  char *body;

  if(path == NULL || out_body == NULL) {
    return -1;
  }
  *out_body = NULL;
  fp = fopen(path, "rb");
  if(fp == NULL) {
    return -1;
  }
  if(fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return -1;
  }
  size = ftell(fp);
  if(size < 0 || (size_t)size > max_size) {
    fclose(fp);
    return -1;
  }
  if(fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return -1;
  }
  body = (char *)malloc((size_t)size + 1);
  if(body == NULL) {
    fclose(fp);
    return -1;
  }
  if(fread(body, 1, (size_t)size, fp) != (size_t)size) {
    free(body);
    fclose(fp);
    return -1;
  }
  body[size] = '\0';
  fclose(fp);
  *out_body = body;
  return 0;
}

static int
ensure_activation_dir(void) {
  struct stat st;

  if(stat(ONION_ACTIVATION_DIR, &st) == 0) {
    return S_ISDIR(st.st_mode) ? 0 : -1;
  }
  if(mkdir("/data", 0777) != 0 && errno != EEXIST) {
    /* best-effort */
  }
  if(mkdir("/data/OnionHEN", 0777) != 0 && errno != EEXIST) {
    /* best-effort */
  }
  if(mkdir(ONION_ACTIVATION_DIR, 0777) != 0 && errno != EEXIST) {
    return -1;
  }
  return 0;
}

static int
load_license(onion_license_t *license) {
  char *body = NULL;
  int ret;

  if(read_file_alloc(ONION_ACTIVATION_LICENSE_PATH, &body, 128 * 1024) < 0) {
    return -1;
  }
  ret = onion_license_parse(body, license);
  free(body);
  return ret;
}

static int
validate_license(const onion_license_t *license, char *reason,
                 size_t reason_size) {
  char device_id[ONION_ACTIVATION_DEVICE_ID_LENGTH];
  long long now = (long long)time(NULL);

  if(license == NULL) {
    snprintf(reason, reason_size, "license missing");
    return -1;
  }
  if(license->version != 1) {
    snprintf(reason, reason_size, "unsupported license version");
    return -1;
  }
  if(onion_license_verify_signature(license) < 0) {
    snprintf(reason, reason_size, "license signature invalid");
    return -1;
  }
  if(onion_activation_get_device_id(device_id, sizeof(device_id)) < 0 ||
     strcmp(device_id, license->device_id) != 0) {
    snprintf(reason, reason_size, "license is not for this device");
    return -1;
  }
  if(license->expires_at > 0 && now > license->expires_at) {
    snprintf(reason, reason_size, "license expired");
    return -1;
  }
  snprintf(reason, reason_size, "OK");
  return 0;
}

static int
write_license_body(const char *body, char *err, size_t err_size) {
  FILE *fp;

  if(ensure_activation_dir() < 0) {
    set_err(err, err_size, "unable to create activation directory");
    return -1;
  }
  fp = fopen(ONION_ACTIVATION_LICENSE_PATH, "wb");
  if(fp == NULL) {
    set_err(err, err_size, "unable to write license");
    return -1;
  }
  if(fwrite(body, 1, strlen(body), fp) != strlen(body)) {
    fclose(fp);
    set_err(err, err_size, "unable to write license");
    return -1;
  }
  fclose(fp);
  return 0;
}

int
onion_activation_get_status(onion_activation_status_t *status) {
  onion_license_t license;
  char reason[160] = "license missing";

  if(status == NULL) {
    return -1;
  }
  memset(status, 0, sizeof(*status));
  if(onion_activation_get_device_id(status->device_id,
                                    sizeof(status->device_id)) == 0) {
    snprintf(status->device_code, sizeof(status->device_code), "%.32s",
             status->device_id + 7);
  }
  if(load_license(&license) == 0 &&
     validate_license(&license, reason, sizeof(reason)) == 0) {
    status->activated = 1;
    snprintf(status->license_id, sizeof(status->license_id), "%s",
             license.license_id);
    snprintf(status->subject, sizeof(status->subject), "%s", license.subject);
    status->expires_at = license.expires_at;
    snprintf(status->reason, sizeof(status->reason), "OK");
    return 0;
  }
  status->activated = 0;
  snprintf(status->reason, sizeof(status->reason), "%s", reason);
  return -1;
}

int
onion_activation_is_active(void) {
  onion_activation_status_t status;
  return onion_activation_get_status(&status) == 0 ? 1 : 0;
}

int
onion_activation_install_license_json(const char *json, char *err,
                                      size_t err_size) {
  onion_license_t license;
  char reason[160] = "";

  if(json == NULL) {
    set_err(err, err_size, "invalid license");
    return -1;
  }
  if(onion_license_parse(json, &license) < 0 ||
     validate_license(&license, reason, sizeof(reason)) < 0) {
    set_err(err, err_size, reason[0] != '\0' ? reason : "invalid license");
    return -1;
  }
  return write_license_body(json, err, err_size);
}

static int
try_poll_drop_paths(void) {
  static const char *const kPaths[] = {
      ONION_ACTIVATION_LICENSE_PATH,
      "/mnt/usb0/onion_license.json",
      "/mnt/usb1/onion_license.json",
      "/data/OnionHEN/onion_license.json",
      NULL,
  };
  char err[160];

  for(size_t i = 0; kPaths[i] != NULL; ++i) {
    char *body = NULL;
    if(read_file_alloc(kPaths[i], &body, 128 * 1024) < 0) {
      continue;
    }
    if(onion_activation_install_license_json(body, err, sizeof(err)) == 0) {
      free(body);
      LOG_INFO("[activation] installed license from %s", kPaths[i]);
      return 0;
    }
    free(body);
  }
  return -1;
}

static int
open_activation_tcp(void) {
  int fd;
  int yes = 1;
  struct sockaddr_in addr;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) {
    return -1;
  }
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(ONION_ACTIVATION_TCP_PORT);
  if(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    close(fd);
    return -1;
  }
  if(listen(fd, 2) != 0) {
    close(fd);
    return -1;
  }
  fcntl(fd, F_SETFL, O_NONBLOCK);
  return fd;
}

static void
trim_inplace(char *s) {
  size_t len;
  size_t start = 0;

  if(s == NULL) {
    return;
  }
  len = strlen(s);
  while(len > 0 &&
        (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' ||
         s[len - 1] == '\t')) {
    s[--len] = '\0';
  }
  while(s[start] == ' ' || s[start] == '\t') {
    start++;
  }
  if(start > 0) {
    memmove(s, s + start, strlen(s + start) + 1);
  }
}

static void
reply_fd(int cfd, const char *fmt, ...) {
  char out[1024];
  va_list ap;
  int n;

  va_start(ap, fmt);
  n = vsnprintf(out, sizeof(out), fmt, ap);
  va_end(ap);
  if(n > 0) {
    size_t len = (size_t)n < sizeof(out) ? (size_t)n : sizeof(out) - 1;
    (void)write(cfd, out, len);
  }
}

static void
handle_activation_client(int cfd) {
  char buf[8192];
  char err[160];
  ssize_t n;
  char *line;
  char *body;

  n = recv(cfd, buf, sizeof(buf) - 1, 0);
  if(n <= 0) {
    close(cfd);
    return;
  }
  buf[n] = '\0';
  line = buf;
  trim_inplace(line);

  if(strncasecmp(line, "STATUS", 6) == 0) {
    onion_activation_status_t st;
    onion_activation_get_status(&st);
    reply_fd(cfd, "activated=%d device_id=%s device_code=%s reason=%s\n",
             st.activated, st.device_id, st.device_code, st.reason);
  } else if(strncasecmp(line, "LICENSE ", 8) == 0) {
    if(onion_activation_install_license_json(line + 8, err, sizeof(err)) == 0) {
      reply_fd(cfd, "OK activated\n");
    } else {
      reply_fd(cfd, "ERR %s\n", err);
    }
  } else if(strncasecmp(line, "LICENSE", 7) == 0) {
    body = strchr(buf, '\n');
    if(body != NULL) {
      body++;
      if(onion_activation_install_license_json(body, err, sizeof(err)) == 0) {
        reply_fd(cfd, "OK activated\n");
      } else {
        reply_fd(cfd, "ERR %s\n", err);
      }
    } else {
      reply_fd(cfd, "ERR usage: LICENSE <json> | STATUS\n");
    }
  } else {
    reply_fd(cfd, "ERR usage: LICENSE <json> | STATUS\n");
  }
  close(cfd);
}

int
onion_activation_gate(void) {
  onion_activation_status_t status;
  int listen_fd = -1;
  int notify_tick = 0;

  ensure_activation_dir();

  if(onion_activation_get_status(&status) == 0) {
    LOG_INFO("[activation] already active license_id=%s subject=%s",
             status.license_id, status.subject);
    return 0;
  }

  if(status.device_id[0] == '\0') {
    LOG_ERROR("[activation] cannot read device identity");
    onion_notify_debug("Beta activation failed: cannot read device identity");
    return -1;
  }

  LOG_INFO("[activation] not activated device_id=%s port=%d", status.device_id,
           ONION_ACTIVATION_TCP_PORT);
  onion_notify_debug(
      "Beta certificate required\nDevice code: %.12s...\n"
      "Place license.json under %s\nOr TCP :%d LICENSE <json>",
      status.device_code, ONION_ACTIVATION_DIR, ONION_ACTIVATION_TCP_PORT);

  listen_fd = open_activation_tcp();
  if(listen_fd < 0) {
    LOG_WARN("[activation] TCP :%d unavailable; file drop only",
             ONION_ACTIVATION_TCP_PORT);
  } else {
    LOG_INFO("[activation] listening on 0.0.0.0:%d", ONION_ACTIVATION_TCP_PORT);
  }

  for(;;) {
    if(try_poll_drop_paths() == 0 || onion_activation_is_active()) {
      if(listen_fd >= 0) {
        close(listen_fd);
      }
      onion_notify_debug("Beta activation successful\nWelcome to OnionHEN");
      LOG_INFO("[activation] gate passed");
      return 0;
    }

    if(listen_fd >= 0) {
      for(;;) {
        int cfd = accept(listen_fd, NULL, NULL);
        if(cfd < 0) {
          break;
        }
        handle_activation_client(cfd);
        if(onion_activation_is_active()) {
          close(listen_fd);
          onion_notify_debug("Beta activation successful\nWelcome to OnionHEN");
          LOG_INFO("[activation] gate passed via TCP");
          return 0;
        }
      }
    }

    if((notify_tick++ % 30) == 0) {
      onion_notify_debug(
          "Waiting for beta certificate\nDevice code: %.12s...\nTCP :%d",
          status.device_code, ONION_ACTIVATION_TCP_PORT);
    }
    sleep(2);
  }
}
