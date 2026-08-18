#include "cheats/sync/http_transport_ps5.hpp"

#include <onion/log.h>

#include <cstdlib>
#include <cstring>
#include <string>

#if !defined(ONION_HOST_TEST)
#include <dlfcn.h>
#endif

namespace onion::cheats::sync {
namespace {

bool host_allowed(const char *url, const char *allow) {
  if (!url) {
    return false;
  }
  if (std::strncmp(url, "https://", 8) != 0) {
    return false;
  }
  if (!allow || !allow[0]) {
    return true;
  }
  const char *host = url + 8;
  const char *slash = std::strchr(host, '/');
  const size_t host_len = slash ? static_cast<size_t>(slash - host) : std::strlen(host);
  const size_t allow_len = std::strlen(allow);
  if (host_len == allow_len && std::strncmp(host, allow, allow_len) == 0) {
    return true;
  }
  /* Allow "www." prefix mismatch only if the allow host is an exact suffix. */
  if (host_len > allow_len + 1 && host[host_len - allow_len - 1] == '.' &&
      std::strncmp(host + host_len - allow_len, allow, allow_len) == 0) {
    return true;
  }
  return false;
}

#if !defined(ONION_HOST_TEST)
typedef int (*http_init_fn)(int, int);
typedef int (*http_term_fn)(void);
typedef int (*http_create_fn)(int, int, int *);
typedef int (*http_delete_fn)(int);
typedef int (*http_create_conn_fn)(int, const char *, const char *, unsigned short,
                                   int, int *);
typedef int (*http_delete_conn_fn)(int);
typedef int (*http_create_req_fn)(int, int, const char *, const char *, int *);
typedef int (*http_delete_req_fn)(int);
typedef int (*http_add_header_fn)(int, const char *, const char *);
typedef int (*http_send_request_fn)(int, const void *, unsigned int);
typedef int (*http_get_status_fn)(int, int *);
typedef int (*http_read_data_fn)(int, void *, unsigned int, unsigned int *);
typedef int (*http_set_timeout_fn)(int, unsigned int);

struct SceHttpApi {
  bool resolved = false;
  http_init_fn init = nullptr;
  http_term_fn term = nullptr;
  http_create_fn create = nullptr;
  http_delete_fn destroy = nullptr;
  http_create_conn_fn create_conn = nullptr;
  http_delete_conn_fn delete_conn = nullptr;
  http_create_req_fn create_req = nullptr;
  http_delete_req_fn delete_req = nullptr;
  http_add_header_fn add_header = nullptr;
  http_send_request_fn send_request = nullptr;
  http_get_status_fn get_status = nullptr;
  http_read_data_fn read_data = nullptr;
  http_set_timeout_fn set_connect_timeout = nullptr;
  http_set_timeout_fn set_send_timeout = nullptr;
  http_set_timeout_fn set_recv_timeout = nullptr;
};

SceHttpApi &sce_http() {
  static SceHttpApi api;
  if (api.resolved) {
    return api;
  }
  api.resolved = true;
  void *mod = dlopen("libSceHttp.sprx", RTLD_NOW);
  if (!mod) {
    LOG_ERROR("Ps5HttpTransport: dlopen libSceHttp.sprx failed");
    return api;
  }
  api.init = reinterpret_cast<http_init_fn>(dlsym(mod, "sceHttpInit"));
  api.term = reinterpret_cast<http_term_fn>(dlsym(mod, "sceHttpTerm"));
  api.create = reinterpret_cast<http_create_fn>(dlsym(mod, "sceHttpCreateTemplate"));
  api.destroy = reinterpret_cast<http_delete_fn>(dlsym(mod, "sceHttpDeleteTemplate"));
  api.create_conn =
      reinterpret_cast<http_create_conn_fn>(dlsym(mod, "sceHttpCreateConnectionWithURL"));
  api.delete_conn =
      reinterpret_cast<http_delete_conn_fn>(dlsym(mod, "sceHttpDeleteConnection"));
  api.create_req =
      reinterpret_cast<http_create_req_fn>(dlsym(mod, "sceHttpCreateRequestWithURL"));
  api.delete_req =
      reinterpret_cast<http_delete_req_fn>(dlsym(mod, "sceHttpDeleteRequest"));
  api.add_header =
      reinterpret_cast<http_add_header_fn>(dlsym(mod, "sceHttpAddRequestHeader"));
  api.send_request =
      reinterpret_cast<http_send_request_fn>(dlsym(mod, "sceHttpSendRequest"));
  api.get_status =
      reinterpret_cast<http_get_status_fn>(dlsym(mod, "sceHttpGetStatusCode"));
  api.read_data = reinterpret_cast<http_read_data_fn>(dlsym(mod, "sceHttpReadData"));
  api.set_connect_timeout =
      reinterpret_cast<http_set_timeout_fn>(dlsym(mod, "sceHttpSetConnectTimeOut"));
  api.set_send_timeout =
      reinterpret_cast<http_set_timeout_fn>(dlsym(mod, "sceHttpSetSendTimeOut"));
  api.set_recv_timeout =
      reinterpret_cast<http_set_timeout_fn>(dlsym(mod, "sceHttpSetRecvTimeOut"));
  return api;
}

bool parse_url_host(const char *url, std::string &host, std::string &path,
                    unsigned short &port) {
  host.clear();
  path = "/";
  port = 443;
  if (!url || std::strncmp(url, "https://", 8) != 0) {
    return false;
  }
  const char *p = url + 8;
  const char *slash = std::strchr(p, '/');
  const char *colon = std::strchr(p, ':');
  if (colon && (!slash || colon < slash)) {
    host.assign(p, colon);
    port = static_cast<unsigned short>(std::atoi(colon + 1));
  } else if (slash) {
    host.assign(p, slash);
  } else {
    host = p;
  }
  if (slash) {
    path = slash;
  }
  return !host.empty();
}
#endif

} // namespace

GitStatus Ps5HttpTransport::perform(
    const HttpRequest &req,
    const std::function<GitStatus(const void *, size_t)> &on_data) {
  if (!host_allowed(req.url, req.host_allow)) {
    LOG_ERROR("Ps5HttpTransport: url rejected");
    return GitStatus::Rejected;
  }
#if defined(ONION_HOST_TEST)
  (void)on_data;
  return GitStatus::Unavailable;
#else
  SceHttpApi &api = sce_http();
  if (!api.init || !api.create || !api.create_conn || !api.create_req ||
      !api.send_request || !api.read_data) {
    LOG_ERROR("Ps5HttpTransport: SceHttp symbols missing");
    return GitStatus::Unavailable;
  }

  std::string host;
  std::string path;
  unsigned short port = 443;
  if (!parse_url_host(req.url, host, path, port)) {
    return GitStatus::Rejected;
  }

  if (api.init(0, 0) < 0) {
    /* Already initialized is fine on some firmwares; continue. */
  }
  int tmpl = 0;
  if (api.create(2, 0, &tmpl) < 0) {
    return GitStatus::Network;
  }
  int conn = 0;
  if (api.create_conn(tmpl, req.url, host.c_str(), port, 1, &conn) < 0) {
    if (api.destroy) {
      api.destroy(tmpl);
    }
    return GitStatus::Network;
  }
  const int method = (req.method && std::strcmp(req.method, "POST") == 0) ? 1 : 0;
  int request = 0;
  if (api.create_req(conn, method, req.url, path.c_str(), &request) < 0) {
    if (api.delete_conn) {
      api.delete_conn(conn);
    }
    if (api.destroy) {
      api.destroy(tmpl);
    }
    return GitStatus::Network;
  }
  if (req.timeout_ms > 0) {
    const unsigned int usec =
        static_cast<unsigned int>(req.timeout_ms) * 1000u;
    if (api.set_connect_timeout) {
      (void)api.set_connect_timeout(request, usec);
    }
    if (api.set_send_timeout) {
      (void)api.set_send_timeout(request, usec);
    }
    if (api.set_recv_timeout) {
      (void)api.set_recv_timeout(request, usec);
    }
  }
  if (req.content_type && api.add_header) {
    (void)api.add_header(request, "Content-Type", req.content_type);
  }
  if (api.send_request(request, req.body,
                       static_cast<unsigned int>(req.body_len)) < 0) {
    if (api.delete_req) {
      api.delete_req(request);
    }
    if (api.delete_conn) {
      api.delete_conn(conn);
    }
    if (api.destroy) {
      api.destroy(tmpl);
    }
    return GitStatus::Network;
  }
  if (api.get_status) {
    int code = 0;
    const int min_code = req.status_min > 0 ? req.status_min : 200;
    const int max_code = req.status_max > 0 ? req.status_max : 299;
    if (api.get_status(request, &code) == 0 &&
        (code < min_code || code > max_code)) {
      LOG_ERROR("Ps5HttpTransport: HTTP %d", code);
      if (api.delete_req) {
        api.delete_req(request);
      }
      if (api.delete_conn) {
        api.delete_conn(conn);
      }
      if (api.destroy) {
        api.destroy(tmpl);
      }
      return GitStatus::Network;
    }
  }

  GitStatus st = GitStatus::Ok;
  char buf[8192];
  for (;;) {
    unsigned int got = 0;
    const int rr =
        api.read_data(request, buf, static_cast<unsigned int>(sizeof(buf)), &got);
    if (rr < 0) {
      st = GitStatus::Network;
      break;
    }
    if (got == 0) {
      break;
    }
    if (on_data) {
      st = on_data(buf, got);
      if (st != GitStatus::Ok) {
        break;
      }
    }
  }

  if (api.delete_req) {
    api.delete_req(request);
  }
  if (api.delete_conn) {
    api.delete_conn(conn);
  }
  if (api.destroy) {
    api.destroy(tmpl);
  }
  return st;
#endif
}

} // namespace onion::cheats::sync
