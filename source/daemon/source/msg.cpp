/* Copyright (C) 2025 OnionHEN / LightningMods */

#include "daemon_ops.hpp"
#include <onion/platform.h>
#include <onion/ipc_server.hpp>
#include <msg.hpp>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <cstring>
#include <errno.h>

bool is_handler_enabled = true;
std::atomic_bool g_stack_shutting_down{false};

static void handleIPC_adapt(onion::IpcClientArgs *client, std::string &msg,
                            DaemonCommands cmd) {
  handleIPC(client, msg, cmd);
}

static std::atomic_bool g_crit_ipc_running{true};
static std::atomic<int> g_crit_ipc_server_fd{-1};

static onion::IpcServerOptions g_crit_ipc_opts = {
    CRIT_IPC_SOC,
    handleIPC_adapt,
    BREW_RETURN_VALUE,
    true,
    "crit",
    &g_crit_ipc_running,
    &g_crit_ipc_server_fd,
};

void *IPC_loop(void *args) {
  (void)args;
  return onion::ipc_server_loop(&g_crit_ipc_opts);
}

/** Re-create the crit Unix IPC listener (called after a standby resume). */
void restart_crit_ipc_server() { onion::ipc_server_restart(&g_crit_ipc_opts); }

/**
 * PC control: TCP :9048
 * Frame (little-endian):
 *   u32 magic = ONION_CTRL_TCP_MAGIC (0x4F4E494F 'ONIO')
 *   u32 cmd   = ONION_CTRL_TCP_CMD_SHUTDOWN (1)
 * Reply: 1 byte 0 = accepted, then daemon shuts the stack down.
 *
 * The listener self-heals: if accept() fails (socket dead after a standby
 * resume, or restart_crit_tcp requested), the port is re-bound.
 */
static std::atomic<int> g_ctrl_fd{-1};

/** Re-bind the TCP :9048 listener (called after a standby resume). */
void control_tcp_restart() {
  const int fd = g_ctrl_fd.load();
  if (fd >= 0)
    shutdown(fd, SHUT_RDWR);
}

void *control_tcp_loop(void *args) {
  (void)args;
  while (is_handler_enabled) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
      LOG_ERROR("control_tcp: socket failed: %s", strerror(errno));
      break;
    }
    int yes = 1;
    (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(ONION_CTRL_TCP_PORT);
    if (bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
      LOG_ERROR("control_tcp: bind :%d failed: %s", ONION_CTRL_TCP_PORT,
               strerror(errno));
      close(s);
      break;
    }
    if (listen(s, 2) < 0) {
      LOG_ERROR("control_tcp: listen failed: %s", strerror(errno));
      close(s);
      break;
    }
    g_ctrl_fd.store(s);
    LOG_INFO("control_tcp: listening on 0.0.0.0:%d (PC shutdown)",
             ONION_CTRL_TCP_PORT);

    while (is_handler_enabled) {
      int client = accept(s, nullptr, nullptr);
      if (client < 0) {
        break;  // listener shut down for a restart, or a transient error
      }

      uint32_t frame[2] = {0, 0};
      ssize_t n = recv(client, frame, sizeof(frame), MSG_WAITALL);
      if (n == static_cast<ssize_t>(sizeof(frame)) &&
          frame[0] == ONION_CTRL_TCP_MAGIC &&
          frame[1] == ONION_CTRL_TCP_CMD_SHUTDOWN) {
        const uint8_t ok = 0;
        (void)send(client, &ok, 1, MSG_NOSIGNAL);
        close(client);
        close(s);
        LOG_INFO("control_tcp: SHUTDOWN from LAN client");
        usleep(100 * 1000);
        cmd_shutdown_onion_stack();
        /* noreturn */
      }

      const uint8_t err = 1;
      (void)send(client, &err, 1, MSG_NOSIGNAL);
      close(client);
    }

    close(s);
    g_ctrl_fd.store(-1);
    if (!is_handler_enabled)
      break;
    LOG_WARN("control_tcp: accept failed; re-listening");
    usleep(500000);
  }
  return nullptr;
}
