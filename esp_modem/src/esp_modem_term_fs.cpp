// Copyright 2021 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <optional>
#include <unistd.h>
#include <sys/fcntl.h>
#include "cxx_include/esp_modem_dte.hpp"
#include "esp_log.h"
#include "esp_modem_config.h"
#include "exception_stub.hpp"
#include "uart_resource.hpp"        // In case of VFS using UART

static const char *TAG = "fs_terminal";

namespace esp_modem {

class Resource {
public:
    Resource(const esp_modem_dte_config *config);

    std::optional<uart_resource> uart;
};

class FdTerminal : public Terminal {
public:
    explicit FdTerminal(const esp_modem_dte_config *config);

    ~FdTerminal() override;

    void start() override {
        signal.set(TASK_START);
    }

    void stop() override {
        signal.clear(TASK_START);
    }

    int write(uint8_t *data, size_t len) override;

    int read(uint8_t *data, size_t len) override;

    void set_read_cb(std::function<bool(uint8_t *data, size_t len)> f) override {
        on_data = std::move(f);
        signal.set(TASK_PARAMS);
    }

private:
    void task();

    static const size_t TASK_INIT = SignalGroup::bit0;
    static const size_t TASK_START = SignalGroup::bit1;
    static const size_t TASK_STOP = SignalGroup::bit2;
    static const size_t TASK_PARAMS = SignalGroup::bit3;

    uart_resource uart;
    SignalGroup signal;
    int fd;
    Task task_handle;
};

std::unique_ptr<Terminal> create_vfs_terminal(const esp_modem_dte_config *config) {
    TRY_CATCH_RET_NULL(
            auto term = std::make_unique<FdTerminal>(config);
            term->start();
            return term;
    )
}

FdTerminal::FdTerminal(const esp_modem_dte_config *config) :
        uart(config, nullptr), signal(), fd(-1),
        task_handle(config->task_stack_size, config->task_priority, this, [](void* p){
            auto t = static_cast<FdTerminal *>(p);
            t->task();
            Task::Delete();
            if (t->fd >= 0) {
                close(t->fd);
            }
        })
{
    fd = open(config->vfs_config.dev_name, O_RDWR);
    throw_if_false(fd >= 0, "Cannot open the fd");
}

void FdTerminal::task()
{
    std::function<bool(uint8_t *data, size_t len)> on_data_priv = nullptr;
    signal.set(TASK_INIT);
    signal.wait_any(TASK_START | TASK_STOP, portMAX_DELAY);
    if (signal.is_any(TASK_STOP)) {
        return; // exits to the static method where the task gets deleted
    }

    // Set the FD to non-blocking mode
    int flags = fcntl(fd, F_GETFL, nullptr) | O_NONBLOCK;
    fcntl(fd, F_SETFL, flags);

    while (signal.is_any(TASK_START)) {
        int s;
        fd_set rfds;
        struct timeval tv = {
                .tv_sec = 1,
                .tv_usec = 0,
        };
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        s = select(fd + 1, &rfds, nullptr, nullptr, &tv);
        if (signal.is_any(TASK_PARAMS)) {
            on_data_priv = on_data;
            signal.clear(TASK_PARAMS);
        }

        if (s < 0) {
            break;
        } else if (s == 0) {
//            ESP_LOGV(TAG, "Select exited with timeout");
        } else {
            if (FD_ISSET(fd, &rfds)) {
                if (on_data_priv) {
                    if (on_data_priv(nullptr, 0)) {
                        on_data_priv = nullptr;
                    }
                }
            }
        }
        Task::Relinquish();
    }
}

int FdTerminal::read(uint8_t *data, size_t len)
{
    int size = ::read(fd, data, len);
    if (size < 0) {
        if (errno != EAGAIN) {
            ESP_LOGE(TAG, "Error occurred during read: %d", errno);
        }
        return 0;
    }

    return size;
}

int FdTerminal::write(uint8_t *data, size_t len)
{
    int size = ::write(fd, data, len);
    if (size < 0) {
        ESP_LOGE(TAG, "Error occurred during read: %d", errno);
        return 0;
    }
    return size;
}

FdTerminal::~FdTerminal()
{
    stop();
}

} // namespace esp_modem
